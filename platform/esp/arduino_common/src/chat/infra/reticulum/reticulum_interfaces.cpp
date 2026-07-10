/**
 * @file reticulum_interfaces.cpp
 * @brief Reticulum carrier interface set for device-side LXMF runtime
 */

#include "platform/esp/arduino_common/chat/infra/reticulum/reticulum_interfaces.h"

#include "chat/time_utils.h"
#include "platform/ui/wifi_access_runtime.h"
#include "platform/ui/wifi_runtime.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <lwip/netdb.h>

namespace chat::reticulum::interfaces
{

namespace
{

const char* boolLabel(bool value)
{
    return value ? "true" : "false";
}

bool copyHost(char* out, size_t out_len, const char* value)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (!value)
    {
        return true;
    }
    std::strncpy(out, value, out_len - 1);
    out[out_len - 1] = '\0';
    return true;
}

void fillTimestamp(RxMeta* out)
{
    if (!out)
    {
        return;
    }
    out->rx_timestamp_ms = millis();
    const uint32_t epoch_s = now_epoch_seconds();
    if (is_valid_epoch(epoch_s))
    {
        out->rx_timestamp_s = epoch_s;
        out->time_source = RxTimeSource::DeviceUtc;
    }
    else
    {
        out->rx_timestamp_s = out->rx_timestamp_ms / 1000U;
        out->time_source = RxTimeSource::Uptime;
    }
}

} // namespace

LoRaReticulumInterface::LoRaReticulumInterface(LoraBoard& board)
    : raw_(board)
{
}

void LoRaReticulumInterface::applyConfig(const MeshConfig& config)
{
    enabled_ = config.reticulum_lora_enabled &&
               config.reticulum_interface_policy != ReticulumInterfacePolicy::WifiGatewayOnly;
    if (enabled_)
    {
        raw_.applyConfig(config);
    }
    Serial.printf("[Reticulum][IF][LoRa] enabled=%s ready=%s\n",
                  boolLabel(enabled_),
                  boolLabel(isReady()));
}

bool LoRaReticulumInterface::isReady() const
{
    return enabled_ && raw_.isReady();
}

bool LoRaReticulumInterface::sendPacket(const uint8_t* data, size_t len)
{
    if (!isReady())
    {
        return false;
    }
    return raw_.sendAppData(ChannelId::PRIMARY, 0, data, len);
}

bool LoRaReticulumInterface::pollPacket(RxPacket* out)
{
    if (!out || !enabled_)
    {
        return false;
    }

    size_t len = 0;
    if (!raw_.pollIncomingRawPacket(out->data, len, sizeof(out->data)))
    {
        return false;
    }

    out->len = len;
    out->interface_kind = InterfaceKind::LoRa;
    fillTimestamp(&out->rx_meta);
    out->rx_meta.origin = RxOrigin::LoRa;
    out->rx_meta.direct = true;
    out->rx_meta.from_is = false;
    out->rx_meta.rssi_dbm_x10 = static_cast<int16_t>(std::lround(raw_.lastRxRssi() * 10.0f));
    out->rx_meta.snr_db_x10 = static_cast<int16_t>(std::lround(raw_.lastRxSnr() * 10.0f));
    return true;
}

bool LoRaReticulumInterface::pollLegacyIncomingData(MeshIncomingData* out)
{
    return enabled_ && raw_.pollIncomingData(out);
}

void LoRaReticulumInterface::handleRawPacket(const uint8_t* data, size_t size)
{
    if (enabled_)
    {
        raw_.handleRawPacket(data, size);
    }
}

void LoRaReticulumInterface::setLastRxStats(float rssi, float snr)
{
    raw_.setLastRxStats(rssi, snr);
}

float LoRaReticulumInterface::lastRxRssi() const
{
    return raw_.lastRxRssi();
}

float LoRaReticulumInterface::lastRxSnr() const
{
    return raw_.lastRxSnr();
}

WifiGatewayReticulumInterface::WifiGatewayReticulumInterface() = default;

void WifiGatewayReticulumInterface::applyConfig(const MeshConfig& config)
{
    const bool next_enabled = config.reticulum_wifi_gateway_enabled &&
                              config.reticulum_interface_policy != ReticulumInterfacePolicy::LoRaOnly;
    char next_host[kReticulumGatewayHostMaxLen + 1] = {};
    copyHost(next_host, sizeof(next_host), config.reticulum_wifi_gateway_host);
    const uint16_t next_port =
        config.reticulum_wifi_gateway_port != 0 ? config.reticulum_wifi_gateway_port : 4242;

    const bool changed = next_enabled != enabled_ ||
                         next_port != port_ ||
                         std::strcmp(next_host, host_) != 0;

    enabled_ = next_enabled;
    auto_connect_wifi_ = config.reticulum_wifi_auto_connect;
    copyHost(host_, sizeof(host_), next_host);
    port_ = next_port;

    if (changed)
    {
        stop();
        last_reconnect_ms_ = 0;
        hdlc_in_frame_ = false;
        hdlc_escape_ = false;
        hdlc_frame_len_ = 0;
        rx_queue_.clear();
    }

    Serial.printf("[Reticulum][IF][WiFi] enabled=%s host=%s port=%u auto_wifi=%s available=%s\n",
                  boolLabel(enabled_),
                  host_[0] != '\0' ? host_ : "<unset>",
                  static_cast<unsigned>(port_),
                  boolLabel(auto_connect_wifi_),
                  boolLabel(TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE != 0));
}

void WifiGatewayReticulumInterface::maintain()
{
    if (!enabled_)
    {
        stop();
        return;
    }

    if (host_[0] == '\0')
    {
        stop();
        return;
    }

    if (connected())
    {
        readAvailable();
        return;
    }

    (void)ensureSocket();
}

bool WifiGatewayReticulumInterface::isReady() const
{
    return enabled_ && host_[0] != '\0' && socket_online_;
}

bool WifiGatewayReticulumInterface::isConfigured() const
{
    return enabled_ && host_[0] != '\0';
}

bool WifiGatewayReticulumInterface::sendPacket(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > reticulum::kReticulumMtu)
    {
        return false;
    }
    if (!isReady())
    {
        maintain();
    }
    if (!isReady())
    {
        return false;
    }
    const auto budget = platform::ui::wifi_access::traffic_budget(
        platform::ui::wifi_access::Client::ReticulumGateway,
        platform::ui::wifi_access::Priority::Messaging);
    if (!budget.allow_write || budget.tx_byte_budget == 0)
    {
        return false;
    }

    size_t tx_len = 0;
    tx_frame_[tx_len++] = kHdlcFlag;
    for (size_t i = 0; i < len && tx_len + 2U < sizeof(tx_frame_); ++i)
    {
        const uint8_t byte = data[i];
        if (byte == kHdlcFlag || byte == kHdlcEscape)
        {
            tx_frame_[tx_len++] = kHdlcEscape;
            tx_frame_[tx_len++] = byte ^ kHdlcEscapeMask;
        }
        else
        {
            tx_frame_[tx_len++] = byte;
        }
    }
    if (tx_len + 1U > sizeof(tx_frame_))
    {
        return false;
    }
    tx_frame_[tx_len++] = kHdlcFlag;

#if TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    const size_t written = client_.write(tx_frame_, tx_len);
    if (written != tx_len)
    {
        Serial.printf("[Reticulum][IF][WiFi][TX] failed raw_len=%u frame_len=%u written=%u\n",
                      static_cast<unsigned>(len),
                      static_cast<unsigned>(tx_len),
                      static_cast<unsigned>(written));
        stop();
        return false;
    }
    Serial.printf("[Reticulum][IF][WiFi][TX] ok raw_len=%u frame_len=%u host=%s:%u\n",
                  static_cast<unsigned>(len),
                  static_cast<unsigned>(tx_len),
                  host_,
                  static_cast<unsigned>(port_));
    return true;
#else
    (void)tx_len;
    return false;
#endif
}

bool WifiGatewayReticulumInterface::pollPacket(RxPacket* out)
{
    if (!out)
    {
        return false;
    }
    maintain();

    poll_scratch_ = QueuedPacket{};
    if (!rx_queue_.popOldest(&poll_scratch_))
    {
        return false;
    }

    std::memcpy(out->data, poll_scratch_.data, poll_scratch_.len);
    out->len = poll_scratch_.len;
    out->rx_meta = poll_scratch_.rx_meta;
    out->interface_kind = InterfaceKind::WifiGateway;
    return true;
}

void WifiGatewayReticulumInterface::stop()
{
#if TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    client_.stop();
#endif
    socket_online_ = false;
}

bool WifiGatewayReticulumInterface::connected() const
{
    return socket_online_;
}

#if TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
bool WifiGatewayReticulumInterface::resolveHost(IPAddress* out)
{
    if (!out || host_[0] == '\0')
    {
        return false;
    }

    IPAddress parsed{};
    if (parsed.fromString(host_))
    {
        *out = parsed;
        return true;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* results = nullptr;
    const int err = getaddrinfo(host_, nullptr, &hints, &results);
    if (err != 0 || !results)
    {
        Serial.printf("[Reticulum][IF][WiFi] gateway resolve failed host=%s err=%d\n",
                      host_,
                      err);
        if (results)
        {
            freeaddrinfo(results);
        }
        return false;
    }

    bool resolved = false;
    for (addrinfo* item = results; item != nullptr; item = item->ai_next)
    {
        if (item->ai_family != AF_INET || !item->ai_addr)
        {
            continue;
        }

        const auto* addr = reinterpret_cast<const sockaddr_in*>(item->ai_addr);
        uint8_t bytes[4] = {};
        std::memcpy(bytes, &addr->sin_addr.s_addr, sizeof(bytes));
        *out = IPAddress(bytes);
        resolved = true;
        break;
    }

    freeaddrinfo(results);
    if (!resolved)
    {
        Serial.printf("[Reticulum][IF][WiFi] gateway resolve no_ipv4 host=%s\n", host_);
    }
    return resolved;
}
#endif

bool WifiGatewayReticulumInterface::ensureSocket()
{
    if (!enabled_ || host_[0] == '\0')
    {
        return false;
    }

#if !TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    return false;
#else
    const uint32_t now_ms = millis();
    if (last_reconnect_ms_ != 0 &&
        (now_ms - last_reconnect_ms_) < kReconnectIntervalMs)
    {
        return false;
    }

    platform::ui::wifi::Status wifi_status = platform::ui::wifi::status();
    if (!wifi_status.connected)
    {
        if (auto_connect_wifi_ &&
            (last_wifi_connect_ms_ == 0 ||
             (now_ms - last_wifi_connect_ms_) >= kWifiConnectIntervalMs))
        {
            last_wifi_connect_ms_ = now_ms;
            Serial.printf("[Reticulum][IF][WiFi] requesting station before gateway host=%s:%u\n",
                          host_,
                          static_cast<unsigned>(port_));
            platform::ui::wifi_access::Request request{};
            request.client = platform::ui::wifi_access::Client::ReticulumGateway;
            request.kind = platform::ui::wifi_access::AccessKind::WifiConnect;
            request.priority = platform::ui::wifi_access::Priority::Messaging;
            request.allow_connect = true;
            request.reason = "reticulum_gateway";
            platform::ui::wifi_access::Decision decision =
                platform::ui::wifi_access::Decision::Granted;
            if (!platform::ui::wifi_access::ensure_connected(request, &decision))
            {
                Serial.printf("[Reticulum][IF][WiFi] station denied decision=%s host=%s:%u\n",
                              platform::ui::wifi_access::decision_name(decision),
                              host_,
                              static_cast<unsigned>(port_));
            }
            wifi_status = platform::ui::wifi::status();
        }

        if (!wifi_status.connected)
        {
            last_reconnect_ms_ = now_ms;
            return false;
        }
    }

    last_reconnect_ms_ = now_ms;
    client_.stop();

    IPAddress remote_ip{};
    if (!resolveHost(&remote_ip))
    {
        socket_online_ = false;
        client_.stop();
        return false;
    }

    platform::ui::wifi_access::Request socket_request{};
    socket_request.client = platform::ui::wifi_access::Client::ReticulumGateway;
    socket_request.kind = platform::ui::wifi_access::AccessKind::LongLivedSocket;
    socket_request.priority = platform::ui::wifi_access::Priority::Messaging;
    socket_request.reason = "reticulum_gateway_socket";
    const auto lease = platform::ui::wifi_access::acquire(socket_request);
    if (!lease.granted)
    {
        socket_online_ = false;
        client_.stop();
        Serial.printf("[Reticulum][IF][WiFi] gateway connect deferred decision=%s host=%s port=%u\n",
                      platform::ui::wifi_access::decision_name(lease.decision),
                      host_,
                      static_cast<unsigned>(port_));
        return false;
    }

    Serial.printf("[Reticulum][IF][WiFi] gateway connect host=%s port=%u\n",
                  host_,
                  static_cast<unsigned>(port_));
    if (!client_.connect(remote_ip, port_, kSocketConnectTimeoutMs))
    {
        socket_online_ = false;
        client_.stop();
        Serial.printf("[Reticulum][IF][WiFi] gateway connect failed host=%s port=%u\n",
                      host_,
                      static_cast<unsigned>(port_));
        return false;
    }

    client_.setNoDelay(true);
    socket_online_ = true;
    hdlc_in_frame_ = false;
    hdlc_escape_ = false;
    hdlc_frame_len_ = 0;
    Serial.printf("[Reticulum][IF][WiFi] gateway connected host=%s port=%u\n",
                  host_,
                  static_cast<unsigned>(port_));
    return true;
#endif
}

void WifiGatewayReticulumInterface::readAvailable()
{
#if TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    if (!socket_online_)
    {
        return;
    }

    if (!client_.connected())
    {
        Serial.printf("[Reticulum][IF][WiFi] gateway disconnected host=%s port=%u\n",
                      host_,
                      static_cast<unsigned>(port_));
        stop();
        return;
    }

    int remaining = client_.available();
    const uint32_t now_ms = millis();
    const auto budget = platform::ui::wifi_access::traffic_budget(
        platform::ui::wifi_access::Client::ReticulumGateway,
        platform::ui::wifi_access::Priority::Messaging);
    if (!budget.allow_read || budget.rx_byte_budget == 0)
    {
        if (remaining > 0)
        {
            ++rx_stats_read_skips_;
        }
        if (!budget.allow_connect && !budget.allow_write)
        {
            stop();
            last_reconnect_ms_ = now_ms;
        }
        return;
    }
    if (remaining > 0 &&
        last_socket_read_ms_ != 0 &&
        (now_ms - last_socket_read_ms_) < budget.min_read_interval_ms)
    {
        ++rx_stats_read_skips_;
        return;
    }
    if (remaining > 0)
    {
        last_socket_read_ms_ = now_ms;
    }

    int processed = 0;
    while (remaining > 0 && processed < static_cast<int>(budget.rx_byte_budget))
    {
        const int value = client_.read();
        if (value < 0)
        {
            break;
        }
        feedHdlcByte(static_cast<uint8_t>(value));
        ++processed;
        --remaining;
    }
    rx_stats_bytes_ += static_cast<uint32_t>(processed);
#endif
}

void WifiGatewayReticulumInterface::feedHdlcByte(uint8_t byte)
{
    if (byte == kHdlcFlag)
    {
        if (hdlc_in_frame_ && hdlc_frame_len_ > 0)
        {
            enqueueFrame(hdlc_frame_, hdlc_frame_len_);
        }
        hdlc_in_frame_ = true;
        hdlc_escape_ = false;
        hdlc_frame_len_ = 0;
        return;
    }

    if (!hdlc_in_frame_)
    {
        return;
    }

    if (byte == kHdlcEscape)
    {
        hdlc_escape_ = true;
        return;
    }

    if (hdlc_escape_)
    {
        byte ^= kHdlcEscapeMask;
        hdlc_escape_ = false;
    }

    if (hdlc_frame_len_ >= sizeof(hdlc_frame_))
    {
        Serial.printf("[Reticulum][IF][WiFi][RX] drop reason=frame_too_large len=%u\n",
                      static_cast<unsigned>(hdlc_frame_len_));
        hdlc_in_frame_ = false;
        hdlc_escape_ = false;
        hdlc_frame_len_ = 0;
        return;
    }

    hdlc_frame_[hdlc_frame_len_++] = byte;
}

void WifiGatewayReticulumInterface::enqueueFrame(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > reticulum::kReticulumMtu)
    {
        return;
    }

    enqueue_scratch_ = QueuedPacket{};
    std::memcpy(enqueue_scratch_.data, data, len);
    enqueue_scratch_.len = len;
    fillRxMeta(&enqueue_scratch_.rx_meta);
    bool dropped = false;
    rx_queue_.append(enqueue_scratch_, &dropped);

    ++rx_stats_frames_;
    if (dropped)
    {
        ++rx_stats_drops_;
    }
    const uint32_t now_ms = millis();
    if (rx_stats_last_log_ms_ == 0 ||
        (now_ms - rx_stats_last_log_ms_) >= kRxStatsLogIntervalMs)
    {
        Serial.printf("[Reticulum][IF][WiFi][RX] stats frames=%u drops=%u bytes=%u read_skips=%u depth=%u last_len=%u\n",
                      static_cast<unsigned>(rx_stats_frames_),
                      static_cast<unsigned>(rx_stats_drops_),
                      static_cast<unsigned>(rx_stats_bytes_),
                      static_cast<unsigned>(rx_stats_read_skips_),
                      static_cast<unsigned>(rx_queue_.size()),
                      static_cast<unsigned>(len));
        rx_stats_last_log_ms_ = now_ms;
        rx_stats_frames_ = 0;
        rx_stats_drops_ = 0;
        rx_stats_bytes_ = 0;
        rx_stats_read_skips_ = 0;
    }
}

void WifiGatewayReticulumInterface::fillRxMeta(RxMeta* out) const
{
    if (!out)
    {
        return;
    }
    fillTimestamp(out);
    out->origin = RxOrigin::WiFi;
    out->direct = true;
    out->from_is = false;
    out->rssi_dbm_x10 = 0;
    out->snr_db_x10 = 0;
    out->freq_hz = 0;
    out->bw_hz = 0;
    out->sf = 0;
    out->cr = 0;
}

ReticulumInterfaceSet::ReticulumInterfaceSet(LoraBoard& board)
    : lora_(board)
{
}

void ReticulumInterfaceSet::applyConfig(const MeshConfig& config)
{
    config_ = config;
    lora_.applyConfig(config_);
    wifi_.applyConfig(config_);
    maintain();
}

void ReticulumInterfaceSet::maintain()
{
    if (wifiAllowed())
    {
        wifi_.maintain();
    }
}

bool ReticulumInterfaceSet::hasReadyInterface() const
{
    return (loraAllowed() && lora_.isReady()) ||
           (wifiAllowed() && wifi_.isReady());
}

bool ReticulumInterfaceSet::hasReadyWifiGateway() const
{
    return wifiAllowed() && wifi_.isReady();
}

bool ReticulumInterfaceSet::wifiGatewayConfigured() const
{
    return wifiAllowed() && wifi_.isConfigured();
}

bool ReticulumInterfaceSet::sendPacket(const uint8_t* data, size_t len)
{
    last_tx_result_ = {};
    if (!data || len == 0)
    {
        return false;
    }

    maintain();

    last_tx_result_.lora_required = loraAllowed();
    last_tx_result_.lora_ready = last_tx_result_.lora_required && lora_.isReady();
    last_tx_result_.wifi_required = wifiAllowed() && wifi_.isConfigured();
    last_tx_result_.wifi_ready = last_tx_result_.wifi_required && wifi_.isReady();

    if (last_tx_result_.lora_ready)
    {
        last_tx_result_.lora_ok = lora_.sendPacket(data, len);
    }
    if (last_tx_result_.wifi_ready)
    {
        last_tx_result_.wifi_ok = wifi_.sendPacket(data, len);
    }

    const bool sent = last_tx_result_.sent();
    Serial.printf("[Reticulum][IF][TX] raw_len=%u lora_req=%u lora_ready=%u lora=%u wifi_req=%u wifi_ready=%u wifi=%u sent=%u\n",
                  static_cast<unsigned>(len),
                  last_tx_result_.lora_required ? 1U : 0U,
                  last_tx_result_.lora_ready ? 1U : 0U,
                  last_tx_result_.lora_ok ? 1U : 0U,
                  last_tx_result_.wifi_required ? 1U : 0U,
                  last_tx_result_.wifi_ready ? 1U : 0U,
                  last_tx_result_.wifi_ok ? 1U : 0U,
                  sent ? 1U : 0U);
    return sent;
}

bool ReticulumInterfaceSet::sendPacketWifiOnly(const uint8_t* data, size_t len)
{
    last_tx_result_ = {};
    if (!data || len == 0)
    {
        return false;
    }

    maintain();

    last_tx_result_.wifi_required = wifiAllowed() && wifi_.isConfigured();
    last_tx_result_.wifi_ready = last_tx_result_.wifi_required && wifi_.isReady();
    if (last_tx_result_.wifi_ready)
    {
        last_tx_result_.wifi_ok = wifi_.sendPacket(data, len);
    }

    const bool sent = last_tx_result_.sent();
    Serial.printf("[Reticulum][IF][TX] raw_len=%u mode=wifi_only wifi_req=%u wifi_ready=%u wifi=%u sent=%u\n",
                  static_cast<unsigned>(len),
                  last_tx_result_.wifi_required ? 1U : 0U,
                  last_tx_result_.wifi_ready ? 1U : 0U,
                  last_tx_result_.wifi_ok ? 1U : 0U,
                  sent ? 1U : 0U);
    return sent;
}

bool ReticulumInterfaceSet::pollIncomingPacket(RxPacket* out)
{
    if (!out)
    {
        return false;
    }

    maintain();

    for (uint8_t i = 0; i < 2; ++i)
    {
        const uint8_t index = static_cast<uint8_t>((next_poll_index_ + i) % 2U);
        bool got = false;
        if (index == 0)
        {
            got = loraAllowed() && lora_.pollPacket(out);
        }
        else
        {
            got = wifiAllowed() && wifi_.pollPacket(out);
        }
        if (got)
        {
            next_poll_index_ = static_cast<uint8_t>((index + 1U) % 2U);
            last_rx_meta_ = out->rx_meta;
            has_last_rx_meta_ = true;
            return true;
        }
    }

    return false;
}

bool ReticulumInterfaceSet::pollLegacyIncomingData(MeshIncomingData* out)
{
    return lora_.pollLegacyIncomingData(out);
}

void ReticulumInterfaceSet::handleRawPacket(const uint8_t* data, size_t size)
{
    if (loraAllowed())
    {
        lora_.handleRawPacket(data, size);
    }
}

void ReticulumInterfaceSet::setLastRxStats(float rssi, float snr)
{
    lora_.setLastRxStats(rssi, snr);
}

float ReticulumInterfaceSet::lastRxRssi() const
{
    if (has_last_rx_meta_)
    {
        return static_cast<float>(last_rx_meta_.rssi_dbm_x10) / 10.0f;
    }
    return lora_.lastRxRssi();
}

float ReticulumInterfaceSet::lastRxSnr() const
{
    if (has_last_rx_meta_)
    {
        return static_cast<float>(last_rx_meta_.snr_db_x10) / 10.0f;
    }
    return lora_.lastRxSnr();
}

bool ReticulumInterfaceSet::loraAllowed() const
{
    return config_.reticulum_lora_enabled &&
           config_.reticulum_interface_policy != ReticulumInterfacePolicy::WifiGatewayOnly;
}

bool ReticulumInterfaceSet::wifiAllowed() const
{
    return config_.reticulum_wifi_gateway_enabled &&
           config_.reticulum_interface_policy != ReticulumInterfacePolicy::LoRaOnly;
}

} // namespace chat::reticulum::interfaces
