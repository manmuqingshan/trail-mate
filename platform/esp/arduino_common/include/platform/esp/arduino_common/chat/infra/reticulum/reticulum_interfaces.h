/**
 * @file reticulum_interfaces.h
 * @brief Reticulum carrier interface set for device-side LXMF runtime
 */

#pragma once

#include "board/LoraBoard.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "platform/esp/arduino_common/chat/infra/rnode/rnode_adapter.h"
#include "sys/ringbuf.h"

#include <cstddef>
#include <cstdint>

#if __has_include(<WiFi.h>)
#include <WiFi.h>
#define TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE 1
#else
#define TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE 0
#endif

namespace chat::reticulum::interfaces
{

enum class InterfaceKind : uint8_t
{
    LoRa = 0,
    WifiGateway = 1,
};

struct RxPacket
{
    uint8_t data[reticulum::kReticulumMtu] = {};
    size_t len = 0;
    RxMeta rx_meta{};
    InterfaceKind interface_kind = InterfaceKind::LoRa;
};

struct TxResult
{
    bool lora_required = false;
    bool lora_ready = false;
    bool lora_ok = false;
    bool wifi_required = false;
    bool wifi_ready = false;
    bool wifi_ok = false;

    bool sent() const { return lora_ok || wifi_ok; }
    bool reachedRequiredInterfaces() const
    {
        return sent() &&
               (!lora_required || lora_ok) &&
               (!wifi_required || wifi_ok);
    }
};

class LoRaReticulumInterface
{
  public:
    explicit LoRaReticulumInterface(LoraBoard& board);

    void applyConfig(const MeshConfig& config);
    bool isReady() const;
    bool sendPacket(const uint8_t* data, size_t len);
    bool pollPacket(RxPacket* out);
    bool pollLegacyIncomingData(MeshIncomingData* out);
    void handleRawPacket(const uint8_t* data, size_t size);
    void setLastRxStats(float rssi, float snr);
    float lastRxRssi() const;
    float lastRxSnr() const;

  private:
    rnode::RNodeAdapter raw_;
    bool enabled_ = true;
};

class WifiGatewayReticulumInterface
{
  public:
    WifiGatewayReticulumInterface();

    void applyConfig(const MeshConfig& config);
    void maintain();
    bool isReady() const;
    bool isConfigured() const;
    bool sendPacket(const uint8_t* data, size_t len);
    bool pollPacket(RxPacket* out);
    const char* host() const { return host_; }
    uint16_t port() const { return port_; }

  private:
    struct QueuedPacket
    {
        uint8_t data[reticulum::kReticulumMtu] = {};
        size_t len = 0;
        RxMeta rx_meta{};
    };

    static constexpr size_t kRxQueueDepth = 4;
    static constexpr uint8_t kHdlcFlag = 0x7E;
    static constexpr uint8_t kHdlcEscape = 0x7D;
    static constexpr uint8_t kHdlcEscapeMask = 0x20;
    static constexpr uint32_t kReconnectIntervalMs = 10000;
    static constexpr uint32_t kWifiConnectIntervalMs = 60000;
    static constexpr uint32_t kRxStatsLogIntervalMs = 5000;
    static constexpr int32_t kSocketConnectTimeoutMs = 5000;

    bool enabled_ = false;
    bool auto_connect_wifi_ = true;
    char host_[kReticulumGatewayHostMaxLen + 1] = {};
    uint16_t port_ = 4242;
    bool socket_online_ = false;
    uint32_t last_reconnect_ms_ = 0;
    uint32_t last_wifi_connect_ms_ = 0;
    uint32_t last_socket_read_ms_ = 0;
    bool hdlc_in_frame_ = false;
    bool hdlc_escape_ = false;
    size_t hdlc_frame_len_ = 0;
    uint32_t rx_stats_last_log_ms_ = 0;
    uint32_t rx_stats_frames_ = 0;
    uint32_t rx_stats_drops_ = 0;
    uint32_t rx_stats_bytes_ = 0;
    uint32_t rx_stats_read_skips_ = 0;
    uint8_t hdlc_frame_[reticulum::kReticulumMtu] = {};
    uint8_t tx_frame_[(reticulum::kReticulumMtu * 2U) + 2U] = {};
    QueuedPacket poll_scratch_{};
    QueuedPacket enqueue_scratch_{};
    sys::RingBuffer<QueuedPacket, kRxQueueDepth> rx_queue_;

#if TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    WiFiClient client_;
#endif

    void stop();
    bool connected() const;
#if TRAIL_MATE_RETICULUM_WIFI_GATEWAY_AVAILABLE
    bool resolveHost(IPAddress* out);
#endif
    bool ensureSocket();
    void readAvailable();
    void feedHdlcByte(uint8_t byte);
    void enqueueFrame(const uint8_t* data, size_t len);
    void fillRxMeta(RxMeta* out) const;
};

class ReticulumInterfaceSet
{
  public:
    explicit ReticulumInterfaceSet(LoraBoard& board);

    void applyConfig(const MeshConfig& config);
    void maintain();
    bool hasReadyInterface() const;
    bool hasReadyWifiGateway() const;
    bool wifiGatewayConfigured() const;
    bool sendPacket(const uint8_t* data, size_t len);
    bool sendPacketWifiOnly(const uint8_t* data, size_t len);
    bool pollIncomingPacket(RxPacket* out);
    bool pollLegacyIncomingData(MeshIncomingData* out);
    void handleRawPacket(const uint8_t* data, size_t size);
    void setLastRxStats(float rssi, float snr);
    float lastRxRssi() const;
    float lastRxSnr() const;
    const RxMeta& lastRxMeta() const { return last_rx_meta_; }
    const TxResult& lastTxResult() const { return last_tx_result_; }

  private:
    LoRaReticulumInterface lora_;
    WifiGatewayReticulumInterface wifi_;
    MeshConfig config_{};
    RxMeta last_rx_meta_{};
    TxResult last_tx_result_{};
    bool has_last_rx_meta_ = false;
    uint8_t next_poll_index_ = 0;

    bool loraAllowed() const;
    bool wifiAllowed() const;
};

} // namespace chat::reticulum::interfaces
