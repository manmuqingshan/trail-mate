/**
 * @file lxmf_announce_ingestor.cpp
 * @brief Verified announce ingestion owner for embedded LXMF.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_ingestor.h"

#include "chat/infra/lxmf/lxmf_wire.h"

#include "platform/esp/common/reticulum_runtime_compat.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

constexpr const char* kAnonymousPeerDisplayName = "Anonymous Peer";
constexpr const char* kAnonymousNodeDisplayName = "Anonymous Node";

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

void copyCString(char* out, std::size_t out_len, const char* in)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!in)
    {
        return;
    }
    const std::size_t max_copy_len = out_len - 1U;
    const auto* terminator =
        static_cast<const char*>(std::memchr(in, '\0', max_copy_len));
    const std::size_t copy_len =
        terminator ? static_cast<std::size_t>(terminator - in) : max_copy_len;
    std::memcpy(out, in, copy_len);
    out[copy_len] = '\0';
}

bool isZeroBytes(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool peerHasUsableRatchet(const PeerInfo& peer)
{
    return peer.has_ratchet &&
           !isZeroBytes(peer.ratchet_pub, sizeof(peer.ratchet_pub));
}

void formatHashHex(const uint8_t* hash,
                   std::size_t hash_len,
                   char* out,
                   std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || hash_len == 0 || out_len < ((hash_len * 2U) + 1U))
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    std::size_t used = 0;
    for (std::size_t index = 0; index < hash_len && used + 2U < out_len; ++index)
    {
        used += static_cast<std::size_t>(
            std::snprintf(out + used,
                          out_len - used,
                          "%02X",
                          static_cast<unsigned>(hash[index])));
    }
}

bool copyTextAppDataDisplayName(const uint8_t* data,
                                std::size_t len,
                                char* out,
                                std::size_t out_len)
{
    if (!data || len == 0 || len > 96 || !out || out_len == 0)
    {
        return false;
    }

    std::size_t used = 0;
    bool has_visible = false;
    for (std::size_t index = 0; index < len; ++index)
    {
        uint8_t byte = data[index];
        if (byte == '\t' || byte == '\r' || byte == '\n')
        {
            byte = ' ';
        }
        else if (byte == 0 || byte < 0x20 || byte == 0x7F)
        {
            out[0] = '\0';
            return false;
        }

        if (used + 1U < out_len)
        {
            out[used++] = static_cast<char>(byte);
        }
        if (byte != ' ')
        {
            has_visible = true;
        }
    }
    while (used != 0 && out[used - 1U] == ' ')
    {
        --used;
    }
    out[used] = '\0';
    return has_visible && used != 0;
}

bool isNameHash(const reticulum::ParsedAnnounce& announce,
                const char* app_name,
                const char* aspect)
{
    if (!announce.valid || !announce.name_hash || !app_name || !aspect)
    {
        return false;
    }
    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash(app_name, aspect, expected_name_hash);
    return hashesEqual(expected_name_hash,
                       announce.name_hash,
                       sizeof(expected_name_hash));
}

bool isLxmfDeliveryAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "lxmf", "delivery");
}

bool isLxmfPropagationAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "lxmf", "propagation");
}

bool isLxstTelephonyAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "lxst", "telephony");
}

bool isCallAudioAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "call", "audio") ||
           isLxstTelephonyAnnounce(announce);
}

bool isNomadNetworkNodeAnnounce(const reticulum::ParsedAnnounce& announce)
{
    return isNameHash(announce, "nomadnetwork", "node");
}

void destinationHashForAspect(
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    const char* aspect,
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!identity_hash || !aspect || !out_hash)
    {
        return;
    }
    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", aspect, name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, out_hash);
}

const char* pathAnnounceDecisionLabel(PathAnnounceDecision decision)
{
    switch (decision)
    {
    case PathAnnounceDecision::AcceptNew:
        return "new";
    case PathAnnounceDecision::AcceptNewer:
        return "newer";
    case PathAnnounceDecision::AcceptExpired:
        return "expired";
    case PathAnnounceDecision::RejectReplay:
        return "replay";
    case PathAnnounceDecision::RejectStale:
        return "stale";
    }
    return "unknown";
}

} // namespace

bool AnnounceIngestor::ingest(const uint8_t* raw_packet,
                              std::size_t raw_len,
                              const reticulum::ParsedPacket& packet,
                              const LxmfIdentity& local_identity,
                              DestinationRegistry& destination_registry,
                              PathManager& path_manager,
                              const AnnounceIngestOptions& options,
                              AnnounceIngestResult* out_result)
{
    if (!out_result)
    {
        return false;
    }
    *out_result = AnnounceIngestResult{};
    AnnounceIngestResult& result = *out_result;

    if (!raw_packet || raw_len == 0 || !packet.destination_hash ||
        packet.destination_type != reticulum::DestinationType::Single ||
        (packet.context != static_cast<uint8_t>(reticulum::PacketContext::None) &&
         packet.context !=
             static_cast<uint8_t>(reticulum::PacketContext::PathResponse)))
    {
        result.reason = "invalid_packet";
        return false;
    }

    if (!reticulum::parseAnnounce(packet, &result.announce) ||
        !result.announce.valid)
    {
        char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      packet_hash_hex,
                      sizeof(packet_hash_hex));
        Serial.printf("[LXMF][AnnounceRX] drop reason=parse_failed dest=%s payload_len=%u\n",
                      packet_hash_hex,
                      static_cast<unsigned>(packet.payload_len));
        result.reason = "parse_failed";
        return false;
    }

    reticulum::computeIdentityHash(result.announce.public_key,
                                   result.identity_hash);
    reticulum::computeDestinationHash(result.announce.name_hash,
                                      result.identity_hash,
                                      result.expected_destination_hash);
    if (!hashesEqual(result.expected_destination_hash,
                     packet.destination_hash,
                     reticulum::kTruncatedHashSize))
    {
        char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        char expected_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      packet_hash_hex,
                      sizeof(packet_hash_hex));
        formatHashHex(result.expected_destination_hash,
                      sizeof(result.expected_destination_hash),
                      expected_hash_hex,
                      sizeof(expected_hash_hex));
        Serial.printf("[LXMF][AnnounceRX] drop reason=destination_mismatch packet=%s expected=%s\n",
                      packet_hash_hex,
                      expected_hash_hex);
        result.reason = "destination_mismatch";
        return false;
    }

    std::size_t signed_len = 0;
    std::memcpy(signed_scratch_ + signed_len,
                packet.destination_hash,
                reticulum::kTruncatedHashSize);
    signed_len += reticulum::kTruncatedHashSize;
    std::memcpy(signed_scratch_ + signed_len,
                result.announce.public_key,
                reticulum::kCombinedPublicKeySize);
    signed_len += reticulum::kCombinedPublicKeySize;
    std::memcpy(signed_scratch_ + signed_len,
                result.announce.name_hash,
                reticulum::kNameHashSize);
    signed_len += reticulum::kNameHashSize;
    std::memcpy(signed_scratch_ + signed_len, result.announce.random_hash, 10);
    signed_len += 10;
    if (result.announce.has_ratchet && result.announce.ratchet &&
        result.announce.ratchet_len != 0)
    {
        if (signed_len + result.announce.ratchet_len > sizeof(signed_scratch_))
        {
            result.reason = "signed_data_overflow";
            return false;
        }
        std::memcpy(signed_scratch_ + signed_len,
                    result.announce.ratchet,
                    result.announce.ratchet_len);
        signed_len += result.announce.ratchet_len;
    }
    if (result.announce.app_data_len != 0)
    {
        if (signed_len + result.announce.app_data_len > sizeof(signed_scratch_))
        {
            result.reason = "signed_data_overflow";
            return false;
        }
        std::memcpy(signed_scratch_ + signed_len,
                    result.announce.app_data,
                    result.announce.app_data_len);
        signed_len += result.announce.app_data_len;
    }

    const uint8_t* sig_pub =
        result.announce.public_key + reticulum::kEncryptionPublicKeySize;
    if (!LxmfIdentity::verify(sig_pub,
                              result.announce.signature,
                              signed_scratch_,
                              signed_len))
    {
        char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      packet_hash_hex,
                      sizeof(packet_hash_hex));
        Serial.printf("[LXMF][AnnounceRX] drop reason=signature_failed dest=%s\n",
                      packet_hash_hex);
        result.reason = "signature_failed";
        return false;
    }

    if (options.resolve_local_destination)
    {
        result.local_destination =
            options.resolve_local_destination(options.local_destination_context,
                                              packet.destination_hash,
                                              &result.local_kind);
    }
    if (result.local_destination)
    {
        result.status = AnnounceIngestResult::Status::Ignored;
        result.reason = "local_destination";
        return true;
    }
    if (packet.hops > options.max_transport_hops)
    {
        Serial.printf("[LXMF][AnnounceRX] ignore reason=max_hops hops=%u\n",
                      static_cast<unsigned>(packet.hops));
        result.status = AnnounceIngestResult::Status::Ignored;
        result.reason = "max_hops";
        return true;
    }

    const PathEntry* existing_path =
        path_manager.findAnyPath(packet.destination_hash);
    result.path_decision = evaluatePathAnnounce(existing_path,
                                                packet.hops,
                                                result.announce.random_hash,
                                                options.now_ms,
                                                options.path_ttl_ms);
    if (!pathAnnounceAccepted(result.path_decision))
    {
        char destination_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      destination_hex,
                      sizeof(destination_hex));
        Serial.printf("[LXMF][AnnounceRX] ignore reason=path_%s dest=%s hops=%u previous_hops=%u\n",
                      pathAnnounceDecisionLabel(result.path_decision),
                      destination_hex,
                      static_cast<unsigned>(packet.hops),
                      static_cast<unsigned>(existing_path ? existing_path->hops : 0));
        result.status = AnnounceIngestResult::Status::Ignored;
        result.reason = "path_rejected";
        return true;
    }

    result.path = path_manager.observeAnnouncePath(packet.destination_hash,
                                                   packet.hops,
                                                   result.announce.random_hash,
                                                   options.now_ms,
                                                   options.now_s,
                                                   options.ingress_interface_id,
                                                   packet.transport_id,
                                                   raw_packet,
                                                   raw_len,
                                                   options.max_paths);
    if (!result.path)
    {
        result.reason = "path_observe_failed";
        return false;
    }

    result.delivery_announce = isLxmfDeliveryAnnounce(result.announce);
    result.propagation_announce = isLxmfPropagationAnnounce(result.announce);
    result.call_audio_announce = isCallAudioAnnounce(result.announce);
    result.lxst_telephony_announce = isLxstTelephonyAnnounce(result.announce);
    result.nomad_node_announce = isNomadNetworkNodeAnnounce(result.announce);
    result.contact_announce =
        result.delivery_announce || result.lxst_telephony_announce;
    result.packet_has_ratchet =
        result.announce.has_ratchet &&
        result.announce.ratchet &&
        result.announce.ratchet_len == reticulum::kRatchetSize &&
        !isZeroBytes(result.announce.ratchet, result.announce.ratchet_len);

    bool has_stamp_cost = false;
    uint8_t stamp_cost = 0;
    if (result.call_audio_announce && result.announce.app_data &&
        result.announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(result.announce.app_data,
                                         result.announce.app_data_len,
                                         result.display_name,
                                         sizeof(result.display_name));
    }
    else if (result.delivery_announce && result.announce.app_data &&
             result.announce.app_data_len != 0 &&
             unpackPeerAnnounceAppData(result.announce.app_data,
                                       result.announce.app_data_len,
                                       result.display_name,
                                       sizeof(result.display_name),
                                       &has_stamp_cost,
                                       &stamp_cost))
    {
        (void)has_stamp_cost;
        (void)stamp_cost;
    }
    else if (result.nomad_node_announce && result.announce.app_data &&
             result.announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(result.announce.app_data,
                                         result.announce.app_data_len,
                                         result.display_name,
                                         sizeof(result.display_name));
    }
    else if (!(result.delivery_announce || result.propagation_announce ||
               result.call_audio_announce || result.nomad_node_announce) &&
             result.announce.app_data && result.announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(result.announce.app_data,
                                         result.announce.app_data_len,
                                         result.display_name,
                                         sizeof(result.display_name));
    }
    if ((result.delivery_announce ||
         (result.call_audio_announce && !result.lxst_telephony_announce)) &&
        result.display_name[0] == '\0')
    {
        copyCString(result.display_name,
                    sizeof(result.display_name),
                    kAnonymousPeerDisplayName);
    }
    else if (result.nomad_node_announce && result.display_name[0] == '\0')
    {
        copyCString(result.display_name,
                    sizeof(result.display_name),
                    kAnonymousNodeDisplayName);
    }

    if (result.contact_announce)
    {
        uint8_t peer_destination_hash[reticulum::kTruncatedHashSize] = {};
        if (result.delivery_announce)
        {
            copyHash(peer_destination_hash,
                     packet.destination_hash,
                     sizeof(peer_destination_hash));
        }
        else
        {
            destinationHashForAspect(result.identity_hash,
                                     "delivery",
                                     peer_destination_hash);
        }

        PeerInfo& peer = destination_registry.upsertDestination(peer_destination_hash);
        const uint32_t previous_seen_s = peer.last_seen_s;
        const bool delivery_ratchet_available =
            result.delivery_announce && result.packet_has_ratchet;
        result.ratchet_changed =
            result.delivery_announce &&
            (peerHasUsableRatchet(peer) != delivery_ratchet_available ||
             (delivery_ratchet_available &&
              std::memcmp(peer.ratchet_pub,
                          result.announce.ratchet,
                          sizeof(peer.ratchet_pub)) != 0));
        result.identity_changed =
            isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) ||
            !hashesEqual(peer.identity_hash,
                         result.identity_hash,
                         sizeof(peer.identity_hash)) ||
            std::memcmp(peer.enc_pub,
                        result.announce.public_key,
                        sizeof(peer.enc_pub)) != 0 ||
            std::memcmp(peer.sig_pub, sig_pub, sizeof(peer.sig_pub)) != 0;
        result.display_changed =
            result.display_name[0] != '\0' &&
            std::strncmp(peer.display_name,
                         result.display_name,
                         sizeof(peer.display_name)) != 0;

        copyHash(peer.identity_hash,
                 result.identity_hash,
                 sizeof(peer.identity_hash));
        std::memcpy(peer.enc_pub, result.announce.public_key, sizeof(peer.enc_pub));
        std::memcpy(peer.sig_pub, sig_pub, sizeof(peer.sig_pub));
        peer.last_seen_s = options.now_s;
        if (result.delivery_announce)
        {
            if (delivery_ratchet_available)
            {
                std::memcpy(peer.ratchet_pub,
                            result.announce.ratchet,
                            sizeof(peer.ratchet_pub));
                peer.has_ratchet = true;
                peer.ratchet_seen_s = options.now_s;
            }
            else
            {
                std::memset(peer.ratchet_pub, 0, sizeof(peer.ratchet_pub));
                peer.has_ratchet = false;
                peer.ratchet_seen_s = 0;
            }
        }
        if (result.display_name[0] != '\0')
        {
            copyCString(peer.display_name,
                        sizeof(peer.display_name),
                        result.display_name);
        }
        else if (peer.display_name[0] == '\0')
        {
            copyCString(peer.display_name,
                        sizeof(peer.display_name),
                        kAnonymousPeerDisplayName);
        }

        result.address_refresh_due =
            previous_seen_s == 0 ||
            (options.now_s >= previous_seen_s &&
             (options.now_s - previous_seen_s) >=
                 options.directory_address_refresh_interval_s);
        result.should_store_address =
            options.ingress_interface !=
                reticulum::interfaces::InterfaceKind::WifiGateway ||
            result.identity_changed || result.ratchet_changed ||
            result.display_changed || result.address_refresh_due;
        result.learned_peer = &peer;
    }

    result.status = AnnounceIngestResult::Status::Accepted;
    result.reason = "accepted";
    (void)local_identity;
    return true;
}

} // namespace chat::lxmf::runtime
