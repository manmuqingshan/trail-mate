#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::idf_common::wireless_companion
{

enum class BleBackend : uint8_t
{
    None = 0,
    Local = 1,
    C6Companion = 2,
};

enum class CompanionState : uint8_t
{
    Unsupported = 0,
    NotStarted = 1,
    Missing = 2,
    TransportPending = 3,
    Present = 4,
    Error = 5,
};

enum class BleProfile : uint8_t
{
    None = 0,
    Meshtastic = 1,
    MeshCore = 2,
    TrailMate = 3,
};

enum class PairingMode : uint8_t
{
    Disabled = 0,
    RandomPin = 1,
    FixedPin = 2,
    NoPinDebugOnly = 3,
};

struct BleCompanionConfig
{
    bool enabled = true;
    bool meshtastic_enabled = true;
    bool meshcore_enabled = true;
    bool trailmate_enabled = true;
    PairingMode pairing_mode = PairingMode::FixedPin;
    bool fixed_pin_enabled = true;
    char fixed_pin[8] = "123456";
    char device_name[32] = "TrailMate-C6";
    uint16_t preferred_mtu = 247;
};

struct EspNowCompanionConfig
{
    bool enabled = false;
    bool team_discovery_enabled = false;
    uint8_t channel = 0;
    uint16_t beacon_interval_ms = 1000;
    uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
};

struct WifiCompanionConfig
{
    bool enabled = false;
    bool sta_enabled = false;
    bool ap_enabled = false;
    bool persist_credentials = false;
    char sta_ssid[32] = {};
    char sta_password[64] = {};
    char ap_ssid[32] = "TrailMate-C6";
    char ap_password[64] = {};
    uint8_t sta_channel = 0;
    uint8_t ap_channel = 1;
};

enum class WifiCommand : uint8_t
{
    Scan = 1,
    Connect = 2,
    Disconnect = 3,
    GetIp = 4,
    ApStart = 5,
    ApStop = 6,
};

struct WifiControl
{
    WifiCommand command = WifiCommand::Scan;
    uint8_t flags = 0;
    char ssid[32] = {};
    char password[64] = {};
    uint8_t channel = 0;
};

struct WifiScanResult
{
    char ssid[32] = {};
    int8_t rssi = -127;
    uint8_t channel = 0;
    uint8_t authmode = 0;
};

struct EspNowPacket
{
    uint8_t peer_mac[6] = {};
    bool rssi_valid = false;
    int8_t rssi = 0;
    uint8_t channel = 0;
    uint8_t payload_len = 0;
    uint8_t payload[240] = {};
};

struct C6CompanionStatus
{
    bool board_capable = false;
    bool started = false;
    bool present = false;
    CompanionState state = CompanionState::Unsupported;
    uint16_t protocol_min = 1;
    uint16_t protocol_max = 1;
    uint16_t selected_protocol = 0;
    uint32_t supported_features = 0;
    uint32_t firmware_version = 0;
    uint32_t free_heap = 0;
    uint32_t enabled_features = 0;
    uint32_t config_seq = 0;
    uint16_t config_error = 0;
    uint16_t selected_mtu = 0;
    uint8_t ble_state = 0;
    uint8_t espnow_state = 0;
    uint8_t wifi_state = 0;
    uint32_t ping_nonce = 0;
    uint32_t ping_count = 0;
    uint32_t pong_count = 0;
    uint32_t ble_uplink_count = 0;
    uint32_t ble_event_count = 0;
    uint32_t espnow_uplink_count = 0;
    uint32_t espnow_event_count = 0;
    uint32_t wifi_event_count = 0;
    bool wifi_connected = false;
    bool wifi_scanning = false;
    uint16_t wifi_error = 0;
    uint32_t wifi_ipv4_addr = 0;
    uint8_t wifi_scan_result_count = 0;
    char wifi_ssid[32] = {};
    WifiScanResult wifi_scan_results[6] = {};
    const char* detail = "unsupported";
};

class WirelessCompanion
{
  public:
    virtual bool begin() = 0;
    virtual bool isPresent() const = 0;
    virtual uint32_t capabilities() const = 0;
    virtual C6CompanionStatus status() const = 0;
    virtual bool configureBle(const BleCompanionConfig& config) = 0;
    virtual bool configureEspNow(const EspNowCompanionConfig& config) = 0;
    virtual bool configureWifi(const WifiCompanionConfig& config) = 0;
    virtual bool sendBleDownlink(BleProfile profile, const uint8_t* data, size_t len) = 0;
    virtual bool sendEspNow(const EspNowPacket& packet) = 0;
    virtual bool sendWifiControl(const WifiControl& control) = 0;
    virtual void poll() = 0;
    virtual ~WirelessCompanion() = default;
};

WirelessCompanion& c6_companion();
bool ensure_c6_companion_started();
C6CompanionStatus get_c6_companion_status();
const char* companion_state_name(CompanionState state);

} // namespace platform::esp::idf_common::wireless_companion
