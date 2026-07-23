#include "ble/meshcore_ble.h"

#include "sys/clock.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace
{
constexpr size_t kPubKeySize = chat::meshcore::MeshCoreIdentity::kPubKeySize;
constexpr uint8_t kAdvTypeChat = 1;
constexpr bool kMeshCoreBleSecurityEnabled = true;

bool isConfiguredBlePin(uint32_t pin)
{
    return pin == 0 || (pin >= 100000 && pin <= 999999);
}

void copyBounded(char* dst, size_t dst_len, const char* src)
{
    if (!dst || dst_len == 0)
    {
        return;
    }
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, dst_len - 1);
    dst[dst_len - 1] = '\0';
}

int8_t encodeSnrQdb(int16_t snr_x10)
{
    if (snr_x10 == std::numeric_limits<int16_t>::min())
    {
        return 0;
    }
    int32_t qdb = (static_cast<int32_t>(snr_x10) * 4) / 10;
    if (qdb > 127)
    {
        qdb = 127;
    }
    else if (qdb < -128)
    {
        qdb = -128;
    }
    return static_cast<int8_t>(qdb);
}

int8_t encodeRssiDbm(int16_t rssi_x10)
{
    if (rssi_x10 == std::numeric_limits<int16_t>::min())
    {
        return 0;
    }
    int32_t dbm = rssi_x10 / 10;
    if (dbm > 127)
    {
        dbm = 127;
    }
    else if (dbm < -128)
    {
        dbm = -128;
    }
    return static_cast<int8_t>(dbm);
}

uint32_t nowSeconds()
{
    return sys::epoch_seconds_now();
}
} // namespace

namespace ble
{
phone::meshcore::MeshCorePhoneBatteryInfo MeshCoreBleService::getBatteryInfo() const
{
    return phone_facade_.getBatteryInfo();
}

phone::meshcore::MeshCorePhoneLocation MeshCoreBleService::getAdvertLocation() const
{
    phone::meshcore::MeshCorePhoneLocation location{};
    location.lat_i6 = advert_lat_;
    location.lon_i6 = advert_lon_;
    return location;
}

uint32_t MeshCoreBleService::getReportedBlePin() const
{
    return effectiveBlePin();
}

uint8_t MeshCoreBleService::getAdvertLocationPolicy() const
{
    return advert_loc_policy_;
}

uint8_t MeshCoreBleService::getTelemetryModeBits() const
{
    return static_cast<uint8_t>((telemetry_mode_base_ & 0x03U) |
                                ((telemetry_mode_loc_ & 0x03U) << 2U) |
                                ((telemetry_mode_env_ & 0x03U) << 4U));
}

bool MeshCoreBleService::getManualAddContacts() const
{
    return manual_add_contacts_;
}

std::size_t MeshCoreBleService::meshCoreContactCount() const
{
    std::size_t total = manual_contacts_.size();
    if (const auto* adapter = meshCoreAdapter())
    {
        std::vector<chat::meshcore::MeshCoreAdapter::PeerInfo> peers;
        adapter->getPeerInfos(peers);
        for (const auto& peer : peers)
        {
            if (!peer.has_pubkey)
            {
                continue;
            }
            bool manual_duplicate = false;
            for (const auto& manual : manual_contacts_)
            {
                if (std::memcmp(manual.pubkey, peer.pubkey, kPubKeySize) == 0)
                {
                    manual_duplicate = true;
                    break;
                }
            }
            if (!manual_duplicate)
            {
                ++total;
            }
        }
    }
    return total;
}

bool MeshCoreBleService::getMeshCoreContactByIndex(std::size_t index,
                                                   phone::meshcore::MeshCorePhoneContactView* out) const
{
    if (!out)
    {
        return false;
    }
    *out = {};
    if (index < manual_contacts_.size())
    {
        const ContactRecord& record = manual_contacts_[index];
        std::memcpy(out->pubkey, record.pubkey, kPubKeySize);
        out->node_id = deriveNodeIdFromPubkey(out->pubkey, kPubKeySize);
        out->type = record.type;
        out->flags = record.flags;
        out->out_path_len = record.out_path_len;
        std::memcpy(out->out_path, record.out_path, sizeof(out->out_path));
        copyBounded(out->name, sizeof(out->name), record.name);
        out->last_advert = record.last_advert;
        out->lat_i6 = record.lat;
        out->lon_i6 = record.lon;
        out->lastmod = record.lastmod != 0 ? record.lastmod : record.last_advert;
        out->path_meta.profile = record.out_path_profile;
        out->path_meta.hash_bytes = record.out_path_hash_bytes == 0 ? 1 : record.out_path_hash_bytes;
        out->path_meta.hop_count = out->out_path_len > 0
                                       ? static_cast<uint8_t>(out->out_path_len / out->path_meta.hash_bytes)
                                       : 0;
        return true;
    }

    index -= manual_contacts_.size();
    const auto* adapter = meshCoreAdapter();
    if (!adapter)
    {
        return false;
    }

    std::vector<chat::meshcore::MeshCoreAdapter::PeerInfo> peers;
    adapter->getPeerInfos(peers);
    for (const auto& peer : peers)
    {
        if (!peer.has_pubkey)
        {
            continue;
        }
        bool manual_duplicate = false;
        for (const auto& manual : manual_contacts_)
        {
            if (std::memcmp(manual.pubkey, peer.pubkey, kPubKeySize) == 0)
            {
                manual_duplicate = true;
                break;
            }
        }
        if (manual_duplicate)
        {
            continue;
        }
        if (index > 0)
        {
            --index;
            continue;
        }

        out->node_id = peer.node_id;
        std::memcpy(out->pubkey, peer.pubkey, kPubKeySize);
        out->type = kAdvTypeChat;
        out->flags = 0;
        out->out_path_len = peer.out_path_len;
        if (out->out_path_len > 0)
        {
            const size_t copy_len = std::min(static_cast<size_t>(out->out_path_len), sizeof(out->out_path));
            std::memcpy(out->out_path, peer.out_path, copy_len);
        }
        const auto* directory_peer =
            ctx_.getContactService().getPeerByNodeId(peer.node_id);
        if (directory_peer)
        {
            copyBounded(out->name,
                        sizeof(out->name),
                        directory_peer->long_name[0] != '\0'
                            ? directory_peer->long_name
                            : directory_peer->short_name);
        }
        if (out->name[0] == '\0')
        {
            std::snprintf(out->name, sizeof(out->name), "%02X%02X", out->pubkey[0], out->pubkey[1]);
        }
        out->last_advert = peer.last_seen_ms / 1000U;
        out->lastmod = out->last_advert;
        out->path_meta.profile = static_cast<uint8_t>(peer.out_path_profile);
        out->path_meta.hash_bytes = static_cast<uint8_t>(
            chat::meshcore::payloadHashBytes(peer.out_path_profile));
        out->path_meta.hop_count = out->out_path_len > 0
                                       ? static_cast<uint8_t>(out->out_path_len / out->path_meta.hash_bytes)
                                       : 0;
        return true;
    }
    return false;
}

bool MeshCoreBleService::resolveMeshCoreContactNodeId(const uint8_t* prefix,
                                                      std::size_t len,
                                                      uint32_t* out_node_id) const
{
    if (!prefix || len == 0 || !out_node_id)
    {
        return false;
    }
    *out_node_id = 0;

    chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
    if (lookupPeerByPrefix(prefix, len, &peer) && peer.node_id != 0)
    {
        *out_node_id = peer.node_id;
        return true;
    }

    const size_t cmp_len = std::min(len, kPubKeySize);
    for (const auto& manual : manual_contacts_)
    {
        if (std::memcmp(manual.pubkey, prefix, cmp_len) == 0)
        {
            *out_node_id = deriveNodeIdFromPubkey(manual.pubkey, kPubKeySize);
            return *out_node_id != 0;
        }
    }
    return false;
}

bool MeshCoreBleService::resolvePeerPublicKey(const uint8_t* in_pubkey, size_t in_len,
                                              uint8_t* out_pubkey, size_t out_len) const
{
    if (!in_pubkey || !out_pubkey || out_len < kPubKeySize || in_len < kPubKeySize)
    {
        return false;
    }
    chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
    if (!resolveContactByPubkey(in_pubkey, &peer, nullptr))
    {
        return false;
    }
    std::memcpy(out_pubkey, peer.pubkey, kPubKeySize);
    if (auto* adapter = const_cast<MeshCoreBleService*>(this)->meshCoreAdapter())
    {
        adapter->ensurePeerPublicKey(out_pubkey, kPubKeySize, peer.pubkey_verified);
    }
    return true;
}

void MeshCoreBleService::onPendingBinaryRequest(uint32_t tag)
{
    clearPendingRequests();
    pending_req_ = tag;
}

void MeshCoreBleService::onPendingTelemetryRequest(uint32_t tag)
{
    clearPendingRequests();
    pending_telemetry_ = tag;
}

void MeshCoreBleService::onPendingPathDiscoveryRequest(uint32_t tag)
{
    clearPendingRequests();
    pending_discovery_ = tag;
}

void MeshCoreBleService::onSentRoute(bool sent_flood)
{
    noteSentRoute(sent_flood);
}

bool MeshCoreBleService::lookupAdvertPath(const uint8_t* pubkey, size_t len,
                                          uint32_t* out_ts, uint8_t* out_path, size_t* inout_len) const
{
    if (!out_ts || !out_path || !inout_len)
    {
        return false;
    }
    phone::meshcore::MeshCorePhoneAdvertPath path{};
    if (!lookupAdvertPathEx(pubkey, len, &path))
    {
        return false;
    }
    *out_ts = path.timestamp;
    const size_t copy_len = std::min(static_cast<size_t>(path.path_len), *inout_len);
    if (copy_len > 0)
    {
        std::memcpy(out_path, path.path, copy_len);
    }
    *inout_len = copy_len;
    return true;
}

bool MeshCoreBleService::lookupAdvertPathEx(const uint8_t* pubkey, size_t len,
                                            phone::meshcore::MeshCorePhoneAdvertPath* out) const
{
    if (!pubkey || len < kPubKeySize || !out)
    {
        return false;
    }

    if (const ContactRecord* rec = const_cast<MeshCoreBleService*>(this)->findManualContact(pubkey))
    {
        *out = {};
        out->timestamp = rec->last_advert;
        const size_t copy_len = std::min(static_cast<size_t>(rec->out_path_len), sizeof(out->path));
        if (copy_len > 0)
        {
            std::memcpy(out->path, rec->out_path, copy_len);
        }
        out->path_len = static_cast<uint8_t>(copy_len);
        out->meta.profile = rec->out_path_profile;
        out->meta.hash_bytes = rec->out_path_hash_bytes == 0 ? 1 : rec->out_path_hash_bytes;
        out->meta.hop_count = copy_len > 0
                                  ? static_cast<uint8_t>(copy_len / out->meta.hash_bytes)
                                  : 0;
        return true;
    }

    if (const auto* adapter = meshCoreAdapter())
    {
        chat::meshcore::MeshCoreAdapter::PeerInfo peer{};
        if (adapter->lookupPeerByHash(pubkey[0], &peer) &&
            peer.has_pubkey &&
            std::memcmp(peer.pubkey, pubkey, kPubKeySize) == 0)
        {
            *out = {};
            out->timestamp = peer.last_seen_ms / 1000U;
            const size_t copy_len = std::min(static_cast<size_t>(peer.out_path_len), sizeof(out->path));
            if (copy_len > 0)
            {
                std::memcpy(out->path, peer.out_path, copy_len);
            }
            out->path_len = static_cast<uint8_t>(copy_len);
            out->meta.profile = static_cast<uint8_t>(peer.out_path_profile);
            out->meta.hash_bytes = static_cast<uint8_t>(
                chat::meshcore::payloadHashBytes(peer.out_path_profile));
            out->meta.hop_count = copy_len > 0
                                      ? static_cast<uint8_t>(copy_len / out->meta.hash_bytes)
                                      : 0;
            return true;
        }
    }

    return false;
}

bool MeshCoreBleService::hasActiveConnection(const uint8_t* prefix, size_t len) const
{
    if (!prefix || len < sizeof(uint32_t))
    {
        return false;
    }
    uint32_t prefix4 = 0;
    std::memcpy(&prefix4, prefix, sizeof(prefix4));
    return hasConnectionPrefix(prefix4, millis());
}

void MeshCoreBleService::logoutActiveConnection(const uint8_t* prefix, size_t len)
{
    if (!prefix || len < sizeof(uint32_t))
    {
        return;
    }
    uint32_t prefix4 = 0;
    std::memcpy(&prefix4, prefix, sizeof(prefix4));
    removeConnectionPrefix(prefix4);
}

bool MeshCoreBleService::getRadioStats(phone::meshcore::MeshCorePhoneRadioStats* out) const
{
    if (!out)
    {
        return false;
    }
    out->last_rssi_dbm = encodeRssiDbm(last_rssi_dbm_x10_);
    out->last_snr_qdb = encodeSnrQdb(last_snr_db_x10_);
    out->noise_floor_dbm = 0;
    out->tx_air_seconds = 0;
    out->rx_air_seconds = 0;
    if (const auto* adapter = meshCoreAdapter())
    {
        const auto radio_stats = adapter->getRadioStats();
        out->noise_floor_dbm = radio_stats.noise_floor_dbm;
        out->tx_air_seconds = radio_stats.tx_airtime_ms / 1000U;
        out->rx_air_seconds = radio_stats.rx_airtime_ms / 1000U;
    }
    return true;
}

bool MeshCoreBleService::getPacketStats(phone::meshcore::MeshCorePhonePacketStats* out) const
{
    if (!out)
    {
        return false;
    }
    out->rx_packets = stats_rx_packets_;
    out->tx_packets = stats_tx_packets_;
    out->tx_flood = stats_tx_flood_;
    out->tx_direct = stats_tx_direct_;
    out->rx_flood = stats_rx_flood_;
    out->rx_direct = stats_rx_direct_;
    return true;
}

bool MeshCoreBleService::setAdvertLocation(int32_t lat, int32_t lon)
{
    advert_lat_ = lat;
    advert_lon_ = lon;
    return true;
}

bool MeshCoreBleService::upsertContactFromFrame(const uint8_t* frame, size_t len)
{
    if (!frame)
    {
        return false;
    }
    ContactRecord record{};
    uint32_t lastmod = 0;
    if (!decodeContactPayload(frame, len, &record, &lastmod))
    {
        return false;
    }
    if (lastmod == 0)
    {
        lastmod = nowSeconds();
    }
    record.lastmod = lastmod;
    if (record.last_advert == 0)
    {
        record.last_advert = nowSeconds();
    }
    upsertManualContact(record);
    saveManualContacts();
    return true;
}

bool MeshCoreBleService::removeContact(const uint8_t* pubkey, size_t len)
{
    if (!pubkey || len < kPubKeySize)
    {
        return false;
    }
    if (!removeManualContact(pubkey))
    {
        return false;
    }
    saveManualContacts();
    return true;
}

bool MeshCoreBleService::exportContact(const uint8_t* pubkey, size_t len, uint8_t* out, size_t* out_len) const
{
    if (!out || !out_len)
    {
        return false;
    }
    auto* adapter = const_cast<MeshCoreBleService*>(this)->meshCoreAdapter();
    if (!adapter)
    {
        return false;
    }
    std::vector<uint8_t> frame;
    if (!pubkey || len < kPubKeySize)
    {
        const bool include_location = (advert_loc_policy_ != 0);
        if (!adapter->exportAdvertFrame(nullptr, 0, frame, include_location, advert_lat_, advert_lon_))
        {
            return false;
        }
    }
    else if (!adapter->exportAdvertFrame(pubkey, kPubKeySize, frame, false, 0, 0))
    {
        return false;
    }
    if (frame.empty() || frame.size() > *out_len)
    {
        return false;
    }
    std::memcpy(out, frame.data(), frame.size());
    *out_len = frame.size();
    return true;
}

bool MeshCoreBleService::importContact(const uint8_t* frame, size_t len)
{
    auto* adapter = meshCoreAdapter();
    return adapter ? adapter->importAdvertFrame(frame, len) : false;
}

bool MeshCoreBleService::shareContact(const uint8_t* pubkey, size_t len)
{
    auto* adapter = meshCoreAdapter();
    return (adapter && pubkey && len >= kPubKeySize) ? adapter->sendStoredAdvert(pubkey, kPubKeySize) : false;
}

bool MeshCoreBleService::popOfflineMessage(uint8_t* out, size_t* out_len)
{
    if (!out || !out_len || offline_queue_.empty())
    {
        return false;
    }
    const Frame* frame = offline_queue_.front();
    if (!frame)
    {
        return false;
    }
    if (*out_len < frame->len)
    {
        *out_len = frame->len;
        return false;
    }
    std::memcpy(out, frame->buf.data(), frame->len);
    *out_len = frame->len;
    offline_queue_.pop_front();
    return true;
}

bool MeshCoreBleService::setTuningParams(const phone::meshcore::MeshCorePhoneTuningParams& params)
{
    return phone_facade_.setTuningParams(params);
}

bool MeshCoreBleService::getTuningParams(phone::meshcore::MeshCorePhoneTuningParams* out) const
{
    if (!out)
    {
        return false;
    }
    return phone_facade_.getTuningParams(out);
}

bool MeshCoreBleService::setOtherParams(uint8_t manual_add_contacts, uint8_t telemetry_bits,
                                        bool has_multi_acks, uint8_t advert_loc_policy, uint8_t multi_acks)
{
    manual_add_contacts_ = (manual_add_contacts != 0);
    telemetry_mode_base_ = telemetry_bits & 0x03U;
    telemetry_mode_loc_ = (telemetry_bits >> 2U) & 0x03U;
    telemetry_mode_env_ = (telemetry_bits >> 4U) & 0x03U;
    advert_loc_policy_ = advert_loc_policy;
    if (has_multi_acks)
    {
        auto cfg = phone_facade_.getMeshCorePhoneConfig();
        cfg.mesh.meshcore_multi_acks = (multi_acks != 0);
        phone_facade_.setMeshCorePhoneConfig(cfg);
        multi_acks_ = (multi_acks != 0) ? 1U : 0U;
        phone_facade_.saveConfig();
        phone_facade_.applyMeshConfig();
    }
    saveBlePin();
    return true;
}

bool MeshCoreBleService::setDevicePin(uint32_t pin)
{
    if (!isConfiguredBlePin(pin))
    {
        return false;
    }
    ble_pin_ = pin;
    if (kMeshCoreBleSecurityEnabled)
    {
        refreshBlePin();
    }
    saveBlePin();
    return true;
}

bool MeshCoreBleService::getCustomVars(std::string* out) const
{
    if (!out)
    {
        return false;
    }
    out->clear();
    char buf[40];
    phone_facade_.getCustomVars(out);
    std::snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(ble_pin_));
    appendCustomVar(*out, "ble_pin", buf);
    appendCustomVar(*out, "manual_add_contacts", manual_add_contacts_ ? "1" : "0");
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(telemetry_mode_base_));
    appendCustomVar(*out, "telemetry_base", buf);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(telemetry_mode_loc_));
    appendCustomVar(*out, "telemetry_loc", buf);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(telemetry_mode_env_));
    appendCustomVar(*out, "telemetry_env", buf);
    std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(advert_loc_policy_));
    appendCustomVar(*out, "advert_loc_policy", buf);
    return true;
}

bool MeshCoreBleService::setCustomVar(const char* key, const char* value)
{
    return handleCustomVarSet(key, value) || phone_facade_.setCustomVar(key, value);
}

void MeshCoreBleService::onFactoryReset()
{
    manual_contacts_.clear();
    saveManualContacts();
    ble_pin_ = 0;
    manual_add_contacts_ = false;
    telemetry_mode_base_ = 0;
    telemetry_mode_loc_ = 0;
    telemetry_mode_env_ = 0;
    advert_loc_policy_ = 0;
    advert_lat_ = 0;
    advert_lon_ = 0;
    if (kMeshCoreBleSecurityEnabled)
    {
        refreshBlePin();
    }
    saveBlePin();
    clearConnections();
    clearKnownPeerHashes();
    multi_acks_ = 0;
    clearPendingRequests();
    if (auto* adapter = meshCoreAdapter())
    {
        adapter->setFloodScopeKey(nullptr, 0);
    }
}

} // namespace ble
