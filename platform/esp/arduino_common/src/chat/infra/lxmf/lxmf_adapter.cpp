/**
 * @file lxmf_adapter.cpp
 * @brief Device-side LXMF adapter over the existing RNode raw carrier
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"

#include "chat/domain/contact_types.h"
#include "chat/domain/reticulum_identity.h"
#include "chat/infra/meshcore/crypto/ed25519/ed_25519.h"
#include "chat/time_utils.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_service_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_resource_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_transport_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "sys/event_bus.h"

#include <Arduino.h>
#include <Curve25519.h>
#include <bzlib.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>

namespace chat::lxmf
{
namespace
{
namespace rtdir = ::platform::ui::reticulum_directory;
namespace rtpage = ::platform::ui::reticulum_page;
namespace screen_runtime = ::platform::ui::screen;
using PageFailureKind = rtpage::RequestProgress::FailureKind;

constexpr size_t kMaxPacketLen = reticulum::kReticulumMtu;
constexpr size_t kMaxLxmfMessageLen = reticulum::kReticulumMtu;
constexpr size_t kSignedPartMaxLen = reticulum::kReticulumMtu;
constexpr size_t kMaxTokenPlaintextLen = reticulum::kReticulumMtu;
constexpr size_t kPathRequestTagSize = reticulum::kTruncatedHashSize;
constexpr uint32_t kPathRequestMinIntervalMs = 20000;
constexpr uint32_t kPathRefreshAgeS = 300;
constexpr size_t kMaxPersistedPeers = 64;
constexpr size_t kMaxPaths = 96;
constexpr size_t kMaxPacketFilter = 128;
constexpr size_t kMaxReverseEntries = 64;
constexpr size_t kMaxLinkRelays = 24;
constexpr size_t kMaxLinkSessions = 12;
constexpr size_t kMaxPendingPathRequests = 32;
constexpr uint32_t kLinkSessionTtlMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kLinkHandshakeTimeoutMs = 30000;
constexpr uint32_t kLinkIdleTimeoutMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kLinkRequestTtlMs = 60000;
constexpr uint32_t kNomadPageLinkRetryMs = 10000;
constexpr uint32_t kPendingPathRequestTtlMs = 45000;
constexpr uint32_t kLinkKeepaliveMinMs = 5000;
constexpr uint32_t kLinkKeepaliveMaxMs = 360000;
constexpr float kLinkKeepaliveMaxRttS = 1.75f;
constexpr uint32_t kLinkKeepaliveTimeoutFactor = 4;
constexpr uint32_t kLinkStaleGraceMs = 5000;
constexpr uint32_t kResourceTransferTtlMs = 60000;
constexpr uint32_t kResourceWindowSize = 4;
constexpr size_t kMaxPropagationEntries = 64;
constexpr size_t kMaxPropagationTransients = 192;
constexpr size_t kMaxPropagationPeers = 32;
constexpr uint32_t kPropagationEntryTtlS = 3UL * 24UL * 60UL * 60UL;
constexpr uint32_t kPropagationTransientTtlS = 3UL * 24UL * 60UL * 60UL;
constexpr uint32_t kPropagationTransferLimitKb = 64;
constexpr uint32_t kPropagationSyncLimitKb = 64;
constexpr uint32_t kLoraDiscoveryForegroundDetailLogIntervalMs = 15000;
constexpr uint32_t kLoraDiscoverySleepDetailLogIntervalMs = 10000;
constexpr uint32_t kLoraAnnounceIgnoreDetailLogIntervalMs = 10000;
constexpr uint8_t kLinkModeAes256Cbc = 0x01;
constexpr size_t kLinkRequestBaseLen = 64;
constexpr size_t kLinkSignallingLen = 3;
constexpr size_t kResourceMapHashLen = 4;
constexpr size_t kResourceDataPrefixLen = 4;
constexpr size_t kResourceAdvertisementOverhead = 134;
constexpr uint8_t kResourceFlagEncrypted = 1U << 0U;
constexpr uint8_t kResourceFlagCompressed = 1U << 1U;
constexpr uint8_t kResourceFlagSplit = 1U << 2U;
constexpr uint8_t kResourceFlagRequest = 1U << 3U;
constexpr uint8_t kResourceFlagResponse = 1U << 4U;
constexpr uint8_t kResourceFlagHasMetadata = 1U << 5U;
constexpr uint32_t kPacketFilterTtlMs = 30000;
constexpr uint32_t kReverseEntryTtlMs = 60000;
constexpr uint32_t kLinkRelayTtlMs = 300000;
constexpr uint32_t kDirectoryAddressRefreshIntervalS = 300;
constexpr uint8_t kMaxTransportHops = 128;
constexpr const char* kAnonymousPeerDisplayName = "Anonymous Peer";
constexpr const char* kAnonymousNodeDisplayName = "Anonymous Node";
constexpr uint8_t kPropagationMetaName = 0x01;

void formatHashPrefix(const uint8_t* hash, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || out_len < 9)
    {
        snprintf(out, out_len, "-");
        return;
    }
    snprintf(out,
             out_len,
             "%02X%02X%02X%02X",
             static_cast<unsigned>(hash[0]),
             static_cast<unsigned>(hash[1]),
             static_cast<unsigned>(hash[2]),
             static_cast<unsigned>(hash[3]));
}

void formatHashHex(const uint8_t* hash, size_t hash_len, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || hash_len == 0 || out_len < ((hash_len * 2U) + 1U))
    {
        snprintf(out, out_len, "-");
        return;
    }

    size_t used = 0;
    for (size_t index = 0; index < hash_len && used + 2U < out_len; ++index)
    {
        used += static_cast<size_t>(
            snprintf(out + used,
                     out_len - used,
                     "%02X",
                     static_cast<unsigned>(hash[index])));
    }
}

bool decodeMsgpackByteString(const std::vector<uint8_t>& packed,
                             std::vector<uint8_t>* out)
{
    if (packed.empty() || !out)
    {
        return false;
    }

    const uint8_t marker = packed[0];
    size_t offset = 1;
    size_t len = 0;
    if ((marker & 0xE0U) == 0xA0U)
    {
        len = marker & 0x1FU;
    }
    else if (marker == 0xC4 || marker == 0xD9)
    {
        if (packed.size() < 2)
        {
            return false;
        }
        len = packed[1];
        offset = 2;
    }
    else if (marker == 0xC5 || marker == 0xDA)
    {
        if (packed.size() < 3)
        {
            return false;
        }
        len = (static_cast<size_t>(packed[1]) << 8) |
              static_cast<size_t>(packed[2]);
        offset = 3;
    }
    else if (marker == 0xC6 || marker == 0xDB)
    {
        if (packed.size() < 5)
        {
            return false;
        }
        len = (static_cast<size_t>(packed[1]) << 24) |
              (static_cast<size_t>(packed[2]) << 16) |
              (static_cast<size_t>(packed[3]) << 8) |
              static_cast<size_t>(packed[4]);
        offset = 5;
    }
    else
    {
        return false;
    }

    if (offset > packed.size() || len > (packed.size() - offset))
    {
        return false;
    }
    out->assign(packed.data() + offset, packed.data() + offset + len);
    return true;
}

void formatLogTextPreview(const std::string& text, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    const size_t max_copy = out_len - 1U;
    size_t used = 0;
    for (char value : text)
    {
        if (used >= max_copy)
        {
            break;
        }
        const unsigned char c = static_cast<unsigned char>(value);
        if (c == '\r' || c == '\n' || c == '\t')
        {
            out[used++] = ' ';
        }
        else if (c < 0x20U || c == 0x7FU)
        {
            out[used++] = '.';
        }
        else
        {
            out[used++] = value;
        }
    }
    out[used] = '\0';
}

const char* txBearerName(const reticulum::interfaces::TxResult& result)
{
    if (result.lora_ok && result.wifi_ok)
    {
        return "lora+wifi";
    }
    if (result.lora_ok)
    {
        return "lora";
    }
    if (result.wifi_ok)
    {
        return "wifi";
    }
    return "none";
}

const char* localDestinationKindLabel(runtime::LocalDestinationKind kind)
{
    switch (kind)
    {
    case runtime::LocalDestinationKind::Propagation:
        return "propagation";
    case runtime::LocalDestinationKind::CallAudio:
        return "call_audio";
    case runtime::LocalDestinationKind::NomadPage:
        return "nomad_page";
    case runtime::LocalDestinationKind::Delivery:
    default:
        return "delivery";
    }
}

const char* linkStateLabel(runtime::LinkState state)
{
    switch (state)
    {
    case runtime::LinkState::Pending:
        return "pending";
    case runtime::LinkState::Handshake:
        return "handshake";
    case runtime::LinkState::Active:
        return "active";
    case runtime::LinkState::Stale:
        return "stale";
    case runtime::LinkState::Closed:
        return "closed";
    }
    return "unknown";
}

void logNomadLinkProofEvent(const runtime::LinkSession& session,
                            const char* event,
                            const char* reason,
                            uint32_t first = 0,
                            uint32_t second = 0)
{
    if (session.destination != runtime::LocalDestinationKind::NomadPage)
    {
        return;
    }

    char destination_hash[12] = {};
    char link_hash[12] = {};
    formatHashPrefix(session.remote_destination_hash,
                     destination_hash,
                     sizeof(destination_hash));
    formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
    Serial.printf("[LXMF][LinkRX] proof_%s kind=nomad_page reason=%s dest=%s link=%s state=%s a=%lu b=%lu\n",
                  event ? event : "event",
                  reason ? reason : "-",
                  destination_hash,
                  link_hash,
                  linkStateLabel(session.state),
                  static_cast<unsigned long>(first),
                  static_cast<unsigned long>(second));
}

#ifndef LXMF_NOMAD_PAGE_TRACE
#define LXMF_NOMAD_PAGE_TRACE 0
#endif
#ifndef LXMF_LINK_PROOF_TRACE
#define LXMF_LINK_PROOF_TRACE 0
#endif
constexpr bool kNomadPageTraceEnabled = LXMF_NOMAD_PAGE_TRACE != 0;

#if LXMF_NOMAD_PAGE_TRACE
#define LXMF_NOMAD_PAGE_LOG(...)                         \
    do                                                   \
    {                                                    \
        Serial.printf("[LXMF][NomadPage] " __VA_ARGS__); \
    } while (0)
#else
#define LXMF_NOMAD_PAGE_LOG(...) \
    do                           \
    {                            \
    } while (0)
#endif

#if LXMF_LINK_PROOF_TRACE
#define LXMF_LINK_PROOF_LOG(...)                         \
    do                                                   \
    {                                                    \
        Serial.printf("[LXMF][LinkProof] " __VA_ARGS__); \
    } while (0)
#else
#define LXMF_LINK_PROOF_LOG(...) \
    do                           \
    {                            \
    } while (0)
#endif

bool appendMsgpackByte(uint8_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (!out || used >= out_len)
    {
        return false;
    }

    out[used++] = value;
    return true;
}

bool appendMsgpackBytes(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if ((!data && len != 0) || !out || (used + len) > out_len)
    {
        return false;
    }

    if (len != 0)
    {
        memcpy(out + used, data, len);
    }
    used += len;
    return true;
}

bool appendMsgpackArrayHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(0x90U | count), out, out_len, used);
    }

    return appendMsgpackByte(0xDC, out, out_len, used) &&
           appendMsgpackByte(0x00, out, out_len, used) &&
           appendMsgpackByte(count, out, out_len, used);
}

bool appendMsgpackMapHeader(uint8_t count, uint8_t* out, size_t out_len, size_t& used)
{
    if (count <= 0x0FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(0x80U | count), out, out_len, used);
    }

    return appendMsgpackByte(0xDE, out, out_len, used) &&
           appendMsgpackByte(0x00, out, out_len, used) &&
           appendMsgpackByte(count, out, out_len, used);
}

bool appendMsgpackBool(bool value, uint8_t* out, size_t out_len, size_t& used)
{
    return appendMsgpackByte(value ? 0xC3 : 0xC2, out, out_len, used);
}

bool appendMsgpackUint(uint32_t value, uint8_t* out, size_t out_len, size_t& used)
{
    if (value <= 0x7FU)
    {
        return appendMsgpackByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFU)
    {
        return appendMsgpackByte(0xCC, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(value), out, out_len, used);
    }
    if (value <= 0xFFFFU)
    {
        return appendMsgpackByte(0xCD, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
    }

    return appendMsgpackByte(0xCE, out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 24) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 16) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>((value >> 8) & 0xFFU), out, out_len, used) &&
           appendMsgpackByte(static_cast<uint8_t>(value & 0xFFU), out, out_len, used);
}

bool appendMsgpackBin(const uint8_t* data, size_t len, uint8_t* out, size_t out_len, size_t& used)
{
    if (len <= 0xFFU)
    {
        return appendMsgpackByte(0xC4, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(len), out, out_len, used) &&
               appendMsgpackBytes(data, len, out, out_len, used);
    }
    if (len <= 0xFFFFU)
    {
        return appendMsgpackByte(0xC5, out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>((len >> 8) & 0xFFU), out, out_len, used) &&
               appendMsgpackByte(static_cast<uint8_t>(len & 0xFFU), out, out_len, used) &&
               appendMsgpackBytes(data, len, out, out_len, used);
    }

    return false;
}

bool packPropagationAnnounceAppData(uint32_t timebase_s,
                                    bool node_state,
                                    uint32_t per_transfer_limit_kb,
                                    uint32_t per_sync_limit_kb,
                                    const char* display_name,
                                    uint8_t* out_data,
                                    size_t* inout_len)
{
    if (!out_data || !inout_len)
    {
        return false;
    }

    const uint8_t* name_bytes = reinterpret_cast<const uint8_t*>(display_name ? display_name : "");
    const size_t name_len = (display_name && display_name[0] != '\0') ? strlen(display_name) : 0;

    size_t used = 0;
    const uint8_t metadata_entries = (name_len != 0) ? 1 : 0;
    if (!appendMsgpackArrayHeader(7, out_data, *inout_len, used) ||
        !appendMsgpackBool(false, out_data, *inout_len, used) ||
        !appendMsgpackUint(timebase_s, out_data, *inout_len, used) ||
        !appendMsgpackBool(node_state, out_data, *inout_len, used) ||
        !appendMsgpackUint(per_transfer_limit_kb, out_data, *inout_len, used) ||
        !appendMsgpackUint(per_sync_limit_kb, out_data, *inout_len, used) ||
        !appendMsgpackArrayHeader(3, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackUint(0, out_data, *inout_len, used) ||
        !appendMsgpackMapHeader(metadata_entries, out_data, *inout_len, used))
    {
        return false;
    }

    if (metadata_entries != 0 &&
        (!appendMsgpackUint(kPropagationMetaName, out_data, *inout_len, used) ||
         !appendMsgpackBin(name_bytes, name_len, out_data, *inout_len, used)))
    {
        return false;
    }

    *inout_len = used;
    return true;
}

void destinationHashForServiceAspect(
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    const char* app_name,
    const char* aspect,
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!identity_hash || !app_name || !aspect || !out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash(app_name, aspect, name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, out_hash);
}

void destinationHashForAspect(const uint8_t identity_hash[reticulum::kTruncatedHashSize],
                              const char* aspect,
                              uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    destinationHashForServiceAspect(identity_hash, "lxmf", aspect, out_hash);
}

void callAudioDestinationHashForIdentity(
    const uint8_t identity_hash[reticulum::kTruncatedHashSize],
    uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    destinationHashForServiceAspect(identity_hash, "call", "audio", out_hash);
}

void fillRandomBytes(uint8_t* out, size_t len)
{
    if (!out || len == 0)
    {
        return;
    }

    size_t offset = 0;
    while (offset < len)
    {
        const uint32_t rnd = static_cast<uint32_t>(esp_random());
        const size_t chunk = (len - offset >= sizeof(rnd)) ? sizeof(rnd) : (len - offset);
        memcpy(out + offset, &rnd, chunk);
        offset += chunk;
    }
}

bool isZeroBytes(const uint8_t* data, size_t len)
{
    if (!data)
    {
        return true;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool peerHasUsableRatchet(const runtime::PeerInfo& peer)
{
    return peer.has_ratchet &&
           !isZeroBytes(peer.ratchet_pub, sizeof(peer.ratchet_pub));
}

void formatRatchetIdPrefix(const uint8_t* ratchet_pub, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!ratchet_pub || isZeroBytes(ratchet_pub, reticulum::kRatchetSize))
    {
        snprintf(out, out_len, "-");
        return;
    }

    uint8_t ratchet_hash[reticulum::kFullHashSize] = {};
    reticulum::fullHash(ratchet_pub, reticulum::kRatchetSize, ratchet_hash);
    formatHashPrefix(ratchet_hash, out, out_len);
}

bool hashesEqual(const uint8_t* a, const uint8_t* b, size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

bool unpackRnsLxmfEnvelope(const uint8_t expected_destination_hash[reticulum::kTruncatedHashSize],
                           const uint8_t* payload, size_t payload_len,
                           DecodedEnvelope* out_envelope,
                           bool* out_embedded_destination_hash)
{
    if (!expected_destination_hash || !payload || payload_len == 0 || !out_envelope)
    {
        return false;
    }

    const size_t full_envelope_min_len =
        (reticulum::kTruncatedHashSize * 2U) + reticulum::kSignatureSize;
    if (payload_len >= full_envelope_min_len &&
        hashesEqual(payload, expected_destination_hash, reticulum::kTruncatedHashSize) &&
        unpackMessageEnvelope(payload, payload_len, out_envelope))
    {
        if (out_embedded_destination_hash)
        {
            *out_embedded_destination_hash = true;
        }
        return true;
    }

    const size_t opportunistic_tail_min_len =
        reticulum::kTruncatedHashSize + reticulum::kSignatureSize;
    if (payload_len < opportunistic_tail_min_len)
    {
        return false;
    }

    std::vector<uint8_t> full_payload(reticulum::kTruncatedHashSize + payload_len);
    memcpy(full_payload.data(), expected_destination_hash, reticulum::kTruncatedHashSize);
    memcpy(full_payload.data() + reticulum::kTruncatedHashSize, payload, payload_len);
    if (!unpackMessageEnvelope(full_payload.data(), full_payload.size(), out_envelope))
    {
        return false;
    }
    if (out_embedded_destination_hash)
    {
        *out_embedded_destination_hash = false;
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    memcpy(out, in, len);
}

::chat::ReticulumPeerIdentity reticulumIdentityForPeer(const runtime::PeerInfo& peer)
{
    return ::chat::makeReticulumPeerIdentity(peer.destination_hash,
                                             peer.identity_hash);
}

MeshPeerSource meshPeerSourceFromDirectorySource(rtdir::EntrySource source)
{
    switch (source)
    {
    case rtdir::EntrySource::RuntimeRx:
        return MeshPeerSource::RuntimeRx;
    case rtdir::EntrySource::PathResponse:
        return MeshPeerSource::DiscoveryResponse;
    case rtdir::EntrySource::Manual:
        return MeshPeerSource::Manual;
    case rtdir::EntrySource::Import:
        return MeshPeerSource::Import;
    case rtdir::EntrySource::Unknown:
    default:
        return MeshPeerSource::Unknown;
    }
}

void copyCString(char* out, size_t out_len, const char* in)
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

    strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
}

bool copyTextAppDataDisplayName(const uint8_t* data,
                                size_t len,
                                char* out,
                                size_t out_len)
{
    if (!data || len == 0 || len > 96 || !out || out_len == 0)
    {
        return false;
    }

    size_t used = 0;
    bool has_visible = false;
    for (size_t index = 0; index < len; ++index)
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

bool isLxmfDeliveryAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool isLxmfPropagationAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "propagation", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool isCallAudioAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("call", "audio", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool isNomadNetworkNodeAnnounce(const reticulum::ParsedAnnounce& announce)
{
    if (!announce.valid || !announce.name_hash)
    {
        return false;
    }

    uint8_t expected_name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("nomadnetwork", "node", expected_name_hash);
    return hashesEqual(expected_name_hash, announce.name_hash, sizeof(expected_name_hash));
}

bool packetContextUsesRawLinkPayload(uint8_t context)
{
    return context == static_cast<uint8_t>(reticulum::PacketContext::Keepalive) ||
           context == static_cast<uint8_t>(reticulum::PacketContext::Resource);
}

size_t resourceHashmapSegmentCapacity(uint16_t mdu)
{
    if (mdu <= kResourceAdvertisementOverhead)
    {
        return 1;
    }

    const size_t available = static_cast<size_t>(mdu) - kResourceAdvertisementOverhead;
    return std::max<size_t>(1, available / kResourceMapHashLen);
}

void* bzip2PsramAlloc(void*, int items, int size)
{
    if (items <= 0 || size <= 0)
    {
        return nullptr;
    }

    const size_t count = static_cast<size_t>(items);
    const size_t item_size = static_cast<size_t>(size);
    if (count > (std::numeric_limits<size_t>::max() / item_size))
    {
        return nullptr;
    }

    const size_t bytes = count * item_size;
    void* allocated = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!allocated)
    {
        allocated = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }
    return allocated;
}

void bzip2PsramFree(void*, void* address)
{
    if (address)
    {
        heap_caps_free(address);
    }
}

bool decompressBzip2Payload(const uint8_t* compressed,
                            size_t compressed_len,
                            size_t expected_size,
                            std::vector<uint8_t>* out_payload,
                            int* out_status)
{
    if (out_status)
    {
        *out_status = BZ_PARAM_ERROR;
    }
    if (!compressed || compressed_len == 0 || !out_payload ||
        compressed_len > std::numeric_limits<unsigned int>::max() ||
        expected_size > std::numeric_limits<unsigned int>::max())
    {
        return false;
    }

    std::vector<uint8_t> output(expected_size, 0);
    bz_stream stream{};
    stream.bzalloc = bzip2PsramAlloc;
    stream.bzfree = bzip2PsramFree;
    stream.opaque = nullptr;
    stream.next_in = reinterpret_cast<char*>(const_cast<uint8_t*>(compressed));
    stream.avail_in = static_cast<unsigned int>(compressed_len);
    stream.next_out = reinterpret_cast<char*>(output.data());
    stream.avail_out = static_cast<unsigned int>(output.size());

    int status = BZ2_bzDecompressInit(&stream, 0, 1);
    if (status != BZ_OK)
    {
        if (out_status)
        {
            *out_status = status;
        }
        return false;
    }

    do
    {
        status = BZ2_bzDecompress(&stream);
        if (status == BZ_STREAM_END)
        {
            break;
        }
        if (status != BZ_OK)
        {
            (void)BZ2_bzDecompressEnd(&stream);
            if (out_status)
            {
                *out_status = status;
            }
            return false;
        }
        if (stream.avail_out == 0)
        {
            (void)BZ2_bzDecompressEnd(&stream);
            if (out_status)
            {
                *out_status = BZ_OUTBUFF_FULL;
            }
            return false;
        }
    } while (stream.avail_in > 0 || stream.avail_out > 0);

    const size_t produced = output.size() - stream.avail_out;
    const int end_status = BZ2_bzDecompressEnd(&stream);
    if (status != BZ_STREAM_END || end_status != BZ_OK || produced != expected_size)
    {
        if (out_status)
        {
            *out_status = status != BZ_STREAM_END ? status : end_status;
        }
        return false;
    }

    output.resize(produced);
    *out_payload = std::move(output);
    if (out_status)
    {
        *out_status = BZ_OK;
    }
    return true;
}

void buildLinkSignallingBytes(uint16_t mtu, uint8_t out_bytes[kLinkSignallingLen])
{
    if (!out_bytes)
    {
        return;
    }

    const uint32_t signalling_value =
        (static_cast<uint32_t>(mtu) & 0x1FFFFFU) |
        ((static_cast<uint32_t>((kLinkModeAes256Cbc << 5) & 0xE0U)) << 16);
    out_bytes[0] = static_cast<uint8_t>((signalling_value >> 16) & 0xFFU);
    out_bytes[1] = static_cast<uint8_t>((signalling_value >> 8) & 0xFFU);
    out_bytes[2] = static_cast<uint8_t>(signalling_value & 0xFFU);
}

uint16_t mtuFromLinkSignalling(const uint8_t* signalling_bytes, size_t len)
{
    if (!signalling_bytes || len < kLinkSignallingLen)
    {
        return reticulum::kReticulumMtu;
    }
    const uint32_t mtu =
        ((static_cast<uint32_t>(signalling_bytes[0]) << 16) |
         (static_cast<uint32_t>(signalling_bytes[1]) << 8) |
         static_cast<uint32_t>(signalling_bytes[2])) &
        0x1FFFFFU;
    return static_cast<uint16_t>(std::min<uint32_t>(mtu, reticulum::kReticulumMtu));
}

uint8_t linkModeFromSignalling(const uint8_t* signalling_bytes, size_t len)
{
    if (!signalling_bytes || len < kLinkSignallingLen)
    {
        return kLinkModeAes256Cbc;
    }
    return static_cast<uint8_t>((signalling_bytes[0] & 0xE0U) >> 5);
}

uint32_t keepaliveIntervalForRtt(float rtt_s)
{
    if (rtt_s <= 0.0f)
    {
        return kLinkKeepaliveMaxMs;
    }

    const float scaled_ms =
        rtt_s * (static_cast<float>(kLinkKeepaliveMaxMs) / kLinkKeepaliveMaxRttS);
    const uint32_t keepalive_ms = static_cast<uint32_t>(scaled_ms);
    return std::min<uint32_t>(kLinkKeepaliveMaxMs,
                              std::max<uint32_t>(kLinkKeepaliveMinMs, keepalive_ms));
}

bool packFloat64(double value, uint8_t* out_payload, size_t* inout_len)
{
    if (!out_payload || !inout_len || *inout_len < 9)
    {
        if (inout_len)
        {
            *inout_len = 9;
        }
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    bits.d = value;

    out_payload[0] = 0xCB;
    for (int i = 7; i >= 0; --i)
    {
        out_payload[8 - i] = bits.b[i];
    }
    *inout_len = 9;
    return true;
}

bool unpackFloat64(const uint8_t* data, size_t len, double* out_value)
{
    if (!data || len != 9 || data[0] != 0xCB || !out_value)
    {
        return false;
    }

    union
    {
        double d;
        uint8_t b[8];
    } bits{};
    for (int i = 7; i >= 0; --i)
    {
        bits.b[i] = data[8 - i];
    }
    *out_value = bits.d;
    return true;
}

bool computeLinkIdFromLinkRequest(const uint8_t* raw_packet, size_t raw_len,
                                  const reticulum::ParsedPacket& packet,
                                  uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!raw_packet || raw_len == 0 || !out_hash ||
        packet.packet_type != reticulum::PacketType::LinkRequest ||
        !packet.payload)
    {
        return false;
    }

    uint8_t scratch[kMaxPacketLen] = {};
    if (raw_len > sizeof(scratch))
    {
        return false;
    }

    size_t working_len = raw_len;
    memcpy(scratch, raw_packet, raw_len);

    if (packet.payload_len > 64)
    {
        const size_t trim = packet.payload_len - 64;
        if (trim >= working_len)
        {
            return false;
        }
        working_len -= trim;
    }

    reticulum::computeTruncatedPacketHash(scratch, working_len, out_hash);
    return true;
}

} // namespace

LxmfAdapter::LxmfAdapter(LoraBoard& board,
                         IMeshPeerDirectory* peer_directory)
    : interfaces_(board),
      peer_directory_(peer_directory)
{
    uint8_t seed[sizeof(next_app_packet_id_)] = {};
    fillRandomBytes(seed, sizeof(seed));
    memcpy(&next_app_packet_id_, seed, sizeof(next_app_packet_id_));
    if (next_app_packet_id_ == 0)
    {
        next_app_packet_id_ = 1;
    }
}

void* LxmfAdapter::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void LxmfAdapter::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void LxmfAdapter::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
}

MeshCapabilities LxmfAdapter::getCapabilities() const
{
    MeshCapabilities caps;
    caps.supports_unicast_text = true;
    caps.supports_reticulum_destination_text = true;
    caps.supports_unicast_appdata = true;
    caps.supports_broadcast_appdata = true;
    caps.provides_appdata_sender = true;
    caps.supports_node_info = true;
    caps.supports_reticulum_destination_ping = true;
    caps.supports_reticulum_audio_call = true;
    return caps;
}

bool LxmfAdapter::sendText(ChannelId channel, const std::string& text,
                           MessageId* out_msg_id, NodeId peer)
{
    const MeshSendResult result = sendTextDetailed(channel, text, 0, peer);
    if (out_msg_id)
    {
        *out_msg_id = result.msg_id;
    }
    return result.ok;
}

MeshSendResult LxmfAdapter::sendTextDetailed(ChannelId channel,
                                             const std::string& text,
                                             MessageId forced_msg_id,
                                             NodeId peer)
{
    (void)forced_msg_id;

    char text_preview[64] = {};
    formatLogTextPreview(text, text_preview, sizeof(text_preview));
    Serial.printf("[LXMF][DirectTX] begin ch=%u peer=%08lX len=%u ready=%u text=\"%s\"\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned long>(peer),
                  static_cast<unsigned>(text.size()),
                  isReady() ? 1U : 0U,
                  text_preview);

    if (channel != ChannelId::PRIMARY || text.empty() || peer == 0)
    {
        Serial.printf("[LXMF][DirectTX] reject reason=invalid_input ch=%u peer=%08lX len=%u\n",
                      static_cast<unsigned>(channel),
                      static_cast<unsigned long>(peer),
                      static_cast<unsigned>(text.size()));
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    }

    if (!isReady())
    {
        Serial.printf("[LXMF][DirectTX] reject reason=not_ready peer=%08lX\n",
                      static_cast<unsigned long>(peer));
        return MeshSendResult::fail(MeshOperationFailure::NotReady);
    }

    PeerInfo* peer_info = findOrLoadPeerByNodeId(peer);
    if (!peer_info)
    {
        Serial.printf("[LXMF][DirectTX] reject reason=peer_key_missing peer=%08lX\n",
                      static_cast<unsigned long>(peer));
        return MeshSendResult::fail(MeshOperationFailure::PeerKeyMissing);
    }
    char peer_hash[12] = {};
    char peer_dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashPrefix(peer_info->destination_hash, peer_hash, sizeof(peer_hash));
    formatHashHex(peer_info->destination_hash,
                  sizeof(peer_info->destination_hash),
                  peer_dest_full,
                  sizeof(peer_dest_full));

    if (shouldRequestPath(*peer_info))
    {
        (void)sendPathRequest(*peer_info);
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeTextPayload(static_cast<double>(currentTimestampSeconds()),
                           "",
                           text.c_str(),
                           packed_payload,
                           &packed_payload_len))
    {
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    }

    uint8_t message_hash[reticulum::kFullHashSize] = {};
    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    if (!buildSignedPart(peer_info->destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         message_hash))
    {
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!identity_.sign(signed_part, signed_part_len, signature) ||
        !packMessage(peer_info->destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        return MeshSendResult::fail(MeshOperationFailure::CryptoFailed);
    }

    const MessageId message_id = messageIdFromHash(message_hash);
    LinkSession* active_link =
        findActiveLinkSessionByDestination(peer_info->destination_hash, LocalDestinationKind::Delivery);
    bool ok = false;
    bool send_result_event_deferred = false;
    const bool use_opportunistic = !active_link && peerHasUsableRatchet(*peer_info);
    const char* send_path =
        active_link ? "link" : (use_opportunistic ? "opportunistic" : "deferred_link");
    if (active_link)
    {
        if (lxmf_message_len <= active_link->mdu)
        {
            ok = sendLinkPacket(*active_link,
                                reticulum::PacketType::Data,
                                reticulum::PacketContext::None,
                                lxmf_message,
                                lxmf_message_len,
                                true);
        }
        else
        {
            ok = queueOutgoingResource(*active_link,
                                       lxmf_message,
                                       lxmf_message_len,
                                       0,
                                       nullptr,
                                       0);
        }
    }
    else if (use_opportunistic)
    {
        uint8_t packet[kMaxPacketLen] = {};
        size_t packet_len = sizeof(packet);
        if (buildSignedMessagePacket(*peer_info,
                                     packed_payload,
                                     packed_payload_len,
                                     packet,
                                     &packet_len,
                                     message_hash) &&
            routeAndSendPacket(packet, packet_len, true))
        {
            ok = true;
        }
    }
    else
    {
        Serial.printf("[LXMF][DirectTX] opportunistic_skip peer=%08lX dest=%s reason=no_ratchet\n",
                      static_cast<unsigned long>(peer_info->node_id),
                      peer_hash);
    }

    if (!active_link && !ok)
    {
        bool started = false;
        LinkSession* session =
            ensureOutboundLinkSession(*peer_info, LocalDestinationKind::Delivery, &started);
        if (session)
        {
            send_path = "deferred_link";
            runtime::DeferredLinkPayload deferred{};
            deferred.payload.assign(lxmf_message, lxmf_message + lxmf_message_len);
            deferred.message_id = message_id;
            session->deferred_payloads.push_back(std::move(deferred));
            if (session->state == LinkState::Active)
            {
                flushDeferredLinkPayloads(*session);
            }
            ok = true;
            send_result_event_deferred = true;
        }
    }

    char message_hash_prefix[12] = {};
    formatHashPrefix(message_hash, message_hash_prefix, sizeof(message_hash_prefix));
    const auto& tx_result = interfaces_.lastTxResult();
    Serial.printf("[LXMF][DirectTX] result ok=%u msg=%lu hash=%s peer=%08lX name=\"%s\" dest=%s dest_full=%s path=%s event_deferred=%u bearer=%s complete=%u payload_len=%u text=\"%s\"\n",
                  ok ? 1U : 0U,
                  static_cast<unsigned long>(message_id),
                  message_hash_prefix,
                  static_cast<unsigned long>(peer_info->node_id),
                  peer_info->display_name[0] != '\0' ? peer_info->display_name : "<unnamed>",
                  peer_hash,
                  peer_dest_full,
                  send_path,
                  send_result_event_deferred ? 1U : 0U,
                  txBearerName(tx_result),
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  static_cast<unsigned>(packed_payload_len),
                  text_preview);
    MeshSendResult result =
        ok ? MeshSendResult::success(message_id)
           : MeshSendResult::fail(MeshOperationFailure::RadioTxFailed, message_id);
    result.reticulum_identity = reticulumIdentityForPeer(*peer_info);
    if (!send_result_event_deferred)
    {
        sys::EventBus::publish(new sys::ChatSendResultEvent(message_id, ok), 0);
    }
    return result;
}

MeshSendResult LxmfAdapter::sendTextToReticulumDestination(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    const ReticulumPeerIdentity& destination)
{
    char dest_hash[12] = {};
    char dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    char text_preview[64] = {};
    formatHashPrefix(destination.destination_hash, dest_hash, sizeof(dest_hash));
    formatHashHex(destination.destination_hash,
                  sizeof(destination.destination_hash),
                  dest_full,
                  sizeof(dest_full));
    formatLogTextPreview(text, text_preview, sizeof(text_preview));
    Serial.printf("[LXMF][DestinationTX] begin ch=%u forced=%lu dest=%s dest_full=%s len=%u ready=%u text=\"%s\"\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned long>(forced_msg_id),
                  dest_hash,
                  dest_full,
                  static_cast<unsigned>(text.size()),
                  isReady() ? 1U : 0U,
                  text_preview);

    if (channel != ChannelId::PRIMARY || text.empty() ||
        !hasReticulumDestinationIdentity(destination))
    {
        Serial.printf("[LXMF][DestinationTX] reject reason=invalid_input ch=%u dest=%s len=%u\n",
                      static_cast<unsigned>(channel),
                      dest_hash,
                      static_cast<unsigned>(text.size()));
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    }

    if (!isReady())
    {
        Serial.printf("[LXMF][DestinationTX] reject reason=not_ready dest=%s\n", dest_hash);
        return MeshSendResult::fail(MeshOperationFailure::NotReady);
    }

    const bool configured_group = isConfiguredGroupDestination(destination);
    if (!configured_group)
    {
        PeerInfo* peer_info =
            findOrLoadPeerByDestinationHash(destination.destination_hash);
        if (!peer_info)
        {
            const bool path_requested =
                sendPathRequestForDestination(destination.destination_hash);
            Serial.printf("[LXMF][DestinationTX] path_pending dest=%s requested=%u\n",
                          dest_hash,
                          path_requested ? 1U : 0U);
            MeshSendResult result = MeshSendResult::fail(
                MeshOperationFailure::NotReady,
                0,
                path_requested ? 1 : 2);
            result.reticulum_identity =
                makeReticulumDestinationIdentity(destination.destination_hash);
            return result;
        }

        Serial.printf("[LXMF][DestinationTX] resolved dest=%s peer=%08lX name=\"%s\"\n",
                      dest_hash,
                      static_cast<unsigned long>(peer_info->node_id),
                      peer_info->display_name[0] != '\0' ? peer_info->display_name : "<unnamed>");
        MeshSendResult result =
            sendTextDetailed(channel, text, forced_msg_id, peer_info->node_id);
        if (!hasReticulumDestinationIdentity(result.reticulum_identity))
        {
            result.reticulum_identity = reticulumIdentityForPeer(*peer_info);
        }
        return result;
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeTextPayload(static_cast<double>(currentTimestampSeconds()),
                           "",
                           text.c_str(),
                           packed_payload,
                           &packed_payload_len))
    {
        Serial.printf("[LXMF][GroupTX] reject reason=encode_failed dest=%s text_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(text.size()));
        return MeshSendResult::fail(MeshOperationFailure::EncodeFailed);
    }

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildGroupMessagePacket(destination,
                                 packed_payload,
                                 packed_payload_len,
                                 packet,
                                 &packet_len,
                                 message_hash))
    {
        Serial.printf("[LXMF][GroupTX] reject reason=build_packet_failed dest=%s payload_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(packed_payload_len));
        return MeshSendResult::fail(MeshOperationFailure::CryptoFailed);
    }

    const MessageId message_id =
        forced_msg_id != 0 ? forced_msg_id : messageIdFromHash(message_hash);
    char message_hash_prefix[12] = {};
    formatHashPrefix(message_hash, message_hash_prefix, sizeof(message_hash_prefix));
    Serial.printf("[LXMF][GroupTX] packet_ready msg=%lu hash=%s dest=%s payload_len=%u packet_len=%u\n",
                  static_cast<unsigned long>(message_id),
                  message_hash_prefix,
                  dest_hash,
                  static_cast<unsigned>(packed_payload_len),
                  static_cast<unsigned>(packet_len));
    const bool ok = routeAndSendPacket(packet, packet_len, true);
    const auto& tx_result = interfaces_.lastTxResult();
    Serial.printf("[LXMF][GroupTX] raw_send ok=%u msg=%lu dest=%s dest_full=%s bearer=%s complete=%u packet_len=%u text=\"%s\"\n",
                  ok ? 1U : 0U,
                  static_cast<unsigned long>(message_id),
                  dest_hash,
                  dest_full,
                  txBearerName(tx_result),
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  static_cast<unsigned>(packet_len),
                  text_preview);
    MeshSendResult result =
        ok ? MeshSendResult::success(message_id)
           : MeshSendResult::fail(MeshOperationFailure::RadioTxFailed, message_id);
    result.reticulum_identity =
        makeReticulumDestinationIdentity(destination.destination_hash);
    sys::EventBus::publish(new sys::ChatSendResultEvent(message_id, ok), 0);
    return result;
}

bool LxmfAdapter::pollIncomingText(MeshIncomingText* out)
{
    return text_receive_queue_.pop(out);
}

bool LxmfAdapter::sendAppData(ChannelId channel, uint32_t portnum,
                              const uint8_t* payload, size_t len,
                              NodeId dest, bool want_ack,
                              MessageId packet_id,
                              bool want_response)
{
    (void)want_ack;

    Serial.printf("[LXMF][AppDataTX] begin ch=%u port=%lu dest=%08lX len=%u ready=%u want_ack=%u want_response=%u\n",
                  static_cast<unsigned>(channel),
                  static_cast<unsigned long>(portnum),
                  static_cast<unsigned long>(dest),
                  static_cast<unsigned>(len),
                  isReady() ? 1U : 0U,
                  want_ack ? 1U : 0U,
                  want_response ? 1U : 0U);

    if (channel != ChannelId::PRIMARY || portnum == 0 || (!payload && len != 0) || !isReady())
    {
        Serial.printf("[LXMF][AppDataTX] reject reason=invalid_input ch=%u port=%lu dest=%08lX len=%u ready=%u\n",
                      static_cast<unsigned>(channel),
                      static_cast<unsigned long>(portnum),
                      static_cast<unsigned long>(dest),
                      static_cast<unsigned>(len),
                      isReady() ? 1U : 0U);
        if (packet_id != 0)
        {
            sys::EventBus::publish(new sys::ChatSendResultEvent(packet_id, false), 0);
        }
        return false;
    }

    MessageId effective_packet_id = packet_id;
    if (effective_packet_id == 0)
    {
        effective_packet_id = next_app_packet_id_++;
        if (next_app_packet_id_ == 0)
        {
            next_app_packet_id_ = 1;
        }
    }
    else if (effective_packet_id >= next_app_packet_id_)
    {
        next_app_packet_id_ = effective_packet_id + 1;
        if (next_app_packet_id_ == 0)
        {
            next_app_packet_id_ = 1;
        }
    }

    uint8_t packed_payload[kMaxLxmfMessageLen] = {};
    size_t packed_payload_len = sizeof(packed_payload);
    if (!encodeAppDataPayload(portnum,
                              effective_packet_id,
                              0,
                              want_response,
                              payload,
                              len,
                              packed_payload,
                              &packed_payload_len))
    {
        sys::EventBus::publish(new sys::ChatSendResultEvent(effective_packet_id, false), 0);
        return false;
    }

    bool ok = false;
    if (dest != 0)
    {
        PeerInfo* peer_info = findOrLoadPeerByNodeId(dest);
        if (!peer_info)
        {
            Serial.printf("[LXMF][AppDataTX] reject reason=peer_key_missing port=%lu dest=%08lX msg=%lu\n",
                          static_cast<unsigned long>(portnum),
                          static_cast<unsigned long>(dest),
                          static_cast<unsigned long>(effective_packet_id));
            sys::EventBus::publish(new sys::ChatSendResultEvent(effective_packet_id, false), 0);
            return false;
        }

        if (shouldRequestPath(*peer_info))
        {
            (void)sendPathRequest(*peer_info);
        }

        uint8_t message_hash[reticulum::kFullHashSize] = {};
        uint8_t signed_part[kSignedPartMaxLen] = {};
        size_t signed_part_len = sizeof(signed_part);
        uint8_t signature[reticulum::kSignatureSize] = {};
        uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
        size_t lxmf_message_len = sizeof(lxmf_message);
        const bool have_link_payload =
            buildSignedPart(peer_info->destination_hash,
                            identity_.destinationHash(),
                            packed_payload,
                            packed_payload_len,
                            signed_part,
                            &signed_part_len,
                            message_hash) &&
            identity_.sign(signed_part, signed_part_len, signature) &&
            packMessage(peer_info->destination_hash,
                        identity_.destinationHash(),
                        signature,
                        packed_payload,
                        packed_payload_len,
                        lxmf_message,
                        &lxmf_message_len);

        LinkSession* active_link =
            findActiveLinkSessionByDestination(peer_info->destination_hash, LocalDestinationKind::Delivery);
        if (active_link && have_link_payload)
        {
            if (lxmf_message_len <= active_link->mdu)
            {
                ok = sendLinkPacket(*active_link,
                                    reticulum::PacketType::Data,
                                    reticulum::PacketContext::None,
                                    lxmf_message,
                                    lxmf_message_len,
                                    true);
            }
            else
            {
                ok = queueOutgoingResource(*active_link,
                                           lxmf_message,
                                           lxmf_message_len,
                                           0,
                                           nullptr,
                                           0);
            }
        }
        else
        {
            uint8_t packet[kMaxPacketLen] = {};
            size_t packet_len = sizeof(packet);
            if (buildSignedMessagePacket(*peer_info,
                                         packed_payload,
                                         packed_payload_len,
                                         packet,
                                         &packet_len,
                                         message_hash))
            {
                ok = routeAndSendPacket(packet, packet_len, true);
            }

            if (!ok && have_link_payload)
            {
                bool started = false;
                LinkSession* session =
                    ensureOutboundLinkSession(*peer_info, LocalDestinationKind::Delivery, &started);
                if (session)
                {
                    runtime::DeferredLinkPayload deferred{};
                    deferred.payload.assign(lxmf_message, lxmf_message + lxmf_message_len);
                    session->deferred_payloads.push_back(std::move(deferred));
                    if (session->state == LinkState::Active)
                    {
                        flushDeferredLinkPayloads(*session);
                    }
                    ok = true;
                }
            }
        }
    }
    else
    {
        bool have_peer = false;
        unsigned fanout_count = 0;
        ok = true;
        // LXMF delivery is single-destination. For device-side team/business
        // traffic we currently treat dest==0 as fan-out to every known peer.
        for (auto& peer_info : peers_)
        {
            if (isZeroBytes(peer_info.destination_hash, sizeof(peer_info.destination_hash)))
            {
                continue;
            }

            have_peer = true;
            ++fanout_count;
            if (shouldRequestPath(peer_info))
            {
                (void)sendPathRequest(peer_info);
            }

            uint8_t packet[kMaxPacketLen] = {};
            size_t packet_len = sizeof(packet);
            uint8_t message_hash[reticulum::kFullHashSize] = {};
            if (!buildSignedMessagePacket(peer_info,
                                          packed_payload,
                                          packed_payload_len,
                                          packet,
                                          &packet_len,
                                          message_hash) ||
                !routeAndSendPacket(packet, packet_len, true))
            {
                ok = false;
            }
        }
        ok = have_peer && ok;
        Serial.printf("[LXMF][AppDataTX] fanout port=%lu msg=%lu peers=%u ok=%u\n",
                      static_cast<unsigned long>(portnum),
                      static_cast<unsigned long>(effective_packet_id),
                      fanout_count,
                      ok ? 1U : 0U);
    }

    Serial.printf("[LXMF][AppDataTX] end port=%lu msg=%lu dest=%08lX ok=%u\n",
                  static_cast<unsigned long>(portnum),
                  static_cast<unsigned long>(effective_packet_id),
                  static_cast<unsigned long>(dest),
                  ok ? 1U : 0U);
    sys::EventBus::publish(new sys::ChatSendResultEvent(effective_packet_id, ok), 0);
    return ok;
}

bool LxmfAdapter::pollIncomingData(MeshIncomingData* out)
{
    return data_receive_queue_.pop(out);
}

bool LxmfAdapter::requestNodeInfo(NodeId dest, bool want_response)
{
    (void)want_response;

    if (dest != 0 && dest != 0xFFFFFFFFUL)
    {
        PeerInfo* peer = findOrLoadPeerByNodeId(dest);
        if (!peer)
        {
            return false;
        }

        if (!shouldRequestPath(*peer))
        {
            return true;
        }
        return sendPathRequest(*peer);
    }

    return broadcastSelfIdentity();
}

bool LxmfAdapter::broadcastSelfIdentity()
{
    if (config_.reticulum_anonymous_peer)
    {
        Serial.println("[LXMF][AnnounceTX] skip reason=anonymous_peer trigger=broadcast_self");
        announce_pending_ = false;
        return false;
    }

    announce_pending_ = true;
    const bool delivery_ok = sendAnnounce(LocalDestinationKind::Delivery);
    const bool delivery_complete = lastAnnounceTxReachedRequiredInterfaces(delivery_ok);
    const bool propagation_ok = sendAnnounce(LocalDestinationKind::Propagation);
    const bool propagation_complete = lastAnnounceTxReachedRequiredInterfaces(propagation_ok);
    const bool call_audio_ok = sendAnnounce(LocalDestinationKind::CallAudio);
    const bool call_audio_complete =
        !interfaces_.wifiGatewayConfigured() ||
        lastAnnounceTxReachedRequiredInterfaces(call_audio_ok);
    if (delivery_ok || propagation_ok || call_audio_ok)
    {
        last_announce_ms_ = millis();
    }
    last_announce_attempt_ms_ = millis();
    announce_pending_ = !(delivery_complete && propagation_complete && call_audio_complete);
    return delivery_ok || propagation_ok || call_audio_ok;
}

NodeId LxmfAdapter::getNodeId() const
{
    return identity_.nodeId();
}

bool LxmfAdapter::getReticulumLocalIdentityInfo(ReticulumLocalIdentityInfo* out) const
{
    if (!out)
    {
        return false;
    }
    *out = ReticulumLocalIdentityInfo{};
    out->anonymous_peer = config_.reticulum_anonymous_peer;
    const char* display_name = effectiveDisplayName();
    if (display_name && display_name[0] != '\0')
    {
        copyCString(out->display_name, sizeof(out->display_name), display_name);
    }
    if (!identity_.isReady())
    {
        return false;
    }

    out->ready = true;
    out->node_id = identity_.nodeId();
    std::memcpy(out->identity_hash,
                identity_.identityHash(),
                reticulum::kTruncatedHashSize);
    localDestinationHash(LocalDestinationKind::Delivery, out->lxmf_address);
    localDestinationHash(LocalDestinationKind::Propagation, out->propagation_address);
    return true;
}

MeshActionResult LxmfAdapter::startReticulumAudioCall(
    const ReticulumPeerIdentity& destination)
{
    if (config_.reticulum_anonymous_peer)
    {
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }
    if (!chat::hasReticulumDestinationIdentity(destination) || !identity_.isReady())
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    interfaces_.maintain();
    if (!interfaces_.wifiGatewayConfigured() || !interfaces_.hasReadyWifiGateway())
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }

    const PeerInfo* peer = findOrLoadPeerByDestinationHash(destination.destination_hash);
    if (!peer && !isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        peer = findPeerByIdentityHash(destination.identity_hash);
    }

    uint8_t remote_identity_hash[reticulum::kTruncatedHashSize] = {};
    if (peer && !isZeroBytes(peer->identity_hash, sizeof(peer->identity_hash)))
    {
        copyHash(remote_identity_hash, peer->identity_hash, sizeof(remote_identity_hash));
    }
    else if (!isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        copyHash(remote_identity_hash,
                 destination.identity_hash,
                 sizeof(remote_identity_hash));
    }
    else
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    uint8_t call_destination_hash[reticulum::kTruncatedHashSize] = {};
    callAudioDestinationHashForIdentity(remote_identity_hash, call_destination_hash);
    if (isZeroBytes(call_destination_hash, sizeof(call_destination_hash)))
    {
        return MeshActionResult::fail(MeshOperationFailure::EncodeFailed);
    }

    if (runtime::findOpenLinkSessionByDestination(
            links_, call_destination_hash, LocalDestinationKind::CallAudio))
    {
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }

    const PathEntry* path = findPath(call_destination_hash);
    if (!path)
    {
        (void)sendPathRequestForDestination(call_destination_hash);
        return MeshActionResult::fail(MeshOperationFailure::NotReady, 1);
    }

    LinkSession& session = runtime::appendLinkSession(links_, kMaxLinkSessions);
    session.created_ms = millis();
    session.request_ms = session.created_ms;
    session.last_inbound_ms = session.created_ms;
    session.initiator = true;
    session.destination = LocalDestinationKind::CallAudio;
    session.state = LinkState::Pending;
    session.close_reason = LinkCloseReason::None;
    session.expected_hops = path->hops;
    session.remote_identity_known =
        !isZeroBytes(remote_identity_hash, sizeof(remote_identity_hash));
    session.validated = false;
    session.keepalive_interval_ms = kLinkKeepaliveMaxMs;
    session.stale_timeout_ms = kLinkKeepaliveMaxMs * 2U;
    copyHash(session.remote_destination_hash,
             call_destination_hash,
             sizeof(session.remote_destination_hash));
    copyHash(session.remote_identity_hash,
             remote_identity_hash,
             sizeof(session.remote_identity_hash));
    if (peer)
    {
        memcpy(session.peer_identity_sig_pub,
               peer->sig_pub,
               sizeof(session.peer_identity_sig_pub));
    }

    Curve25519::dh1(session.local_enc_pub, session.local_enc_priv);
    if (isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)) ||
        !generateLinkSigningKey(session.local_sig_pub, session.local_sig_priv) ||
        !sendLinkRequest(session))
    {
        links_.sessions.pop_back();
        return MeshActionResult::fail(MeshOperationFailure::RadioTxFailed);
    }

    ::platform::ui::reticulum_call::Peer call_peer{};
    copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
    if (peer)
    {
        copyHash(call_peer.destination_hash,
                 peer->destination_hash,
                 sizeof(call_peer.destination_hash));
        copyHash(call_peer.identity_hash,
                 peer->identity_hash,
                 sizeof(call_peer.identity_hash));
        call_peer.display_name =
            peer->display_name[0] != '\0' ? peer->display_name : nullptr;
    }
    else
    {
        copyHash(call_peer.destination_hash,
                 destination.destination_hash,
                 sizeof(call_peer.destination_hash));
        copyHash(call_peer.identity_hash,
                 remote_identity_hash,
                 sizeof(call_peer.identity_hash));
    }
    call_peer.incoming = false;
    ::platform::ui::reticulum_call::update_peer(call_peer);

    return MeshActionResult::success();
}

MeshActionResult LxmfAdapter::pingReticulumDestination(
    const ReticulumPeerIdentity& destination)
{
    char dest_hash[12] = {};
    char dest_full[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashPrefix(destination.destination_hash, dest_hash, sizeof(dest_hash));
    formatHashHex(destination.destination_hash,
                  sizeof(destination.destination_hash),
                  dest_full,
                  sizeof(dest_full));
    Serial.printf("[LXMF][PingTX] begin dest=%s dest_full=%s ready=%u\n",
                  dest_hash,
                  dest_full,
                  isReady() ? 1U : 0U);

    if (!chat::hasReticulumDestinationIdentity(destination))
    {
        Serial.printf("[LXMF][PingTX] reject reason=invalid_input dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }
    if (!isReady())
    {
        Serial.printf("[LXMF][PingTX] reject reason=not_ready dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }
    if (isConfiguredGroupDestination(destination))
    {
        Serial.printf("[LXMF][PingTX] reject reason=group_destination dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }

    PeerInfo* peer = findOrLoadPeerByDestinationHash(destination.destination_hash);
    if (!peer && !isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        if (const PeerInfo* by_identity = findPeerByIdentityHash(destination.identity_hash))
        {
            peer = findOrLoadPeerByDestinationHash(by_identity->destination_hash);
        }
    }
    if (!peer)
    {
        const bool path_requested =
            sendPathRequestForDestination(destination.destination_hash);
        Serial.printf("[LXMF][PingTX] path_pending dest=%s requested=%u\n",
                      dest_hash,
                      path_requested ? 1U : 0U);
        return MeshActionResult::fail(MeshOperationFailure::NotReady,
                                      path_requested ? 1 : 2);
    }
    if (isZeroBytes(peer->identity_hash, sizeof(peer->identity_hash)) ||
        (!peerHasUsableRatchet(*peer) && isZeroBytes(peer->enc_pub, sizeof(peer->enc_pub))))
    {
        Serial.printf("[LXMF][PingTX] reject reason=peer_key_missing dest=%s peer=%08lX\n",
                      dest_hash,
                      static_cast<unsigned long>(peer->node_id));
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    if (shouldRequestPath(*peer))
    {
        const bool requested = sendPathRequest(*peer);
        Serial.printf("[LXMF][PingTX] path_refresh dest=%s requested=%u\n",
                      dest_hash,
                      requested ? 1U : 0U);
    }

    std::vector<uint8_t> packet(kMaxPacketLen, 0);
    size_t packet_len = packet.size();
    if (!buildEncryptedPacketForPeer(*peer, nullptr, 0, packet.data(), &packet_len))
    {
        Serial.printf("[LXMF][PingTX] reject reason=build_packet_failed dest=%s\n",
                      dest_hash);
        return MeshActionResult::fail(MeshOperationFailure::CryptoFailed);
    }

    const bool ok = routeAndSendPacket(packet.data(), packet_len, true);
    const auto& tx_result = interfaces_.lastTxResult();
    Serial.printf("[LXMF][PingTX] raw_send ok=%u dest=%s dest_full=%s bearer=%s complete=%u packet_len=%u\n",
                  ok ? 1U : 0U,
                  dest_hash,
                  dest_full,
                  txBearerName(tx_result),
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  static_cast<unsigned>(packet_len));
    return ok ? MeshActionResult::success()
              : MeshActionResult::fail(MeshOperationFailure::RadioTxFailed);
}

MeshActionResult LxmfAdapter::persistReticulumPeer(
    const ReticulumPeerIdentity& destination,
    bool favorite)
{
    if (!chat::hasReticulumDestinationIdentity(destination))
    {
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }

    PeerInfo* peer = findOrLoadPeerByDestinationHash(destination.destination_hash);
    if (!peer && !isZeroBytes(destination.identity_hash, sizeof(destination.identity_hash)))
    {
        if (const PeerInfo* by_identity = findPeerByIdentityHash(destination.identity_hash))
        {
            peer = findOrLoadPeerByDestinationHash(by_identity->destination_hash);
        }
    }
    if (!peer)
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }

    const MeshActionResult result = persistPeerAddressNow(*peer, favorite);
    if (result.ok)
    {
        publishPeerUpdate(*peer);
    }
    return result;
}

MeshActionResult LxmfAdapter::requestNomadPage(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const char* path)
{
    char destination_text[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(destination_hash,
                  reticulum::kTruncatedHashSize,
                  destination_text,
                  sizeof(destination_text));
    LXMF_NOMAD_PAGE_LOG("queue begin dest=%s path=%s ready=%u pending=%u\n",
                        destination_text,
                        path ? path : "<null>",
                        isReady() ? 1U : 0U,
                        static_cast<unsigned>(pending_nomad_page_requests_.size()));

    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize) ||
        !path || path[0] == '\0' ||
        std::strlen(path) >= kNomadPagePathMaxLen)
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=invalid_input dest=%s path=%s\n",
                            destination_text,
                            path ? path : "<null>");
        return MeshActionResult::fail(MeshOperationFailure::InvalidInput);
    }
    if (!isReady())
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=not_ready dest=%s path=%s\n",
                            destination_text,
                            path);
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }

    for (const auto& pending : pending_nomad_page_requests_)
    {
        if (hashesEqual(pending.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::strcmp(pending.path, path) == 0)
        {
            updateNomadPageProgress(pending,
                                    5,
                                    "Queued Nomad page request",
                                    path,
                                    true,
                                    false,
                                    PageFailureKind::None);
            MeshActionResult result = MeshActionResult::success();
            result.detail = 1;
            LXMF_NOMAD_PAGE_LOG("queue duplicate dest=%s path=%s\n",
                                destination_text,
                                path);
            return result;
        }
    }

    if (pending_nomad_page_requests_.size() >= kMaxPendingNomadPageRequests)
    {
        LXMF_NOMAD_PAGE_LOG("queue reject reason=busy dest=%s path=%s depth=%u\n",
                            destination_text,
                            path,
                            static_cast<unsigned>(pending_nomad_page_requests_.size()));
        return MeshActionResult::fail(MeshOperationFailure::Busy);
    }

    PendingNomadPageRequest request{};
    copyHash(request.destination_hash,
             destination_hash,
             sizeof(request.destination_hash));
    std::snprintf(request.path, sizeof(request.path), "%s", path);
    request.created_ms = millis();
    pending_nomad_page_requests_.push_back(request);
    updateNomadPageProgress(pending_nomad_page_requests_.back(),
                            5,
                            "Queued Nomad page request",
                            path,
                            true,
                            false,
                            PageFailureKind::None);

    LXMF_NOMAD_PAGE_LOG("queued dest=%s path=%s pending=%u\n",
                        destination_text,
                        path,
                        static_cast<unsigned>(pending_nomad_page_requests_.size()));
    pumpNomadPageRequests();
    return MeshActionResult::success();
}

void LxmfAdapter::applyConfig(const MeshConfig& config)
{
    config_ = config;
    if (identity_.init() && !peers_loaded_)
    {
        loadPersistedPeers();
    }
    interfaces_.applyConfig(config_);
    if (identity_.isReady())
    {
        uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
        localDestinationHash(LocalDestinationKind::Delivery, delivery_hash);
        localDestinationHash(LocalDestinationKind::Propagation, propagation_hash);

        char identity_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        char delivery_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        char propagation_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(identity_.identityHash(),
                      reticulum::kTruncatedHashSize,
                      identity_hex,
                      sizeof(identity_hex));
        formatHashHex(delivery_hash,
                      sizeof(delivery_hash),
                      delivery_hex,
                      sizeof(delivery_hex));
        formatHashHex(propagation_hash,
                      sizeof(propagation_hash),
                      propagation_hex,
                      sizeof(propagation_hex));
        Serial.printf("[LXMF][Identity] node=%08lX identity=%s delivery=%s propagation=%s anonymous=%d peers=%u\n",
                      static_cast<unsigned long>(identity_.nodeId()),
                      identity_hex,
                      delivery_hex,
                      propagation_hex,
                      config_.reticulum_anonymous_peer ? 1 : 0,
                      static_cast<unsigned>(peers_.size()));
    }
    last_announce_ms_ = millis();
    last_announce_attempt_ms_ = 0;
    announce_pending_ = !config_.reticulum_anonymous_peer;
}

void LxmfAdapter::setUserInfo(const char* long_name, const char* short_name)
{
    user_long_name_ = (long_name && long_name[0] != '\0') ? long_name : "";
    user_short_name_ = (short_name && short_name[0] != '\0') ? short_name : "";
    last_announce_attempt_ms_ = 0;
    announce_pending_ = true;
}

bool LxmfAdapter::setWifiTransportEnabled(bool enabled)
{
    interfaces_.setWifiTransportEnabled(enabled);
    return true;
}

bool LxmfAdapter::isReady() const
{
    return identity_.isReady() && interfaces_.hasReadyInterface();
}

bool LxmfAdapter::pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len)
{
    (void)out_data;
    (void)out_len;
    (void)max_len;
    return false;
}

void LxmfAdapter::handleRawPacket(const uint8_t* data, size_t size)
{
    interfaces_.handleRawPacket(data, size);
}

void LxmfAdapter::setLastRxStats(float rssi, float snr)
{
    interfaces_.setLastRxStats(rssi, snr);
}

void LxmfAdapter::processSendQueue()
{
    processRuntime();
}

LxmfAdapter::RuntimeBudget LxmfAdapter::makeRuntimeBudget() const
{
    RuntimeBudget budget{};
    if (::platform::ui::reticulum_call::realtime_mode_active())
    {
        budget.live_packet_limit = kCallIngressPacketsPerPoll;
        budget.deferred_discovery_limit = 0;
        budget.allow_public_discovery = false;
        budget.allow_persistence = false;
        budget.allow_peer_projection = false;
        budget.allow_announce_tx = false;
        budget.drop_public_discovery = true;
        budget.phase = "call";
        return budget;
    }

    if (!pending_nomad_page_requests_.empty())
    {
        budget.live_packet_limit = kMaxIngressPacketsPerPoll;
        budget.deferred_discovery_limit = 0;
        budget.allow_public_discovery = false;
        budget.allow_persistence = false;
        budget.allow_peer_projection = false;
        budget.allow_announce_tx = false;
        budget.drop_public_discovery = true;
        budget.phase = "nomad";
        return budget;
    }

    const bool maintenance_window =
        screen_runtime::is_sleeping() && !screen_runtime::is_saver_active();
    if (maintenance_window)
    {
        budget.live_packet_limit = kMaxIngressPacketsPerPoll;
        budget.deferred_discovery_limit = 1;
        budget.allow_public_discovery = true;
        budget.allow_persistence = true;
        budget.allow_peer_projection = true;
        budget.allow_announce_tx = true;
        budget.phase = "sleep";
        return budget;
    }

    budget.live_packet_limit = 1;
    budget.deferred_discovery_limit = 0;
    budget.allow_public_discovery = false;
    budget.allow_persistence = false;
    budget.allow_peer_projection = !screen_runtime::is_saver_active();
    budget.allow_announce_tx = true;
    budget.phase = screen_runtime::is_saver_active() ? "saver" : "screen";
    return budget;
}

void LxmfAdapter::processRuntime()
{
    const RuntimeBudget budget = makeRuntimeBudget();
    if (::platform::ui::reticulum_call::realtime_mode_active())
    {
        processRadioPackets(budget);
        pumpReticulumAudioCall();
        return;
    }

    cullTransportState();
    cullLinkSessions();
    const runtime::PropagationRuntimeLimits propagation_limits{
        kMaxPropagationEntries,
        kMaxPropagationTransients,
        kMaxPropagationPeers,
        kPropagationEntryTtlS,
        kPropagationTransientTtlS,
        kPropagationEntryTtlS};
    runtime::cullPropagationRuntime(propagation_,
                                    currentTimestampSeconds(),
                                    propagation_limits);

    processRadioPackets(budget);
    pumpReticulumAudioCall();
    pumpNomadPageRequests();
    processDeferredDiscoveryPackets(budget);

    if (budget.allow_peer_projection)
    {
        pumpPendingPeerUpdates();
    }
    if (budget.allow_announce_tx)
    {
        maybeAnnounce();
    }
}

void LxmfAdapter::processRadioPackets(const RuntimeBudget& budget)
{
    uint8_t polled_packets = 0;
    while (polled_packets < budget.live_packet_limit &&
           interfaces_.pollIncomingPacket(&rx_packet_scratch_))
    {
        ++polled_packets;
        (void)processOneRadioPacket(rx_packet_scratch_, budget, false);
    }

    MeshIncomingData discarded;
    while (interfaces_.pollLegacyIncomingData(&discarded))
    {
    }
}

bool LxmfAdapter::processOneRadioPacket(
    const reticulum::interfaces::RxPacket& rx_packet,
    const RuntimeBudget& budget,
    bool deferred_replay)
{
    const uint8_t* packet = rx_packet.data;
    const size_t packet_len = rx_packet.len;
    const auto ingress_interface = rx_packet.interface_kind;
    const bool ingress_wifi =
        ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway;
    const char* iface_label = ingress_wifi ? "wifi" : "lora";
    active_rx_meta_ = rx_packet.rx_meta;
    has_active_rx_meta_ = true;
    struct ActiveRxMetaScope
    {
        bool& active;
        ~ActiveRxMetaScope() { active = false; }
    } active_rx_meta_scope{has_active_rx_meta_};

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(packet, packet_len, &parsed))
    {
        noteRxSummary(false, false, true);
        if (!ingress_wifi)
        {
            Serial.printf("[LXMF][RawRX] drop reason=parse_failed raw_len=%u first=%02X\n",
                          static_cast<unsigned>(packet_len),
                          static_cast<unsigned>(packet_len > 0 ? packet[0] : 0));
        }
        return false;
    }

    char dest_hash[12] = {};
    formatHashPrefix(parsed.destination_hash, dest_hash, sizeof(dest_hash));
    if (::platform::ui::reticulum_call::resource_preempt_active())
    {
        uint8_t call_link_id[reticulum::kTruncatedHashSize] = {};
        const bool has_call_link =
            ::platform::ui::reticulum_call::current_link_id(call_link_id);
        LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
        const bool call_link_request =
            ingress_wifi &&
            parsed.packet_type == reticulum::PacketType::LinkRequest &&
            isLocalDestinationHash(parsed.destination_hash, &local_kind) &&
            local_kind == LocalDestinationKind::CallAudio;
        const bool current_call_link_packet =
            has_call_link &&
            parsed.destination_type == reticulum::DestinationType::Link &&
            parsed.destination_hash &&
            hashesEqual(parsed.destination_hash,
                        call_link_id,
                        reticulum::kTruncatedHashSize);
        if (!call_link_request && !current_call_link_packet)
        {
            noteRxSummary(true, false, false);
            return false;
        }
    }
    if (budget.drop_public_discovery &&
        isPublicDiscoveryPacket(parsed) &&
        !isForegroundDiscoveryDestination(parsed.destination_hash))
    {
        noteRxSummary(false, false, false, false, false, true);
        return false;
    }

    noteRxSummary();
    const bool log_detail =
        shouldLogRxDetail(parsed, ingress_interface, budget) && !deferred_replay;
    if (log_detail)
    {
        Serial.printf("[LXMF][RawRX] packet iface=%s type=%u dest_type=%u context=%u dest=%s raw_len=%u payload_len=%u hops=%u phase=%s\n",
                      iface_label,
                      static_cast<unsigned>(parsed.packet_type),
                      static_cast<unsigned>(parsed.destination_type),
                      static_cast<unsigned>(parsed.context),
                      dest_hash,
                      static_cast<unsigned>(packet_len),
                      static_cast<unsigned>(parsed.payload_len),
                      static_cast<unsigned>(parsed.hops),
                      budget.phase);
    }
    else if (!deferred_replay && ingress_wifi && budget.phase &&
             std::strcmp(budget.phase, "nomad") == 0 && packet_len >= 256U)
    {
        char transport_hash[12] = {};
        formatHashPrefix(parsed.transport_id, transport_hash, sizeof(transport_hash));
        Serial.printf("[LXMF][RawRX] packet_probe iface=%s type=%u dest_type=%u context=%u dest=%s transport=%s raw_len=%u payload_len=%u hops=%u phase=%s\n",
                      iface_label,
                      static_cast<unsigned>(parsed.packet_type),
                      static_cast<unsigned>(parsed.destination_type),
                      static_cast<unsigned>(parsed.context),
                      dest_hash,
                      transport_hash,
                      static_cast<unsigned>(packet_len),
                      static_cast<unsigned>(parsed.payload_len),
                      static_cast<unsigned>(parsed.hops),
                      budget.phase);
    }
    if (parsed.hops < 0xFF)
    {
        parsed.hops += 1;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(packet, packet_len, packet_hash);
    if (isDuplicatePacket(packet_hash))
    {
        noteRxSummary(false, true, false);
        if (!ingress_wifi && !deferred_replay)
        {
            Serial.printf("[LXMF][RawRX] drop reason=duplicate dest=%s\n", dest_hash);
        }
        return false;
    }

    if (shouldDeferDiscoveryPacket(parsed, ingress_interface, budget))
    {
        if (!hasDeferredDiscoveryPacket(packet_hash) &&
            enqueueDeferredDiscoveryPacket(rx_packet, packet_hash))
        {
            noteRxSummary(false, false, false, true, false);
            return true;
        }
        noteRxSummary(false, false, false, true, true);
        return false;
    }

    rememberPacket(packet_hash);

    if (!deferred_replay && ingress_wifi && !shouldProcessWifiIngressPacket(parsed))
    {
        if (budget.phase && std::strcmp(budget.phase, "nomad") == 0 &&
            (packet_len >= 256U ||
             parsed.destination_type == reticulum::DestinationType::Link ||
             parsed.context == static_cast<uint8_t>(reticulum::PacketContext::Resource)))
        {
            char transport_hash[12] = {};
            formatHashPrefix(parsed.transport_id, transport_hash, sizeof(transport_hash));
            Serial.printf("[LXMF][RawRX] skip reason=wifi_not_foreground type=%u dest_type=%u context=%u dest=%s transport=%s raw_len=%u payload_len=%u hops=%u phase=%s\n",
                          static_cast<unsigned>(parsed.packet_type),
                          static_cast<unsigned>(parsed.destination_type),
                          static_cast<unsigned>(parsed.context),
                          dest_hash,
                          transport_hash,
                          static_cast<unsigned>(packet_len),
                          static_cast<unsigned>(parsed.payload_len),
                          static_cast<unsigned>(parsed.hops),
                          budget.phase);
        }
        noteRxSummary(true, false, false);
        return false;
    }

    if (parsed.packet_type == reticulum::PacketType::Announce)
    {
        return handleAnnouncePacket(packet,
                                    packet_len,
                                    parsed,
                                    ingress_interface,
                                    budget.allow_persistence || deferred_replay);
    }
    if (parsed.packet_type == reticulum::PacketType::Proof)
    {
        return handleProofPacket(packet, packet_len, parsed, ingress_interface);
    }
    if (parsed.packet_type == reticulum::PacketType::LinkRequest)
    {
        return handleLinkRequestPacket(packet, packet_len, parsed, ingress_interface);
    }
    if (parsed.packet_type == reticulum::PacketType::Data)
    {
        if (!handlePathRequestPacket(parsed) &&
            !handleCacheRequestPacket(parsed) &&
            !handleLocalLinkPacket(packet, packet_len, parsed, ingress_interface) &&
            !maybeForwardLinkPacket(packet, packet_len, parsed) &&
            !maybeForwardTransportPacket(packet, packet_len, parsed))
        {
            return handleDataPacket(packet, packet_len, parsed);
        }
        return true;
    }

    const bool handled_local =
        handleLocalLinkPacket(packet, packet_len, parsed, ingress_interface);
    const bool handled_link = maybeForwardLinkPacket(packet, packet_len, parsed);
    const bool handled_transport = maybeForwardTransportPacket(packet, packet_len, parsed);
    return handled_local || handled_link || handled_transport;
}

bool LxmfAdapter::shouldDeferDiscoveryPacket(
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface,
    const RuntimeBudget& budget)
{
    if (!isPublicDiscoveryPacket(packet))
    {
        return false;
    }
    if (budget.drop_public_discovery &&
        isForegroundDiscoveryDestination(packet.destination_hash))
    {
        return false;
    }
    if (!budget.allow_public_discovery)
    {
        return true;
    }

    return !consumeDiscoveryBudget(ingress_interface);
}

bool LxmfAdapter::isPublicDiscoveryPacket(const reticulum::ParsedPacket& packet) const
{
    if (packet.packet_type != reticulum::PacketType::Announce ||
        !packet.destination_hash)
    {
        return false;
    }
    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return false;
    }
    return packet.context != static_cast<uint8_t>(reticulum::PacketContext::PathResponse) ||
           !findPendingPathRequest(packet.destination_hash);
}

bool LxmfAdapter::enqueueDeferredDiscoveryPacket(
    const reticulum::interfaces::RxPacket& packet,
    const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash || packet.len == 0 || packet.len > reticulum::kReticulumMtu)
    {
        return false;
    }

    deferred_discovery_scratch_ = DeferredDiscoveryPacket{};
    DeferredDiscoveryPacket& deferred = deferred_discovery_scratch_;
    memcpy(deferred.data, packet.data, packet.len);
    deferred.len = packet.len;
    deferred.rx_meta = packet.rx_meta;
    deferred.interface_kind = packet.interface_kind;
    memcpy(deferred.packet_hash, packet_hash, sizeof(deferred.packet_hash));

    bool dropped = false;
    deferred_discovery_queue_.pushDropOldest(deferred, &dropped);
    if (dropped)
    {
        noteRxSummary(false, false, false, false, true);
    }
    return true;
}

bool LxmfAdapter::hasDeferredDiscoveryPacket(
    const uint8_t packet_hash[reticulum::kFullHashSize]) const
{
    if (!packet_hash)
    {
        return false;
    }
    for (std::size_t i = 0; i < deferred_discovery_queue_.size(); ++i)
    {
        const DeferredDiscoveryPacket* queued = deferred_discovery_queue_.get(i);
        if (queued &&
            hashesEqual(queued->packet_hash, packet_hash, reticulum::kFullHashSize))
        {
            return true;
        }
    }
    return false;
}

void LxmfAdapter::processDeferredDiscoveryPackets(const RuntimeBudget& budget)
{
    if (!budget.allow_public_discovery || budget.deferred_discovery_limit == 0)
    {
        return;
    }

    uint8_t processed = 0;
    while (processed < budget.deferred_discovery_limit &&
           deferred_discovery_queue_.popOldest(&deferred_discovery_scratch_))
    {
        rx_packet_scratch_.len = deferred_discovery_scratch_.len;
        memcpy(rx_packet_scratch_.data,
               deferred_discovery_scratch_.data,
               deferred_discovery_scratch_.len);
        rx_packet_scratch_.rx_meta = deferred_discovery_scratch_.rx_meta;
        rx_packet_scratch_.interface_kind = deferred_discovery_scratch_.interface_kind;
        (void)processOneRadioPacket(rx_packet_scratch_, budget, true);
        ++processed;
    }
}

void LxmfAdapter::maybeAnnounce()
{
    if (config_.reticulum_anonymous_peer)
    {
        announce_pending_ = false;
        last_announce_attempt_ms_ = 0;
        return;
    }
    const uint32_t now_ms = millis();
    if (!announce_pending_ && (now_ms - last_announce_ms_) < kAnnounceIntervalMs)
    {
        return;
    }
    if (announce_pending_)
    {
        const bool first_attempt = last_announce_attempt_ms_ == 0;
        const uint32_t wait_ms = first_attempt ? kInitialAnnounceDelayMs : kPendingAnnounceRetryMs;
        const uint32_t basis_ms = first_attempt ? last_announce_ms_ : last_announce_attempt_ms_;
        if ((now_ms - basis_ms) < wait_ms)
        {
            return;
        }
    }

    const bool delivery_ok = sendAnnounce(LocalDestinationKind::Delivery);
    const bool delivery_complete = lastAnnounceTxReachedRequiredInterfaces(delivery_ok);
    const bool propagation_ok = sendAnnounce(LocalDestinationKind::Propagation);
    const bool propagation_complete = lastAnnounceTxReachedRequiredInterfaces(propagation_ok);
    const bool call_audio_ok = sendAnnounce(LocalDestinationKind::CallAudio);
    const bool call_audio_complete =
        !interfaces_.wifiGatewayConfigured() ||
        lastAnnounceTxReachedRequiredInterfaces(call_audio_ok);
    last_announce_attempt_ms_ = now_ms;
    if (delivery_ok || propagation_ok || call_audio_ok)
    {
        last_announce_ms_ = now_ms;
    }
    announce_pending_ = !(delivery_complete && propagation_complete && call_audio_complete);
    if (!announce_pending_)
    {
        last_announce_attempt_ms_ = 0;
    }
}

bool LxmfAdapter::sendAnnounce(LocalDestinationKind kind,
                               reticulum::PacketContext context)
{
    interfaces_.maintain();
    if (config_.reticulum_anonymous_peer)
    {
        Serial.printf("[LXMF][AnnounceTX] skip reason=anonymous_peer kind=%u context=%u\n",
                      static_cast<unsigned>(kind),
                      static_cast<unsigned>(context));
        return false;
    }
    const bool call_audio = kind == LocalDestinationKind::CallAudio;
    if (call_audio && !interfaces_.wifiGatewayConfigured())
    {
        return false;
    }
    if (call_audio)
    {
        interfaces_.maintain();
        if (!identity_.isReady() || !interfaces_.hasReadyWifiGateway())
        {
            return false;
        }
    }
    else if (!isReady())
    {
        return false;
    }

    uint8_t app_data[96] = {};
    size_t app_data_len = sizeof(app_data);
    bool app_data_ok = false;
    if (kind == LocalDestinationKind::CallAudio)
    {
        const char* display_name = effectiveDisplayName();
        app_data_len = display_name ? std::min(strlen(display_name), sizeof(app_data)) : 0;
        if (app_data_len != 0)
        {
            memcpy(app_data, display_name, app_data_len);
        }
        app_data_ok = true;
    }
    else if (kind == LocalDestinationKind::Propagation)
    {
        app_data_ok = packPropagationAnnounceAppData(currentTimestampSeconds(),
                                                     true,
                                                     kPropagationTransferLimitKb,
                                                     kPropagationSyncLimitKb,
                                                     effectiveDisplayName(),
                                                     app_data,
                                                     &app_data_len);
    }
    else
    {
        app_data_ok = packPeerAnnounceAppData(effectiveDisplayName(),
                                              false,
                                              0,
                                              app_data,
                                              &app_data_len);
    }
    if (!app_data_ok)
    {
        return false;
    }

    uint8_t combined_pub[reticulum::kCombinedPublicKeySize] = {};
    identity_.combinedPublicKey(combined_pub);

    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(kind, destination_hash);

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    if (kind == LocalDestinationKind::CallAudio)
    {
        reticulum::computeNameHash("call", "audio", name_hash);
    }
    else
    {
        reticulum::computeNameHash("lxmf",
                                   (kind == LocalDestinationKind::Propagation) ? "propagation" : "delivery",
                                   name_hash);
    }

    uint8_t random_hash[10] = {};
    fillRandomBytes(random_hash, 5);
    const uint64_t now_s = currentTimestampSeconds();
    random_hash[5] = static_cast<uint8_t>((now_s >> 32) & 0xFFU);
    random_hash[6] = static_cast<uint8_t>((now_s >> 24) & 0xFFU);
    random_hash[7] = static_cast<uint8_t>((now_s >> 16) & 0xFFU);
    random_hash[8] = static_cast<uint8_t>((now_s >> 8) & 0xFFU);
    random_hash[9] = static_cast<uint8_t>(now_s & 0xFFU);

    uint8_t* signed_data = announce_tx_signed_scratch_;
    size_t signed_len = 0;
    memcpy(signed_data + signed_len, destination_hash, reticulum::kTruncatedHashSize);
    signed_len += reticulum::kTruncatedHashSize;
    memcpy(signed_data + signed_len, combined_pub, sizeof(combined_pub));
    signed_len += sizeof(combined_pub);
    memcpy(signed_data + signed_len, name_hash, sizeof(name_hash));
    signed_len += sizeof(name_hash);
    memcpy(signed_data + signed_len, random_hash, sizeof(random_hash));
    signed_len += sizeof(random_hash);
    if (signed_len + app_data_len > reticulum::kReticulumMtu)
    {
        return false;
    }
    memcpy(signed_data + signed_len, app_data, app_data_len);
    signed_len += app_data_len;

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_data, signed_len, signature))
    {
        return false;
    }

    uint8_t* announce_payload = announce_tx_payload_scratch_;
    size_t announce_payload_len = 0;
    memcpy(announce_payload + announce_payload_len, combined_pub, sizeof(combined_pub));
    announce_payload_len += sizeof(combined_pub);
    memcpy(announce_payload + announce_payload_len, name_hash, sizeof(name_hash));
    announce_payload_len += sizeof(name_hash);
    memcpy(announce_payload + announce_payload_len, random_hash, sizeof(random_hash));
    announce_payload_len += sizeof(random_hash);
    memcpy(announce_payload + announce_payload_len, signature, sizeof(signature));
    announce_payload_len += sizeof(signature);
    if (announce_payload_len + app_data_len > reticulum::kReticulumMtu)
    {
        return false;
    }
    memcpy(announce_payload + announce_payload_len, app_data, app_data_len);
    announce_payload_len += app_data_len;

    uint8_t* packet = announce_tx_packet_scratch_;
    size_t packet_len = reticulum::kReticulumMtu;
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       context,
                                       false,
                                       destination_hash,
                                       announce_payload,
                                       announce_payload_len,
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    char destination_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(destination_hash,
                  sizeof(destination_hash),
                  destination_hex,
                  sizeof(destination_hex));
    const bool sent = call_audio
                          ? interfaces_.sendPacketWifiOnly(packet, packet_len)
                          : routeAndSendPacket(packet, packet_len, false);
    const auto& tx_result = interfaces_.lastTxResult();
    const char* display_name = effectiveDisplayName();
    Serial.printf("[LXMF][AnnounceTX] kind=%s context=%u dest=%s name=%s app_len=%u packet_len=%u ok=%u complete=%u lora=%u/%u wifi=%u/%u\n",
                  localDestinationKindLabel(kind),
                  static_cast<unsigned>(context),
                  destination_hex,
                  (display_name && display_name[0] != '\0') ? display_name : "<none>",
                  static_cast<unsigned>(app_data_len),
                  static_cast<unsigned>(packet_len),
                  sent ? 1U : 0U,
                  tx_result.reachedRequiredInterfaces() ? 1U : 0U,
                  tx_result.lora_ok ? 1U : 0U,
                  tx_result.lora_required ? 1U : 0U,
                  tx_result.wifi_ok ? 1U : 0U,
                  tx_result.wifi_required ? 1U : 0U);
    if (!sent)
    {
        return false;
    }

    return true;
}

bool LxmfAdapter::lastAnnounceTxReachedRequiredInterfaces(bool sent) const
{
    if (!sent)
    {
        return false;
    }
    return interfaces_.lastTxResult().reachedRequiredInterfaces();
}

bool LxmfAdapter::handleAnnouncePacket(const uint8_t* raw_packet, size_t raw_len,
                                       const reticulum::ParsedPacket& packet,
                                       reticulum::interfaces::InterfaceKind ingress_interface,
                                       bool allow_persistence)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash)
    {
        return false;
    }
    if (packet.destination_type != reticulum::DestinationType::Single)
    {
        return false;
    }
    if (packet.context != static_cast<uint8_t>(reticulum::PacketContext::None) &&
        packet.context != static_cast<uint8_t>(reticulum::PacketContext::PathResponse))
    {
        return false;
    }

    reticulum::ParsedAnnounce announce{};
    if (!reticulum::parseAnnounce(packet, &announce) || !announce.valid)
    {
        char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      packet_hash_hex,
                      sizeof(packet_hash_hex));
        Serial.printf("[LXMF][AnnounceRX] drop reason=parse_failed dest=%s payload_len=%u\n",
                      packet_hash_hex,
                      static_cast<unsigned>(packet.payload_len));
        return false;
    }

    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeIdentityHash(announce.public_key, identity_hash);

    uint8_t expected_destination_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeDestinationHash(announce.name_hash, identity_hash, expected_destination_hash);
    if (!hashesEqual(expected_destination_hash, packet.destination_hash, reticulum::kTruncatedHashSize))
    {
        char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        char expected_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      packet_hash_hex,
                      sizeof(packet_hash_hex));
        formatHashHex(expected_destination_hash,
                      sizeof(expected_destination_hash),
                      expected_hash_hex,
                      sizeof(expected_hash_hex));
        Serial.printf("[LXMF][AnnounceRX] drop reason=destination_mismatch packet=%s expected=%s\n",
                      packet_hash_hex,
                      expected_hash_hex);
        return false;
    }

    uint8_t* signed_data = announce_rx_signed_scratch_;
    size_t signed_len = 0;
    memcpy(signed_data + signed_len, packet.destination_hash, reticulum::kTruncatedHashSize);
    signed_len += reticulum::kTruncatedHashSize;
    memcpy(signed_data + signed_len, announce.public_key, reticulum::kCombinedPublicKeySize);
    signed_len += reticulum::kCombinedPublicKeySize;
    memcpy(signed_data + signed_len, announce.name_hash, reticulum::kNameHashSize);
    signed_len += reticulum::kNameHashSize;
    memcpy(signed_data + signed_len, announce.random_hash, 10);
    signed_len += 10;
    if (announce.has_ratchet && announce.ratchet && announce.ratchet_len != 0)
    {
        if (signed_len + announce.ratchet_len > reticulum::kReticulumMtu)
        {
            return false;
        }
        memcpy(signed_data + signed_len, announce.ratchet, announce.ratchet_len);
        signed_len += announce.ratchet_len;
    }
    if (announce.app_data_len != 0)
    {
        if (signed_len + announce.app_data_len > reticulum::kReticulumMtu)
        {
            return false;
        }
        memcpy(signed_data + signed_len, announce.app_data, announce.app_data_len);
        signed_len += announce.app_data_len;
    }

    const uint8_t* sig_pub = announce.public_key + reticulum::kEncryptionPublicKeySize;
    if (!LxmfIdentity::verify(sig_pub, announce.signature, signed_data, signed_len))
    {
        char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(packet.destination_hash,
                      reticulum::kTruncatedHashSize,
                      packet_hash_hex,
                      sizeof(packet_hash_hex));
        Serial.printf("[LXMF][AnnounceRX] drop reason=signature_failed dest=%s\n",
                      packet_hash_hex);
        return false;
    }

    PathEntry& path = upsertPath(packet.destination_hash);
    const uint32_t now_s = currentTimestampSeconds();
    path.hops = packet.hops;
    path.last_seen_s = now_s;
    path.direct = (packet.transport_id == nullptr);
    resolvePendingPathRequest(packet.destination_hash);
    if (packet.transport_id)
    {
        copyHash(path.next_hop_transport, packet.transport_id, sizeof(path.next_hop_transport));
    }
    else
    {
        copyHash(path.next_hop_transport, packet.destination_hash, sizeof(path.next_hop_transport));
    }

    for (auto& session : links_.sessions)
    {
        if (!session.initiator ||
            session.state != LinkState::Pending ||
            !hashesEqual(session.remote_destination_hash,
                         packet.destination_hash,
                         sizeof(session.remote_destination_hash)))
        {
            continue;
        }

        session.expected_hops = path.hops;
        const bool retried = sendLinkRequest(session);
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][LinkTX] retry_after_path dest=%s kind=%u ok=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      retried ? 1U : 0U);
    }

    if (raw_len <= sizeof(path.cached_announce))
    {
        memcpy(path.cached_announce, raw_packet, raw_len);
        path.cached_announce_len = raw_len;
        reticulum::computePacketHash(raw_packet, raw_len, path.cached_packet_hash);
    }

    if (shouldRebroadcastAnnounce(packet, ingress_interface))
    {
        (void)rebroadcastAnnounce(path, packet);
    }

    const bool delivery_announce = isLxmfDeliveryAnnounce(announce);
    const bool propagation_announce = isLxmfPropagationAnnounce(announce);
    const bool call_audio_announce = isCallAudioAnnounce(announce);
    const bool nomad_node_announce = isNomadNetworkNodeAnnounce(announce);
    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    const bool local_destination = isLocalDestinationHash(packet.destination_hash, &local_kind);

    char packet_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    char identity_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    char announce_ratchet_id[12] = {};
    formatHashHex(packet.destination_hash,
                  reticulum::kTruncatedHashSize,
                  packet_hash_hex,
                  sizeof(packet_hash_hex));
    formatHashHex(identity_hash,
                  sizeof(identity_hash),
                  identity_hash_hex,
                  sizeof(identity_hash_hex));
    const bool packet_has_ratchet =
        announce.has_ratchet &&
        announce.ratchet &&
        announce.ratchet_len == reticulum::kRatchetSize &&
        !isZeroBytes(announce.ratchet, announce.ratchet_len);
    formatRatchetIdPrefix(packet_has_ratchet ? announce.ratchet : nullptr,
                          announce_ratchet_id,
                          sizeof(announce_ratchet_id));

    char announce_display_name[32] = {};
    bool has_stamp_cost = false;
    uint8_t stamp_cost = 0;
    if (call_audio_announce && announce.app_data && announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(announce.app_data,
                                         announce.app_data_len,
                                         announce_display_name,
                                         sizeof(announce_display_name));
    }
    else if (delivery_announce && announce.app_data && announce.app_data_len != 0 &&
             unpackPeerAnnounceAppData(announce.app_data,
                                       announce.app_data_len,
                                       announce_display_name,
                                       sizeof(announce_display_name),
                                       &has_stamp_cost,
                                       &stamp_cost))
    {
        (void)has_stamp_cost;
        (void)stamp_cost;
    }
    else if (nomad_node_announce && announce.app_data && announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(announce.app_data,
                                         announce.app_data_len,
                                         announce_display_name,
                                         sizeof(announce_display_name));
    }
    else if (!(delivery_announce || propagation_announce || call_audio_announce ||
               nomad_node_announce) &&
             announce.app_data && announce.app_data_len != 0)
    {
        (void)copyTextAppDataDisplayName(announce.app_data,
                                         announce.app_data_len,
                                         announce_display_name,
                                         sizeof(announce_display_name));
    }
    if ((delivery_announce || call_audio_announce) && announce_display_name[0] == '\0')
    {
        copyCString(announce_display_name,
                    sizeof(announce_display_name),
                    kAnonymousPeerDisplayName);
    }
    else if (nomad_node_announce && announce_display_name[0] == '\0')
    {
        copyCString(announce_display_name,
                    sizeof(announce_display_name),
                    kAnonymousNodeDisplayName);
    }

    rtdir::AnnounceRecord directory_announce{};
    directory_announce.valid = true;
    copyHash(directory_announce.destination_hash,
             packet.destination_hash,
             sizeof(directory_announce.destination_hash));
    copyHash(directory_announce.identity_hash,
             identity_hash,
             sizeof(directory_announce.identity_hash));
    directory_announce.aspect =
        delivery_announce
            ? rtdir::AnnounceAspect::LxmfDelivery
            : (propagation_announce
                   ? rtdir::AnnounceAspect::LxmfPropagation
                   : (call_audio_announce ? rtdir::AnnounceAspect::CallAudio
                                          : (nomad_node_announce
                                                 ? rtdir::AnnounceAspect::NomadNetworkNode
                                                 : rtdir::AnnounceAspect::Unknown)));
    directory_announce.source =
        packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse)
            ? rtdir::EntrySource::PathResponse
            : rtdir::EntrySource::RuntimeRx;
    directory_announce.first_seen_s = now_s;
    directory_announce.last_seen_s = now_s;
    directory_announce.hops = packet.hops;
    directory_announce.path_response =
        packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse);
    directory_announce.local_destination = local_destination;
    directory_announce.delivery = delivery_announce;
    directory_announce.propagation = propagation_announce;
    copyCString(directory_announce.display_name,
                sizeof(directory_announce.display_name),
                announce_display_name);
    directory_announce.raw_packet = raw_packet;
    directory_announce.raw_packet_len = raw_len;
    directory_announce.app_data = announce.app_data;
    directory_announce.app_data_len = announce.app_data_len;
    if (allow_persistence)
    {
        const auto announce_store_status = rtdir::record_announce(directory_announce);
        if (announce_store_status.sd_present && !announce_store_status.saved)
        {
            Serial.printf("[LXMF][AnnounceRX] directory_save failed message=%s detail=%s\n",
                          announce_store_status.message,
                          announce_store_status.detail);
        }
    }

    bool log_announce_detail =
        ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway ||
        local_destination;
    const bool contact_announce = delivery_announce || call_audio_announce;
    if (log_announce_detail &&
        ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway &&
        !local_destination &&
        !contact_announce)
    {
        const uint32_t log_now_ms = millis();
        if (last_lora_announce_ignore_log_ms_ != 0 &&
            log_now_ms - last_lora_announce_ignore_log_ms_ <
                kLoraAnnounceIgnoreDetailLogIntervalMs)
        {
            ++suppressed_lora_announce_ignore_logs_;
            log_announce_detail = false;
        }
        else
        {
            if (suppressed_lora_announce_ignore_logs_ != 0)
            {
                Serial.printf("[LXMF][AnnounceRX] ignored_suppressed iface=lora suppressed=%u\n",
                              static_cast<unsigned>(suppressed_lora_announce_ignore_logs_));
                suppressed_lora_announce_ignore_logs_ = 0;
            }
            last_lora_announce_ignore_log_ms_ = log_now_ms;
        }
    }
    if (log_announce_detail)
    {
        Serial.printf("[LXMF][AnnounceRX] seen dest=%s identity=%s hops=%u context=%u flag=%u app_len=%u ratchet=%u ratchet_id=%s delivery=%u propagation=%u call=%u nomad=%u local=%u kind=%s\n",
                      packet_hash_hex,
                      identity_hash_hex,
                      static_cast<unsigned>(packet.hops),
                      static_cast<unsigned>(packet.context),
                      static_cast<unsigned>(packet.context_flag),
                      static_cast<unsigned>(announce.app_data_len),
                      packet_has_ratchet ? 1U : 0U,
                      announce_ratchet_id,
                      delivery_announce ? 1U : 0U,
                      propagation_announce ? 1U : 0U,
                      call_audio_announce ? 1U : 0U,
                      nomad_node_announce ? 1U : 0U,
                      local_destination ? 1U : 0U,
                      localDestinationKindLabel(local_kind));
    }

    if (propagation_announce && !local_destination)
    {
        uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
        destinationHashForAspect(identity_hash, "delivery", delivery_hash);
        PropagationPeerState& propagation_peer =
            runtime::upsertPropagationPeer(propagation_,
                                           packet.destination_hash,
                                           delivery_hash,
                                           identity_hash,
                                           kMaxPropagationPeers);
        runtime::markPropagationPeerSeen(propagation_peer, now_s);
    }

    if (!contact_announce || local_destination)
    {
        if (log_announce_detail)
        {
            Serial.printf("[LXMF][AnnounceRX] ignore reason=%s dest=%s\n",
                          local_destination ? "local_destination" : "not_contact_announce",
                          packet_hash_hex);
        }
        return true;
    }

    uint8_t peer_destination_hash[reticulum::kTruncatedHashSize] = {};
    if (delivery_announce)
    {
        copyHash(peer_destination_hash, packet.destination_hash, sizeof(peer_destination_hash));
    }
    else
    {
        destinationHashForAspect(identity_hash, "delivery", peer_destination_hash);
    }

    PeerInfo& peer = upsertPeer(peer_destination_hash);
    const uint32_t previous_seen_s = peer.last_seen_s;
    const bool delivery_ratchet_available = delivery_announce && packet_has_ratchet;
    const bool identity_changed =
        isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) ||
        !hashesEqual(peer.identity_hash, identity_hash, sizeof(peer.identity_hash)) ||
        memcmp(peer.enc_pub, announce.public_key, sizeof(peer.enc_pub)) != 0 ||
        memcmp(peer.sig_pub, sig_pub, sizeof(peer.sig_pub)) != 0;
    const bool display_changed =
        announce_display_name[0] != '\0' &&
        strncmp(peer.display_name, announce_display_name, sizeof(peer.display_name)) != 0;
    copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    memcpy(peer.enc_pub, announce.public_key, sizeof(peer.enc_pub));
    memcpy(peer.sig_pub, sig_pub, sizeof(peer.sig_pub));
    peer.last_seen_s = now_s;
    if (delivery_announce)
    {
        if (delivery_ratchet_available)
        {
            memcpy(peer.ratchet_pub, announce.ratchet, sizeof(peer.ratchet_pub));
            peer.has_ratchet = true;
            peer.ratchet_seen_s = now_s;
        }
        else
        {
            memset(peer.ratchet_pub, 0, sizeof(peer.ratchet_pub));
            peer.has_ratchet = false;
            peer.ratchet_seen_s = 0;
        }
    }

    if (announce_display_name[0] != '\0')
    {
        copyCString(peer.display_name, sizeof(peer.display_name), announce_display_name);
    }
    else if (peer.display_name[0] == '\0')
    {
        copyCString(peer.display_name, sizeof(peer.display_name), kAnonymousPeerDisplayName);
    }

    const bool address_refresh_due =
        previous_seen_s == 0 ||
        (now_s >= previous_seen_s &&
         (now_s - previous_seen_s) >= kDirectoryAddressRefreshIntervalS);
    const bool should_store_address =
        ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway ||
        identity_changed ||
        display_changed ||
        address_refresh_due;
    if (allow_persistence && should_store_address)
    {
        if (!recordPeerInDirectory(
                peer,
                meshPeerSourceFromDirectorySource(directory_announce.source),
                false,
                false))
        {
            Serial.printf("[LXMF][AnnounceRX] address_save failed status=mesh_peer_directory\n");
        }
    }

    if (ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway)
    {
        queuePeerUpdate(peer);
    }
    else
    {
        if (allow_persistence)
        {
            publishPeerUpdate(peer);
        }
        else
        {
            queuePeerUpdate(peer);
        }
    }
    char peer_ratchet_id[12] = {};
    formatRatchetIdPrefix(peerHasUsableRatchet(peer) ? peer.ratchet_pub : nullptr,
                          peer_ratchet_id,
                          sizeof(peer_ratchet_id));
    Serial.printf("[LXMF][AnnounceRX] learned peer=%08lX dest=%s identity=%s name=%s ratchet=%u ratchet_id=%s peers=%u\n",
                  static_cast<unsigned long>(peer.node_id),
                  packet_hash_hex,
                  identity_hash_hex,
                  peer.display_name[0] != '\0' ? peer.display_name : "<unnamed>",
                  peerHasUsableRatchet(peer) ? 1U : 0U,
                  peer_ratchet_id,
                  static_cast<unsigned>(peers_.size()));
    return true;
}

bool LxmfAdapter::handleDataPacket(const uint8_t* raw_packet, size_t raw_len,
                                   const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.payload || !packet.destination_hash)
    {
        return false;
    }

    if (packet.destination_type == reticulum::DestinationType::Group)
    {
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][GroupRX] packet raw_len=%u payload_len=%u dest=%s hops=%u\n",
                      static_cast<unsigned>(raw_len),
                      static_cast<unsigned>(packet.payload_len),
                      dest_hash,
                      static_cast<unsigned>(packet.hops));
        const ReticulumGroupDestinationConfig* group =
            findConfiguredGroupDestination(packet.destination_hash);
        if (!group || packet.payload_len == 0)
        {
            Serial.printf("[LXMF][GroupRX] drop reason=%s dest=%s\n",
                          group ? "empty_payload" : "unconfigured_group",
                          dest_hash);
            return false;
        }
        Serial.printf("[LXMF][GroupRX] matched group=%s dest=%s\n",
                      group->name[0] != '\0' ? group->name : "<unnamed>",
                      dest_hash);
        const ReticulumPeerIdentity conversation_identity =
            makeReticulumDestinationIdentity(packet.destination_hash);
        return acceptVerifiedEnvelopeForDestination(packet.destination_hash,
                                                    conversation_identity,
                                                    true,
                                                    false,
                                                    packet.payload,
                                                    packet.payload_len,
                                                    raw_packet,
                                                    raw_len);
    }

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (!isLocalDestinationHash(packet.destination_hash, &local_kind))
    {
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][DirectRX] drop reason=not_local dest=%s raw_len=%u payload_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(raw_len),
                      static_cast<unsigned>(packet.payload_len));
        return false;
    }
    if (packet.destination_type != reticulum::DestinationType::Single ||
        packet.payload_len <= (reticulum::kEncryptionPublicKeySize + reticulum::kTokenOverhead))
    {
        char dest_hash[12] = {};
        formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][DirectRX] drop reason=invalid_single_packet kind=%s dest=%s type=%u payload_len=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      static_cast<unsigned>(packet.destination_type),
                      static_cast<unsigned>(packet.payload_len));
        return false;
    }

    char dest_hash[12] = {};
    formatHashPrefix(packet.destination_hash, dest_hash, sizeof(dest_hash));
    Serial.printf("[LXMF][DirectRX] packet kind=%s dest=%s raw_len=%u payload_len=%u hops=%u\n",
                  localDestinationKindLabel(local_kind),
                  dest_hash,
                  static_cast<unsigned>(raw_len),
                  static_cast<unsigned>(packet.payload_len),
                  static_cast<unsigned>(packet.hops));

    const uint8_t* peer_ephemeral_pub = packet.payload;
    const uint8_t* token = packet.payload + reticulum::kEncryptionPublicKeySize;
    const size_t token_len = packet.payload_len - reticulum::kEncryptionPublicKeySize;

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_ephemeral_pub, shared_secret))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                               identity_.identityHash(), reticulum::kTruncatedHashSize,
                               nullptr, 0,
                               derived_key, sizeof(derived_key)))
    {
        return false;
    }

    uint8_t plaintext[kMaxTokenPlaintextLen] = {};
    size_t plaintext_len = sizeof(plaintext);
    if (!reticulum::tokenDecrypt(derived_key, token, token_len, plaintext, &plaintext_len))
    {
        Serial.printf("[LXMF][DirectRX] drop reason=decrypt_failed kind=%s dest=%s token_len=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      static_cast<unsigned>(token_len));
        return false;
    }

    if (local_kind == LocalDestinationKind::Delivery && plaintext_len == 0)
    {
        const bool proof_sent = sendProofForPacket(raw_packet, raw_len);
        Serial.printf("[LXMF][DirectRX] ping kind=%s dest=%s proof=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      proof_sent ? 1U : 0U);
        return true;
    }

    bool handled = false;
    if (local_kind == LocalDestinationKind::Propagation)
    {
        LinkSession propagation_session{};
        propagation_session.destination = LocalDestinationKind::Propagation;
        propagation_session.state = LinkState::Active;
        handled = handlePropagationBatch(propagation_session, plaintext, plaintext_len);
    }
    else
    {
        handled = acceptVerifiedEnvelope(plaintext, plaintext_len, raw_packet, raw_len);
    }

    if (!handled)
    {
        Serial.printf("[LXMF][DirectRX] drop reason=handler_rejected kind=%s dest=%s plaintext_len=%u\n",
                      localDestinationKindLabel(local_kind),
                      dest_hash,
                      static_cast<unsigned>(plaintext_len));
        return false;
    }

    const bool proof_sent = sendProofForPacket(raw_packet, raw_len);
    Serial.printf("[LXMF][DirectRX] proof kind=%s dest=%s ok=%u\n",
                  localDestinationKindLabel(local_kind),
                  dest_hash,
                  proof_sent ? 1U : 0U);
    return true;
}

bool LxmfAdapter::handleProofPacket(
    const uint8_t* raw_packet, size_t raw_len,
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    if (!raw_packet || !packet.destination_hash || packet.packet_type != reticulum::PacketType::Proof)
    {
        return false;
    }

    if (packet.destination_type == reticulum::DestinationType::Link)
    {
        if (handleLocalLinkPacket(raw_packet, raw_len, packet, ingress_interface))
        {
            return true;
        }
        if (!pending_nomad_page_requests_.empty() &&
            packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof))
        {
            char link_hash[12] = {};
            char transport_hash[12] = {};
            formatHashPrefix(packet.destination_hash, link_hash, sizeof(link_hash));
            formatHashPrefix(packet.transport_id, transport_hash, sizeof(transport_hash));
            Serial.printf("[LXMF][LinkRX] proof_unmatched reason=no_local_link link=%s transport=%s raw_len=%u payload_len=%u hops=%u pending_nomad=%u\n",
                          link_hash,
                          transport_hash,
                          static_cast<unsigned>(raw_len),
                          static_cast<unsigned>(packet.payload_len),
                          static_cast<unsigned>(packet.hops),
                          static_cast<unsigned>(pending_nomad_page_requests_.size()));
        }
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof) ||
        packet.destination_type == reticulum::DestinationType::Link)
    {
        return maybeForwardLinkPacket(raw_packet, raw_len, packet);
    }

    ReverseEntry* reverse = findReversePath(packet.destination_hash);
    if (!reverse)
    {
        return false;
    }
    if (packet.hops != reverse->expected_hops)
    {
        return false;
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader1Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops,
                                       reticulum::TransportType::Broadcast))
    {
        return false;
    }

    reverse->created_ms = 0;
    return interfaces_.sendPacket(forward_packet, forward_len);
}

bool LxmfAdapter::handleLinkRequestPacket(
    const uint8_t* raw_packet, size_t raw_len,
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash ||
        packet.packet_type != reticulum::PacketType::LinkRequest)
    {
        return false;
    }

    if (packet.transport_id &&
        !hashesEqual(packet.transport_id, identity_.identityHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (isLocalDestinationHash(packet.destination_hash, &local_kind))
    {
        if (local_kind == LocalDestinationKind::CallAudio &&
            ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway)
        {
            return false;
        }

        if (!packet.payload ||
            (packet.payload_len != kLinkRequestBaseLen &&
             packet.payload_len != (kLinkRequestBaseLen + kLinkSignallingLen)))
        {
            return false;
        }

        const size_t signalling_len =
            (packet.payload_len > kLinkRequestBaseLen) ? (packet.payload_len - kLinkRequestBaseLen) : 0;
        if (signalling_len != 0 &&
            linkModeFromSignalling(packet.payload + kLinkRequestBaseLen, signalling_len) != kLinkModeAes256Cbc)
        {
            return false;
        }

        uint8_t link_id[reticulum::kTruncatedHashSize] = {};
        if (!computeLinkIdFromLinkRequest(raw_packet, raw_len, packet, link_id))
        {
            return false;
        }

        LinkSession* session = findLinkSession(link_id);
        if (!session)
        {
            if (links_.sessions.size() >= kMaxLinkSessions)
            {
                links_.sessions.erase(links_.sessions.begin());
            }

            links_.sessions.push_back(LinkSession{});
            session = &links_.sessions.back();
            copyHash(session->link_id, link_id, sizeof(session->link_id));
            memcpy(session->peer_enc_pub, packet.payload, LxmfIdentity::kEncPubKeySize);
            memcpy(session->peer_link_sig_pub,
                   packet.payload + LxmfIdentity::kEncPubKeySize,
                   LxmfIdentity::kSigPubKeySize);
            Curve25519::dh1(session->local_enc_pub, session->local_enc_priv);
            memcpy(session->local_sig_pub, identity_.signingPublicKey(), sizeof(session->local_sig_pub));
            session->mtu = (signalling_len != 0)
                               ? mtuFromLinkSignalling(packet.payload + kLinkRequestBaseLen, signalling_len)
                               : reticulum::kReticulumMtu;
            session->mdu = linkMduForMtu(session->mtu);
            session->created_ms = millis();
            session->request_ms = session->created_ms;
            session->last_inbound_ms = session->created_ms;
            session->destination = local_kind;
            session->initiator = false;
            session->state = LinkState::Handshake;

            if (!deriveLinkKey(*session))
            {
                links_.sessions.pop_back();
                return false;
            }

            if (local_kind == LocalDestinationKind::CallAudio)
            {
                ::platform::ui::reticulum_call::Peer call_peer{};
                copyHash(call_peer.link_id, session->link_id, sizeof(call_peer.link_id));
                call_peer.display_name = "Incoming call";
                call_peer.incoming = true;
                const bool ui_started =
                    ::platform::ui::reticulum_call::begin_incoming(call_peer);
                char link_hash[12] = {};
                formatHashPrefix(session->link_id, link_hash, sizeof(link_hash));
                Serial.printf("[LXMF][CallRX] begin_incoming link=%s ui=%u iface=%u\n",
                              link_hash,
                              ui_started ? 1U : 0U,
                              static_cast<unsigned>(ingress_interface));
            }
        }
        else
        {
            session->last_inbound_ms = millis();
        }

        return sendLinkHandshakeProof(*session);
    }

    const PathEntry* path = findPath(packet.destination_hash);
    if (!path)
    {
        return false;
    }

    uint8_t link_id[reticulum::kTruncatedHashSize] = {};
    if (computeLinkIdFromLinkRequest(raw_packet, raw_len, packet, link_id))
    {
        LinkRelayEntry& relay = upsertLinkRelay(link_id);
        relay.initiator_hops = packet.hops;
        relay.responder_hops = path->hops;
        relay.last_seen_ms = millis();
    }

    if (path->hops <= 1 || path->direct)
    {
        uint8_t forward_packet[kMaxPacketLen] = {};
        size_t forward_len = sizeof(forward_packet);
        if (!reticulum::buildHeader1Packet(packet.packet_type,
                                           packet.destination_type,
                                           static_cast<reticulum::PacketContext>(packet.context),
                                           packet.context_flag != 0,
                                           packet.destination_hash,
                                           packet.payload,
                                           packet.payload_len,
                                           forward_packet,
                                           &forward_len,
                                           packet.hops,
                                           reticulum::TransportType::Broadcast))
        {
            return false;
        }

        return interfaces_.sendPacket(forward_packet, forward_len);
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader2Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       path->next_hop_transport,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops))
    {
        return false;
    }

    return interfaces_.sendPacket(forward_packet, forward_len);
}

bool LxmfAdapter::handlePathRequestPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid ||
        packet.packet_type != reticulum::PacketType::Data ||
        packet.destination_type != reticulum::DestinationType::Plain ||
        !packet.destination_hash ||
        !packet.payload ||
        packet.payload_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);
    if (!hashesEqual(packet.destination_hash, control_hash, sizeof(control_hash)))
    {
        return false;
    }

    const uint8_t* requested_hash = packet.payload;
    const uint8_t* tag = nullptr;
    size_t tag_len = 0;

    if (packet.payload_len > (reticulum::kTruncatedHashSize * 2))
    {
        tag = packet.payload + (reticulum::kTruncatedHashSize * 2);
        tag_len = packet.payload_len - (reticulum::kTruncatedHashSize * 2);
    }
    else
    {
        tag = packet.payload + reticulum::kTruncatedHashSize;
        tag_len = packet.payload_len - reticulum::kTruncatedHashSize;
    }

    if (tag_len > kPathRequestTagSize)
    {
        tag_len = kPathRequestTagSize;
    }
    if (tag_len == 0)
    {
        return true;
    }

    LocalDestinationKind local_kind = LocalDestinationKind::Delivery;
    if (isLocalDestinationHash(requested_hash, &local_kind))
    {
        (void)tag;
        return sendAnnounce(local_kind, reticulum::PacketContext::PathResponse);
    }

    const PathEntry* path = findPath(requested_hash);
    if (!path || path->cached_announce_len == 0)
    {
        return true;
    }

    return sendCachedAnnounceResponse(*path, reticulum::PacketContext::PathResponse);
}

bool LxmfAdapter::handleCacheRequestPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.valid ||
        packet.packet_type != reticulum::PacketType::Data ||
        packet.context != static_cast<uint8_t>(reticulum::PacketContext::CacheRequest) ||
        !packet.payload ||
        packet.payload_len != reticulum::kFullHashSize)
    {
        return false;
    }

    return sendCachedPacketReplay(packet.payload);
}

bool LxmfAdapter::handleLocalLinkPacket(
    const uint8_t* raw_packet, size_t raw_len,
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    if (!packet.destination_hash ||
        packet.destination_type != reticulum::DestinationType::Link)
    {
        return false;
    }

    LinkSession* session = findLinkSession(packet.destination_hash);
    if (!session)
    {
        return false;
    }
    if (session->destination == LocalDestinationKind::CallAudio &&
        ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway)
    {
        return false;
    }

    session->last_inbound_ms = millis();
    if (packet.packet_type == reticulum::PacketType::Proof)
    {
        return handleLinkProofPacket(*session, raw_packet, raw_len, packet);
    }
    if (packet.packet_type == reticulum::PacketType::Data)
    {
        return handleLinkDataPacket(*session, raw_packet, raw_len, packet);
    }
    return false;
}

bool LxmfAdapter::handleLinkDataPacket(LinkSession& session,
                                       const uint8_t* raw_packet, size_t raw_len,
                                       const reticulum::ParsedPacket& packet)
{
    if (!packet.payload)
    {
        return false;
    }

    std::vector<uint8_t> plaintext;
    const uint8_t context = packet.context;
    const bool raw_payload = packetContextUsesRawLinkPayload(context);
    if (!raw_payload)
    {
        if (!decryptLinkPayload(session, packet.payload, packet.payload_len, &plaintext))
        {
            return false;
        }
    }

    const uint8_t* payload_ptr = raw_payload ? packet.payload : plaintext.data();
    const size_t payload_len = raw_payload ? packet.payload_len : plaintext.size();
    bool handled = false;
    bool should_prove = false;

    if (context == static_cast<uint8_t>(reticulum::PacketContext::None))
    {
        if (session.state == LinkState::Active)
        {
            if (session.destination == LocalDestinationKind::Propagation)
            {
                handled = handlePropagationBatch(session, payload_ptr, payload_len);
            }
            else if (session.destination == LocalDestinationKind::CallAudio)
            {
                handled = ::platform::ui::reticulum_call::enqueue_inbound_audio(
                    session.link_id,
                    payload_ptr,
                    payload_len);
            }
            else if (session.destination == LocalDestinationKind::Delivery)
            {
                handled = acceptVerifiedEnvelope(payload_ptr, payload_len, raw_packet, raw_len);
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LinkIdentify))
    {
        if (payload_len == reticulum::kCombinedPublicKeySize + reticulum::kSignatureSize)
        {
            const uint8_t* combined_pub = payload_ptr;
            const uint8_t* signature = payload_ptr + reticulum::kCombinedPublicKeySize;
            std::array<uint8_t, reticulum::kTruncatedHashSize + reticulum::kCombinedPublicKeySize> signed_data{};
            memcpy(signed_data.data(), session.link_id, reticulum::kTruncatedHashSize);
            memcpy(signed_data.data() + reticulum::kTruncatedHashSize,
                   combined_pub,
                   reticulum::kCombinedPublicKeySize);
            const uint8_t* sign_pub = combined_pub + LxmfIdentity::kEncPubKeySize;
            if (LxmfIdentity::verify(sign_pub, signature, signed_data.data(), signed_data.size()))
            {
                PeerInfo* peer = rememberPeerIdentity(combined_pub);
                if (peer)
                {
                    if (session.destination != LocalDestinationKind::CallAudio &&
                        session.destination != LocalDestinationKind::NomadPage)
                    {
                        copyHash(session.remote_destination_hash,
                                 peer->destination_hash,
                                 sizeof(session.remote_destination_hash));
                    }
                    copyHash(session.remote_identity_hash,
                             peer->identity_hash,
                             sizeof(session.remote_identity_hash));
                    memcpy(session.peer_identity_sig_pub,
                           peer->sig_pub,
                           sizeof(session.peer_identity_sig_pub));
                    session.remote_identity_known = true;
                    if (session.destination == LocalDestinationKind::CallAudio)
                    {
                        ::platform::ui::reticulum_call::Peer call_peer{};
                        copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
                        copyHash(call_peer.destination_hash,
                                 peer->destination_hash,
                                 sizeof(call_peer.destination_hash));
                        copyHash(call_peer.identity_hash,
                                 peer->identity_hash,
                                 sizeof(call_peer.identity_hash));
                        call_peer.display_name =
                            peer->display_name[0] != '\0' ? peer->display_name : nullptr;
                        call_peer.incoming = !session.initiator;
                        ::platform::ui::reticulum_call::update_peer(call_peer);
                    }
                }
                handled = true;
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Request))
    {
        DecodedLinkRequest request{};
        if (decodeLinkRequestPayload(payload_ptr, payload_len, &request))
        {
            uint8_t request_id[reticulum::kTruncatedHashSize] = {};
            reticulum::computeTruncatedPacketHash(raw_packet, raw_len, request_id);

            if (session.destination == LocalDestinationKind::Propagation)
            {
                handled = handlePropagationRequest(session,
                                                   request,
                                                   request_id,
                                                   sizeof(request_id));
            }
            else
            {
                handled = sendLinkResponse(session,
                                           request_id,
                                           sizeof(request_id),
                                           nullptr,
                                           0,
                                           true);
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Response))
    {
        DecodedLinkResponse response{};
        if (decodeLinkResponsePayload(payload_ptr, payload_len, &response))
        {
            for (auto& pending : session.pending_requests)
            {
                if (pending.request_id == response.request_id)
                {
                    pending.response_ready = true;
                    if (!response.data_is_nil)
                    {
                        pending.response = std::move(response.packed_data);
                    }
                    handled = true;
                    break;
                }
            }
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LrRtt))
    {
        double rtt_value = 0.0;
        if (!session.initiator &&
            unpackFloat64(payload_ptr, payload_len, &rtt_value))
        {
            session.rtt_s = static_cast<float>(rtt_value);
            session.validated = true;
            session.keepalive_interval_ms = keepaliveIntervalForRtt(session.rtt_s);
            session.stale_timeout_ms = session.keepalive_interval_ms * 2U;
            session.last_keepalive_ms = 0;
            session.state = LinkState::Active;
            if (session.destination == LocalDestinationKind::CallAudio)
            {
                (void)sendLinkIdentify(session);
                ::platform::ui::reticulum_call::mark_link_active(session.link_id);
            }
            handled = true;
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::LinkClose))
    {
        if (payload_len == reticulum::kTruncatedHashSize &&
            hashesEqual(payload_ptr, session.link_id, sizeof(session.link_id)))
        {
            closeLinkSession(session, LinkCloseReason::RemoteClose);
            handled = true;
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Keepalive))
    {
        if (payload_len == 1 && payload_ptr[0] == 0xFF)
        {
            handled = sendLinkKeepaliveAck(session);
        }
        else
        {
            handled = (payload_len == 1 && payload_ptr[0] == 0xFE);
            if (handled && session.state == LinkState::Stale)
            {
                session.state = LinkState::Active;
            }
        }
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceAdv))
    {
        handled = handleLinkResourceAdvertisement(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceReq))
    {
        handled = handleLinkResourceRequest(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceHmu))
    {
        handled = handleLinkResourceHashmapUpdate(session, payload_ptr, payload_len);
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceIcl))
    {
        if (payload_len == reticulum::kFullHashSize)
        {
            (void)runtime::eraseLinkResourceByHash(session.incoming_resources, payload_ptr);
            handled = true;
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::ResourceRcl))
    {
        if (payload_len == reticulum::kFullHashSize)
        {
            (void)runtime::eraseLinkResourceByHash(session.outgoing_resources, payload_ptr);
            handled = true;
        }
        should_prove = handled;
    }
    else if (context == static_cast<uint8_t>(reticulum::PacketContext::Resource))
    {
        handled = handleLinkResourcePart(session, packet);
    }

    if (handled && should_prove)
    {
        (void)sendLinkPacketProof(session, raw_packet, raw_len);
    }
    return handled;
}

bool LxmfAdapter::handleLinkProofPacket(LinkSession& session,
                                        const uint8_t* raw_packet, size_t raw_len,
                                        const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;

    if (!packet.payload)
    {
        return false;
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::LrProof))
    {
        if (!session.initiator || packet.payload_len < (reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize))
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "bad_state_or_short",
                                   session.initiator ? 1U : 0U,
                                   static_cast<uint32_t>(packet.payload_len));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=bad_state_or_short kind=%s initiator=%u payload=%u\n",
                                localDestinationKindLabel(session.destination),
                                session.initiator ? 1U : 0U,
                                static_cast<unsigned>(packet.payload_len));
            return false;
        }

        if (session.expected_hops != 0 && packet.hops != session.expected_hops)
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "hop_mismatch",
                                   session.expected_hops,
                                   packet.hops);
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=hop_mismatch kind=%s dest=%s expected=%u got=%u\n",
                                localDestinationKindLabel(session.destination),
                                dest_hash_hex,
                                static_cast<unsigned>(session.expected_hops),
                                static_cast<unsigned>(packet.hops));
            return false;
        }

        const uint8_t* signature = packet.payload;
        const uint8_t* peer_enc_pub = packet.payload + reticulum::kSignatureSize;
        const uint8_t* signalling = (packet.payload_len >= (reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize + kLinkSignallingLen))
                                        ? (packet.payload + reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize)
                                        : nullptr;
        const size_t signalling_len = signalling ? kLinkSignallingLen : 0;

        const PeerInfo* peer =
            session.destination == LocalDestinationKind::CallAudio
                ? findPeerByIdentityHash(session.remote_identity_hash)
                : findPeerByDestinationHash(session.remote_destination_hash);
        const uint8_t* peer_sig_pub = peer ? peer->sig_pub : nullptr;
        uint8_t announce_identity_hash[reticulum::kTruncatedHashSize] = {};
        uint8_t announce_sig_pub[LxmfIdentity::kSigPubKeySize] = {};
        bool announce_identity_known = false;
        if (!peer_sig_pub &&
            session.destination == LocalDestinationKind::CallAudio &&
            !isZeroBytes(session.peer_identity_sig_pub,
                         sizeof(session.peer_identity_sig_pub)))
        {
            peer_sig_pub = session.peer_identity_sig_pub;
        }
        if (!peer_sig_pub && session.destination == LocalDestinationKind::NomadPage)
        {
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));

            const PathEntry* path = findPath(session.remote_destination_hash);
            if (!path)
            {
                logNomadLinkProofEvent(session, "drop", "path_missing");
                LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=path_missing dest=%s\n",
                                    dest_hash_hex);
            }
            else if (path->cached_announce_len == 0)
            {
                logNomadLinkProofEvent(session,
                                       "drop",
                                       "announce_missing",
                                       path->hops,
                                       0);
                LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=announce_missing dest=%s hops=%u\n",
                                    dest_hash_hex,
                                    static_cast<unsigned>(path->hops));
            }
            else
            {
                reticulum::ParsedPacket announce_packet{};
                reticulum::ParsedAnnounce announce{};
                const bool parsed =
                    reticulum::parsePacket(path->cached_announce,
                                           path->cached_announce_len,
                                           &announce_packet) &&
                    announce_packet.packet_type == reticulum::PacketType::Announce &&
                    reticulum::parseAnnounce(announce_packet, &announce) &&
                    announce.valid;
                if (!parsed || !isNomadNetworkNodeAnnounce(announce))
                {
                    logNomadLinkProofEvent(session,
                                           "drop",
                                           "announce_parse_or_aspect",
                                           static_cast<uint32_t>(path->cached_announce_len),
                                           0);
                    LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=announce_parse_or_aspect dest=%s cached=%u\n",
                                        dest_hash_hex,
                                        static_cast<unsigned>(path->cached_announce_len));
                }
                else
                {
                    uint8_t expected_destination_hash[reticulum::kTruncatedHashSize] = {};
                    reticulum::computeIdentityHash(announce.public_key,
                                                   announce_identity_hash);
                    reticulum::computeDestinationHash(announce.name_hash,
                                                      announce_identity_hash,
                                                      expected_destination_hash);
                    if (!hashesEqual(expected_destination_hash,
                                     session.remote_destination_hash,
                                     reticulum::kTruncatedHashSize))
                    {
                        logNomadLinkProofEvent(session,
                                               "drop",
                                               "announce_dest_mismatch");
                        char expected_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
                        formatHashHex(expected_destination_hash,
                                      sizeof(expected_destination_hash),
                                      expected_hash_hex,
                                      sizeof(expected_hash_hex));
                        LXMF_LINK_PROOF_LOG("lrproof nomad key miss reason=announce_dest_mismatch dest=%s expected=%s\n",
                                            dest_hash_hex,
                                            expected_hash_hex);
                    }
                    else
                    {
                        memcpy(announce_sig_pub,
                               announce.public_key + reticulum::kEncryptionPublicKeySize,
                               sizeof(announce_sig_pub));
                        peer_sig_pub = announce_sig_pub;
                        announce_identity_known = true;
                        LXMF_LINK_PROOF_LOG("lrproof nomad key resolved dest=%s hops=%u cached=%u\n",
                                            dest_hash_hex,
                                            static_cast<unsigned>(path->hops),
                                            static_cast<unsigned>(path->cached_announce_len));
                    }
                }
            }
        }
        if (!peer_sig_pub)
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "peer_sig_missing",
                                   session.remote_identity_known ? 1U : 0U,
                                   0);
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=peer_sig_missing kind=%s dest=%s identity_known=%u\n",
                                localDestinationKindLabel(session.destination),
                                dest_hash_hex,
                                session.remote_identity_known ? 1U : 0U);
            return false;
        }

        std::array<uint8_t, reticulum::kTruncatedHashSize + LxmfIdentity::kEncPubKeySize +
                                LxmfIdentity::kSigPubKeySize + kLinkSignallingLen>
            signed_data{};
        size_t used = 0;
        memcpy(signed_data.data() + used, session.link_id, reticulum::kTruncatedHashSize);
        used += reticulum::kTruncatedHashSize;
        memcpy(signed_data.data() + used, peer_enc_pub, LxmfIdentity::kEncPubKeySize);
        used += LxmfIdentity::kEncPubKeySize;
        memcpy(signed_data.data() + used, peer_sig_pub, LxmfIdentity::kSigPubKeySize);
        used += LxmfIdentity::kSigPubKeySize;
        if (signalling_len != 0)
        {
            memcpy(signed_data.data() + used, signalling, signalling_len);
            used += signalling_len;
        }

        if (!LxmfIdentity::verify(peer_sig_pub, signature, signed_data.data(), used))
        {
            logNomadLinkProofEvent(session,
                                   "drop",
                                   "signature_failed",
                                   static_cast<uint32_t>(used),
                                   0);
            char dest_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            formatHashHex(session.remote_destination_hash,
                          sizeof(session.remote_destination_hash),
                          dest_hash_hex,
                          sizeof(dest_hash_hex));
            LXMF_LINK_PROOF_LOG("lrproof drop reason=signature_failed kind=%s dest=%s signed=%u\n",
                                localDestinationKindLabel(session.destination),
                                dest_hash_hex,
                                static_cast<unsigned>(used));
            return false;
        }

        memcpy(session.peer_enc_pub, peer_enc_pub, sizeof(session.peer_enc_pub));
        memcpy(session.peer_identity_sig_pub,
               peer_sig_pub,
               sizeof(session.peer_identity_sig_pub));
        if (peer)
        {
            copyHash(session.remote_identity_hash,
                     peer->identity_hash,
                     sizeof(session.remote_identity_hash));
        }
        else if (announce_identity_known)
        {
            copyHash(session.remote_identity_hash,
                     announce_identity_hash,
                     sizeof(session.remote_identity_hash));
        }
        session.remote_identity_known = true;
        session.mtu = signalling ? mtuFromLinkSignalling(signalling, signalling_len) : reticulum::kReticulumMtu;
        session.mdu = linkMduForMtu(session.mtu);
        if (!deriveLinkKey(session))
        {
            logNomadLinkProofEvent(session, "drop", "derive_key_failed");
            LXMF_LINK_PROOF_LOG("lrproof drop reason=derive_key_failed kind=%s\n",
                                localDestinationKindLabel(session.destination));
            return false;
        }

        session.rtt_s = static_cast<float>((millis() - session.request_ms) / 1000.0f);
        session.validated = true;
        session.keepalive_interval_ms = keepaliveIntervalForRtt(session.rtt_s);
        session.stale_timeout_ms = session.keepalive_interval_ms * 2U;
        session.last_keepalive_ms = 0;
        session.state = LinkState::Active;
        const bool rtt_sent = sendLinkRtt(session);
        logNomadLinkProofEvent(session,
                               "active",
                               "ok",
                               static_cast<uint32_t>(millis() - session.request_ms),
                               rtt_sent ? 1U : 0U);
        char link_hash_hex[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
        formatHashHex(session.link_id,
                      sizeof(session.link_id),
                      link_hash_hex,
                      sizeof(link_hash_hex));
        LXMF_LINK_PROOF_LOG("lrproof active kind=%s link=%s rtt_ms=%lu mtu=%u mdu=%u rtt_tx=%u\n",
                            localDestinationKindLabel(session.destination),
                            link_hash_hex,
                            static_cast<unsigned long>(millis() - session.request_ms),
                            static_cast<unsigned>(session.mtu),
                            static_cast<unsigned>(session.mdu),
                            rtt_sent ? 1U : 0U);
        if (session.destination == LocalDestinationKind::NomadPage)
        {
            updateNomadPageProgressForDestination(session.remote_destination_hash,
                                                  35,
                                                  "Nomad page link active",
                                                  "ready to request page",
                                                  true,
                                                  false,
                                                  PageFailureKind::None);
        }
        if (session.destination == LocalDestinationKind::CallAudio)
        {
            (void)sendLinkIdentify(session);
            ::platform::ui::reticulum_call::Peer call_peer{};
            copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
            copyHash(call_peer.identity_hash,
                     session.remote_identity_hash,
                     sizeof(call_peer.identity_hash));
            if (peer)
            {
                copyHash(call_peer.destination_hash,
                         peer->destination_hash,
                         sizeof(call_peer.destination_hash));
                call_peer.display_name =
                    peer->display_name[0] != '\0' ? peer->display_name : nullptr;
            }
            call_peer.incoming = false;
            ::platform::ui::reticulum_call::update_peer(call_peer);
            ::platform::ui::reticulum_call::mark_link_active(session.link_id);
        }
        flushDeferredLinkPayloads(session);
        return rtt_sent;
    }

    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::ResourcePrf))
    {
        return handleLinkResourceProof(session, packet);
    }

    return false;
}

bool LxmfAdapter::handleLinkResourceAdvertisement(LinkSession& session,
                                                  const uint8_t* plaintext, size_t plaintext_len)
{
    DecodedResourceAdvertisement advertisement{};
    if (!decodeResourceAdvertisement(plaintext, plaintext_len, &advertisement))
    {
        Serial.printf("[LXMF][ResourceRX] adv_decode_failed len=%u\n",
                      static_cast<unsigned>(plaintext_len));
        return false;
    }

    const bool encrypted = (advertisement.flags & kResourceFlagEncrypted) != 0;
    const bool compressed = (advertisement.flags & kResourceFlagCompressed) != 0;
    const bool split = (advertisement.flags & kResourceFlagSplit) != 0;
    const bool has_metadata = (advertisement.flags & kResourceFlagHasMetadata) != 0;
    char resource_prefix[9] = {};
    formatHashPrefix(advertisement.resource_hash, resource_prefix, sizeof(resource_prefix));
    const char* reject_reason = nullptr;
    if (has_metadata)
    {
        reject_reason = "metadata";
    }
    else if (advertisement.segment_index == 0 ||
             advertisement.total_segments == 0 ||
             advertisement.segment_index > advertisement.total_segments)
    {
        reject_reason = "segment";
    }
    if (reject_reason)
    {
        Serial.printf("[LXMF][ResourceRX] adv_reject reason=%s resource=%s flags=%02X enc=%u comp=%u "
                      "meta=%u split=%u parts=%u seg=%u/%u transfer=%u data=%u req=%u\n",
                      reject_reason,
                      resource_prefix,
                      static_cast<unsigned>(advertisement.flags),
                      encrypted ? 1U : 0U,
                      compressed ? 1U : 0U,
                      has_metadata ? 1U : 0U,
                      split ? 1U : 0U,
                      static_cast<unsigned>(advertisement.part_count),
                      static_cast<unsigned>(advertisement.segment_index),
                      static_cast<unsigned>(advertisement.total_segments),
                      static_cast<unsigned>(advertisement.transfer_size),
                      static_cast<unsigned>(advertisement.data_size),
                      static_cast<unsigned>(advertisement.request_id.size()));
        (void)sendLinkPacket(session,
                             reticulum::PacketType::Data,
                             reticulum::PacketContext::ResourceRcl,
                             advertisement.resource_hash,
                             reticulum::kFullHashSize,
                             true);
        return true;
    }

    if (advertisement.part_count == 0 ||
        advertisement.hashmap.empty() ||
        (advertisement.hashmap.size() % kResourceMapHashLen) != 0)
    {
        return false;
    }

    if (runtime::findLinkResource(session.incoming_resources, advertisement.resource_hash))
    {
        return true;
    }

    if (session.destination == LocalDestinationKind::NomadPage || compressed || split)
    {
        Serial.printf("[LXMF][ResourceRX] adv_accept resource=%s flags=%02X enc=%u comp=%u "
                      "split=%u parts=%u seg=%u/%u transfer=%u data=%u req=%u\n",
                      resource_prefix,
                      static_cast<unsigned>(advertisement.flags),
                      encrypted ? 1U : 0U,
                      compressed ? 1U : 0U,
                      split ? 1U : 0U,
                      static_cast<unsigned>(advertisement.part_count),
                      static_cast<unsigned>(advertisement.segment_index),
                      static_cast<unsigned>(advertisement.total_segments),
                      static_cast<unsigned>(advertisement.transfer_size),
                      static_cast<unsigned>(advertisement.data_size),
                      static_cast<unsigned>(advertisement.request_id.size()));
    }

    LinkResourceTransfer resource{};
    if (!runtime::initialiseIncomingResourceTransfer(resource,
                                                     advertisement.resource_hash,
                                                     advertisement.random_hash,
                                                     advertisement.original_hash,
                                                     std::move(advertisement.request_id),
                                                     std::move(advertisement.hashmap),
                                                     advertisement.data_size,
                                                     advertisement.transfer_size,
                                                     advertisement.part_count,
                                                     advertisement.segment_index,
                                                     advertisement.total_segments,
                                                     advertisement.flags,
                                                     encrypted,
                                                     compressed,
                                                     has_metadata,
                                                     split,
                                                     millis(),
                                                     kResourceWindowSize))
    {
        return false;
    }

    session.incoming_resources.push_back(std::move(resource));
    LinkResourceTransfer& incoming_resource = session.incoming_resources.back();
    if (session.destination == LocalDestinationKind::NomadPage &&
        !incoming_resource.request_id.empty())
    {
        if (PendingNomadPageRequest* page_request =
                findPendingNomadPageRequestById(
                    session.remote_destination_hash,
                    incoming_resource.request_id.data(),
                    incoming_resource.request_id.size()))
        {
            char detail[32] = {};
            std::snprintf(detail,
                          sizeof(detail),
                          "0/%u parts",
                          static_cast<unsigned>(advertisement.part_count));
            updateNomadPageProgress(*page_request,
                                    45,
                                    "Receiving Nomad page",
                                    detail,
                                    true,
                                    false,
                                    PageFailureKind::None);
        }
    }
    return requestNextResourceWindow(session, incoming_resource);
}

bool LxmfAdapter::requestNextResourceWindow(LinkSession& session,
                                            LinkResourceTransfer& resource)
{
    if (resource.complete || resource.part_count == 0)
    {
        return false;
    }

    const runtime::ResourceWindowRequest request =
        runtime::buildNextResourceWindowRequest(resource);
    if (!request.valid)
    {
        return false;
    }

    std::vector<uint8_t> request_data;
    request_data.reserve(1 + kResourceMapHashLen + reticulum::kFullHashSize +
                         (request.requested_hashes.size() * kResourceMapHashLen));
    if (request.needs_more_hashmap)
    {
        request_data.push_back(0xFF);
        request_data.insert(request_data.end(),
                            request.last_known_hash.begin(),
                            request.last_known_hash.end());
    }
    else
    {
        request_data.push_back(0x00);
    }
    request_data.insert(request_data.end(),
                        resource.resource_hash,
                        resource.resource_hash + reticulum::kFullHashSize);
    for (const auto& requested_hash : request.requested_hashes)
    {
        request_data.insert(request_data.end(), requested_hash.begin(), requested_hash.end());
    }

    runtime::noteResourceWindowRequest(resource, request.needs_more_hashmap, millis());
    const bool ok = sendLinkPacket(session,
                                   reticulum::PacketType::Data,
                                   reticulum::PacketContext::ResourceReq,
                                   request_data.data(),
                                   request_data.size(),
                                   true);
    if (session.destination == LocalDestinationKind::NomadPage)
    {
        char resource_prefix[9] = {};
        char first_hash[9] = {};
        char last_known_hash[9] = {};
        formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
        if (!request.requested_hashes.empty())
        {
            formatHashPrefix(request.requested_hashes.front().data(),
                             first_hash,
                             sizeof(first_hash));
        }
        if (request.needs_more_hashmap)
        {
            formatHashPrefix(request.last_known_hash.data(),
                             last_known_hash,
                             sizeof(last_known_hash));
        }
        Serial.printf("[LXMF][ResourceTX] req resource=%s more_hashmap=%u hashes=%u first=%s last_known=%s payload_len=%u ok=%u\n",
                      resource_prefix,
                      request.needs_more_hashmap ? 1U : 0U,
                      static_cast<unsigned>(request.requested_hashes.size()),
                      first_hash[0] != '\0' ? first_hash : "-",
                      last_known_hash[0] != '\0' ? last_known_hash : "-",
                      static_cast<unsigned>(request_data.size()),
                      ok ? 1U : 0U);
    }
    return ok;
}

bool LxmfAdapter::handleLinkResourceRequest(LinkSession& session,
                                            const uint8_t* plaintext, size_t plaintext_len)
{
    if (!plaintext || plaintext_len < (1 + reticulum::kFullHashSize))
    {
        return false;
    }

    const bool wants_more_hashmap = plaintext[0] == 0xFF;
    size_t offset = 1;
    const uint8_t* last_map_hash = nullptr;
    if (wants_more_hashmap)
    {
        if (plaintext_len < (1 + kResourceMapHashLen + reticulum::kFullHashSize))
        {
            return false;
        }
        last_map_hash = plaintext + offset;
        offset += kResourceMapHashLen;
    }

    const uint8_t* resource_hash = plaintext + offset;
    offset += reticulum::kFullHashSize;
    LinkResourceTransfer* resource = runtime::findLinkResource(session.outgoing_resources, resource_hash);
    if (!resource)
    {
        return false;
    }

    bool sent_any = false;
    while (offset + kResourceMapHashLen <= plaintext_len)
    {
        const uint8_t* requested_hash = plaintext + offset;
        offset += kResourceMapHashLen;

        for (size_t index = 0; index < resource->map_hashes.size() && index < resource->parts.size(); ++index)
        {
            if (memcmp(resource->map_hashes[index].data(), requested_hash, kResourceMapHashLen) == 0)
            {
                if (!resource->parts[index].empty())
                {
                    sent_any = sendLinkPacket(session,
                                              reticulum::PacketType::Data,
                                              reticulum::PacketContext::Resource,
                                              resource->parts[index].data(),
                                              resource->parts[index].size(),
                                              false) ||
                               sent_any;
                }
                break;
            }
        }
    }

    if (wants_more_hashmap && last_map_hash)
    {
        const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
        size_t last_index = resource->part_count;
        for (size_t index = 0; index < resource->map_hashes.size(); ++index)
        {
            if (memcmp(resource->map_hashes[index].data(), last_map_hash, kResourceMapHashLen) == 0)
            {
                last_index = index;
                break;
            }
        }

        if (last_index < resource->part_count)
        {
            const size_t next_index = last_index + 1U;
            if (next_index < resource->part_count && (next_index % segment_capacity) == 0)
            {
                const uint32_t segment = static_cast<uint32_t>(next_index / segment_capacity);
                const size_t slice_offset = next_index * kResourceMapHashLen;
                const size_t remaining_hashes = static_cast<size_t>(resource->part_count) - next_index;
                const size_t slice_hashes = std::min(segment_capacity, remaining_hashes);

                uint8_t update_payload[kMaxPacketLen] = {};
                size_t update_len = sizeof(update_payload);
                if (encodeResourceHashmapUpdate(segment,
                                                resource->hashmap.data() + slice_offset,
                                                slice_hashes * kResourceMapHashLen,
                                                update_payload + reticulum::kFullHashSize,
                                                &update_len))
                {
                    memcpy(update_payload, resource->resource_hash, reticulum::kFullHashSize);
                    const size_t wire_len = reticulum::kFullHashSize + update_len;
                    sent_any = sendLinkPacket(session,
                                              reticulum::PacketType::Data,
                                              reticulum::PacketContext::ResourceHmu,
                                              update_payload,
                                              wire_len,
                                              true) ||
                               sent_any;
                }
            }
        }
    }

    resource->last_activity_ms = millis();
    return sent_any;
}

bool LxmfAdapter::handleLinkResourceHashmapUpdate(LinkSession& session,
                                                  const uint8_t* plaintext, size_t plaintext_len)
{
    if (!plaintext || plaintext_len <= reticulum::kFullHashSize)
    {
        return false;
    }

    LinkResourceTransfer* resource = runtime::findLinkResource(session.incoming_resources, plaintext);
    if (!resource)
    {
        return false;
    }

    DecodedResourceHashmapUpdate update{};
    if (!decodeResourceHashmapUpdate(plaintext + reticulum::kFullHashSize,
                                     plaintext_len - reticulum::kFullHashSize,
                                     &update) ||
        (update.hashmap.size() % kResourceMapHashLen) != 0)
    {
        return false;
    }

    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t start_index = static_cast<size_t>(update.segment) * segment_capacity;
    if (start_index >= resource->part_count)
    {
        return false;
    }

    if (!runtime::applyResourceHashmapUpdate(*resource,
                                             update.segment,
                                             update.hashmap,
                                             segment_capacity,
                                             millis()))
    {
        return false;
    }
    (void)requestNextResourceWindow(session, *resource);
    return true;
}

bool LxmfAdapter::handleLinkResourcePart(LinkSession& session,
                                         const reticulum::ParsedPacket& packet)
{
    if (!packet.payload || packet.payload_len == 0)
    {
        return false;
    }

    bool saw_incoming_resource = false;
    for (auto& resource : session.incoming_resources)
    {
        if (resource.complete)
        {
            continue;
        }
        saw_incoming_resource = true;

        uint8_t full_hash[reticulum::kFullHashSize] = {};
        std::vector<uint8_t> hash_material(packet.payload_len + sizeof(resource.random_hash), 0);
        memcpy(hash_material.data(), packet.payload, packet.payload_len);
        memcpy(hash_material.data() + packet.payload_len,
               resource.random_hash,
               sizeof(resource.random_hash));
        reticulum::fullHash(hash_material.data(),
                            hash_material.size(),
                            full_hash);

        bool complete = false;
        std::size_t matched_index = resource.part_count;
        if (!runtime::recordResourcePart(resource,
                                         packet.payload,
                                         packet.payload_len,
                                         full_hash,
                                         millis(),
                                         &matched_index,
                                         &complete))
        {
            if (session.destination == LocalDestinationKind::NomadPage)
            {
                char resource_prefix[9] = {};
                char part_hash[9] = {};
                char first_expected[9] = {};
                uint32_t known_count = 0;
                formatHashPrefix(resource.resource_hash,
                                 resource_prefix,
                                 sizeof(resource_prefix));
                formatHashPrefix(full_hash, part_hash, sizeof(part_hash));
                for (std::size_t index = 0; index < resource.map_hash_known.size(); ++index)
                {
                    known_count += resource.map_hash_known[index] != 0 ? 1U : 0U;
                }
                if (!resource.map_hashes.empty())
                {
                    formatHashPrefix(resource.map_hashes.front().data(),
                                     first_expected,
                                     sizeof(first_expected));
                }
                Serial.printf("[LXMF][ResourceRX] part_unmatched resource=%s part_hash=%s first_expected=%s known=%u/%u payload_len=%u\n",
                              resource_prefix,
                              part_hash,
                              first_expected[0] != '\0' ? first_expected : "-",
                              static_cast<unsigned>(known_count),
                              static_cast<unsigned>(resource.part_count),
                              static_cast<unsigned>(packet.payload_len));
            }
            continue;
        }

        if (session.destination == LocalDestinationKind::NomadPage)
        {
            char resource_prefix[9] = {};
            char part_hash[9] = {};
            formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
            formatHashPrefix(full_hash, part_hash, sizeof(part_hash));
            Serial.printf("[LXMF][ResourceRX] part_accept resource=%s part_hash=%s index=%u/%u payload_len=%u complete=%u\n",
                          resource_prefix,
                          part_hash,
                          static_cast<unsigned>(matched_index),
                          static_cast<unsigned>(resource.part_count),
                          static_cast<unsigned>(packet.payload_len),
                          complete ? 1U : 0U);
        }

        if (session.destination == LocalDestinationKind::NomadPage &&
            !resource.request_id.empty())
        {
            if (PendingNomadPageRequest* page_request =
                    findPendingNomadPageRequestById(
                        session.remote_destination_hash,
                        resource.request_id.data(),
                        resource.request_id.size()))
            {
                uint32_t received_count = 0;
                for (uint8_t received : resource.received_bitmap)
                {
                    received_count += received != 0 ? 1U : 0U;
                }
                const uint32_t total_count = resource.part_count != 0
                                                 ? resource.part_count
                                                 : 1U;
                int progress_percent =
                    45 + static_cast<int>((received_count * 45U) / total_count);
                if (progress_percent > 90)
                {
                    progress_percent = 90;
                }
                char detail[40] = {};
                std::snprintf(detail,
                              sizeof(detail),
                              "%u/%u parts",
                              static_cast<unsigned>(received_count),
                              static_cast<unsigned>(resource.part_count));
                updateNomadPageProgress(*page_request,
                                        complete ? 90 : progress_percent,
                                        "Receiving Nomad page",
                                        detail,
                                        true,
                                        false,
                                        PageFailureKind::None);
            }
        }

        if (!complete)
        {
            (void)requestNextResourceWindow(session, resource);
            return true;
        }

        std::vector<uint8_t> assembled;
        assembled.reserve(resource.transfer_size);
        for (const auto& part : resource.parts)
        {
            assembled.insert(assembled.end(), part.begin(), part.end());
        }
        if (assembled.size() > resource.transfer_size)
        {
            assembled.resize(resource.transfer_size);
        }

        std::vector<uint8_t> resource_stream;
        if (resource.encrypted)
        {
            if (!decryptLinkPayload(session,
                                    assembled.data(),
                                    assembled.size(),
                                    &resource_stream))
            {
                return false;
            }
        }
        else
        {
            resource_stream = std::move(assembled);
        }

        if (resource.has_metadata || resource_stream.size() < kResourceDataPrefixLen)
        {
            char resource_prefix[9] = {};
            formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
            Serial.printf("[LXMF][ResourceRX] assemble_reject resource=%s reason=%s stream=%u\n",
                          resource_prefix,
                          resource.has_metadata ? "metadata" : "short_stream",
                          static_cast<unsigned>(resource_stream.size()));
            return false;
        }

        const uint8_t* resource_payload =
            resource_stream.data() + kResourceDataPrefixLen;
        const size_t resource_payload_len =
            resource_stream.size() - kResourceDataPrefixLen;
        std::vector<uint8_t> payload_data;
        if (resource.compressed)
        {
            int bz_status = BZ_OK;
            if (!decompressBzip2Payload(resource_payload,
                                        resource_payload_len,
                                        resource.data_size,
                                        &payload_data,
                                        &bz_status))
            {
                char resource_prefix[9] = {};
                formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                Serial.printf("[LXMF][ResourceRX] decompress_failed resource=%s bz=%d comp_len=%u "
                              "expected=%u stream=%u\n",
                              resource_prefix,
                              bz_status,
                              static_cast<unsigned>(resource_payload_len),
                              static_cast<unsigned>(resource.data_size),
                              static_cast<unsigned>(resource_stream.size()));
                return false;
            }
            if (session.destination == LocalDestinationKind::NomadPage)
            {
                char resource_prefix[9] = {};
                formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
                Serial.printf("[LXMF][ResourceRX] decompressed resource=%s comp_len=%u out=%u\n",
                              resource_prefix,
                              static_cast<unsigned>(resource_payload_len),
                              static_cast<unsigned>(payload_data.size()));
            }
        }
        else
        {
            payload_data.assign(resource_stream.begin() + kResourceDataPrefixLen,
                                resource_stream.end());
        }
        if (payload_data.size() != resource.data_size)
        {
            char resource_prefix[9] = {};
            formatHashPrefix(resource.resource_hash, resource_prefix, sizeof(resource_prefix));
            Serial.printf("[LXMF][ResourceRX] assemble_reject resource=%s reason=size actual=%u expected=%u\n",
                          resource_prefix,
                          static_cast<unsigned>(payload_data.size()),
                          static_cast<unsigned>(resource.data_size));
            return false;
        }

        std::vector<uint8_t> resource_hash_material(payload_data.size() + sizeof(resource.random_hash), 0);
        if (!payload_data.empty())
        {
            memcpy(resource_hash_material.data(), payload_data.data(), payload_data.size());
        }
        memcpy(resource_hash_material.data() + payload_data.size(),
               resource.random_hash,
               sizeof(resource.random_hash));

        uint8_t expected_resource_hash[reticulum::kFullHashSize] = {};
        reticulum::fullHash(resource_hash_material.data(),
                            resource_hash_material.size(),
                            expected_resource_hash);
        if (!hashesEqual(expected_resource_hash,
                         resource.resource_hash,
                         reticulum::kFullHashSize))
        {
            return false;
        }

        std::vector<uint8_t> proof_material(payload_data.size() + reticulum::kFullHashSize, 0);
        if (!payload_data.empty())
        {
            memcpy(proof_material.data(), payload_data.data(), payload_data.size());
        }
        memcpy(proof_material.data() + payload_data.size(),
               resource.resource_hash,
               reticulum::kFullHashSize);
        reticulum::fullHash(proof_material.data(),
                            proof_material.size(),
                            resource.expected_proof);

        std::array<uint8_t, reticulum::kFullHashSize * 2> proof_payload{};
        memcpy(proof_payload.data(), resource.resource_hash, reticulum::kFullHashSize);
        memcpy(proof_payload.data() + reticulum::kFullHashSize,
               resource.expected_proof,
               reticulum::kFullHashSize);
        (void)sendLinkPacket(session,
                             reticulum::PacketType::Proof,
                             reticulum::PacketContext::ResourcePrf,
                             proof_payload.data(),
                             proof_payload.size(),
                             false);

        runtime::markResourceComplete(resource, millis());

        const runtime::ResourceAssemblyResult assembly_result =
            runtime::appendResourceAssemblySegment(session, resource, payload_data, millis());
        if (assembly_result == runtime::ResourceAssemblyResult::Rejected)
        {
            return false;
        }
        if (assembly_result == runtime::ResourceAssemblyResult::WaitingForNextSegment)
        {
            return true;
        }

        const bool is_request = (resource.flags & kResourceFlagRequest) != 0;
        const bool is_response = (resource.flags & kResourceFlagResponse) != 0;
        if (is_request)
        {
            DecodedLinkRequest request{};
            if (decodeLinkRequestPayload(payload_data.data(), payload_data.size(), &request))
            {
                uint8_t request_id[reticulum::kTruncatedHashSize] = {};
                reticulum::truncatedHash(payload_data.data(), payload_data.size(), request_id);
                if (session.destination == LocalDestinationKind::Propagation)
                {
                    (void)handlePropagationRequest(session,
                                                   request,
                                                   request_id,
                                                   sizeof(request_id));
                }
                else
                {
                    (void)sendLinkResponse(session,
                                           request_id,
                                           sizeof(request_id),
                                           nullptr,
                                           0,
                                           true);
                }
            }
        }
        else if (is_response)
        {
            DecodedLinkResponse response{};
            if (decodeLinkResponsePayload(payload_data.data(), payload_data.size(), &response))
            {
                for (auto& pending : session.pending_requests)
                {
                    if (pending.request_id == response.request_id)
                    {
                        pending.response_ready = true;
                        if (!response.data_is_nil)
                        {
                            pending.response = std::move(response.packed_data);
                        }
                        break;
                    }
                }
            }
        }
        else if (session.destination == LocalDestinationKind::Delivery)
        {
            (void)acceptVerifiedEnvelope(payload_data.data(), payload_data.size(), nullptr, 0);
        }
        else if (session.destination == LocalDestinationKind::Propagation)
        {
            (void)handlePropagationBatch(session, payload_data.data(), payload_data.size());
        }

        return true;
    }

    if (!saw_incoming_resource && session.destination == LocalDestinationKind::NomadPage)
    {
        char link_hash[9] = {};
        formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
        Serial.printf("[LXMF][ResourceRX] part_drop reason=no_incoming_resource link=%s payload_len=%u\n",
                      link_hash,
                      static_cast<unsigned>(packet.payload_len));
    }
    return false;
}

bool LxmfAdapter::handleLinkResourceProof(LinkSession& session,
                                          const reticulum::ParsedPacket& packet)
{
    if (!packet.payload || packet.payload_len != (reticulum::kFullHashSize * 2))
    {
        return false;
    }

    const uint8_t* resource_hash = packet.payload;
    const uint8_t* expected_proof = packet.payload + reticulum::kFullHashSize;
    LinkResourceTransfer* resource = runtime::findLinkResource(session.outgoing_resources, resource_hash);
    if (!resource)
    {
        return false;
    }

    return runtime::markResourceProofReceived(*resource, expected_proof, millis());
}

bool LxmfAdapter::handlePropagationBatch(LinkSession& session,
                                         const uint8_t* plaintext,
                                         size_t plaintext_len)
{
    runtime::PropagationBatchContext batch_context{};
    batch_context.offer_validated = session.propagation_offer_validated;
    batch_context.local_delivery_hash_known = true;
    localDestinationHash(LocalDestinationKind::Delivery, batch_context.local_delivery_hash);
    batch_context.peer_context.remote_identity_known = session.remote_identity_known;
    batch_context.now_s = currentTimestampSeconds();
    if (session.remote_identity_known)
    {
        destinationHashForAspect(session.remote_identity_hash,
                                 "delivery",
                                 batch_context.peer_context.remote_delivery_hash);
        destinationHashForAspect(session.remote_identity_hash,
                                 "propagation",
                                 batch_context.peer_context.remote_propagation_hash);
        copyHash(batch_context.peer_context.remote_identity_hash,
                 session.remote_identity_hash,
                 sizeof(batch_context.peer_context.remote_identity_hash));
    }

    const runtime::PropagationBatchLimits batch_limits{
        kMaxPropagationEntries,
        kMaxPropagationTransients,
        kMaxPropagationPeers,
        1};
    runtime::PropagationBatchAcceptance batch_acceptance{};
    if (!runtime::planPropagationBatchAcceptance(propagation_,
                                                 plaintext,
                                                 plaintext_len,
                                                 batch_context,
                                                 batch_limits,
                                                 &batch_acceptance))
    {
        return false;
    }

    bool handled_any = false;
    for (const auto& message : batch_acceptance.messages)
    {
        bool handled = false;
        if (message.action == runtime::PropagationMessageAction::Duplicate ||
            message.action == runtime::PropagationMessageAction::Stored)
        {
            handled = true;
        }
        else if (message.action == runtime::PropagationMessageAction::DeliverLocal)
        {
            const bool delivered =
                acceptPropagatedDelivery(message.local_delivery_payload.empty()
                                             ? nullptr
                                             : message.local_delivery_payload.data(),
                                         message.local_delivery_payload.size());
            runtime::notePropagationLocalDeliveryResult(propagation_,
                                                        message.transient_id,
                                                        delivered,
                                                        currentTimestampSeconds(),
                                                        kMaxPropagationTransients);
            handled = delivered;
        }

        if (handled)
        {
            handled_any = true;
            runtime::notePropagationBatchMessageHandled(propagation_, batch_acceptance);
        }
    }

    return handled_any;
}

bool LxmfAdapter::handlePropagationRequest(LinkSession& session,
                                           const DecodedLinkRequest& request,
                                           const uint8_t* request_id,
                                           size_t request_id_len)
{
    runtime::PropagationServicePeerContext peer_context{};
    peer_context.remote_identity_known = session.remote_identity_known;
    if (session.remote_identity_known)
    {
        destinationHashForAspect(session.remote_identity_hash,
                                 "delivery",
                                 peer_context.remote_delivery_hash);
        destinationHashForAspect(session.remote_identity_hash,
                                 "propagation",
                                 peer_context.remote_propagation_hash);
        copyHash(peer_context.remote_identity_hash,
                 session.remote_identity_hash,
                 sizeof(peer_context.remote_identity_hash));
    }

    const runtime::PropagationServiceLimits limits{
        kMaxPropagationTransients,
        kMaxPropagationPeers,
        24,
        16};
    runtime::PropagationServiceResponse response{};
    if (!runtime::planPropagationServiceResponse(propagation_,
                                                 request,
                                                 peer_context,
                                                 currentTimestampSeconds(),
                                                 limits,
                                                 &response))
    {
        return false;
    }
    if (response.offer_validated)
    {
        session.propagation_offer_validated = true;
    }
    if (!response.send_response)
    {
        return false;
    }
    return sendLinkResponse(session,
                            request_id,
                            request_id_len,
                            response.packed_response.empty() ? nullptr
                                                             : response.packed_response.data(),
                            response.packed_response.size(),
                            response.response_data_is_nil);
}

bool LxmfAdapter::acceptPropagatedDelivery(const uint8_t* propagated_payload,
                                           size_t propagated_payload_len)
{
    if (!propagated_payload ||
        propagated_payload_len <= (reticulum::kEncryptionPublicKeySize + reticulum::kTokenOverhead))
    {
        return false;
    }

    const uint8_t* peer_ephemeral_pub = propagated_payload;
    const uint8_t* token = propagated_payload + reticulum::kEncryptionPublicKeySize;
    const size_t token_len = propagated_payload_len - reticulum::kEncryptionPublicKeySize;

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    if (!identity_.deriveSharedSecret(peer_ephemeral_pub, shared_secret))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret,
                               sizeof(shared_secret),
                               identity_.identityHash(),
                               reticulum::kTruncatedHashSize,
                               nullptr,
                               0,
                               derived_key,
                               sizeof(derived_key)))
    {
        return false;
    }

    std::vector<uint8_t> plaintext(token_len, 0);
    size_t plaintext_len = plaintext.size();
    if (!reticulum::tokenDecrypt(derived_key,
                                 token,
                                 token_len,
                                 plaintext.data(),
                                 &plaintext_len))
    {
        return false;
    }
    plaintext.resize(plaintext_len);

    std::vector<uint8_t> lxmf_message(reticulum::kTruncatedHashSize + plaintext.size(), 0);
    memcpy(lxmf_message.data(), identity_.destinationHash(), reticulum::kTruncatedHashSize);
    if (!plaintext.empty())
    {
        memcpy(lxmf_message.data() + reticulum::kTruncatedHashSize,
               plaintext.data(),
               plaintext.size());
    }

    return acceptVerifiedEnvelope(lxmf_message.data(), lxmf_message.size(), nullptr, 0);
}

bool LxmfAdapter::maybeForwardTransportPacket(const uint8_t* raw_packet, size_t raw_len,
                                              const reticulum::ParsedPacket& packet)
{
    if (!raw_packet || raw_len == 0 || !packet.destination_hash)
    {
        return false;
    }

    if (!packet.transport_id ||
        !hashesEqual(packet.transport_id, identity_.identityHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }

    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return false;
    }

    const PathEntry* path = findPath(packet.destination_hash);
    if (!path)
    {
        return false;
    }

    if (packet.packet_type != reticulum::PacketType::Proof &&
        packet.packet_type != reticulum::PacketType::Announce)
    {
        uint8_t proof_hash[reticulum::kTruncatedHashSize] = {};
        reticulum::computeTruncatedPacketHash(raw_packet, raw_len, proof_hash);
        rememberReversePath(proof_hash, path->hops);
    }

    if (path->hops <= 1 || path->direct)
    {
        uint8_t forward_packet[kMaxPacketLen] = {};
        size_t forward_len = sizeof(forward_packet);
        if (!reticulum::buildHeader1Packet(packet.packet_type,
                                           packet.destination_type,
                                           static_cast<reticulum::PacketContext>(packet.context),
                                           packet.context_flag != 0,
                                           packet.destination_hash,
                                           packet.payload,
                                           packet.payload_len,
                                           forward_packet,
                                           &forward_len,
                                           packet.hops,
                                           reticulum::TransportType::Broadcast))
        {
            return false;
        }

        return interfaces_.sendPacket(forward_packet, forward_len);
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader2Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       path->next_hop_transport,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops))
    {
        return false;
    }

    return interfaces_.sendPacket(forward_packet, forward_len);
}

bool LxmfAdapter::maybeForwardLinkPacket(const uint8_t* raw_packet, size_t raw_len,
                                         const reticulum::ParsedPacket& packet)
{
    (void)raw_packet;
    (void)raw_len;

    if (!packet.destination_hash)
    {
        return false;
    }

    if (packet.destination_type != reticulum::DestinationType::Link)
    {
        return false;
    }

    LinkRelayEntry* relay = findLinkRelay(packet.destination_hash);
    if (!relay)
    {
        return false;
    }

    const bool from_initiator = (packet.hops == relay->initiator_hops);
    const bool from_responder = (packet.hops == relay->responder_hops);
    if (!from_initiator && !from_responder)
    {
        return false;
    }

    uint8_t forward_packet[kMaxPacketLen] = {};
    size_t forward_len = sizeof(forward_packet);
    if (!reticulum::buildHeader1Packet(packet.packet_type,
                                       packet.destination_type,
                                       static_cast<reticulum::PacketContext>(packet.context),
                                       packet.context_flag != 0,
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       forward_packet,
                                       &forward_len,
                                       packet.hops,
                                       reticulum::TransportType::Broadcast))
    {
        return false;
    }

    relay->last_seen_ms = millis();
    return interfaces_.sendPacket(forward_packet, forward_len);
}

bool LxmfAdapter::sendProofForPacket(const uint8_t* raw_packet, size_t raw_len)
{
    if (!raw_packet || raw_len == 0 || !isReady())
    {
        return false;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(raw_packet, raw_len, packet_hash);

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(packet_hash, sizeof(packet_hash), signature))
    {
        return false;
    }

    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    memcpy(destination_hash, packet_hash, sizeof(destination_hash));

    uint8_t proof_packet[kMaxPacketLen] = {};
    size_t proof_len = sizeof(proof_packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Proof,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       false,
                                       destination_hash,
                                       signature,
                                       sizeof(signature),
                                       proof_packet,
                                       &proof_len))
    {
        return false;
    }

    return routeAndSendPacket(proof_packet, proof_len, false);
}

bool LxmfAdapter::sendPathRequest(PeerInfo& peer)
{
    if (!isReady() || isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (peer.last_path_request_ms != 0 &&
        (now_ms - peer.last_path_request_ms) < kPathRequestMinIntervalMs)
    {
        return false;
    }

    if (const PendingPathRequest* pending = findPendingPathRequest(peer.destination_hash))
    {
        if (!pending->resolved &&
            pending->last_attempt_ms != 0 &&
            (now_ms - pending->last_attempt_ms) < kPathRequestMinIntervalMs)
        {
            return false;
        }
    }

    uint8_t request_payload[reticulum::kTruncatedHashSize + kPathRequestTagSize] = {};
    memcpy(request_payload, peer.destination_hash, reticulum::kTruncatedHashSize);
    fillRandomBytes(request_payload + reticulum::kTruncatedHashSize, kPathRequestTagSize);

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Plain,
                                       reticulum::PacketContext::None,
                                       false,
                                       control_hash,
                                       request_payload,
                                       sizeof(request_payload),
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    if (!routeAndSendPacket(packet, packet_len, false))
    {
        return false;
    }

    notePendingPathRequest(peer.destination_hash, now_ms);
    peer.last_path_request_ms = now_ms;
    return true;
}

bool LxmfAdapter::sendPathRequestForDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize) ||
        !identity_.isReady())
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (const PendingPathRequest* pending = findPendingPathRequest(destination_hash))
    {
        if (!pending->resolved &&
            pending->last_attempt_ms != 0 &&
            (now_ms - pending->last_attempt_ms) < kPathRequestMinIntervalMs)
        {
            return false;
        }
    }

    uint8_t request_payload[reticulum::kTruncatedHashSize + kPathRequestTagSize] = {};
    memcpy(request_payload, destination_hash, reticulum::kTruncatedHashSize);
    fillRandomBytes(request_payload + reticulum::kTruncatedHashSize, kPathRequestTagSize);

    uint8_t control_hash[reticulum::kTruncatedHashSize] = {};
    pathRequestDestinationHash(control_hash);

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Plain,
                                       reticulum::PacketContext::None,
                                       false,
                                       control_hash,
                                       request_payload,
                                       sizeof(request_payload),
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    if (!routeAndSendPacket(packet, packet_len, false, true))
    {
        return false;
    }

    notePendingPathRequest(destination_hash, now_ms);
    return true;
}

bool LxmfAdapter::shouldRequestPath(const PeerInfo& peer) const
{
    if (isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (const PendingPathRequest* pending = findPendingPathRequest(peer.destination_hash))
    {
        if (!pending->resolved &&
            pending->created_ms != 0 &&
            (now_ms - pending->created_ms) < kPendingPathRequestTtlMs)
        {
            return false;
        }
    }

    if (peer.last_path_request_ms != 0 &&
        (now_ms - peer.last_path_request_ms) < kPathRequestMinIntervalMs)
    {
        return false;
    }

    const PathEntry* path = findPath(peer.destination_hash);
    if (!path || path->last_seen_s == 0)
    {
        return true;
    }

    const uint32_t now_s = currentTimestampSeconds();
    if (now_s < path->last_seen_s)
    {
        return true;
    }

    return (now_s - path->last_seen_s) >= kPathRefreshAgeS;
}

LxmfAdapter::LinkSession* LxmfAdapter::ensureOutboundLinkSession(PeerInfo& peer,
                                                                 LocalDestinationKind kind,
                                                                 bool* out_started)
{
    if (out_started)
    {
        *out_started = false;
    }

    if (isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)))
    {
        return nullptr;
    }

    if (LinkSession* session =
            runtime::findOpenLinkSessionByDestination(links_, peer.destination_hash, kind))
    {
        return session;
    }

    const PathEntry* path = findPath(peer.destination_hash);
    bool path_requested = false;
    if (!path && shouldRequestPath(peer))
    {
        path_requested = sendPathRequest(peer);
    }

    LinkSession& session = runtime::appendLinkSession(links_, kMaxLinkSessions);
    session.created_ms = millis();
    session.request_ms = session.created_ms;
    session.last_inbound_ms = session.created_ms;
    session.last_outbound_ms = 0;
    session.initiator = true;
    session.destination = kind;
    session.state = LinkState::Pending;
    session.close_reason = LinkCloseReason::None;
    session.expected_hops = path ? path->hops : 0;
    session.remote_identity_known = !isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash));
    session.validated = false;
    session.keepalive_interval_ms = kLinkKeepaliveMaxMs;
    session.stale_timeout_ms = kLinkKeepaliveMaxMs * 2U;
    copyHash(session.remote_destination_hash,
             peer.destination_hash,
             sizeof(session.remote_destination_hash));
    copyHash(session.remote_identity_hash,
             peer.identity_hash,
             sizeof(session.remote_identity_hash));
    memcpy(session.peer_identity_sig_pub, peer.sig_pub, sizeof(session.peer_identity_sig_pub));

    Curve25519::dh1(session.local_enc_pub, session.local_enc_priv);
    char dest_hash[12] = {};
    formatHashPrefix(peer.destination_hash, dest_hash, sizeof(dest_hash));
    if (isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)))
    {
        Serial.printf("[LXMF][LinkTX] start_failed peer=%08lX dest=%s kind=%u reason=local_key\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind));
        links_.sessions.pop_back();
        return nullptr;
    }

    if (!generateLinkSigningKey(session.local_sig_pub, session.local_sig_priv))
    {
        Serial.printf("[LXMF][LinkTX] start_failed peer=%08lX dest=%s kind=%u reason=signing_key\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind));
        links_.sessions.pop_back();
        return nullptr;
    }

    if (!path)
    {
        Serial.printf("[LXMF][LinkTX] wait_path peer=%08lX dest=%s kind=%u requested=%u\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind),
                      path_requested ? 1U : 0U);
        if (out_started)
        {
            *out_started = true;
        }
        return &session;
    }

    if (!sendLinkRequest(session))
    {
        Serial.printf("[LXMF][LinkTX] start_failed peer=%08lX dest=%s kind=%u reason=request_send\n",
                      static_cast<unsigned long>(peer.node_id),
                      dest_hash,
                      static_cast<unsigned>(kind));
        links_.sessions.pop_back();
        return nullptr;
    }

    if (out_started)
    {
        *out_started = true;
    }
    return &session;
}

bool LxmfAdapter::sendLinkRequest(LinkSession& session)
{
    if (!isReady() || isZeroBytes(session.remote_destination_hash, sizeof(session.remote_destination_hash)))
    {
        char dest_hash[12] = {};
        formatHashPrefix(session.remote_destination_hash, dest_hash, sizeof(dest_hash));
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=not_ready ready=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      isReady() ? 1U : 0U);
        return false;
    }

    char dest_hash[12] = {};
    formatHashPrefix(session.remote_destination_hash, dest_hash, sizeof(dest_hash));

    uint8_t signalling[kLinkSignallingLen] = {};
    buildLinkSignallingBytes(reticulum::kReticulumMtu, signalling);

    uint8_t request_payload[kLinkRequestBaseLen + kLinkSignallingLen] = {};
    memcpy(request_payload, session.local_enc_pub, LxmfIdentity::kEncPubKeySize);
    memcpy(request_payload + LxmfIdentity::kEncPubKeySize,
           session.local_sig_pub,
           LxmfIdentity::kSigPubKeySize);
    memcpy(request_payload + kLinkRequestBaseLen, signalling, sizeof(signalling));

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::LinkRequest,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       false,
                                       session.remote_destination_hash,
                                       request_payload,
                                       sizeof(request_payload),
                                       packet,
                                       &packet_len))
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=build\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination));
        return false;
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(packet, packet_len, &parsed) || !parsed.destination_hash)
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=parse raw_len=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      static_cast<unsigned>(packet_len));
        return false;
    }

    uint8_t routed_packet[kMaxPacketLen] = {};
    const uint8_t* tx_packet = packet;
    size_t tx_packet_len = packet_len;
    bool routed = false;
    const PathEntry* tx_path = findPath(parsed.destination_hash);
    if (tx_path)
    {
        if (tx_path->hops > 1 && !tx_path->direct)
        {
            tx_packet_len = sizeof(routed_packet);
            if (!reticulum::buildHeader2Packet(parsed.packet_type,
                                               parsed.destination_type,
                                               static_cast<reticulum::PacketContext>(parsed.context),
                                               parsed.context_flag != 0,
                                               tx_path->next_hop_transport,
                                               parsed.destination_hash,
                                               parsed.payload,
                                               parsed.payload_len,
                                               routed_packet,
                                               &tx_packet_len,
                                               packet[1]))
            {
                Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=route_build raw_len=%u\n",
                              dest_hash,
                              static_cast<unsigned>(session.destination),
                              static_cast<unsigned>(packet_len));
                return false;
            }
            tx_packet = routed_packet;
            routed = true;
        }
    }

    reticulum::ParsedPacket tx_parsed{};
    if (!reticulum::parsePacket(tx_packet, tx_packet_len, &tx_parsed) ||
        !computeLinkIdFromLinkRequest(tx_packet, tx_packet_len, tx_parsed, session.link_id))
    {
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=link_id raw_len=%u routed=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      static_cast<unsigned>(tx_packet_len),
                      routed ? 1U : 0U);
        return false;
    }

    session.request_ms = millis();
    const bool wifi_only = session.destination == LocalDestinationKind::CallAudio;
    if (wifi_only)
    {
        ::platform::ui::reticulum_call::Peer call_peer{};
        copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
        copyHash(call_peer.destination_hash,
                 session.remote_destination_hash,
                 sizeof(call_peer.destination_hash));
        copyHash(call_peer.identity_hash,
                 session.remote_identity_hash,
                 sizeof(call_peer.identity_hash));
        call_peer.incoming = false;
        if (!::platform::ui::reticulum_call::begin_outgoing(call_peer))
        {
            Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=call_realtime_busy\n",
                          dest_hash,
                          static_cast<unsigned>(session.destination));
            return false;
        }
    }
    const bool sent = wifi_only ? interfaces_.sendPacketWifiOnly(tx_packet,
                                                                 tx_packet_len,
                                                                 session.link_id)
                                : interfaces_.sendPacket(tx_packet, tx_packet_len);
    const auto& tx_result = interfaces_.lastTxResult();
    if (sent)
    {
        session.last_outbound_ms = session.request_ms;
        session.mtu = reticulum::kReticulumMtu;
        session.mdu = linkMduForMtu(session.mtu);
        char link_hash[12] = {};
        char next_hop[12] = {};
        formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
        formatHashPrefix(tx_path ? tx_path->next_hop_transport : nullptr,
                         next_hop,
                         sizeof(next_hop));
        const uint32_t now_s = currentTimestampSeconds();
        const uint32_t path_age_s =
            (tx_path && tx_path->last_seen_s != 0 && now_s >= tx_path->last_seen_s)
                ? (now_s - tx_path->last_seen_s)
                : 0;
        Serial.printf("[LXMF][LinkTX] request_sent dest=%s kind=%u link=%s raw_len=%u routed=%u path_hops=%u path_direct=%u path_age_s=%lu next=%s announce=%u bearer=%s complete=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      link_hash,
                      static_cast<unsigned>(tx_packet_len),
                      routed ? 1U : 0U,
                      static_cast<unsigned>(tx_path ? tx_path->hops : 0U),
                      tx_path && tx_path->direct ? 1U : 0U,
                      static_cast<unsigned long>(path_age_s),
                      next_hop,
                      static_cast<unsigned>(tx_path ? tx_path->cached_announce_len : 0U),
                      txBearerName(tx_result),
                      tx_result.reachedRequiredInterfaces() ? 1U : 0U);
    }
    else
    {
        if (wifi_only)
        {
            ::platform::ui::reticulum_call::notify_link_closed(session.link_id);
        }
        Serial.printf("[LXMF][LinkTX] request_fail dest=%s kind=%u reason=send raw_len=%u routed=%u bearer=%s complete=%u\n",
                      dest_hash,
                      static_cast<unsigned>(session.destination),
                      static_cast<unsigned>(tx_packet_len),
                      routed ? 1U : 0U,
                      txBearerName(tx_result),
                      tx_result.reachedRequiredInterfaces() ? 1U : 0U);
    }
    return sent;
}

bool LxmfAdapter::buildSignedMessagePacket(const PeerInfo& peer,
                                           const uint8_t* packed_payload, size_t packed_payload_len,
                                           uint8_t* out_packet, size_t* inout_len,
                                           uint8_t out_message_hash[reticulum::kFullHashSize])
{
    if ((!packed_payload && packed_payload_len != 0) || !out_packet || !inout_len || !out_message_hash)
    {
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    if (!buildSignedPart(peer.destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         out_message_hash))
    {
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_part, signed_part_len, signature))
    {
        return false;
    }

    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!packMessage(peer.destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        return false;
    }

    if (lxmf_message_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    return buildEncryptedPacketForPeer(peer,
                                       lxmf_message + reticulum::kTruncatedHashSize,
                                       lxmf_message_len - reticulum::kTruncatedHashSize,
                                       out_packet,
                                       inout_len);
}

bool LxmfAdapter::buildGroupMessagePacket(
    const ReticulumPeerIdentity& destination,
    const uint8_t* packed_payload, size_t packed_payload_len,
    uint8_t* out_packet, size_t* inout_len,
    uint8_t out_message_hash[reticulum::kFullHashSize])
{
    if (!hasReticulumDestinationIdentity(destination) ||
        (!packed_payload && packed_payload_len != 0) ||
        !out_packet ||
        !inout_len ||
        !out_message_hash)
    {
        return false;
    }

    uint8_t signed_part[kSignedPartMaxLen] = {};
    size_t signed_part_len = sizeof(signed_part);
    if (!buildSignedPart(destination.destination_hash,
                         identity_.destinationHash(),
                         packed_payload,
                         packed_payload_len,
                         signed_part,
                         &signed_part_len,
                         out_message_hash))
    {
        return false;
    }

    uint8_t signature[reticulum::kSignatureSize] = {};
    if (!identity_.sign(signed_part, signed_part_len, signature))
    {
        return false;
    }

    uint8_t lxmf_message[kMaxLxmfMessageLen] = {};
    size_t lxmf_message_len = sizeof(lxmf_message);
    if (!packMessage(destination.destination_hash,
                     identity_.destinationHash(),
                     signature,
                     packed_payload,
                     packed_payload_len,
                     lxmf_message,
                     &lxmf_message_len))
    {
        return false;
    }

    if (lxmf_message_len <= reticulum::kTruncatedHashSize)
    {
        return false;
    }

    return reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Group,
                                         reticulum::PacketContext::None,
                                         false,
                                         destination.destination_hash,
                                         lxmf_message + reticulum::kTruncatedHashSize,
                                         lxmf_message_len - reticulum::kTruncatedHashSize,
                                         out_packet,
                                         inout_len);
}

bool LxmfAdapter::buildEncryptedPacketForPeer(const PeerInfo& peer,
                                              const uint8_t* plaintext, size_t plaintext_len,
                                              uint8_t* out_packet, size_t* inout_len)
{
    if ((!plaintext && plaintext_len != 0) || !out_packet || !inout_len)
    {
        return false;
    }

    uint8_t ephemeral_pub[LxmfIdentity::kEncPubKeySize] = {};
    uint8_t ephemeral_priv[LxmfIdentity::kEncPrivKeySize] = {};
    Curve25519::dh1(ephemeral_pub, ephemeral_priv);

    const bool use_ratchet = peerHasUsableRatchet(peer);
    const uint8_t* encryption_target = use_ratchet ? peer.ratchet_pub : peer.enc_pub;
    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    memcpy(shared_secret, encryption_target, sizeof(shared_secret));
    if (!Curve25519::dh2(shared_secret, ephemeral_priv))
    {
        return false;
    }

    uint8_t derived_key[reticulum::kDerivedTokenKeySize] = {};
    if (!reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                               peer.identity_hash, sizeof(peer.identity_hash),
                               nullptr, 0,
                               derived_key, sizeof(derived_key)))
    {
        return false;
    }

    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));

    uint8_t encrypted_token[kMaxPacketLen] = {};
    size_t encrypted_token_len = sizeof(encrypted_token);
    if (!reticulum::tokenEncrypt(derived_key, iv, plaintext, plaintext_len,
                                 encrypted_token, &encrypted_token_len))
    {
        return false;
    }

    uint8_t payload[kMaxPacketLen] = {};
    const size_t payload_len = sizeof(ephemeral_pub) + encrypted_token_len;
    if (payload_len > sizeof(payload))
    {
        return false;
    }
    memcpy(payload, ephemeral_pub, sizeof(ephemeral_pub));
    memcpy(payload + sizeof(ephemeral_pub), encrypted_token, encrypted_token_len);

    char dest_hash[12] = {};
    char ratchet_id[12] = {};
    formatHashPrefix(peer.destination_hash, dest_hash, sizeof(dest_hash));
    formatRatchetIdPrefix(use_ratchet ? peer.ratchet_pub : nullptr,
                          ratchet_id,
                          sizeof(ratchet_id));
    Serial.printf("[LXMF][EncryptTX] dest=%s enc_target=%s ratchet=%u ratchet_id=%s plaintext_len=%u token_len=%u\n",
                  dest_hash,
                  use_ratchet ? "ratchet" : "identity",
                  use_ratchet ? 1U : 0U,
                  ratchet_id,
                  static_cast<unsigned>(plaintext_len),
                  static_cast<unsigned>(encrypted_token_len));

    return reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                         reticulum::DestinationType::Single,
                                         reticulum::PacketContext::None,
                                         false,
                                         peer.destination_hash,
                                         payload,
                                         payload_len,
                                         out_packet,
                                         inout_len);
}

bool LxmfAdapter::routeAndSendPacket(const uint8_t* raw_packet, size_t raw_len,
                                     bool allow_transport,
                                     bool wifi_only)
{
    if (!raw_packet || raw_len == 0)
    {
        return false;
    }

    auto send_packet = [&](const uint8_t* data, size_t len) -> bool
    {
        return wifi_only ? interfaces_.sendPacketWifiOnly(data, len)
                         : interfaces_.sendPacket(data, len);
    };

    if (!allow_transport)
    {
        return send_packet(raw_packet, raw_len);
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(raw_packet, raw_len, &parsed) || !parsed.destination_hash)
    {
        return send_packet(raw_packet, raw_len);
    }

    if (parsed.packet_type == reticulum::PacketType::Announce ||
        parsed.packet_type == reticulum::PacketType::Proof ||
        parsed.destination_type == reticulum::DestinationType::Plain ||
        parsed.destination_type == reticulum::DestinationType::Group)
    {
        return send_packet(raw_packet, raw_len);
    }

    const PathEntry* path = findPath(parsed.destination_hash);
    if (!path || path->hops <= 1 || path->direct)
    {
        return send_packet(raw_packet, raw_len);
    }

    uint8_t routed_packet[kMaxPacketLen] = {};
    size_t routed_len = sizeof(routed_packet);
    if (!reticulum::buildHeader2Packet(parsed.packet_type,
                                       parsed.destination_type,
                                       static_cast<reticulum::PacketContext>(parsed.context),
                                       parsed.context_flag != 0,
                                       path->next_hop_transport,
                                       parsed.destination_hash,
                                       parsed.payload,
                                       parsed.payload_len,
                                       routed_packet,
                                       &routed_len,
                                       raw_packet[1]))
    {
        return false;
    }

    return send_packet(routed_packet, routed_len);
}

bool LxmfAdapter::sendCachedAnnounceResponse(const PathEntry& path,
                                             reticulum::PacketContext context)
{
    if (path.cached_announce_len == 0)
    {
        return false;
    }

    reticulum::ParsedPacket parsed{};
    if (!reticulum::parsePacket(path.cached_announce, path.cached_announce_len, &parsed) ||
        parsed.packet_type != reticulum::PacketType::Announce)
    {
        return false;
    }

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader2Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       context,
                                       parsed.context_flag != 0,
                                       identity_.identityHash(),
                                       parsed.destination_hash,
                                       parsed.payload,
                                       parsed.payload_len,
                                       packet,
                                       &packet_len,
                                       path.hops))
    {
        return false;
    }

    return interfaces_.sendPacket(packet, packet_len);
}

bool LxmfAdapter::sendCachedPacketReplay(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    if (!packet_hash)
    {
        return false;
    }

    for (const auto& path : transport_.paths)
    {
        if (path.cached_announce_len == 0)
        {
            continue;
        }
        if (hashesEqual(path.cached_packet_hash, packet_hash, reticulum::kFullHashSize))
        {
            return interfaces_.sendPacket(path.cached_announce, path.cached_announce_len);
        }
    }

    return false;
}

bool LxmfAdapter::shouldProcessWifiIngressPacket(const reticulum::ParsedPacket& packet)
{
    if (!packet.destination_hash)
    {
        return false;
    }

    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return true;
    }

    if (packet.transport_id &&
        hashesEqual(packet.transport_id, identity_.identityHash(), reticulum::kTruncatedHashSize))
    {
        return true;
    }

    if (packet.destination_type == reticulum::DestinationType::Group &&
        findConfiguredGroupDestination(packet.destination_hash))
    {
        return true;
    }

    if (packet.destination_type == reticulum::DestinationType::Link &&
        findLinkSession(packet.destination_hash))
    {
        return true;
    }

    if (packet.packet_type == reticulum::PacketType::Proof &&
        findReversePath(packet.destination_hash))
    {
        return true;
    }

    if (packet.packet_type == reticulum::PacketType::Data &&
        packet.destination_type == reticulum::DestinationType::Plain &&
        packet.payload &&
        packet.payload_len > reticulum::kTruncatedHashSize)
    {
        const uint8_t* requested_hash = packet.payload;
        if (isLocalDestinationHash(requested_hash, nullptr) ||
            findConfiguredGroupDestination(requested_hash))
        {
            return true;
        }
        return false;
    }

    if (packet.packet_type == reticulum::PacketType::Announce)
    {
        if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse) &&
            findPendingPathRequest(packet.destination_hash))
        {
            return true;
        }
        return screen_runtime::is_sleeping() &&
               !screen_runtime::is_saver_active();
    }

    return false;
}

bool LxmfAdapter::shouldLogRxDetail(
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface,
    const RuntimeBudget& budget)
{
    if (ingress_interface != reticulum::interfaces::InterfaceKind::WifiGateway)
    {
        if (!isPublicDiscoveryPacket(packet))
        {
            return true;
        }
        const uint32_t now_ms = millis();
        const uint32_t interval_ms =
            budget.allow_persistence
                ? kLoraDiscoverySleepDetailLogIntervalMs
                : kLoraDiscoveryForegroundDetailLogIntervalMs;
        if (last_lora_discovery_detail_log_ms_ != 0 &&
            now_ms - last_lora_discovery_detail_log_ms_ < interval_ms)
        {
            ++suppressed_lora_discovery_detail_logs_;
            return false;
        }
        if (suppressed_lora_discovery_detail_logs_ != 0)
        {
            Serial.printf("[LXMF][RawRX] detail_suppressed iface=lora public_discovery=1 phase=%s suppressed=%u\n",
                          budget.phase ? budget.phase : "-",
                          static_cast<unsigned>(suppressed_lora_discovery_detail_logs_));
            suppressed_lora_discovery_detail_logs_ = 0;
        }
        last_lora_discovery_detail_log_ms_ = now_ms;
        return true;
    }
    if (!packet.destination_hash)
    {
        return false;
    }
    if (isLocalDestinationHash(packet.destination_hash, nullptr))
    {
        return true;
    }
    if (packet.destination_type == reticulum::DestinationType::Group &&
        findConfiguredGroupDestination(packet.destination_hash))
    {
        return true;
    }
    return packet.destination_type == reticulum::DestinationType::Link;
}

bool LxmfAdapter::consumeDiscoveryBudget(
    reticulum::interfaces::InterfaceKind ingress_interface)
{
    uint32_t& last_sample_ms =
        ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway
            ? last_wifi_discovery_sample_ms_
            : last_lora_discovery_sample_ms_;
    const uint32_t now_ms = millis();
    if (last_sample_ms != 0 &&
        (now_ms - last_sample_ms) < kDiscoverySampleIntervalMs)
    {
        return false;
    }
    last_sample_ms = now_ms;
    return true;
}

bool LxmfAdapter::isForegroundDiscoveryDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    if (!destination_hash)
    {
        return false;
    }

    for (const auto& request : pending_nomad_page_requests_)
    {
        if (hashesEqual(request.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize))
        {
            return true;
        }
    }

    for (const auto& session : links_.sessions)
    {
        if (session.state == LinkState::Closed ||
            (session.destination != LocalDestinationKind::CallAudio &&
             session.destination != LocalDestinationKind::NomadPage))
        {
            continue;
        }
        if (hashesEqual(session.remote_destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize))
        {
            return true;
        }
    }

    return false;
}

void LxmfAdapter::noteRxSummary(bool wifi_skipped,
                                bool duplicate,
                                bool parse_failed,
                                bool deferred,
                                bool deferred_dropped,
                                bool throttled_discovery)
{
    if (!wifi_skipped && !duplicate && !parse_failed && !deferred &&
        !deferred_dropped && !throttled_discovery)
    {
        ++rx_summary_packets_;
    }
    if (wifi_skipped)
    {
        ++rx_summary_wifi_skipped_;
    }
    if (duplicate)
    {
        ++rx_summary_duplicates_;
    }
    if (parse_failed)
    {
        ++rx_summary_parse_failed_;
    }
    if (deferred)
    {
        ++rx_summary_deferred_;
    }
    if (deferred_dropped)
    {
        ++rx_summary_deferred_dropped_;
    }
    if (throttled_discovery)
    {
        ++rx_summary_throttled_discovery_;
    }

    const uint32_t now_ms = millis();
    if (last_rx_summary_ms_ == 0)
    {
        last_rx_summary_ms_ = now_ms;
        return;
    }
    if ((now_ms - last_rx_summary_ms_) < kRxSummaryIntervalMs)
    {
        return;
    }
    if (rx_summary_packets_ == 0 &&
        rx_summary_wifi_skipped_ == 0 &&
        rx_summary_duplicates_ == 0 &&
        rx_summary_parse_failed_ == 0 &&
        rx_summary_deferred_ == 0 &&
        rx_summary_deferred_dropped_ == 0 &&
        rx_summary_throttled_discovery_ == 0)
    {
        last_rx_summary_ms_ = now_ms;
        return;
    }

    Serial.printf("[LXMF][RawRX] stats packets=%u wifi_skipped=%u duplicate=%u parse_failed=%u deferred=%u deferred_drop=%u throttled_discovery=%u\n",
                  static_cast<unsigned>(rx_summary_packets_),
                  static_cast<unsigned>(rx_summary_wifi_skipped_),
                  static_cast<unsigned>(rx_summary_duplicates_),
                  static_cast<unsigned>(rx_summary_parse_failed_),
                  static_cast<unsigned>(rx_summary_deferred_),
                  static_cast<unsigned>(rx_summary_deferred_dropped_),
                  static_cast<unsigned>(rx_summary_throttled_discovery_));
    rx_summary_packets_ = 0;
    rx_summary_wifi_skipped_ = 0;
    rx_summary_duplicates_ = 0;
    rx_summary_parse_failed_ = 0;
    rx_summary_deferred_ = 0;
    rx_summary_deferred_dropped_ = 0;
    rx_summary_throttled_discovery_ = 0;
    last_rx_summary_ms_ = now_ms;
}

bool LxmfAdapter::shouldRebroadcastAnnounce(
    const reticulum::ParsedPacket& packet,
    reticulum::interfaces::InterfaceKind ingress_interface) const
{
    if (ingress_interface == reticulum::interfaces::InterfaceKind::WifiGateway)
    {
        return false;
    }
    if (!packet.destination_hash)
    {
        return false;
    }
    if (packet.context == static_cast<uint8_t>(reticulum::PacketContext::PathResponse))
    {
        return false;
    }
    if (packet.hops >= kMaxTransportHops)
    {
        return false;
    }
    if (hashesEqual(packet.destination_hash, identity_.destinationHash(), reticulum::kTruncatedHashSize))
    {
        return false;
    }
    return true;
}

bool LxmfAdapter::rebroadcastAnnounce(const PathEntry& path, const reticulum::ParsedPacket& packet)
{
    if (!packet.destination_hash || path.cached_announce_len == 0)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if (last_announce_rebroadcast_ms_ != 0 &&
        (now_ms - last_announce_rebroadcast_ms_) < kAnnounceRebroadcastIntervalMs)
    {
        return false;
    }
    last_announce_rebroadcast_ms_ = now_ms;

    uint8_t rebroadcast[kMaxPacketLen] = {};
    size_t rebroadcast_len = sizeof(rebroadcast);
    if (!reticulum::buildHeader2Packet(reticulum::PacketType::Announce,
                                       reticulum::DestinationType::Single,
                                       reticulum::PacketContext::None,
                                       packet.context_flag != 0,
                                       identity_.identityHash(),
                                       packet.destination_hash,
                                       packet.payload,
                                       packet.payload_len,
                                       rebroadcast,
                                       &rebroadcast_len,
                                       packet.hops))
    {
        return false;
    }

    return interfaces_.sendPacket(rebroadcast, rebroadcast_len);
}

bool LxmfAdapter::isDuplicatePacket(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    return runtime::isDuplicatePacket(transport_, packet_hash);
}

void LxmfAdapter::rememberPacket(const uint8_t packet_hash[reticulum::kFullHashSize])
{
    runtime::rememberPacket(transport_, packet_hash, millis(), kMaxPacketFilter);
}

void LxmfAdapter::rememberReversePath(const uint8_t proof_hash[reticulum::kTruncatedHashSize],
                                      uint8_t expected_hops)
{
    runtime::rememberReversePath(transport_, proof_hash, expected_hops, millis(), kMaxReverseEntries);
}

LxmfAdapter::ReverseEntry* LxmfAdapter::findReversePath(
    const uint8_t proof_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findReversePath(transport_, proof_hash);
}

LxmfAdapter::PendingPathRequest* LxmfAdapter::findPendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    return runtime::findPendingPathRequest(transport_, destination_hash);
}

const LxmfAdapter::PendingPathRequest* LxmfAdapter::findPendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    return runtime::findPendingPathRequest(transport_, destination_hash);
}

void LxmfAdapter::notePendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    uint32_t now_ms)
{
    runtime::notePendingPathRequest(transport_, destination_hash, now_ms, kMaxPendingPathRequests);
}

void LxmfAdapter::resolvePendingPathRequest(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    runtime::resolvePendingPathRequest(transport_, destination_hash);
}

void LxmfAdapter::cullTransportState()
{
    const runtime::TransportRuntimeLimits limits{
        kMaxPaths,
        kMaxPacketFilter,
        kMaxReverseEntries,
        kMaxLinkRelays,
        kMaxPendingPathRequests,
        kPacketFilterTtlMs,
        kPendingPathRequestTtlMs,
        kReverseEntryTtlMs,
        kLinkRelayTtlMs};
    runtime::cullTransportRuntime(transport_, millis(), limits);
    cullLinkSessions();
}

LxmfAdapter::PathEntry& LxmfAdapter::upsertPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    return runtime::upsertPath(transport_, destination_hash, kMaxPaths);
}

const LxmfAdapter::PathEntry* LxmfAdapter::findPath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize]) const
{
    return runtime::findPath(transport_, destination_hash);
}

LxmfAdapter::LinkRelayEntry& LxmfAdapter::upsertLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::upsertLinkRelay(transport_, link_id, kMaxLinkRelays);
}

LxmfAdapter::LinkRelayEntry* LxmfAdapter::findLinkRelay(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::findLinkRelay(transport_, link_id);
}

void LxmfAdapter::localDestinationHash(LocalDestinationKind kind,
                                       uint8_t out_hash[reticulum::kTruncatedHashSize]) const
{
    if (!out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    if (kind == LocalDestinationKind::Propagation)
    {
        reticulum::computeNameHash("lxmf", "propagation", name_hash);
    }
    else if (kind == LocalDestinationKind::CallAudio)
    {
        reticulum::computeNameHash("call", "audio", name_hash);
    }
    else
    {
        reticulum::computeNameHash("lxmf", "delivery", name_hash);
    }
    reticulum::computeDestinationHash(name_hash, identity_.identityHash(), out_hash);
}

bool LxmfAdapter::isLocalDestinationHash(const uint8_t hash[reticulum::kTruncatedHashSize],
                                         LocalDestinationKind* out_kind) const
{
    if (!hash)
    {
        return false;
    }

    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::Delivery, delivery_hash);
    if (hashesEqual(hash, delivery_hash, sizeof(delivery_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::Delivery;
        }
        return true;
    }

    uint8_t propagation_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::Propagation, propagation_hash);
    if (hashesEqual(hash, propagation_hash, sizeof(propagation_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::Propagation;
        }
        return true;
    }

    uint8_t call_audio_hash[reticulum::kTruncatedHashSize] = {};
    localDestinationHash(LocalDestinationKind::CallAudio, call_audio_hash);
    if (hashesEqual(hash, call_audio_hash, sizeof(call_audio_hash)))
    {
        if (out_kind)
        {
            *out_kind = LocalDestinationKind::CallAudio;
        }
        return true;
    }

    return false;
}

uint16_t LxmfAdapter::linkMduForMtu(uint16_t mtu)
{
    const size_t encrypted_payload_budget =
        (mtu > reticulum::kPacketHeader1Size) ? (mtu - reticulum::kPacketHeader1Size) : 0;
    if (encrypted_payload_budget <= reticulum::kTokenOverhead)
    {
        return 0;
    }

    const size_t token_budget = encrypted_payload_budget - reticulum::kTokenOverhead;
    const size_t padded = (token_budget / 16U) * 16U;
    if (padded == 0)
    {
        return 0;
    }
    return static_cast<uint16_t>(padded - 1U);
}

bool LxmfAdapter::generateLinkSigningKey(uint8_t out_pub[LxmfIdentity::kSigPubKeySize],
                                         uint8_t out_priv[LxmfIdentity::kSigPrivKeySize])
{
    if (!out_pub || !out_priv)
    {
        return false;
    }

    uint8_t seed[32] = {};
    fillRandomBytes(seed, sizeof(seed));
    ed25519_create_keypair(out_pub, out_priv, seed);
    memset(seed, 0, sizeof(seed));
    return !isZeroBytes(out_pub, LxmfIdentity::kSigPubKeySize) &&
           !isZeroBytes(out_priv, LxmfIdentity::kSigPrivKeySize);
}

bool LxmfAdapter::signWithKey(const uint8_t sign_pub[LxmfIdentity::kSigPubKeySize],
                              const uint8_t sign_priv[LxmfIdentity::kSigPrivKeySize],
                              const uint8_t* message,
                              size_t message_len,
                              uint8_t out_signature[reticulum::kSignatureSize])
{
    if (!sign_pub || !sign_priv || !message || !out_signature)
    {
        return false;
    }

    ed25519_sign(out_signature, message, message_len, sign_pub, sign_priv);
    return true;
}

bool LxmfAdapter::deriveLinkKey(LinkSession& session)
{
    if (isZeroBytes(session.peer_enc_pub, sizeof(session.peer_enc_pub)) ||
        isZeroBytes(session.local_enc_priv, sizeof(session.local_enc_priv)))
    {
        return false;
    }

    uint8_t shared_secret[LxmfIdentity::kEncPubKeySize] = {};
    memcpy(shared_secret, session.peer_enc_pub, sizeof(shared_secret));
    uint8_t local_priv[LxmfIdentity::kEncPrivKeySize] = {};
    memcpy(local_priv, session.local_enc_priv, sizeof(local_priv));
    if (!Curve25519::dh2(shared_secret, local_priv))
    {
        return false;
    }

    return reticulum::hkdfSha256(shared_secret, sizeof(shared_secret),
                                 session.link_id, sizeof(session.link_id),
                                 nullptr, 0,
                                 session.derived_key, sizeof(session.derived_key));
}

bool LxmfAdapter::encryptLinkPayload(const LinkSession& session,
                                     const uint8_t* plaintext, size_t plaintext_len,
                                     uint8_t* out_payload, size_t* inout_len) const
{
    if (!plaintext || plaintext_len == 0 || !out_payload || !inout_len)
    {
        return false;
    }

    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));
    return reticulum::tokenEncrypt(session.derived_key,
                                   iv,
                                   plaintext,
                                   plaintext_len,
                                   out_payload,
                                   inout_len);
}

bool LxmfAdapter::decryptLinkPayload(const LinkSession& session,
                                     const uint8_t* payload, size_t payload_len,
                                     std::vector<uint8_t>* out_plaintext) const
{
    if (!payload || payload_len == 0 || !out_plaintext)
    {
        return false;
    }

    std::vector<uint8_t> plaintext(reticulum::paddedTokenPlaintextSize(payload_len), 0);
    size_t plaintext_len = plaintext.size();
    if (!reticulum::tokenDecrypt(session.derived_key,
                                 payload,
                                 payload_len,
                                 plaintext.data(),
                                 &plaintext_len))
    {
        return false;
    }

    plaintext.resize(plaintext_len);
    *out_plaintext = std::move(plaintext);
    return true;
}

bool LxmfAdapter::sendLinkPacket(LinkSession& session,
                                 reticulum::PacketType packet_type,
                                 reticulum::PacketContext context,
                                 const uint8_t* payload, size_t payload_len,
                                 bool encrypt_payload)
{
    if (!isReady() || (!payload && payload_len != 0))
    {
        return false;
    }

    uint8_t wire_payload[kMaxPacketLen] = {};
    const uint8_t* effective_payload = payload;
    size_t effective_payload_len = payload_len;
    if (encrypt_payload)
    {
        effective_payload = wire_payload;
        effective_payload_len = sizeof(wire_payload);
        if (!encryptLinkPayload(session, payload, payload_len, wire_payload, &effective_payload_len))
        {
            return false;
        }
    }

    uint8_t packet[kMaxPacketLen] = {};
    size_t packet_len = sizeof(packet);
    if (!reticulum::buildHeader1Packet(packet_type,
                                       reticulum::DestinationType::Link,
                                       context,
                                       false,
                                       session.link_id,
                                       effective_payload,
                                       effective_payload_len,
                                       packet,
                                       &packet_len))
    {
        return false;
    }

    const bool ok = session.destination == LocalDestinationKind::CallAudio
                        ? interfaces_.sendPacketWifiOnly(packet,
                                                         packet_len,
                                                         session.link_id)
                        : interfaces_.sendPacket(packet, packet_len);
    if (ok)
    {
        session.last_outbound_ms = millis();
    }
    return ok;
}

bool LxmfAdapter::sendNomadPageRequestPacket(
    LinkSession& session,
    PendingNomadPageRequest& request)
{
    if (!isReady() || session.state != LinkState::Active || !request.path[0])
    {
        return false;
    }

    uint8_t path_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::truncatedHash(reinterpret_cast<const uint8_t*>(request.path),
                             std::strlen(request.path),
                             path_hash);

    std::memset(nomad_page_request_payload_scratch_,
                0,
                sizeof(nomad_page_request_payload_scratch_));
    size_t request_payload_len = sizeof(nomad_page_request_payload_scratch_);
    if (!encodeLinkRequestPayload(static_cast<double>(currentTimestampSeconds()),
                                  path_hash,
                                  nullptr,
                                  0,
                                  true,
                                  nomad_page_request_payload_scratch_,
                                  &request_payload_len) ||
        request_payload_len > session.mdu)
    {
        return false;
    }

    std::memset(nomad_page_wire_payload_scratch_,
                0,
                sizeof(nomad_page_wire_payload_scratch_));
    size_t wire_payload_len = sizeof(nomad_page_wire_payload_scratch_);
    if (!encryptLinkPayload(session,
                            nomad_page_request_payload_scratch_,
                            request_payload_len,
                            nomad_page_wire_payload_scratch_,
                            &wire_payload_len))
    {
        return false;
    }

    std::memset(nomad_page_packet_scratch_,
                0,
                sizeof(nomad_page_packet_scratch_));
    size_t packet_len = sizeof(nomad_page_packet_scratch_);
    if (!reticulum::buildHeader1Packet(reticulum::PacketType::Data,
                                       reticulum::DestinationType::Link,
                                       reticulum::PacketContext::Request,
                                       false,
                                       session.link_id,
                                       nomad_page_wire_payload_scratch_,
                                       wire_payload_len,
                                       nomad_page_packet_scratch_,
                                       &packet_len))
    {
        return false;
    }

    reticulum::computeTruncatedPacketHash(nomad_page_packet_scratch_,
                                          packet_len,
                                          request.request_id);
    const bool ok = interfaces_.sendPacket(nomad_page_packet_scratch_, packet_len);
    if (!ok)
    {
        return false;
    }

    LinkPendingRequest pending{};
    pending.request_id.assign(request.request_id,
                              request.request_id + sizeof(request.request_id));
    pending.created_ms = millis();
    session.pending_requests.push_back(std::move(pending));
    session.last_outbound_ms = millis();
    request.request_sent = true;
    return true;
}

LxmfAdapter::PendingNomadPageRequest*
LxmfAdapter::findPendingNomadPageRequestById(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t* request_id,
    std::size_t request_id_len)
{
    if (!destination_hash || !request_id ||
        request_id_len != reticulum::kTruncatedHashSize)
    {
        return nullptr;
    }

    for (auto& request : pending_nomad_page_requests_)
    {
        if (hashesEqual(request.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::memcmp(request.request_id,
                        request_id,
                        reticulum::kTruncatedHashSize) == 0)
        {
            return &request;
        }
    }
    return nullptr;
}

void LxmfAdapter::updateNomadPageProgress(
    const PendingNomadPageRequest& request,
    int progress_percent,
    const char* message,
    const char* detail,
    bool active,
    bool complete,
    PageFailureKind failure)
{
    char destination_text[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(request.destination_hash,
                  reticulum::kTruncatedHashSize,
                  destination_text,
                  sizeof(destination_text));
    rtpage::update_request_progress(destination_text,
                                    request.path,
                                    progress_percent,
                                    message,
                                    detail,
                                    active,
                                    complete,
                                    failure);
}

void LxmfAdapter::updateNomadPageProgressForDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    int progress_percent,
    const char* message,
    const char* detail,
    bool active,
    bool complete,
    PageFailureKind failure)
{
    if (!destination_hash)
    {
        return;
    }

    for (const auto& request : pending_nomad_page_requests_)
    {
        if (hashesEqual(request.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize))
        {
            updateNomadPageProgress(request,
                                    progress_percent,
                                    message,
                                    detail,
                                    active,
                                    complete,
                                    failure);
        }
    }
}

void LxmfAdapter::completeNomadPageRequest(
    PendingNomadPageRequest& request,
    const std::vector<uint8_t>& packed_response)
{
    char destination_text[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
    formatHashHex(request.destination_hash,
                  reticulum::kTruncatedHashSize,
                  destination_text,
                  sizeof(destination_text));

    std::vector<uint8_t> page_body;
    if (!decodeMsgpackByteString(packed_response, &page_body))
    {
        updateNomadPageProgress(request,
                                100,
                                "Nomad page response decode failed",
                                request.path,
                                false,
                                false,
                                PageFailureKind::Terminal);
        LXMF_NOMAD_PAGE_LOG("response decode failed dest=%s path=%s packed=%u\n",
                            destination_text,
                            request.path,
                            static_cast<unsigned>(packed_response.size()));
        return;
    }

    const char* body =
        page_body.empty() ? "" : reinterpret_cast<const char*>(page_body.data());
    updateNomadPageProgress(request,
                            95,
                            "Caching Nomad page",
                            request.path,
                            true,
                            false,
                            PageFailureKind::None);
    const rtpage::Status status =
        rtpage::store_cached_page_now(destination_text,
                                      request.path,
                                      body,
                                      page_body.size());
    updateNomadPageProgress(request,
                            status.saved ? 100 : 95,
                            status.saved ? "Nomad page loaded"
                                         : "Nomad page cache write failed",
                            status.detail,
                            false,
                            status.saved,
                            status.saved ? PageFailureKind::None
                                         : PageFailureKind::Terminal);
    LXMF_NOMAD_PAGE_LOG("response store dest=%s path=%s body=%u saved=%u sd=%u file=%u msg=\"%s\" detail=\"%s\"\n",
                        destination_text,
                        request.path,
                        static_cast<unsigned>(page_body.size()),
                        status.saved ? 1U : 0U,
                        status.sd_present ? 1U : 0U,
                        status.file_present ? 1U : 0U,
                        status.message,
                        status.detail);
}

void LxmfAdapter::pumpNomadPageRequests()
{
    const uint32_t now_ms = millis();
    for (std::size_t index = 0; index < pending_nomad_page_requests_.size();)
    {
        PendingNomadPageRequest& request = pending_nomad_page_requests_[index];

        if (request.created_ms != 0 &&
            (now_ms - request.created_ms) > kNomadPageRequestTtlMs)
        {
            LinkSession* open_link =
                runtime::findOpenLinkSessionByDestination(links_,
                                                          request.destination_hash,
                                                          LocalDestinationKind::NomadPage);
            char timeout_destination[(reticulum::kTruncatedHashSize * 2U) + 1U] = {};
            char timeout_link[12] = {};
            formatHashHex(request.destination_hash,
                          reticulum::kTruncatedHashSize,
                          timeout_destination,
                          sizeof(timeout_destination));
            formatHashPrefix(open_link ? open_link->link_id : nullptr,
                             timeout_link,
                             sizeof(timeout_link));
            Serial.printf("[LXMF][NomadPage] timeout dest=%s path=%s sent=%u link_started=%u open_link=%u state=%s link=%s expected_hops=%u last_attempt_age_ms=%lu\n",
                          timeout_destination,
                          request.path,
                          request.request_sent ? 1U : 0U,
                          request.link_started ? 1U : 0U,
                          open_link ? 1U : 0U,
                          open_link ? linkStateLabel(open_link->state) : "-",
                          timeout_link,
                          static_cast<unsigned>(open_link ? open_link->expected_hops : 0U),
                          static_cast<unsigned long>(
                              request.last_attempt_ms != 0
                                  ? (now_ms - request.last_attempt_ms)
                                  : 0U));
            updateNomadPageProgress(request,
                                    100,
                                    "Nomad page request timed out",
                                    request.path,
                                    false,
                                    false,
                                    PageFailureKind::Terminal);
            if (kNomadPageTraceEnabled)
            {
                char destination_text[(reticulum::kTruncatedHashSize * 2U) +
                                      1U] = {};
                formatHashHex(request.destination_hash,
                              reticulum::kTruncatedHashSize,
                              destination_text,
                              sizeof(destination_text));
                LXMF_NOMAD_PAGE_LOG("timeout dest=%s path=%s sent=%u link=%u\n",
                                    destination_text,
                                    request.path,
                                    request.request_sent ? 1U : 0U,
                                    request.link_started ? 1U : 0U);
            }
            pending_nomad_page_requests_.erase(
                pending_nomad_page_requests_.begin() +
                static_cast<std::ptrdiff_t>(index));
            continue;
        }

        bool completed = false;
        if (request.request_sent)
        {
            if (LinkSession* session =
                    findActiveLinkSessionByDestination(request.destination_hash,
                                                       LocalDestinationKind::NomadPage))
            {
                for (auto pending = session->pending_requests.begin();
                     pending != session->pending_requests.end();
                     ++pending)
                {
                    if (pending->request_id.size() == sizeof(request.request_id) &&
                        std::memcmp(pending->request_id.data(),
                                    request.request_id,
                                    sizeof(request.request_id)) == 0 &&
                        pending->response_ready)
                    {
                        completeNomadPageRequest(request, pending->response);
                        session->pending_requests.erase(pending);
                        completed = true;
                        break;
                    }
                }
            }
        }

        if (completed)
        {
            pending_nomad_page_requests_.erase(
                pending_nomad_page_requests_.begin() +
                static_cast<std::ptrdiff_t>(index));
            continue;
        }

        LinkSession* active_link =
            findActiveLinkSessionByDestination(request.destination_hash,
                                               LocalDestinationKind::NomadPage);
        if (active_link && !request.request_sent &&
            (request.last_attempt_ms == 0 ||
             (now_ms - request.last_attempt_ms) >= kNomadPageSendRetryMs))
        {
            request.last_attempt_ms = now_ms;
            const bool sent = sendNomadPageRequestPacket(*active_link, request);
            updateNomadPageProgress(request,
                                    sent ? 40 : 35,
                                    sent ? "Nomad page request sent"
                                         : "Nomad page request TX failed",
                                    request.path,
                                    sent,
                                    false,
                                    sent ? PageFailureKind::None
                                         : PageFailureKind::Retryable);
            if (kNomadPageTraceEnabled)
            {
                char destination_text[(reticulum::kTruncatedHashSize * 2U) +
                                      1U] = {};
                formatHashHex(request.destination_hash,
                              reticulum::kTruncatedHashSize,
                              destination_text,
                              sizeof(destination_text));
                LXMF_NOMAD_PAGE_LOG("request tx dest=%s path=%s ok=%u pending_link_requests=%u\n",
                                    destination_text,
                                    request.path,
                                    sent ? 1U : 0U,
                                    static_cast<unsigned>(active_link->pending_requests.size()));
            }
        }
        else if (!active_link)
        {
            const PathEntry* path = findPath(request.destination_hash);
            if (!path)
            {
                if (request.last_path_request_ms == 0 ||
                    (now_ms - request.last_path_request_ms) >= kPathRequestMinIntervalMs)
                {
                    request.last_path_request_ms = now_ms;
                    const bool path_sent =
                        sendPathRequestForDestination(request.destination_hash);
                    request.path_requested = request.path_requested || path_sent;
                    updateNomadPageProgress(request,
                                            path_sent ? 10 : 5,
                                            path_sent ? "Resolving Nomad page path"
                                                      : "Nomad page path request failed",
                                            request.path,
                                            true,
                                            false,
                                            path_sent ? PageFailureKind::None
                                                      : PageFailureKind::Retryable);
                    if (kNomadPageTraceEnabled)
                    {
                        char destination_text[(reticulum::kTruncatedHashSize *
                                               2U) +
                                              1U] = {};
                        formatHashHex(request.destination_hash,
                                      reticulum::kTruncatedHashSize,
                                      destination_text,
                                      sizeof(destination_text));
                        LXMF_NOMAD_PAGE_LOG("path request dest=%s path=%s ok=%u\n",
                                            destination_text,
                                            request.path,
                                            path_sent ? 1U : 0U);
                    }
                }
            }
            else
            {
                LinkSession* open_link =
                    runtime::findOpenLinkSessionByDestination(
                        links_,
                        request.destination_hash,
                        LocalDestinationKind::NomadPage);
                if (open_link &&
                    (open_link->state == LinkState::Pending ||
                     open_link->state == LinkState::Handshake) &&
                    (request.last_attempt_ms == 0 ||
                     (now_ms - request.last_attempt_ms) >=
                         kNomadPageLinkRetryMs))
                {
                    request.last_attempt_ms = now_ms;
                    const bool link_sent = sendLinkRequest(*open_link);
                    request.link_started = request.link_started || link_sent;
                    updateNomadPageProgress(request,
                                            link_sent ? 25 : 10,
                                            link_sent
                                                ? "Retrying Nomad page link"
                                                : "Nomad page link retry failed",
                                            request.path,
                                            link_sent,
                                            false,
                                            link_sent ? PageFailureKind::None
                                                      : PageFailureKind::Retryable);
                    char destination_text[(reticulum::kTruncatedHashSize * 2U) +
                                          1U] = {};
                    char link_hash[12] = {};
                    formatHashHex(request.destination_hash,
                                  reticulum::kTruncatedHashSize,
                                  destination_text,
                                  sizeof(destination_text));
                    formatHashPrefix(open_link->link_id,
                                     link_hash,
                                     sizeof(link_hash));
                    Serial.printf("[LXMF][NomadPage] link_retry dest=%s path=%s link=%s state=%s ok=%u age_ms=%lu\n",
                                  destination_text,
                                  request.path,
                                  link_hash,
                                  linkStateLabel(open_link->state),
                                  link_sent ? 1U : 0U,
                                  static_cast<unsigned long>(
                                      now_ms - open_link->created_ms));
                }
                else if (!open_link &&
                         (request.last_attempt_ms == 0 ||
                          (now_ms - request.last_attempt_ms) >=
                              kNomadPageSendRetryMs))
                {
                    request.last_attempt_ms = now_ms;
                    LinkSession& session =
                        runtime::appendLinkSession(links_, kMaxLinkSessions);
                    session.created_ms = now_ms;
                    session.request_ms = now_ms;
                    session.last_inbound_ms = now_ms;
                    session.initiator = true;
                    session.destination = LocalDestinationKind::NomadPage;
                    session.state = LinkState::Pending;
                    session.close_reason = LinkCloseReason::None;
                    session.expected_hops = path->hops;
                    session.remote_identity_known = false;
                    session.validated = false;
                    session.keepalive_interval_ms = kLinkKeepaliveMaxMs;
                    session.stale_timeout_ms = kLinkKeepaliveMaxMs * 2U;
                    copyHash(session.remote_destination_hash,
                             request.destination_hash,
                             sizeof(session.remote_destination_hash));

                    Curve25519::dh1(session.local_enc_pub,
                                    session.local_enc_priv);
                    const bool link_sent =
                        !isZeroBytes(session.local_enc_priv,
                                     sizeof(session.local_enc_priv)) &&
                        generateLinkSigningKey(session.local_sig_pub,
                                               session.local_sig_priv) &&
                        sendLinkRequest(session);
                    if (!link_sent)
                    {
                        links_.sessions.pop_back();
                    }
                    request.link_started = link_sent;
                    updateNomadPageProgress(request,
                                            link_sent ? 25 : 10,
                                            link_sent ? "Opening Nomad page link"
                                                      : "Nomad page link start failed",
                                            request.path,
                                            link_sent,
                                            false,
                                            link_sent ? PageFailureKind::None
                                                      : PageFailureKind::Retryable);
                    if (kNomadPageTraceEnabled)
                    {
                        char destination_text[(reticulum::kTruncatedHashSize *
                                               2U) +
                                              1U] = {};
                        formatHashHex(request.destination_hash,
                                      reticulum::kTruncatedHashSize,
                                      destination_text,
                                      sizeof(destination_text));
                        LXMF_NOMAD_PAGE_LOG("link start dest=%s path=%s ok=%u hops=%u\n",
                                            destination_text,
                                            request.path,
                                            link_sent ? 1U : 0U,
                                            static_cast<unsigned>(path->hops));
                    }
                }
            }
        }

        ++index;
    }
}

bool LxmfAdapter::sendLinkHandshakeProof(LinkSession& session)
{
    uint8_t signalling[kLinkSignallingLen] = {};
    buildLinkSignallingBytes(session.mtu, signalling);

    uint8_t signed_data[reticulum::kTruncatedHashSize +
                        LxmfIdentity::kEncPubKeySize +
                        LxmfIdentity::kSigPubKeySize +
                        kLinkSignallingLen] = {};
    size_t used = 0;
    memcpy(signed_data + used, session.link_id, reticulum::kTruncatedHashSize);
    used += reticulum::kTruncatedHashSize;
    memcpy(signed_data + used, session.local_enc_pub, LxmfIdentity::kEncPubKeySize);
    used += LxmfIdentity::kEncPubKeySize;
    memcpy(signed_data + used, identity_.signingPublicKey(), LxmfIdentity::kSigPubKeySize);
    used += LxmfIdentity::kSigPubKeySize;
    memcpy(signed_data + used, signalling, sizeof(signalling));
    used += sizeof(signalling);

    uint8_t proof_payload[reticulum::kSignatureSize +
                          LxmfIdentity::kEncPubKeySize +
                          kLinkSignallingLen] = {};
    if (!identity_.sign(signed_data, used, proof_payload))
    {
        return false;
    }
    memcpy(proof_payload + reticulum::kSignatureSize,
           session.local_enc_pub,
           LxmfIdentity::kEncPubKeySize);
    memcpy(proof_payload + reticulum::kSignatureSize + LxmfIdentity::kEncPubKeySize,
           signalling,
           sizeof(signalling));

    return sendLinkPacket(session,
                          reticulum::PacketType::Proof,
                          reticulum::PacketContext::LrProof,
                          proof_payload,
                          sizeof(proof_payload),
                          false);
}

bool LxmfAdapter::sendLinkRtt(LinkSession& session)
{
    uint8_t payload[16] = {};
    size_t payload_len = sizeof(payload);
    if (!packFloat64(static_cast<double>(session.rtt_s), payload, &payload_len))
    {
        return false;
    }
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::LrRtt,
                          payload,
                          payload_len,
                          true);
}

bool LxmfAdapter::sendLinkKeepalive(LinkSession& session)
{
    const uint8_t probe = 0xFF;
    const bool ok = sendLinkPacket(session,
                                   reticulum::PacketType::Data,
                                   reticulum::PacketContext::Keepalive,
                                   &probe,
                                   sizeof(probe),
                                   false);
    if (ok)
    {
        session.last_keepalive_ms = millis();
    }
    return ok;
}

bool LxmfAdapter::sendLinkKeepaliveAck(LinkSession& session)
{
    const uint8_t ack = 0xFE;
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::Keepalive,
                          &ack,
                          sizeof(ack),
                          false);
}

bool LxmfAdapter::sendLinkIdentify(LinkSession& session)
{
    if (!identity_.isReady() || session.state != LinkState::Active)
    {
        return false;
    }

    uint8_t combined_pub[reticulum::kCombinedPublicKeySize] = {};
    identity_.combinedPublicKey(combined_pub);

    std::array<uint8_t, reticulum::kTruncatedHashSize + reticulum::kCombinedPublicKeySize>
        signed_data{};
    memcpy(signed_data.data(), session.link_id, reticulum::kTruncatedHashSize);
    memcpy(signed_data.data() + reticulum::kTruncatedHashSize,
           combined_pub,
           reticulum::kCombinedPublicKeySize);

    uint8_t payload[reticulum::kCombinedPublicKeySize + reticulum::kSignatureSize] = {};
    memcpy(payload, combined_pub, sizeof(combined_pub));
    if (!identity_.sign(signed_data.data(),
                        signed_data.size(),
                        payload + reticulum::kCombinedPublicKeySize))
    {
        return false;
    }

    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::LinkIdentify,
                          payload,
                          sizeof(payload),
                          true);
}

bool LxmfAdapter::sendLinkPacketProof(LinkSession& session,
                                      const uint8_t* raw_packet, size_t raw_len)
{
    if (!raw_packet || raw_len == 0)
    {
        return false;
    }

    uint8_t packet_hash[reticulum::kFullHashSize] = {};
    reticulum::computePacketHash(raw_packet, raw_len, packet_hash);

    uint8_t proof_payload[reticulum::kFullHashSize + reticulum::kSignatureSize] = {};
    memcpy(proof_payload, packet_hash, sizeof(packet_hash));
    bool signed_ok = false;
    if (session.initiator)
    {
        signed_ok = signWithKey(session.local_sig_pub,
                                session.local_sig_priv,
                                packet_hash,
                                sizeof(packet_hash),
                                proof_payload + sizeof(packet_hash));
    }
    else
    {
        signed_ok = identity_.sign(packet_hash,
                                   sizeof(packet_hash),
                                   proof_payload + sizeof(packet_hash));
    }
    if (!signed_ok)
    {
        return false;
    }

    return sendLinkPacket(session,
                          reticulum::PacketType::Proof,
                          reticulum::PacketContext::None,
                          proof_payload,
                          sizeof(proof_payload),
                          false);
}

bool LxmfAdapter::sendLinkResponse(LinkSession& session,
                                   const uint8_t* request_id,
                                   size_t request_id_len,
                                   const uint8_t* packed_data,
                                   size_t packed_data_len,
                                   bool data_is_nil)
{
    const size_t response_capacity = request_id_len + packed_data_len + 32;
    std::vector<uint8_t> response_payload(response_capacity, 0);
    size_t response_len = response_payload.size();
    if (!encodeLinkResponsePayload(request_id,
                                   request_id_len,
                                   packed_data,
                                   packed_data_len,
                                   data_is_nil,
                                   response_payload.data(),
                                   &response_len))
    {
        return false;
    }

    response_payload.resize(response_len);
    if (response_len <= session.mdu)
    {
        return sendLinkPacket(session,
                              reticulum::PacketType::Data,
                              reticulum::PacketContext::Response,
                              response_payload.data(),
                              response_payload.size(),
                              true);
    }

    return queueOutgoingResource(session,
                                 response_payload.data(),
                                 response_payload.size(),
                                 kResourceFlagResponse,
                                 request_id,
                                 request_id_len);
}

bool LxmfAdapter::advertiseLinkResource(LinkSession& session,
                                        LinkResourceTransfer& resource,
                                        uint32_t hashmap_segment)
{
    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t start_hash = static_cast<size_t>(hashmap_segment) * segment_capacity;
    if (segment_capacity == 0 || start_hash >= resource.part_count)
    {
        return false;
    }

    const size_t remaining_hashes = static_cast<size_t>(resource.part_count) - start_hash;
    size_t slice_hashes = std::min(segment_capacity, remaining_hashes);
    while (slice_hashes > 0)
    {
        const size_t slice_offset = start_hash * kResourceMapHashLen;
        const size_t slice_len = slice_hashes * kResourceMapHashLen;

        uint8_t advertisement[kMaxPacketLen] = {};
        size_t advertisement_len = sizeof(advertisement);
        if (encodeResourceAdvertisement(resource.transfer_size,
                                        resource.data_size,
                                        resource.part_count,
                                        resource.resource_hash,
                                        resource.random_hash,
                                        resource.original_hash,
                                        1,
                                        1,
                                        resource.request_id.empty() ? nullptr : resource.request_id.data(),
                                        resource.request_id.size(),
                                        resource.flags,
                                        resource.hashmap.data() + slice_offset,
                                        slice_len,
                                        advertisement,
                                        &advertisement_len) &&
            advertisement_len <= session.mdu)
        {
            resource.last_activity_ms = millis();
            return sendLinkPacket(session,
                                  reticulum::PacketType::Data,
                                  reticulum::PacketContext::ResourceAdv,
                                  advertisement,
                                  advertisement_len,
                                  true);
        }

        --slice_hashes;
    }

    return false;
}

bool LxmfAdapter::queueOutgoingResource(LinkSession& session,
                                        const uint8_t* data, size_t len,
                                        uint8_t flags,
                                        const uint8_t* request_id,
                                        size_t request_id_len)
{
    if (!data || len == 0 || session.mdu == 0 || (request_id_len != 0 && !request_id))
    {
        return false;
    }

    std::vector<uint8_t> stream(kResourceDataPrefixLen + len, 0);
    fillRandomBytes(stream.data(), kResourceDataPrefixLen);
    memcpy(stream.data() + kResourceDataPrefixLen, data, len);

    const size_t encrypted_capacity = reticulum::tokenSizeForPlaintext(stream.size());
    std::vector<uint8_t> encrypted_stream(encrypted_capacity, 0);
    size_t encrypted_len = encrypted_stream.size();
    uint8_t iv[reticulum::kTokenIvSize] = {};
    fillRandomBytes(iv, sizeof(iv));
    if (!reticulum::tokenEncrypt(session.derived_key,
                                 iv,
                                 stream.data(),
                                 stream.size(),
                                 encrypted_stream.data(),
                                 &encrypted_len))
    {
        return false;
    }
    encrypted_stream.resize(encrypted_len);

    const size_t part_count =
        (encrypted_stream.size() + static_cast<size_t>(session.mdu) - 1U) / static_cast<size_t>(session.mdu);
    if (part_count == 0)
    {
        return false;
    }

    const size_t segment_capacity = resourceHashmapSegmentCapacity(session.mdu);
    const size_t collision_guard = (kResourceWindowSize * 2U) + segment_capacity;

    LinkResourceTransfer resource{};
    if (!runtime::initialiseOutgoingResourceTransfer(resource,
                                                     request_id,
                                                     request_id_len,
                                                     static_cast<uint32_t>(len),
                                                     static_cast<uint32_t>(encrypted_stream.size()),
                                                     static_cast<uint32_t>(part_count),
                                                     flags | kResourceFlagEncrypted,
                                                     millis(),
                                                     kResourceWindowSize))
    {
        return false;
    }

    bool mapped = false;
    for (size_t attempt = 0; attempt < 8 && !mapped; ++attempt)
    {
        fillRandomBytes(resource.random_hash, sizeof(resource.random_hash));

        std::vector<uint8_t> hash_material(len + sizeof(resource.random_hash), 0);
        memcpy(hash_material.data(), data, len);
        memcpy(hash_material.data() + len, resource.random_hash, sizeof(resource.random_hash));
        reticulum::fullHash(hash_material.data(),
                            hash_material.size(),
                            resource.resource_hash);
        memcpy(resource.original_hash, resource.resource_hash, sizeof(resource.original_hash));

        std::vector<uint8_t> proof_material(len + reticulum::kFullHashSize, 0);
        memcpy(proof_material.data(), data, len);
        memcpy(proof_material.data() + len,
               resource.resource_hash,
               reticulum::kFullHashSize);
        reticulum::fullHash(proof_material.data(),
                            proof_material.size(),
                            resource.expected_proof);

        resource.hashmap.clear();
        std::vector<std::array<uint8_t, kResourceMapHashLen>> recent_hashes;
        recent_hashes.reserve(collision_guard);
        bool collision = false;

        for (size_t index = 0; index < part_count; ++index)
        {
            const size_t offset = index * static_cast<size_t>(session.mdu);
            const size_t chunk_len =
                std::min(static_cast<size_t>(session.mdu), encrypted_stream.size() - offset);

            resource.parts[index].assign(encrypted_stream.begin() + offset,
                                         encrypted_stream.begin() + offset + chunk_len);

            std::vector<uint8_t> map_material(chunk_len + sizeof(resource.random_hash), 0);
            memcpy(map_material.data(), resource.parts[index].data(), chunk_len);
            memcpy(map_material.data() + chunk_len,
                   resource.random_hash,
                   sizeof(resource.random_hash));

            uint8_t full_hash[reticulum::kFullHashSize] = {};
            reticulum::fullHash(map_material.data(), map_material.size(), full_hash);

            std::array<uint8_t, kResourceMapHashLen> map_hash{};
            memcpy(map_hash.data(), full_hash, map_hash.size());
            if (std::find(recent_hashes.begin(), recent_hashes.end(), map_hash) != recent_hashes.end())
            {
                collision = true;
                break;
            }

            resource.map_hashes[index] = map_hash;
            resource.hashmap.insert(resource.hashmap.end(), map_hash.begin(), map_hash.end());
            recent_hashes.push_back(map_hash);
            if (recent_hashes.size() > collision_guard)
            {
                recent_hashes.erase(recent_hashes.begin());
            }
        }

        mapped = !collision;
    }

    if (!mapped)
    {
        return false;
    }

    session.outgoing_resources.push_back(std::move(resource));
    if (!advertiseLinkResource(session, session.outgoing_resources.back(), 0))
    {
        session.outgoing_resources.pop_back();
        return false;
    }

    return true;
}

void LxmfAdapter::pumpReticulumAudioCall()
{
    ::platform::ui::reticulum_call::set_wifi_ready(
        interfaces_.hasReadyWifiGateway());

    uint8_t hangup_link_id[reticulum::kTruncatedHashSize] = {};
    if (::platform::ui::reticulum_call::consume_hangup_request(hangup_link_id))
    {
        if (LinkSession* session = findLinkSession(hangup_link_id))
        {
            if (session->destination == LocalDestinationKind::CallAudio)
            {
                (void)sendLinkPacket(*session,
                                     reticulum::PacketType::Data,
                                     reticulum::PacketContext::LinkClose,
                                     session->link_id,
                                     sizeof(session->link_id),
                                     true);
                closeLinkSession(*session, LinkCloseReason::LocalClose);
            }
            else
            {
                ::platform::ui::reticulum_call::notify_link_closed(hangup_link_id);
            }
        }
        else
        {
            ::platform::ui::reticulum_call::notify_link_closed(hangup_link_id);
        }
    }

    uint8_t sent_packets = 0;
    ::platform::ui::reticulum_call::AudioPacket audio{};
    while (sent_packets < 2 &&
           ::platform::ui::reticulum_call::dequeue_outbound_audio(&audio))
    {
        LinkSession* session = findLinkSession(audio.link_id);
        if (!session ||
            session->destination != LocalDestinationKind::CallAudio ||
            session->state != LinkState::Active)
        {
            continue;
        }

        if (sendLinkPacket(*session,
                           reticulum::PacketType::Data,
                           reticulum::PacketContext::None,
                           audio.data,
                           audio.len,
                           true))
        {
            ::platform::ui::reticulum_call::note_tx_sent();
            ++sent_packets;
        }
    }
}

void LxmfAdapter::closeLinkSession(LinkSession& session, LinkCloseReason reason)
{
    for (const auto& deferred : session.deferred_payloads)
    {
        if (deferred.message_id != 0)
        {
            Serial.printf("[LXMF][DirectTX] deferred_failed msg=%lu reason=link_close close_reason=%u\n",
                          static_cast<unsigned long>(deferred.message_id),
                          static_cast<unsigned>(reason));
            sys::EventBus::publish(
                new sys::ChatSendResultEvent(deferred.message_id, false), 0);
        }
    }

    const bool transitioned = runtime::closeLinkSession(session, reason, millis());
    if (!transitioned)
    {
        return;
    }

    if (session.destination == LocalDestinationKind::CallAudio)
    {
        ::platform::ui::reticulum_call::notify_link_closed(session.link_id);
    }

    transport_.link_relays.erase(
        std::remove_if(transport_.link_relays.begin(),
                       transport_.link_relays.end(),
                       [&session](const LinkRelayEntry& relay)
                       {
                           return hashesEqual(relay.link_id, session.link_id, sizeof(relay.link_id));
                       }),
        transport_.link_relays.end());

    if ((reason == LinkCloseReason::Timeout || reason == LinkCloseReason::Error) &&
        !isZeroBytes(session.remote_destination_hash, sizeof(session.remote_destination_hash)))
    {
        expirePath(session.remote_destination_hash);
        for (auto& peer : peers_)
        {
            if (hashesEqual(peer.destination_hash,
                            session.remote_destination_hash,
                            sizeof(peer.destination_hash)))
            {
                peer.last_path_request_ms = 0;
                (void)sendPathRequest(peer);
                break;
            }
        }
    }
}

void LxmfAdapter::flushDeferredLinkPayloads(LinkSession& session)
{
    if (session.state != LinkState::Active)
    {
        return;
    }

    while (!session.deferred_payloads.empty())
    {
        const runtime::DeferredLinkPayload& deferred = session.deferred_payloads.front();
        bool sent = false;
        if (deferred.payload.size() <= session.mdu)
        {
            sent = sendLinkPacket(session,
                                  reticulum::PacketType::Data,
                                  reticulum::PacketContext::None,
                                  deferred.payload.data(),
                                  deferred.payload.size(),
                                  true);
        }
        else
        {
            sent = queueOutgoingResource(session,
                                         deferred.payload.data(),
                                         deferred.payload.size(),
                                         deferred.resource_flags,
                                         deferred.request_id.empty() ? nullptr : deferred.request_id.data(),
                                         deferred.request_id.size());
        }

        if (!sent)
        {
            break;
        }

        if (deferred.message_id != 0)
        {
            Serial.printf("[LXMF][DirectTX] deferred_sent msg=%lu path=link payload_len=%u\n",
                          static_cast<unsigned long>(deferred.message_id),
                          static_cast<unsigned>(deferred.payload.size()));
            sys::EventBus::publish(
                new sys::ChatSendResultEvent(deferred.message_id, true), 0);
        }

        session.deferred_payloads.erase(session.deferred_payloads.begin());
    }
}

void LxmfAdapter::expirePath(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return;
    }

    transport_.paths.erase(
        std::remove_if(transport_.paths.begin(),
                       transport_.paths.end(),
                       [destination_hash](const PathEntry& path)
                       {
                           return hashesEqual(path.destination_hash,
                                              destination_hash,
                                              sizeof(path.destination_hash));
                       }),
        transport_.paths.end());
    resolvePendingPathRequest(destination_hash);
}

LxmfAdapter::LinkSession* LxmfAdapter::findLinkSession(
    const uint8_t link_id[reticulum::kTruncatedHashSize])
{
    return runtime::findLinkSession(links_, link_id);
}

LxmfAdapter::LinkSession* LxmfAdapter::findActiveLinkSessionByDestination(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    LocalDestinationKind kind)
{
    return runtime::findActiveLinkSessionByDestination(links_, destination_hash, kind);
}

void LxmfAdapter::cullLinkSessions()
{
    const uint32_t now_ms = millis();
    const runtime::LinkRuntimeLimits limits{
        kMaxLinkSessions,
        kLinkRequestTtlMs,
        kLinkHandshakeTimeoutMs,
        kLinkIdleTimeoutMs,
        kLinkSessionTtlMs,
        kLinkStaleGraceMs,
        kLinkKeepaliveTimeoutFactor,
        5000};
    const runtime::ResourceRuntimeLimits resource_limits{kResourceTransferTtlMs};

    for (auto& session : links_.sessions)
    {
        runtime::cullLinkSessionTables(session, now_ms, limits);
        runtime::cullLinkResources(session, now_ms, resource_limits);
        const runtime::LinkRuntimeMaintenance maintenance =
            runtime::advanceLinkSessionLifecycle(session, now_ms, limits);
        if (maintenance.close_timeout)
        {
            closeLinkSession(session, LinkCloseReason::Timeout);
        }
        else
        {
            if (maintenance.flush_deferred_payloads)
            {
                flushDeferredLinkPayloads(session);
            }
            if (maintenance.send_keepalive)
            {
                (void)sendLinkKeepalive(session);
            }
            if (maintenance.marked_stale)
            {
                runtime::markLinkSessionStale(session);
            }
        }
    }

    runtime::removeExpiredLinkSessions(links_, now_ms, limits);
}

LxmfAdapter::PeerInfo* LxmfAdapter::rememberPeerIdentity(
    const uint8_t combined_pub[reticulum::kCombinedPublicKeySize],
    const char* display_name)
{
    if (!combined_pub)
    {
        return nullptr;
    }

    uint8_t identity_hash[reticulum::kTruncatedHashSize] = {};
    reticulum::computeIdentityHash(combined_pub, identity_hash);

    uint8_t delivery_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("lxmf", "delivery", name_hash);
    reticulum::computeDestinationHash(name_hash, identity_hash, delivery_hash);

    PeerInfo& peer = upsertPeer(delivery_hash);
    copyHash(peer.identity_hash, identity_hash, sizeof(peer.identity_hash));
    memcpy(peer.enc_pub, combined_pub, LxmfIdentity::kEncPubKeySize);
    memcpy(peer.sig_pub,
           combined_pub + LxmfIdentity::kEncPubKeySize,
           LxmfIdentity::kSigPubKeySize);
    peer.last_seen_s = currentTimestampSeconds();
    if (display_name && display_name[0] != '\0')
    {
        copyCString(peer.display_name, sizeof(peer.display_name), display_name);
    }
    const bool allow_persistence =
        screen_runtime::is_sleeping() && !screen_runtime::is_saver_active();
    if (allow_persistence)
    {
        if (!recordPeerInDirectory(peer, MeshPeerSource::RuntimeRx, false, false))
        {
            Serial.printf("[LXMF][Directory] address_save failed status=mesh_peer_directory\n");
        }
    }
    publishPeerUpdate(peer);
    return &peer;
}

bool LxmfAdapter::acceptVerifiedEnvelope(const uint8_t* plaintext, size_t plaintext_len,
                                         const uint8_t* raw_packet, size_t raw_len)
{
    ReticulumPeerIdentity conversation_identity{};
    return acceptVerifiedEnvelopeForDestination(identity_.destinationHash(),
                                                conversation_identity,
                                                false,
                                                true,
                                                plaintext,
                                                plaintext_len,
                                                raw_packet,
                                                raw_len);
}

bool LxmfAdapter::acceptVerifiedEnvelopeForDestination(
    const uint8_t expected_destination_hash[reticulum::kTruncatedHashSize],
    const ReticulumPeerIdentity& conversation_identity,
    bool destination_is_group,
    bool encrypted,
    const uint8_t* plaintext, size_t plaintext_len,
    const uint8_t* raw_packet, size_t raw_len)
{
    (void)raw_packet;

    char expected_hash[12] = {};
    formatHashPrefix(expected_destination_hash, expected_hash, sizeof(expected_hash));
    Serial.printf("[LXMF][%sRX] envelope begin dest=%s plaintext_len=%u raw_len=%u encrypted=%u\n",
                  destination_is_group ? "Group" : "Direct",
                  expected_hash,
                  static_cast<unsigned>(plaintext_len),
                  static_cast<unsigned>(raw_len),
                  encrypted ? 1U : 0U);
    if (!expected_destination_hash || !plaintext || plaintext_len == 0)
    {
        Serial.printf("[LXMF][%sRX] drop reason=invalid_input dest=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash);
        return false;
    }

    DecodedEnvelope envelope{};
    bool embedded_destination_hash = false;
    if (!unpackRnsLxmfEnvelope(expected_destination_hash,
                               plaintext,
                               plaintext_len,
                               &envelope,
                               &embedded_destination_hash))
    {
        Serial.printf("[LXMF][%sRX] drop reason=unpack_envelope_failed dest=%s plaintext_len=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      static_cast<unsigned>(plaintext_len));
        return false;
    }
    char envelope_dest_hash[12] = {};
    char source_hash[12] = {};
    formatHashPrefix(envelope.destination_hash,
                     envelope_dest_hash,
                     sizeof(envelope_dest_hash));
    formatHashPrefix(envelope.source_hash, source_hash, sizeof(source_hash));
    const char* wire_form = embedded_destination_hash ? "full" : "opportunistic_tail";
    Serial.printf("[LXMF][%sRX] envelope parsed wire=%s dest=%s source=%s payload_len=%u\n",
                  destination_is_group ? "Group" : "Direct",
                  wire_form,
                  envelope_dest_hash,
                  source_hash,
                  static_cast<unsigned>(envelope.packed_payload.size()));
    if (!hashesEqual(envelope.destination_hash,
                     expected_destination_hash,
                     reticulum::kTruncatedHashSize))
    {
        Serial.printf("[LXMF][%sRX] drop reason=destination_mismatch expected=%s envelope=%s source=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      envelope_dest_hash,
                      source_hash);
        return false;
    }

    const PeerInfo* peer = findPeerByDestinationHash(envelope.source_hash);
    if (!peer)
    {
        Serial.printf("[LXMF][%sRX] drop reason=unknown_sender dest=%s source=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      source_hash);
        return false;
    }

    const size_t signed_part_required =
        (reticulum::kTruncatedHashSize * 2) +
        envelope.packed_payload.size() +
        reticulum::kFullHashSize;
    std::vector<uint8_t> signed_part(signed_part_required);
    size_t signed_part_len = signed_part.size();
    uint8_t message_hash[reticulum::kFullHashSize] = {};
    if (!buildSignedPart(envelope.destination_hash,
                         envelope.source_hash,
                         envelope.packed_payload.data(),
                         envelope.packed_payload.size(),
                         signed_part.data(),
                         &signed_part_len,
                         message_hash))
    {
        Serial.printf("[LXMF][%sRX] drop reason=signed_part_failed dest=%s source=%s payload_len=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      source_hash,
                      static_cast<unsigned>(envelope.packed_payload.size()));
        return false;
    }

    if (!LxmfIdentity::verify(peer->sig_pub,
                              envelope.signature,
                              signed_part.data(),
                              signed_part_len))
    {
        Serial.printf("[LXMF][%sRX] drop reason=signature_failed dest=%s source=%s node=%08lX\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      source_hash,
                      static_cast<unsigned long>(peer->node_id));
        return false;
    }

    runtime::LxmfDeliveryContext delivery_context{};
    delivery_context.peer_node_id = peer->node_id;
    delivery_context.local_node_id = identity_.nodeId();
    delivery_context.message_id = messageIdFromHash(message_hash);
    delivery_context.timestamp_s = currentTimestampSeconds();
    delivery_context.peer_identity = reticulumIdentityForPeer(*peer);
    delivery_context.conversation_identity =
        hasReticulumDestinationIdentity(conversation_identity)
            ? conversation_identity
            : delivery_context.peer_identity;
    delivery_context.destination_is_group = destination_is_group;
    delivery_context.encrypted = encrypted;
    populateRxMeta(&delivery_context.rx_meta);

    runtime::LxmfVerifiedDelivery delivery{};
    if (!runtime::materialiseVerifiedLxmfDelivery(envelope.packed_payload.data(),
                                                  envelope.packed_payload.size(),
                                                  delivery_context,
                                                  &delivery))
    {
        Serial.printf("[LXMF][%sRX] drop reason=materialise_failed dest=%s source=%s msg=%lu payload_len=%u\n",
                      destination_is_group ? "Group" : "Direct",
                      expected_hash,
                      source_hash,
                      static_cast<unsigned long>(delivery_context.message_id),
                      static_cast<unsigned>(envelope.packed_payload.size()));
        return false;
    }
    Serial.printf("[LXMF][%sRX] verified kind=%u msg=%lu from=%08lX dest=%s source=%s\n",
                  destination_is_group ? "Group" : "Direct",
                  static_cast<unsigned>(delivery.kind),
                  static_cast<unsigned long>(delivery_context.message_id),
                  static_cast<unsigned long>(peer->node_id),
                  expected_hash,
                  source_hash);

    if (delivery.kind == runtime::LxmfDeliveryKind::AppData)
    {
        ::chat::infra::IncomingQueuePushReport report{};
        if (data_receive_queue_.push(delivery.app_data.incoming,
                                     delivery.app_data.payload.empty() ? nullptr
                                                                       : delivery.app_data.payload.data(),
                                     delivery.app_data.payload.size(),
                                     ::chat::infra::IncomingQueuePriority::P1User,
                                     &report))
        {
            if (report.dropped_existing)
            {
                Serial.printf("[LXMF] RX data queue pressure evicted_prio=%u depth=%u\n",
                              static_cast<unsigned>(report.dropped_priority),
                              static_cast<unsigned>(data_receive_queue_.size()));
            }
            Serial.printf("[LXMF][%sRX] queued app_data msg=%lu port=%lu len=%u depth=%u\n",
                          destination_is_group ? "Group" : "Direct",
                          static_cast<unsigned long>(delivery_context.message_id),
                          static_cast<unsigned long>(delivery.app_data.incoming.portnum),
                          static_cast<unsigned>(delivery.app_data.payload.size()),
                          static_cast<unsigned>(data_receive_queue_.size()));
            return true;
        }
        Serial.printf("[LXMF] RX data queue drop port=%lu len=%u depth=%u\n",
                      static_cast<unsigned long>(delivery.app_data.incoming.portnum),
                      static_cast<unsigned>(delivery.app_data.payload.size()),
                      static_cast<unsigned>(data_receive_queue_.size()));
        return true;
    }

    if (delivery.kind != runtime::LxmfDeliveryKind::Text)
    {
        Serial.printf("[LXMF][%sRX] drop reason=unsupported_delivery kind=%u msg=%lu\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned>(delivery.kind),
                      static_cast<unsigned long>(delivery_context.message_id));
        return false;
    }
    ::chat::infra::IncomingQueuePushReport report{};
    if (text_receive_queue_.push(delivery.text.incoming,
                                 delivery.text.text.data(),
                                 delivery.text.text.size(),
                                 ::chat::infra::IncomingQueuePriority::P1User,
                                 &report))
    {
        if (report.dropped_existing)
        {
            Serial.printf("[LXMF] RX text queue pressure evicted_prio=%u depth=%u\n",
                          static_cast<unsigned>(report.dropped_priority),
                          static_cast<unsigned>(text_receive_queue_.size()));
        }
        Serial.printf("[LXMF][%sRX] queued text msg=%lu from=%08lX to=%08lX len=%u depth=%u dest=%s\n",
                      destination_is_group ? "Group" : "Direct",
                      static_cast<unsigned long>(delivery_context.message_id),
                      static_cast<unsigned long>(delivery.text.incoming.from),
                      static_cast<unsigned long>(delivery.text.incoming.to),
                      static_cast<unsigned>(delivery.text.text.size()),
                      static_cast<unsigned>(text_receive_queue_.size()),
                      expected_hash);
        return true;
    }
    Serial.printf("[LXMF] RX text queue drop len=%u depth=%u\n",
                  static_cast<unsigned>(delivery.text.text.size()),
                  static_cast<unsigned>(text_receive_queue_.size()));
    return true;
}

LxmfAdapter::PeerInfo* LxmfAdapter::findPeerByNodeId(NodeId node_id)
{
    for (auto& peer : peers_)
    {
        if (peer.node_id == node_id)
        {
            return &peer;
        }
    }
    return nullptr;
}

const LxmfAdapter::PeerInfo* LxmfAdapter::findPeerByDestinationHash(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }
    for (const auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash, hash, reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

const LxmfAdapter::PeerInfo* LxmfAdapter::findPeerByIdentityHash(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }
    for (const auto& peer : peers_)
    {
        if (hashesEqual(peer.identity_hash, hash, reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    return nullptr;
}

const ReticulumGroupDestinationConfig* LxmfAdapter::findConfiguredGroupDestination(
    const uint8_t hash[reticulum::kTruncatedHashSize]) const
{
    if (!hash)
    {
        return nullptr;
    }

    for (const auto& group : config_.reticulum_groups)
    {
        if (!group.enabled || !hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }
        if (hashesEqual(group.identity.destination_hash,
                        hash,
                        reticulum::kTruncatedHashSize))
        {
            return &group;
        }
    }
    return nullptr;
}

bool LxmfAdapter::isConfiguredGroupDestination(
    const ReticulumPeerIdentity& destination) const
{
    return hasReticulumDestinationIdentity(destination) &&
           findConfiguredGroupDestination(destination.destination_hash) != nullptr;
}

LxmfAdapter::PeerInfo& LxmfAdapter::upsertPeer(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    for (auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash, destination_hash, reticulum::kTruncatedHashSize))
        {
            return peer;
        }
    }

    peers_.push_back(PeerInfo{});
    PeerInfo& peer = peers_.back();
    copyHash(peer.destination_hash, destination_hash, reticulum::kTruncatedHashSize);
    peer.node_id = reticulum::nodeIdFromDestinationHash(destination_hash);
    return peer;
}

LxmfAdapter::PeerInfo* LxmfAdapter::upsertPeerFromDirectoryRecord(
    const MeshPeerRecord& record,
    bool queue_update)
{
    if (!meshPeerRecordIsValid(record) || record.flags.ignored ||
        !meshPeerSameProtocol(record.identity.protocol, MeshProtocol::Reticulum) ||
        record.identity.kind != MeshPeerIdentityKind::ReticulumDestination ||
        !record.reticulum.has_public_keys)
    {
        return nullptr;
    }

    const ReticulumPeerIdentity& identity = record.reticulum.identity.valid
                                                ? record.reticulum.identity
                                                : record.identity.reticulum;
    if (!identity.valid ||
        isZeroBytes(identity.destination_hash, sizeof(identity.destination_hash)) ||
        isZeroBytes(identity.identity_hash, sizeof(identity.identity_hash)) ||
        isZeroBytes(record.reticulum.enc_pub, sizeof(record.reticulum.enc_pub)) ||
        isZeroBytes(record.reticulum.sig_pub, sizeof(record.reticulum.sig_pub)))
    {
        return nullptr;
    }

    PeerInfo& peer = upsertPeer(identity.destination_hash);
    copyHash(peer.identity_hash, identity.identity_hash, sizeof(peer.identity_hash));
    memcpy(peer.enc_pub, record.reticulum.enc_pub, sizeof(peer.enc_pub));
    memcpy(peer.sig_pub, record.reticulum.sig_pub, sizeof(peer.sig_pub));
    peer.last_seen_s = record.last_seen_s != 0
                           ? record.last_seen_s
                           : currentTimestampSeconds();
    peer.last_path_request_ms = 0;
    copyCString(peer.display_name, sizeof(peer.display_name), record.display_name);
    if (queue_update)
    {
        queuePeerUpdate(peer);
    }
    return &peer;
}

LxmfAdapter::PeerInfo* LxmfAdapter::findOrLoadPeerByNodeId(NodeId node_id)
{
    if (node_id == 0)
    {
        return nullptr;
    }
    if (PeerInfo* peer = findPeerByNodeId(node_id))
    {
        return peer;
    }
    if (!peer_directory_)
    {
        return nullptr;
    }

    MeshPeerRecord record{};
    const MeshPeerDirectoryStatus status =
        peer_directory_->findByNodeId(MeshProtocol::Reticulum, node_id, record);
    if (!status.succeeded())
    {
        Serial.printf("[LXMF][Directory] peer_lookup miss node=%08lX status=%u\n",
                      static_cast<unsigned long>(node_id),
                      static_cast<unsigned>(status.code));
        return nullptr;
    }
    PeerInfo* peer = upsertPeerFromDirectoryRecord(record, true);
    if (peer)
    {
        Serial.printf("[LXMF][Directory] peer_lookup loaded node=%08lX name=%s\n",
                      static_cast<unsigned long>(node_id),
                      peer->display_name[0] != '\0' ? peer->display_name : "<unnamed>");
    }
    return peer;
}

LxmfAdapter::PeerInfo* LxmfAdapter::findOrLoadPeerByDestinationHash(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return nullptr;
    }
    for (auto& peer : peers_)
    {
        if (hashesEqual(peer.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize))
        {
            return &peer;
        }
    }
    if (!peer_directory_)
    {
        return nullptr;
    }

    MeshPeerIdentity identity{};
    identity.protocol = MeshProtocol::Reticulum;
    identity.kind = MeshPeerIdentityKind::ReticulumDestination;
    identity.reticulum = makeReticulumDestinationIdentity(destination_hash);
    MeshPeerRecord record{};
    const MeshPeerDirectoryStatus status = peer_directory_->find(identity, record);
    if (!status.succeeded())
    {
        char dest[12] = {};
        formatHashPrefix(destination_hash, dest, sizeof(dest));
        Serial.printf("[LXMF][Directory] peer_lookup miss dest=%s status=%u\n",
                      dest,
                      static_cast<unsigned>(status.code));
        return nullptr;
    }
    PeerInfo* peer = upsertPeerFromDirectoryRecord(record, true);
    if (peer)
    {
        char dest[12] = {};
        formatHashPrefix(peer->destination_hash, dest, sizeof(dest));
        Serial.printf("[LXMF][Directory] peer_lookup loaded dest=%s name=%s\n",
                      dest,
                      peer->display_name[0] != '\0' ? peer->display_name : "<unnamed>");
    }
    return peer;
}

MeshActionResult LxmfAdapter::persistPeerAddressNow(const PeerInfo& peer,
                                                    bool favorite) const
{
    if (isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)) ||
        isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) ||
        isZeroBytes(peer.enc_pub, sizeof(peer.enc_pub)) ||
        isZeroBytes(peer.sig_pub, sizeof(peer.sig_pub)))
    {
        return MeshActionResult::fail(MeshOperationFailure::PeerKeyMissing);
    }
    if (!peer_directory_)
    {
        return MeshActionResult::fail(MeshOperationFailure::NotReady);
    }

    if (!recordPeerInDirectory(peer, MeshPeerSource::Manual, true, favorite))
    {
        return MeshActionResult::fail(MeshOperationFailure::Unknown);
    }
    return MeshActionResult::success();
}

bool LxmfAdapter::recordPeerInDirectory(const PeerInfo& peer,
                                        MeshPeerSource source,
                                        bool update_favorite,
                                        bool favorite) const
{
    if (!peer_directory_ ||
        isZeroBytes(peer.destination_hash, sizeof(peer.destination_hash)) ||
        isZeroBytes(peer.identity_hash, sizeof(peer.identity_hash)) ||
        isZeroBytes(peer.enc_pub, sizeof(peer.enc_pub)) ||
        isZeroBytes(peer.sig_pub, sizeof(peer.sig_pub)))
    {
        return false;
    }

    MeshPeerRecord record{};
    record.valid = true;
    record.identity = makeMeshPeerReticulumIdentity(reticulumIdentityForPeer(peer));
    record.source = source;
    const uint32_t now_s = currentTimestampSeconds();
    record.first_seen_s = peer.last_seen_s != 0 ? peer.last_seen_s : now_s;
    record.last_seen_s = peer.last_seen_s != 0 ? peer.last_seen_s : now_s;
    copyMeshPeerText(record.display_name,
                     sizeof(record.display_name),
                     peer.display_name);
    record.reticulum.identity = record.identity.reticulum;
    record.reticulum.has_public_keys = true;
    memcpy(record.reticulum.enc_pub,
           peer.enc_pub,
           sizeof(record.reticulum.enc_pub));
    memcpy(record.reticulum.sig_pub,
           peer.sig_pub,
           sizeof(record.reticulum.sig_pub));
    record.reticulum.delivery = true;

    MeshPeerUserFlags flags{};
    if (update_favorite)
    {
        MeshPeerRecord existing{};
        if (peer_directory_->find(record.identity, existing).succeeded())
        {
            flags = existing.flags;
        }
        flags.favorite = favorite;
    }

    const MeshPeerDirectoryStatus record_status = peer_directory_->record(record);
    if (!record_status.succeeded())
    {
        Serial.printf("[LXMF][Directory] address_save failed status=%u\n",
                      static_cast<unsigned>(record_status.code));
        return false;
    }

    if (update_favorite)
    {
        const MeshPeerDirectoryStatus flags_status =
            peer_directory_->setUserFlags(record.identity, flags);
        if (!flags_status.succeeded())
        {
            Serial.printf("[LXMF][Directory] flag_save failed status=%u\n",
                          static_cast<unsigned>(flags_status.code));
            return false;
        }
    }
    return true;
}

void LxmfAdapter::queuePeerUpdate(const PeerInfo& peer)
{
    if (peer.node_id == 0)
    {
        return;
    }

    for (std::size_t index = 0; index < pending_peer_projection_count_; ++index)
    {
        if (pending_peer_projection_nodes_[index] == peer.node_id)
        {
            return;
        }
    }

    if (pending_peer_projection_count_ >= pending_peer_projection_nodes_.size())
    {
        return;
    }

    pending_peer_projection_nodes_[pending_peer_projection_count_] = peer.node_id;
    ++pending_peer_projection_count_;
}

void LxmfAdapter::pumpPendingPeerUpdates()
{
    if (pending_peer_projection_count_ == 0)
    {
        return;
    }

    const uint32_t now_ms = millis();
    const bool maintenance_window =
        screen_runtime::is_sleeping() && !screen_runtime::is_saver_active();
    const uint32_t interval_ms = maintenance_window
                                     ? kPeerProjectionSleepIntervalMs
                                     : kPeerProjectionScreenIntervalMs;
    if (last_peer_projection_ms_ != 0 &&
        (now_ms - last_peer_projection_ms_) < interval_ms)
    {
        return;
    }

    const NodeId node_id = pending_peer_projection_nodes_[0];
    for (std::size_t index = 1; index < pending_peer_projection_count_; ++index)
    {
        pending_peer_projection_nodes_[index - 1U] = pending_peer_projection_nodes_[index];
    }
    --pending_peer_projection_count_;
    last_peer_projection_ms_ = now_ms;

    const PeerInfo* peer = findPeerByNodeId(node_id);
    if (peer)
    {
        publishPeerUpdate(*peer);
    }
}

void LxmfAdapter::publishPeerUpdate(const PeerInfo& peer) const
{
    char short_name[10] = {};
    snprintf(short_name, sizeof(short_name), "%04lX",
             static_cast<unsigned long>(peer.node_id & 0xFFFFUL));

    sys::EventBus::publish(new sys::NodeProtocolUpdateEvent(
                               peer.node_id,
                               peer.last_seen_s,
                               static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum)),
                           0);

    auto* node_event = new sys::NodeInfoUpdateEvent(
        peer.node_id,
        short_name,
        peer.display_name,
        interfaces_.lastRxSnr(),
        interfaces_.lastRxRssi(),
        peer.last_seen_s,
        static_cast<uint8_t>(chat::contacts::NodeProtocolType::Reticulum),
        static_cast<uint8_t>(chat::contacts::NodeRoleType::Client),
        0,
        0,
        0xFF);
    node_event->reticulum_identity = reticulumIdentityForPeer(peer);
    sys::EventBus::publish(node_event, 0);
}

void LxmfAdapter::loadDirectoryPeers()
{
    if (!peer_directory_)
    {
        Serial.printf("[LXMF][Directory] load skipped reason=no_mesh_peer_directory\n");
        return;
    }

    std::size_t count = 0;
    const MeshPeerDirectoryStatus status =
        peer_directory_->loadRecent(MeshProtocol::Reticulum,
                                    peer_directory_load_entries_.data(),
                                    peer_directory_load_entries_.size(),
                                    &count);
    if (!status.succeeded())
    {
        Serial.printf("[LXMF][Directory] load failed status=%u\n",
                      static_cast<unsigned>(status.code));
        return;
    }

    std::size_t loaded = 0;
    for (std::size_t index = 0; index < count; ++index)
    {
        if (upsertPeerFromDirectoryRecord(peer_directory_load_entries_[index], true))
        {
            ++loaded;
        }
    }

    if (loaded > 0)
    {
        Serial.printf("[LXMF][Directory] loaded addresses=%u directory=mesh_peer_directory\n",
                      static_cast<unsigned>(loaded));
    }
}

void LxmfAdapter::loadPersistedPeers()
{
    peers_loaded_ = true;
    loadDirectoryPeers();
}

uint32_t LxmfAdapter::currentTimestampSeconds() const
{
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        return epoch_s;
    }
    return millis() / 1000U;
}

const char* LxmfAdapter::effectiveDisplayName() const
{
    if (!user_long_name_.empty())
    {
        return user_long_name_.c_str();
    }
    if (!user_short_name_.empty())
    {
        return user_short_name_.c_str();
    }
    return nullptr;
}

void LxmfAdapter::populateRxMeta(RxMeta* out) const
{
    if (!out)
    {
        return;
    }

    const RxMeta& last_meta = has_active_rx_meta_ ? active_rx_meta_ : interfaces_.lastRxMeta();
    if (last_meta.rx_timestamp_ms != 0 || last_meta.rx_timestamp_s != 0)
    {
        *out = last_meta;
        return;
    }

    out->rx_timestamp_ms = millis();
    out->rx_timestamp_s = currentTimestampSeconds();
    out->time_source = is_valid_epoch(out->rx_timestamp_s)
                           ? RxTimeSource::DeviceUtc
                           : RxTimeSource::Uptime;
    out->origin = RxOrigin::LoRa;
    out->direct = true;
    out->from_is = false;
    out->rssi_dbm_x10 = static_cast<int16_t>(lround(interfaces_.lastRxRssi() * 10.0f));
    out->snr_db_x10 = static_cast<int16_t>(lround(interfaces_.lastRxSnr() * 10.0f));
}

uint32_t LxmfAdapter::messageIdFromHash(const uint8_t hash[reticulum::kFullHashSize])
{
    return (static_cast<uint32_t>(hash[28]) << 24) |
           (static_cast<uint32_t>(hash[29]) << 16) |
           (static_cast<uint32_t>(hash[30]) << 8) |
           static_cast<uint32_t>(hash[31]);
}

void LxmfAdapter::pathRequestDestinationHash(uint8_t out_hash[reticulum::kTruncatedHashSize])
{
    if (!out_hash)
    {
        return;
    }

    uint8_t name_hash[reticulum::kNameHashSize] = {};
    reticulum::computeNameHash("rnstransport", "path.request", name_hash);
    reticulum::computePlainDestinationHash(name_hash, out_hash);
}

} // namespace chat::lxmf
