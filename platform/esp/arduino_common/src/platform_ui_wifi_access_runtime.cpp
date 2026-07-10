#include "platform/ui/wifi_access_runtime.h"

#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "sys/clock.h"

#include <cstddef>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace platform::ui::wifi_access
{
namespace
{

constexpr std::uint32_t kWakeProtectionMs = 6000;
constexpr std::uint32_t kConnectBackoffMs = 10000;
constexpr std::uint32_t kForegroundDownloadSettleMs = 1500;
constexpr std::size_t kClientCount = 6;

struct RuntimeState
{
    bool http_active = false;
    bool ota_active = false;
    Client owner = Client::Unknown;
    AccessKind active_kind = AccessKind::WifiConnect;
    std::uint32_t active_since_ms = 0;
    std::uint32_t foreground_download_settle_until_ms = 0;
    std::uint32_t wake_protected_until_ms = 0;
    bool saw_screen_sample = false;
    bool last_sleeping = false;
    bool last_saver = false;
    std::uint32_t last_connect_attempt_ms[kClientCount] = {};
};

RuntimeState s_state{};
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

bool is_http_kind(AccessKind kind)
{
    return kind == AccessKind::HttpMetadata ||
           kind == AccessKind::HttpDownload ||
           kind == AccessKind::OtaDownload;
}

std::size_t client_index(Client client)
{
    const std::size_t index = static_cast<std::size_t>(client);
    return index < kClientCount ? index : 0;
}

ScreenPhase sample_screen_phase(std::uint32_t now_ms)
{
    const bool sleeping = ::platform::ui::screen::is_sleeping();
    const bool saver = ::platform::ui::screen::is_saver_active();
    std::uint32_t wake_until = 0;

    portENTER_CRITICAL(&s_lock);
    if (s_state.saw_screen_sample &&
        (s_state.last_sleeping || s_state.last_saver) &&
        !sleeping &&
        !saver)
    {
        s_state.wake_protected_until_ms = now_ms + kWakeProtectionMs;
    }
    s_state.saw_screen_sample = true;
    s_state.last_sleeping = sleeping;
    s_state.last_saver = saver;
    wake_until = s_state.wake_protected_until_ms;
    portEXIT_CRITICAL(&s_lock);

    if (saver)
    {
        return ScreenPhase::WakeProtected;
    }
    if (sleeping)
    {
        return ScreenPhase::ScreenOff;
    }
    if (static_cast<std::int32_t>(wake_until - now_ms) > 0)
    {
        return ScreenPhase::WakeProtected;
    }
    return ScreenPhase::ScreenOn;
}

bool mark_connect_attempt(Client client, std::uint32_t now_ms)
{
    const std::size_t index = client_index(client);
    bool allowed = false;
    portENTER_CRITICAL(&s_lock);
    const std::uint32_t last = s_state.last_connect_attempt_ms[index];
    if (last == 0 || (now_ms - last) >= kConnectBackoffMs)
    {
        s_state.last_connect_attempt_ms[index] = now_ms;
        allowed = true;
    }
    portEXIT_CRITICAL(&s_lock);
    return allowed;
}

bool ota_active_for_other(const Request& request)
{
    bool active = false;
    portENTER_CRITICAL(&s_lock);
    active = s_state.ota_active &&
             !(s_state.owner == request.client && s_state.active_kind == request.kind);
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool foreground_download_owner(Client client)
{
    return client == Client::RouteStorage;
}

bool messaging_client(Client client)
{
    return client == Client::MeshMqtt || client == Client::ReticulumGateway;
}

bool foreground_download_active_for_other(const Request& request)
{
    bool active = false;
    portENTER_CRITICAL(&s_lock);
    active = s_state.http_active &&
             !s_state.ota_active &&
             s_state.active_kind == AccessKind::HttpDownload &&
             foreground_download_owner(s_state.owner) &&
             !(s_state.owner == request.client && s_state.active_kind == request.kind);
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool foreground_download_settle_active_for(Client client, std::uint32_t now_ms)
{
    if (!messaging_client(client))
    {
        return false;
    }
    std::uint32_t settle_until_ms = 0;
    portENTER_CRITICAL(&s_lock);
    settle_until_ms = s_state.foreground_download_settle_until_ms;
    portEXIT_CRITICAL(&s_lock);
    return static_cast<std::int32_t>(settle_until_ms - now_ms) > 0;
}

bool acquire_http(const Request& request, ScreenPhase phase, Lease& out)
{
    if (::platform::ui::reticulum_call::realtime_mode_active())
    {
        out.decision = Decision::CallExclusive;
        return false;
    }
    if (phase == ScreenPhase::WakeProtected)
    {
        out.decision = Decision::DeferredForWake;
        return false;
    }
    if (phase == ScreenPhase::ScreenOn &&
        request.priority == Priority::Background)
    {
        out.decision = Decision::DeferredForScreenOn;
        return false;
    }

    const std::uint32_t now_ms = sys::millis_now();
    portENTER_CRITICAL(&s_lock);
    if (s_state.http_active)
    {
        out.decision = s_state.ota_active ? Decision::OtaExclusive : Decision::Busy;
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    s_state.http_active = true;
    s_state.ota_active = request.kind == AccessKind::OtaDownload;
    s_state.owner = request.client;
    s_state.active_kind = request.kind;
    s_state.active_since_ms = now_ms;
    portEXIT_CRITICAL(&s_lock);

    out.granted = true;
    out.release_required = true;
    out.decision = Decision::Granted;
    out.granted_ms = now_ms;
    std::printf("[WiFiAccess] acquire client=%s kind=%s priority=%s phase=%s reason=%s\n",
                client_name(request.client),
                access_kind_name(request.kind),
                priority_name(request.priority),
                screen_phase_name(phase),
                request.reason ? request.reason : "");
    return true;
}

bool long_lived_allowed(const Request& request, ScreenPhase phase, Lease& out)
{
    (void)phase;
    if (request.client == Client::Unknown)
    {
        out.decision = Decision::InvalidRequest;
        return false;
    }
    if (::platform::ui::reticulum_call::realtime_mode_active() &&
        request.client != Client::ReticulumGateway)
    {
        out.decision = Decision::CallExclusive;
        return false;
    }
    if (ota_active_for_other(request))
    {
        out.decision = Decision::OtaExclusive;
        return false;
    }
    if (foreground_download_active_for_other(request) &&
        messaging_client(request.client))
    {
        out.decision = Decision::Busy;
        return false;
    }
    if (foreground_download_settle_active_for(request.client, sys::millis_now()))
    {
        out.decision = Decision::Busy;
        return false;
    }
    out.granted = true;
    out.decision = Decision::Granted;
    out.granted_ms = sys::millis_now();
    return true;
}

TrafficBudget base_budget(Client client, ScreenPhase phase)
{
    TrafficBudget budget{};
    budget.screen_phase = phase;

    if (::platform::ui::reticulum_call::realtime_mode_active())
    {
        if (client == Client::ReticulumGateway)
        {
            budget.allow_connect = true;
            budget.allow_read = true;
            budget.allow_write = true;
            budget.rx_packet_budget = 0;
            budget.tx_packet_budget = 8;
            budget.rx_byte_budget = 768;
            budget.tx_byte_budget = 1024;
            budget.min_read_interval_ms = 0;
        }
        return budget;
    }

    if (phase == ScreenPhase::WakeProtected)
    {
        budget.allow_connect = false;
        budget.allow_read = true;
        budget.allow_write = true;
        budget.rx_packet_budget = client == Client::MeshMqtt ? 1 : 0;
        budget.tx_packet_budget = 1;
        budget.rx_byte_budget = client == Client::ReticulumGateway ? 64 : 512;
        budget.tx_byte_budget = 512;
        budget.min_read_interval_ms = client == Client::ReticulumGateway ? 500 : 0;
        return budget;
    }

    if (phase == ScreenPhase::ScreenOff)
    {
        budget.allow_connect = true;
        budget.allow_read = true;
        budget.allow_write = true;
        budget.rx_packet_budget = client == Client::MeshMqtt ? 8 : 0;
        budget.tx_packet_budget = client == Client::MeshMqtt ? 8 : 2;
        budget.rx_byte_budget = client == Client::ReticulumGateway ? 256 : 4096;
        budget.tx_byte_budget = 4096;
        budget.min_read_interval_ms = client == Client::ReticulumGateway ? 100 : 0;
        return budget;
    }

    budget.allow_connect = true;
    budget.allow_read = true;
    budget.allow_write = true;
    budget.rx_packet_budget = client == Client::MeshMqtt ? 4 : 0;
    budget.tx_packet_budget = client == Client::MeshMqtt ? 4 : 1;
    budget.rx_byte_budget = client == Client::ReticulumGateway ? 128 : 2048;
    budget.tx_byte_budget = 2048;
    budget.min_read_interval_ms = client == Client::ReticulumGateway ? 250 : 0;
    return budget;
}

} // namespace

bool ensure_connected(const Request& request, Decision* out_decision)
{
    const std::uint32_t now_ms = sys::millis_now();
    const ScreenPhase phase = sample_screen_phase(now_ms);
    Decision decision = Decision::Granted;

    if (request.client == Client::Unknown)
    {
        decision = Decision::InvalidRequest;
    }
    else if (ota_active_for_other(request))
    {
        decision = Decision::OtaExclusive;
    }
    else if (foreground_download_active_for_other(request) &&
             messaging_client(request.client))
    {
        decision = Decision::Busy;
    }
    else if (foreground_download_settle_active_for(request.client, now_ms))
    {
        decision = Decision::Busy;
    }
    else if (::platform::ui::reticulum_call::realtime_mode_active() &&
             request.client != Client::ReticulumGateway)
    {
        decision = Decision::CallExclusive;
    }
    else
    {
        const ::platform::ui::wifi::Status wifi_status = ::platform::ui::wifi::status();
        if (!wifi_status.supported)
        {
            decision = Decision::WifiUnavailable;
        }
        else if (!wifi_status.enabled)
        {
            decision = Decision::WifiDisabled;
        }
        else if (!wifi_status.has_credentials)
        {
            decision = Decision::WifiNoCredentials;
        }
        else if (wifi_status.connected)
        {
            decision = Decision::Granted;
        }
        else if (!request.allow_connect)
        {
            decision = Decision::WifiDisconnected;
        }
        else if (phase == ScreenPhase::WakeProtected)
        {
            decision = Decision::ConnectDeferredForWake;
        }
        else if (!mark_connect_attempt(request.client, now_ms))
        {
            decision = Decision::ConnectBackoff;
        }
        else if (!::platform::ui::wifi::connect(nullptr))
        {
            decision = Decision::ConnectFailed;
        }
        else
        {
            decision = ::platform::ui::wifi::status().connected ? Decision::Granted
                                                                : Decision::ConnectFailed;
        }
    }

    if (out_decision)
    {
        *out_decision = decision;
    }
    if (decision != Decision::Granted)
    {
        std::printf("[WiFiAccess] connect denied client=%s kind=%s phase=%s decision=%s reason=%s\n",
                    client_name(request.client),
                    access_kind_name(request.kind),
                    screen_phase_name(phase),
                    decision_name(decision),
                    request.reason ? request.reason : "");
    }
    return decision == Decision::Granted;
}

Lease acquire(const Request& request)
{
    const ScreenPhase phase = sample_screen_phase(sys::millis_now());
    Lease out{};
    out.client = request.client;
    out.kind = request.kind;
    out.screen_phase = phase;

    if (request.client == Client::Unknown)
    {
        out.decision = Decision::InvalidRequest;
        return out;
    }
    if (is_http_kind(request.kind))
    {
        (void)acquire_http(request, phase, out);
        return out;
    }
    if (request.kind == AccessKind::LongLivedSocket ||
        request.kind == AccessKind::WifiConnect)
    {
        (void)long_lived_allowed(request, phase, out);
        return out;
    }
    out.decision = Decision::InvalidRequest;
    return out;
}

void release(const Lease& lease)
{
    if (!lease.release_required)
    {
        return;
    }

    const std::uint32_t now_ms = sys::millis_now();
    Client owner = Client::Unknown;
    AccessKind kind = AccessKind::WifiConnect;
    std::uint32_t held_ms = 0;
    bool route_download_released = false;
    portENTER_CRITICAL(&s_lock);
    owner = s_state.owner;
    kind = s_state.active_kind;
    held_ms = s_state.active_since_ms == 0 ? 0 : now_ms - s_state.active_since_ms;
    if (s_state.http_active && owner == lease.client && kind == lease.kind)
    {
        route_download_released = owner == Client::RouteStorage &&
                                  kind == AccessKind::HttpDownload &&
                                  !s_state.ota_active;
        s_state.http_active = false;
        s_state.ota_active = false;
        s_state.owner = Client::Unknown;
        s_state.active_kind = AccessKind::WifiConnect;
        s_state.active_since_ms = 0;
        if (route_download_released)
        {
            s_state.foreground_download_settle_until_ms =
                now_ms + kForegroundDownloadSettleMs;
        }
    }
    portEXIT_CRITICAL(&s_lock);

    std::printf("[WiFiAccess] release client=%s kind=%s held_ms=%lu\n",
                client_name(lease.client),
                access_kind_name(lease.kind),
                static_cast<unsigned long>(held_ms));
}

TrafficBudget traffic_budget(Client client, Priority priority)
{
    (void)priority;
    const ScreenPhase phase = sample_screen_phase(sys::millis_now());
    TrafficBudget budget = base_budget(client, phase);

    portENTER_CRITICAL(&s_lock);
    const std::uint32_t now_ms = sys::millis_now();
    const bool http_active = s_state.http_active;
    const bool ota = s_state.ota_active;
    const Client owner = s_state.owner;
    const AccessKind active_kind = s_state.active_kind;
    const std::uint32_t foreground_settle_until_ms =
        s_state.foreground_download_settle_until_ms;
    portEXIT_CRITICAL(&s_lock);

    const bool foreground_download_active =
        http_active &&
        !ota &&
        active_kind == AccessKind::HttpDownload &&
        foreground_download_owner(owner);
    const bool foreground_download_settle_active =
        messaging_client(client) &&
        static_cast<std::int32_t>(foreground_settle_until_ms - now_ms) > 0;

    if (ota ||
        ((foreground_download_active || foreground_download_settle_active) &&
         messaging_client(client)))
    {
        budget.allow_connect = false;
        budget.allow_read = false;
        budget.allow_write = false;
        budget.rx_packet_budget = 0;
        budget.tx_packet_budget = 0;
        budget.rx_byte_budget = 0;
        budget.tx_byte_budget = 0;
        return budget;
    }

    if (http_active && (client == Client::MeshMqtt || client == Client::ReticulumGateway))
    {
        budget.allow_connect = false;
        budget.rx_packet_budget = client == Client::MeshMqtt ? 1 : 0;
        budget.tx_packet_budget = 1;
        budget.rx_byte_budget = client == Client::ReticulumGateway ? 64 : 512;
        budget.tx_byte_budget = 512;
        if (client == Client::ReticulumGateway && budget.min_read_interval_ms < 500)
        {
            budget.min_read_interval_ms = 500;
        }
    }
    return budget;
}

RuntimeStatus status()
{
    const std::uint32_t now_ms = sys::millis_now();
    RuntimeStatus out{};
    portENTER_CRITICAL(&s_lock);
    out.http_active = s_state.http_active;
    out.ota_active = s_state.ota_active;
    out.owner = s_state.owner;
    out.active_kind = s_state.active_kind;
    out.active_since_ms = s_state.active_since_ms;
    out.wake_protected_until_ms = s_state.wake_protected_until_ms;
    portEXIT_CRITICAL(&s_lock);
    out.screen_phase = sample_screen_phase(now_ms);
    return out;
}

const char* client_name(Client client)
{
    switch (client)
    {
    case Client::FirmwareUpdate:
        return "firmware_update";
    case Client::PackRepository:
        return "pack_repository";
    case Client::RouteStorage:
        return "route_storage";
    case Client::MeshMqtt:
        return "mesh_mqtt";
    case Client::ReticulumGateway:
        return "reticulum_gateway";
    case Client::Unknown:
    default:
        return "unknown";
    }
}

const char* access_kind_name(AccessKind kind)
{
    switch (kind)
    {
    case AccessKind::WifiConnect:
        return "wifi_connect";
    case AccessKind::LongLivedSocket:
        return "long_lived_socket";
    case AccessKind::HttpMetadata:
        return "http_metadata";
    case AccessKind::HttpDownload:
        return "http_download";
    case AccessKind::OtaDownload:
        return "ota_download";
    default:
        return "unknown";
    }
}

const char* priority_name(Priority priority)
{
    switch (priority)
    {
    case Priority::Messaging:
        return "messaging";
    case Priority::UserForeground:
        return "user_foreground";
    case Priority::Maintenance:
        return "maintenance";
    case Priority::Background:
        return "background";
    default:
        return "unknown";
    }
}

const char* screen_phase_name(ScreenPhase phase)
{
    switch (phase)
    {
    case ScreenPhase::ScreenOn:
        return "screen_on";
    case ScreenPhase::WakeProtected:
        return "wake_protected";
    case ScreenPhase::ScreenOff:
        return "screen_off";
    case ScreenPhase::Unknown:
    default:
        return "unknown";
    }
}

const char* decision_name(Decision decision)
{
    switch (decision)
    {
    case Decision::Granted:
        return "granted";
    case Decision::InvalidRequest:
        return "invalid_request";
    case Decision::WifiUnavailable:
        return "wifi_unavailable";
    case Decision::WifiDisabled:
        return "wifi_disabled";
    case Decision::WifiNoCredentials:
        return "wifi_no_credentials";
    case Decision::WifiDisconnected:
        return "wifi_disconnected";
    case Decision::ConnectDeferredForWake:
        return "connect_deferred_for_wake";
    case Decision::ConnectBackoff:
        return "connect_backoff";
    case Decision::ConnectFailed:
        return "connect_failed";
    case Decision::Busy:
        return "busy";
    case Decision::DeferredForWake:
        return "deferred_for_wake";
    case Decision::DeferredForScreenOn:
        return "deferred_for_screen_on";
    case Decision::OtaExclusive:
        return "ota_exclusive";
    case Decision::CallExclusive:
        return "call_exclusive";
    default:
        return "unknown";
    }
}

} // namespace platform::ui::wifi_access
