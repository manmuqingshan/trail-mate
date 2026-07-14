#pragma once

#include <cstdint>

namespace platform::ui::wifi_access
{

constexpr std::uint8_t kCallLinkIdSize = 16;

enum class Client : std::uint8_t
{
    Unknown = 0,
    FirmwareUpdate,
    PackRepository,
    RouteStorage,
    MeshMqtt,
    ReticulumGateway,
};

enum class AccessKind : std::uint8_t
{
    WifiConnect = 0,
    LongLivedSocket,
    HttpMetadata,
    HttpDownload,
    OtaDownload,
    ReticulumGatewayCallAudio,
    ReticulumGatewayCallControl,
};

enum class Priority : std::uint8_t
{
    Messaging = 0,
    UserForeground,
    Maintenance,
    Background,
};

enum class ScreenPhase : std::uint8_t
{
    Unknown = 0,
    ScreenOn,
    WakeProtected,
    ScreenOff,
};

enum class Decision : std::uint8_t
{
    Granted = 0,
    InvalidRequest,
    WifiUnavailable,
    WifiDisabled,
    WifiNoCredentials,
    WifiDisconnected,
    ConnectDeferredForWake,
    ConnectBackoff,
    ConnectFailed,
    Busy,
    DeferredForWake,
    DeferredForScreenOn,
    OtaExclusive,
    CallExclusive,
    NonPreemptibleBusy,
};

enum class ExclusiveOwner : std::uint8_t
{
    None = 0,
    ReticulumCallAudio,
};

enum class CallPreemptPhase : std::uint8_t
{
    Idle = 0,
    RingingSoft,
    Exclusive,
    ClosingSoft,
    ClosingExclusive,
};

struct Request
{
    Client client = Client::Unknown;
    AccessKind kind = AccessKind::WifiConnect;
    Priority priority = Priority::Background;
    bool allow_connect = false;
    const std::uint8_t* call_link_id = nullptr;
    const char* reason = nullptr;
};

struct Lease
{
    bool granted = false;
    bool release_required = false;
    Client client = Client::Unknown;
    AccessKind kind = AccessKind::WifiConnect;
    Decision decision = Decision::InvalidRequest;
    ScreenPhase screen_phase = ScreenPhase::Unknown;
    std::uint32_t granted_ms = 0;
    std::uint32_t revoke_generation = 0;
    bool call_audio = false;
    std::uint8_t call_link_id[kCallLinkIdSize] = {};
};

struct TrafficBudget
{
    bool allow_connect = false;
    bool allow_read = false;
    bool allow_write = false;
    std::uint16_t rx_packet_budget = 0;
    std::uint16_t tx_packet_budget = 0;
    std::uint16_t rx_byte_budget = 0;
    std::uint16_t tx_byte_budget = 0;
    std::uint16_t min_read_interval_ms = 0;
    ScreenPhase screen_phase = ScreenPhase::Unknown;
};

struct RuntimeStatus
{
    bool http_active = false;
    bool ota_active = false;
    bool foreground_download_pending = false;
    Client owner = Client::Unknown;
    AccessKind active_kind = AccessKind::WifiConnect;
    Client pending_owner = Client::Unknown;
    AccessKind pending_kind = AccessKind::WifiConnect;
    ScreenPhase screen_phase = ScreenPhase::Unknown;
    std::uint32_t active_since_ms = 0;
    std::uint32_t pending_since_ms = 0;
    std::uint32_t wake_protected_until_ms = 0;
    std::uint32_t revoke_generation = 0;
    CallPreemptPhase call_phase = CallPreemptPhase::Idle;
    ExclusiveOwner exclusive_owner = ExclusiveOwner::None;
    bool non_preemptible_active = false;
    const char* non_preemptible_reason = nullptr;
};

bool enter_call_ringing(const std::uint8_t link_id[kCallLinkIdSize]);
bool enter_call_exclusive(const std::uint8_t link_id[kCallLinkIdSize]);
void enter_call_closing(const std::uint8_t link_id[kCallLinkIdSize],
                        bool keep_exclusive);
void exit_call(const std::uint8_t link_id[kCallLinkIdSize]);
void set_non_preemptible_activity(bool active, const char* reason = nullptr);
bool call_soft_preempt_active();
bool call_exclusive_active();
bool call_accept_available();

bool set_transport_enabled(bool enabled);
bool ensure_connected(const Request& request, Decision* out_decision = nullptr);
Lease acquire(const Request& request);
void release(const Lease& lease);
bool lease_revoked(const Lease& lease);
bool begin_foreground_download(const Request& request, Decision* out_decision = nullptr);
void end_foreground_download(Client client, AccessKind kind);
TrafficBudget traffic_budget(Client client,
                             Priority priority,
                             const std::uint8_t* call_link_id = nullptr,
                             AccessKind access_kind = AccessKind::LongLivedSocket);
RuntimeStatus status();

const char* client_name(Client client);
const char* access_kind_name(AccessKind kind);
const char* priority_name(Priority priority);
const char* screen_phase_name(ScreenPhase phase);
const char* decision_name(Decision decision);
const char* call_preempt_phase_name(CallPreemptPhase phase);
const char* exclusive_owner_name(ExclusiveOwner owner);

} // namespace platform::ui::wifi_access
