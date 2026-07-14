#include "ui/widgets/reticulum_ping_overlay.h"

#include "lvgl.h"
#include "sys/clock.h"
#include "ui/widgets/foreground_operation_overlay.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace ui::widgets::reticulum_ping
{
namespace
{

namespace foreground = ::ui::widgets::foreground_operation;

constexpr std::uint32_t kFallbackTimeoutMs = 31000;
constexpr std::uint32_t kProgressHorizonMs = 30000;
constexpr std::uint32_t kProgressUpdateMs = 500;
constexpr std::uint32_t kResultHoldMs = 3200;
constexpr int kInitialProgress = 8;
constexpr int kWaitingProgressLimit = 92;

enum class TimerPurpose : std::uint8_t
{
    None = 0,
    AwaitResult,
    DismissResult,
};

std::array<std::uint8_t, ::chat::kReticulumPeerHashSize> s_destination_hash{};
std::array<char, 96> s_loading_detail{};
lv_timer_t* s_timer = nullptr;
std::uint32_t s_generation = 0;
std::uint32_t s_wait_started_ms = 0;
TimerPurpose s_timer_purpose = TimerPurpose::None;
int s_wait_progress = kInitialProgress;
bool s_active = false;

void stop_timer()
{
    if (s_timer)
    {
        lv_timer_del(s_timer);
        s_timer = nullptr;
    }
    s_timer_purpose = TimerPurpose::None;
}

void publish_result(const char* title, const char* detail, int progress_percent)
{
    foreground::publish(
        foreground::make_snapshot(foreground::Slot::ReticulumPing,
                                  foreground::Policy::OverlayImmediate,
                                  foreground::Priority::Blocking,
                                  title,
                                  detail,
                                  progress_percent,
                                  nullptr,
                                  s_generation));
}

void publish_loading(int progress_percent, foreground::Policy policy)
{
    foreground::publish(
        foreground::make_snapshot(foreground::Slot::ReticulumPing,
                                  policy,
                                  foreground::Priority::Blocking,
                                  "Pinging",
                                  s_loading_detail.data(),
                                  progress_percent,
                                  nullptr,
                                  s_generation));
}

int waiting_progress(std::uint32_t elapsed_ms)
{
    const std::uint32_t bounded = std::min(elapsed_ms, kProgressHorizonMs);
    const std::uint32_t span = static_cast<std::uint32_t>(
        kWaitingProgressLimit - kInitialProgress);
    return kInitialProgress + static_cast<int>((span * bounded) / kProgressHorizonMs);
}

void timer_cb(lv_timer_t* timer)
{
    if (s_timer_purpose == TimerPurpose::AwaitResult)
    {
        if (!s_active)
        {
            s_timer = nullptr;
            s_timer_purpose = TimerPurpose::None;
            lv_timer_del(timer);
            return;
        }

        const std::uint32_t elapsed_ms = sys::millis_now() - s_wait_started_ms;
        if (elapsed_ms >= kFallbackTimeoutMs)
        {
            s_timer = nullptr;
            s_timer_purpose = TimerPurpose::None;
            lv_timer_del(timer);
            publish_result("No response", "Timeout after 30 s", 100);
            s_timer_purpose = TimerPurpose::DismissResult;
            s_timer = lv_timer_create(timer_cb, kResultHoldMs, nullptr);
            if (s_timer)
            {
                lv_timer_set_repeat_count(s_timer, 1);
            }
            return;
        }

        const int progress = waiting_progress(elapsed_ms);
        if (progress > s_wait_progress)
        {
            s_wait_progress = progress;
            publish_loading(s_wait_progress, foreground::Policy::Overlay);
        }
        return;
    }

    const TimerPurpose purpose = s_timer_purpose;
    s_timer = nullptr;
    s_timer_purpose = TimerPurpose::None;

    if (purpose == TimerPurpose::DismissResult)
    {
        foreground::clear(foreground::Slot::ReticulumPing, s_generation);
        s_destination_hash.fill(0);
        s_loading_detail.fill(0);
        s_active = false;
    }
}

void schedule_timer(TimerPurpose purpose, std::uint32_t delay_ms)
{
    stop_timer();
    s_timer_purpose = purpose;
    s_timer = lv_timer_create(timer_cb, delay_ms, nullptr);
    if (s_timer)
    {
        lv_timer_set_repeat_count(s_timer, 1);
    }
}

void schedule_result_dismiss()
{
    schedule_timer(TimerPurpose::DismissResult, kResultHoldMs);
}

void schedule_wait_progress()
{
    stop_timer();
    s_timer_purpose = TimerPurpose::AwaitResult;
    s_timer = lv_timer_create(timer_cb, kProgressUpdateMs, nullptr);
    if (s_timer)
    {
        lv_timer_set_repeat_count(s_timer, -1);
    }
}

std::uint32_t next_generation()
{
    ++s_generation;
    if (s_generation == 0)
    {
        ++s_generation;
    }
    return s_generation;
}

} // namespace

void show_loading(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize],
    const char* display_name)
{
    if (!destination_hash)
    {
        return;
    }

    stop_timer();
    next_generation();
    std::memcpy(s_destination_hash.data(),
                destination_hash,
                s_destination_hash.size());
    s_active = true;

    const char* detail = display_name && display_name[0] != '\0'
                             ? display_name
                             : "Waiting for response";
    std::snprintf(s_loading_detail.data(), s_loading_detail.size(), "%s", detail);
    s_wait_started_ms = sys::millis_now();
    s_wait_progress = kInitialProgress;
    publish_loading(s_wait_progress, foreground::Policy::OverlayImmediate);
    schedule_wait_progress();
}

void show_send_failure(const char* detail)
{
    if (!s_active)
    {
        return;
    }
    publish_result("Ping failed",
                   detail && detail[0] != '\0' ? detail : "Unable to send",
                   100);
    schedule_result_dismiss();
}

void show_delivered(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize],
    std::uint32_t latency_ms,
    std::uint8_t hops)
{
    if (!active_for(destination_hash))
    {
        return;
    }

    char detail[64] = {};
    std::snprintf(detail,
                  sizeof(detail),
                  "Latency: %lu ms | Hops: %u",
                  static_cast<unsigned long>(latency_ms),
                  static_cast<unsigned>(hops));
    publish_result("Reachable", detail, 100);
    schedule_result_dismiss();
}

void show_timeout(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize],
    std::uint32_t elapsed_ms)
{
    if (!active_for(destination_hash))
    {
        return;
    }

    char detail[48] = {};
    std::snprintf(detail,
                  sizeof(detail),
                  "Timeout after %lu s",
                  static_cast<unsigned long>((elapsed_ms + 999U) / 1000U));
    publish_result("No response", detail, 100);
    schedule_result_dismiss();
}

void dismiss()
{
    stop_timer();
    foreground::clear(foreground::Slot::ReticulumPing, s_generation);
    s_destination_hash.fill(0);
    s_loading_detail.fill(0);
    s_active = false;
}

bool active_for(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize])
{
    return s_active && destination_hash &&
           std::memcmp(s_destination_hash.data(),
                       destination_hash,
                       s_destination_hash.size()) == 0;
}

} // namespace ui::widgets::reticulum_ping
