#include "platform/ui/wifi_access_runtime.h"

#include "app/app_facade_access.h"
#include "chat/ports/i_mesh_adapter.h"
#if defined(ARDUINO)
#include "platform/esp/arduino_common/chat/infra/mesh_mqtt_client_runtime.h"
#endif
#include "platform/ui/screen_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "sys/clock.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

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
    bool transport_enabled = true;
    bool http_active = false;
    bool ota_active = false;
    bool foreground_download_pending = false;
    Client owner = Client::Unknown;
    AccessKind active_kind = AccessKind::WifiConnect;
    Client pending_owner = Client::Unknown;
    AccessKind pending_kind = AccessKind::WifiConnect;
    std::uint32_t active_since_ms = 0;
    std::uint32_t pending_since_ms = 0;
    std::uint32_t foreground_download_settle_until_ms = 0;
    std::uint32_t wake_protected_until_ms = 0;
    std::uint32_t revoke_generation = 1;
    CallPreemptPhase call_phase = CallPreemptPhase::Idle;
    ExclusiveOwner exclusive_owner = ExclusiveOwner::None;
    bool non_preemptible_active = false;
    const char* non_preemptible_reason = nullptr;
    std::uint8_t call_link_id[kCallLinkIdSize] = {};
    bool saw_screen_sample = false;
    bool last_sleeping = false;
    bool last_saver = false;
    std::uint32_t last_connect_attempt_ms[kClientCount] = {};
};

RuntimeState s_state{};
portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

bool transport_enabled()
{
    bool enabled = false;
    portENTER_CRITICAL(&s_lock);
    enabled = s_state.transport_enabled;
    portEXIT_CRITICAL(&s_lock);
    return enabled;
}

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

bool link_is_empty(const std::uint8_t* link_id)
{
    if (!link_id)
    {
        return true;
    }
    for (std::uint8_t index = 0; index < kCallLinkIdSize; ++index)
    {
        if (link_id[index] != 0)
        {
            return false;
        }
    }
    return true;
}

void copy_link(std::uint8_t* out, const std::uint8_t* in)
{
    if (out && in)
    {
        std::memcpy(out, in, kCallLinkIdSize);
    }
}

bool links_equal(const std::uint8_t* lhs, const std::uint8_t* rhs)
{
    return lhs && rhs && std::memcmp(lhs, rhs, kCallLinkIdSize) == 0;
}

bool call_soft_preempt_active_locked()
{
    return s_state.call_phase != CallPreemptPhase::Idle;
}

bool call_exclusive_active_locked()
{
    return s_state.exclusive_owner == ExclusiveOwner::ReticulumCallAudio &&
           (s_state.call_phase == CallPreemptPhase::Exclusive ||
            s_state.call_phase == CallPreemptPhase::ClosingExclusive);
}

bool call_link_matches_locked(const std::uint8_t* link_id)
{
    return !link_is_empty(link_id) && links_equal(link_id, s_state.call_link_id);
}

bool request_is_call_audio_locked(const Request& request)
{
    return request.client == Client::ReticulumGateway &&
           request.kind == AccessKind::ReticulumGatewayCallAudio &&
           call_link_matches_locked(request.call_link_id);
}

bool request_is_call_control_locked(const Request& request)
{
    return request.client == Client::ReticulumGateway &&
           request.kind == AccessKind::ReticulumGatewayCallControl &&
           !link_is_empty(request.call_link_id) &&
           !call_link_matches_locked(request.call_link_id);
}

bool call_allows_request_locked(const Request& request)
{
    if (!call_soft_preempt_active_locked())
    {
        return true;
    }
    if (request.client != Client::ReticulumGateway)
    {
        return false;
    }
    if (!call_exclusive_active_locked())
    {
        return true;
    }
    return request.kind == AccessKind::WifiConnect ||
           request.kind == AccessKind::LongLivedSocket ||
           request_is_call_audio_locked(request) ||
           request_is_call_control_locked(request);
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

bool foreground_download_pending_for_other(const Request& request)
{
    bool pending = false;
    portENTER_CRITICAL(&s_lock);
    pending = s_state.foreground_download_pending &&
              foreground_download_owner(s_state.pending_owner) &&
              !(s_state.pending_owner == request.client &&
                s_state.pending_kind == request.kind);
    portEXIT_CRITICAL(&s_lock);
    return pending;
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
    portENTER_CRITICAL(&s_lock);
    const bool call_preempt = call_soft_preempt_active_locked();
    portEXIT_CRITICAL(&s_lock);
    if (call_preempt)
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
    if (s_state.foreground_download_pending &&
        foreground_download_owner(s_state.pending_owner) &&
        !(s_state.pending_owner == request.client &&
          s_state.pending_kind == request.kind))
    {
        out.decision = Decision::Busy;
        portEXIT_CRITICAL(&s_lock);
        return false;
    }
    s_state.http_active = true;
    s_state.ota_active = request.kind == AccessKind::OtaDownload;
    s_state.owner = request.client;
    s_state.active_kind = request.kind;
    s_state.active_since_ms = now_ms;
    const std::uint32_t revoke_generation = s_state.revoke_generation;
    portEXIT_CRITICAL(&s_lock);

    out.granted = true;
    out.release_required = true;
    out.decision = Decision::Granted;
    out.granted_ms = now_ms;
    out.revoke_generation = revoke_generation;
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
    portENTER_CRITICAL(&s_lock);
    const bool allowed_by_call = call_allows_request_locked(request);
    const std::uint32_t revoke_generation = s_state.revoke_generation;
    const bool call_audio = request_is_call_audio_locked(request);
    portEXIT_CRITICAL(&s_lock);
    if (!allowed_by_call)
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
    if (foreground_download_pending_for_other(request) &&
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
    out.revoke_generation = revoke_generation;
    out.call_audio = call_audio;
    if (request.call_link_id)
    {
        copy_link(out.call_link_id, request.call_link_id);
    }
    return true;
}

TrafficBudget base_budget(Client client,
                          ScreenPhase phase,
                          const std::uint8_t* call_link_id,
                          AccessKind access_kind)
{
    TrafficBudget budget{};
    budget.screen_phase = phase;

    portENTER_CRITICAL(&s_lock);
    const bool call_preempt = call_soft_preempt_active_locked();
    const bool call_exclusive = call_exclusive_active_locked();
    const bool link_matches = call_link_matches_locked(call_link_id);
    const bool call_control =
        client == Client::ReticulumGateway &&
        access_kind == AccessKind::ReticulumGatewayCallControl &&
        !link_is_empty(call_link_id) && !link_matches;
    portEXIT_CRITICAL(&s_lock);

    if (call_preempt)
    {
        if (client == Client::ReticulumGateway)
        {
            budget.allow_connect = true;
            budget.allow_read = true;
            budget.allow_write = !call_exclusive || link_matches || call_control;
            budget.rx_packet_budget = 0;
            budget.tx_packet_budget = call_control ? 2 : 8;
            budget.rx_byte_budget = 768;
            budget.tx_byte_budget = call_control ? 512 : 1024;
            budget.min_read_interval_ms = 0;
        }
        return budget;
    }

    if (client == Client::ReticulumGateway)
    {
        budget.allow_connect = true;
        budget.allow_read = true;
        budget.allow_write = true;
        budget.rx_packet_budget = 0;
        budget.tx_packet_budget = 4;
        budget.rx_byte_budget = 512;
        budget.tx_byte_budget = 2048;
        budget.min_read_interval_ms = 0;
        return budget;
    }

    if (phase == ScreenPhase::WakeProtected)
    {
        budget.allow_connect = false;
        budget.allow_read = true;
        budget.allow_write = true;
        budget.rx_packet_budget = client == Client::MeshMqtt ? 1 : 0;
        budget.tx_packet_budget = 1;
        budget.rx_byte_budget = 512;
        budget.tx_byte_budget = 512;
        budget.min_read_interval_ms = 0;
        return budget;
    }

    if (phase == ScreenPhase::ScreenOff)
    {
        budget.allow_connect = true;
        budget.allow_read = true;
        budget.allow_write = true;
        budget.rx_packet_budget = client == Client::MeshMqtt ? 8 : 0;
        budget.tx_packet_budget = client == Client::MeshMqtt ? 8 : 2;
        budget.rx_byte_budget = 4096;
        budget.tx_byte_budget = 4096;
        budget.min_read_interval_ms = 0;
        return budget;
    }

    budget.allow_connect = true;
    budget.allow_read = true;
    budget.allow_write = true;
    budget.rx_packet_budget = client == Client::MeshMqtt ? 4 : 0;
    budget.tx_packet_budget = client == Client::MeshMqtt ? 4 : 1;
    budget.rx_byte_budget = 2048;
    budget.min_read_interval_ms = 0;
    budget.tx_byte_budget = 2048;
    return budget;
}

} // namespace

bool enter_call_ringing(const std::uint8_t link_id[kCallLinkIdSize])
{
    if (link_is_empty(link_id))
    {
        return false;
    }
    portENTER_CRITICAL(&s_lock);
    s_state.call_phase = CallPreemptPhase::RingingSoft;
    s_state.exclusive_owner = ExclusiveOwner::None;
    copy_link(s_state.call_link_id, link_id);
    s_state.foreground_download_pending = false;
    s_state.pending_owner = Client::Unknown;
    s_state.pending_kind = AccessKind::WifiConnect;
    s_state.pending_since_ms = 0;
    portEXIT_CRITICAL(&s_lock);
    std::printf("[WiFiAccess] call ringing soft_preempt link=%02X%02X%02X%02X\n",
                link_id[0],
                link_id[1],
                link_id[2],
                link_id[3]);
    return true;
}

bool call_accept_available()
{
    bool available = false;
    portENTER_CRITICAL(&s_lock);
    available = !s_state.non_preemptible_active;
    portEXIT_CRITICAL(&s_lock);
    return available;
}

bool enter_call_exclusive(const std::uint8_t link_id[kCallLinkIdSize])
{
    if (link_is_empty(link_id))
    {
        return false;
    }

    const char* busy_reason = nullptr;
    std::uint32_t generation = 0;
    portENTER_CRITICAL(&s_lock);
    if (s_state.non_preemptible_active)
    {
        busy_reason = s_state.non_preemptible_reason;
        portEXIT_CRITICAL(&s_lock);
        std::printf("[WiFiAccess] call exclusive denied reason=non_preemptible activity=%s\n",
                    busy_reason ? busy_reason : "unknown");
        return false;
    }
    s_state.call_phase = CallPreemptPhase::Exclusive;
    s_state.exclusive_owner = ExclusiveOwner::ReticulumCallAudio;
    copy_link(s_state.call_link_id, link_id);
    ++s_state.revoke_generation;
    s_state.foreground_download_pending = false;
    s_state.pending_owner = Client::Unknown;
    s_state.pending_kind = AccessKind::WifiConnect;
    s_state.pending_since_ms = 0;
    generation = s_state.revoke_generation;
    portEXIT_CRITICAL(&s_lock);

    std::printf("[WiFiAccess] call exclusive enter owner=%s generation=%lu link=%02X%02X%02X%02X\n",
                exclusive_owner_name(ExclusiveOwner::ReticulumCallAudio),
                static_cast<unsigned long>(generation),
                link_id[0],
                link_id[1],
                link_id[2],
                link_id[3]);
    return true;
}

void enter_call_closing(const std::uint8_t link_id[kCallLinkIdSize],
                        bool keep_exclusive)
{
    if (link_is_empty(link_id))
    {
        return;
    }
    portENTER_CRITICAL(&s_lock);
    s_state.call_phase = keep_exclusive ? CallPreemptPhase::ClosingExclusive
                                        : CallPreemptPhase::ClosingSoft;
    s_state.exclusive_owner = keep_exclusive ? ExclusiveOwner::ReticulumCallAudio
                                             : ExclusiveOwner::None;
    copy_link(s_state.call_link_id, link_id);
    if (keep_exclusive)
    {
        ++s_state.revoke_generation;
    }
    portEXIT_CRITICAL(&s_lock);
    std::printf("[WiFiAccess] call closing phase=%s link=%02X%02X%02X%02X\n",
                keep_exclusive ? "exclusive" : "soft",
                link_id[0],
                link_id[1],
                link_id[2],
                link_id[3]);
}

void exit_call(const std::uint8_t link_id[kCallLinkIdSize])
{
    std::uint32_t generation = 0;
    portENTER_CRITICAL(&s_lock);
    if (!link_is_empty(link_id) &&
        !link_is_empty(s_state.call_link_id) &&
        !links_equal(link_id, s_state.call_link_id))
    {
        portEXIT_CRITICAL(&s_lock);
        return;
    }
    const bool was_exclusive = call_exclusive_active_locked();
    s_state.call_phase = CallPreemptPhase::Idle;
    s_state.exclusive_owner = ExclusiveOwner::None;
    std::memset(s_state.call_link_id, 0, sizeof(s_state.call_link_id));
    if (was_exclusive)
    {
        ++s_state.revoke_generation;
    }
    generation = s_state.revoke_generation;
    portEXIT_CRITICAL(&s_lock);
    std::printf("[WiFiAccess] call exit generation=%lu\n",
                static_cast<unsigned long>(generation));
}

void set_non_preemptible_activity(bool active, const char* reason)
{
    portENTER_CRITICAL(&s_lock);
    s_state.non_preemptible_active = active;
    s_state.non_preemptible_reason = active ? reason : nullptr;
    portEXIT_CRITICAL(&s_lock);
}

bool call_soft_preempt_active()
{
    bool active = false;
    portENTER_CRITICAL(&s_lock);
    active = call_soft_preempt_active_locked();
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool call_exclusive_active()
{
    bool active = false;
    portENTER_CRITICAL(&s_lock);
    active = call_exclusive_active_locked();
    portEXIT_CRITICAL(&s_lock);
    return active;
}

bool set_transport_enabled(bool enabled)
{
    if (!enabled)
    {
        portENTER_CRITICAL(&s_lock);
        if (s_state.transport_enabled)
        {
            ++s_state.revoke_generation;
        }
        s_state.transport_enabled = false;
        s_state.foreground_download_pending = false;
        s_state.pending_owner = Client::Unknown;
        s_state.pending_kind = AccessKind::WifiConnect;
        s_state.pending_since_ms = 0;
        portEXIT_CRITICAL(&s_lock);
    }

    bool clients_ready = true;
#if defined(ARDUINO)
    ::platform::esp::arduino_common::mesh_mqtt::setWifiTransportEnabled(enabled);
#endif
    if (app::hasAppFacade())
    {
        if (chat::IMeshAdapter* adapter = app::appFacade().getMeshAdapter())
        {
            clients_ready = adapter->setWifiTransportEnabled(enabled);
        }
    }

    if (enabled)
    {
        portENTER_CRITICAL(&s_lock);
        s_state.transport_enabled = clients_ready;
        portEXIT_CRITICAL(&s_lock);
    }

    std::printf("[WiFiAccess] transport enabled=%u clients_ready=%u\n",
                enabled ? 1U : 0U,
                clients_ready ? 1U : 0U);
    return clients_ready;
}

bool ensure_connected(const Request& request, Decision* out_decision)
{
    const std::uint32_t now_ms = sys::millis_now();
    const ScreenPhase phase = sample_screen_phase(now_ms);
    Decision decision = Decision::Granted;

    if (!transport_enabled())
    {
        decision = Decision::WifiDisabled;
    }
    else if (request.client == Client::Unknown)
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
    else if (foreground_download_pending_for_other(request) &&
             messaging_client(request.client))
    {
        decision = Decision::Busy;
    }
    else if (foreground_download_settle_active_for(request.client, now_ms))
    {
        decision = Decision::Busy;
    }
    else
    {
        portENTER_CRITICAL(&s_lock);
        const bool allowed_by_call = call_allows_request_locked(request);
        portEXIT_CRITICAL(&s_lock);
        if (!allowed_by_call)
        {
            decision = Decision::CallExclusive;
        }
    }

    if (decision == Decision::Granted)
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
            // ESP-IDF P4 targets delegate Wi-Fi to the C6 companion. A
            // successfully queued Connect command completes asynchronously;
            // absence of an immediate GOT_IP event is not a failed command.
            decision = ::platform::ui::wifi::status().connected ? Decision::Granted
                                                                : Decision::ConnectBackoff;
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

    if (!transport_enabled())
    {
        out.decision = Decision::WifiDisabled;
        return out;
    }
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

bool lease_revoked(const Lease& lease)
{
    if (!lease.granted)
    {
        return false;
    }
    bool revoked = false;
    portENTER_CRITICAL(&s_lock);
    if (lease.revoke_generation != s_state.revoke_generation)
    {
        revoked = true;
    }
    if (call_exclusive_active_locked())
    {
        revoked = !lease.call_audio ||
                  !links_equal(lease.call_link_id, s_state.call_link_id);
    }
    portEXIT_CRITICAL(&s_lock);
    return revoked;
}

bool begin_foreground_download(const Request& request, Decision* out_decision)
{
    const std::uint32_t now_ms = sys::millis_now();
    const ScreenPhase phase = sample_screen_phase(now_ms);
    Decision decision = Decision::Granted;

    if (request.client == Client::Unknown ||
        request.kind != AccessKind::HttpDownload ||
        request.priority != Priority::UserForeground ||
        !foreground_download_owner(request.client))
    {
        decision = Decision::InvalidRequest;
    }
    else if (call_soft_preempt_active())
    {
        decision = Decision::CallExclusive;
    }
    else if (phase == ScreenPhase::WakeProtected)
    {
        decision = Decision::DeferredForWake;
    }
    else
    {
        portENTER_CRITICAL(&s_lock);
        if (s_state.ota_active)
        {
            decision = Decision::OtaExclusive;
        }
        else if (s_state.http_active &&
                 !(s_state.owner == request.client &&
                   s_state.active_kind == request.kind))
        {
            decision = Decision::Busy;
        }
        else if (s_state.foreground_download_pending &&
                 !(s_state.pending_owner == request.client &&
                   s_state.pending_kind == request.kind))
        {
            decision = Decision::Busy;
        }
        else
        {
            s_state.foreground_download_pending = true;
            s_state.pending_owner = request.client;
            s_state.pending_kind = request.kind;
            if (s_state.pending_since_ms == 0)
            {
                s_state.pending_since_ms = now_ms;
            }
        }
        portEXIT_CRITICAL(&s_lock);
    }

    if (out_decision)
    {
        *out_decision = decision;
    }
    if (decision == Decision::Granted)
    {
        std::printf("[WiFiAccess] foreground pending client=%s kind=%s phase=%s reason=%s\n",
                    client_name(request.client),
                    access_kind_name(request.kind),
                    screen_phase_name(phase),
                    request.reason ? request.reason : "");
    }
    else
    {
        std::printf("[WiFiAccess] foreground pending denied client=%s kind=%s phase=%s decision=%s reason=%s\n",
                    client_name(request.client),
                    access_kind_name(request.kind),
                    screen_phase_name(phase),
                    decision_name(decision),
                    request.reason ? request.reason : "");
    }
    return decision == Decision::Granted;
}

void end_foreground_download(Client client, AccessKind kind)
{
    const std::uint32_t now_ms = sys::millis_now();
    std::uint32_t held_ms = 0;
    bool cleared = false;

    portENTER_CRITICAL(&s_lock);
    if (s_state.foreground_download_pending &&
        s_state.pending_owner == client &&
        s_state.pending_kind == kind)
    {
        held_ms = s_state.pending_since_ms == 0 ? 0 : now_ms - s_state.pending_since_ms;
        s_state.foreground_download_pending = false;
        s_state.pending_owner = Client::Unknown;
        s_state.pending_kind = AccessKind::WifiConnect;
        s_state.pending_since_ms = 0;
        if (foreground_download_owner(client) && kind == AccessKind::HttpDownload)
        {
            s_state.foreground_download_settle_until_ms =
                now_ms + kForegroundDownloadSettleMs;
        }
        cleared = true;
    }
    portEXIT_CRITICAL(&s_lock);

    if (cleared)
    {
        std::printf("[WiFiAccess] foreground released client=%s kind=%s held_ms=%lu\n",
                    client_name(client),
                    access_kind_name(kind),
                    static_cast<unsigned long>(held_ms));
    }
}

TrafficBudget traffic_budget(Client client,
                             Priority priority,
                             const std::uint8_t* call_link_id,
                             AccessKind access_kind)
{
    (void)priority;
    const ScreenPhase phase = sample_screen_phase(sys::millis_now());
    TrafficBudget budget = base_budget(client, phase, call_link_id, access_kind);

    portENTER_CRITICAL(&s_lock);
    const std::uint32_t now_ms = sys::millis_now();
    const bool http_active = s_state.http_active;
    const bool ota = s_state.ota_active;
    const Client owner = s_state.owner;
    const AccessKind active_kind = s_state.active_kind;
    const bool foreground_pending = s_state.foreground_download_pending &&
                                    foreground_download_owner(s_state.pending_owner);
    const bool transport_available = s_state.transport_enabled;
    const std::uint32_t foreground_settle_until_ms =
        s_state.foreground_download_settle_until_ms;
    portEXIT_CRITICAL(&s_lock);

    if (!transport_available)
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

    const bool foreground_download_active =
        http_active &&
        !ota &&
        active_kind == AccessKind::HttpDownload &&
        foreground_download_owner(owner);
    const bool foreground_download_settle_active =
        messaging_client(client) &&
        static_cast<std::int32_t>(foreground_settle_until_ms - now_ms) > 0;

    if (ota ||
        ((foreground_download_active ||
          foreground_pending ||
          foreground_download_settle_active) &&
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
    out.foreground_download_pending = s_state.foreground_download_pending;
    out.owner = s_state.owner;
    out.active_kind = s_state.active_kind;
    out.pending_owner = s_state.pending_owner;
    out.pending_kind = s_state.pending_kind;
    out.active_since_ms = s_state.active_since_ms;
    out.pending_since_ms = s_state.pending_since_ms;
    out.wake_protected_until_ms = s_state.wake_protected_until_ms;
    out.revoke_generation = s_state.revoke_generation;
    out.call_phase = s_state.call_phase;
    out.exclusive_owner = s_state.exclusive_owner;
    out.non_preemptible_active = s_state.non_preemptible_active;
    out.non_preemptible_reason = s_state.non_preemptible_reason;
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
    case AccessKind::ReticulumGatewayCallAudio:
        return "reticulum_gateway_call_audio";
    case AccessKind::ReticulumGatewayCallControl:
        return "reticulum_gateway_call_control";
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
    case Decision::NonPreemptibleBusy:
        return "non_preemptible_busy";
    default:
        return "unknown";
    }
}

const char* call_preempt_phase_name(CallPreemptPhase phase)
{
    switch (phase)
    {
    case CallPreemptPhase::Idle:
        return "idle";
    case CallPreemptPhase::RingingSoft:
        return "ringing_soft";
    case CallPreemptPhase::Exclusive:
        return "exclusive";
    case CallPreemptPhase::ClosingSoft:
        return "closing_soft";
    case CallPreemptPhase::ClosingExclusive:
        return "closing_exclusive";
    default:
        return "unknown";
    }
}

const char* exclusive_owner_name(ExclusiveOwner owner)
{
    switch (owner)
    {
    case ExclusiveOwner::None:
        return "none";
    case ExclusiveOwner::ReticulumCallAudio:
        return "reticulum_call_audio";
    default:
        return "unknown";
    }
}

} // namespace platform::ui::wifi_access
