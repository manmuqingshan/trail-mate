#include "platform/ui/screen_runtime.h"

#include "platform/ui/screen_power_state_machine.h"
#include "platform/ui/settings_store.h"

#include <chrono>
#include <cstdint>

namespace platform::ui::screen
{
namespace
{

using Clock = std::chrono::steady_clock;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kScreenTimeoutKey = "screen_timeout";
platform::ui::screen_power::StateMachine s_machine{};

uint32_t now_ms()
{
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch())
            .count());
}

} // namespace

uint32_t clamp_timeout_ms(uint32_t timeout_ms)
{
    return platform::ui::screen_power::StateMachine::clamp_timeout_ms(timeout_ms);
}

uint32_t timeout_ms()
{
    return s_machine.timeout_ms();
}

uint16_t timeout_secs()
{
    return static_cast<uint16_t>(timeout_ms() / 1000U);
}

bool supports_app_timeout_setting()
{
    return false;
}

void set_timeout_ms(uint32_t timeout_ms)
{
    const uint32_t normalized =
        platform::ui::screen_power::StateMachine::clamp_timeout_ms(timeout_ms);
    ::platform::ui::settings_store::put_uint(kSettingsNs, kScreenTimeoutKey, normalized);
    s_machine.set_timeout_ms(normalized);
}

void init(const Hooks& hooks)
{
    (void)hooks;
    s_machine.set_timeout_ms(
        ::platform::ui::settings_store::get_uint(
            kSettingsNs,
            kScreenTimeoutKey,
            platform::ui::screen_power::StateMachine::kDefaultTimeoutMs));
    s_machine.dispatch(platform::ui::screen_power::Event::Initialize, now_ms());
}

bool is_sleeping()
{
    return s_machine.snapshot().state ==
           platform::ui::screen_power::State::Sleeping;
}

bool is_sleep_disabled()
{
    return s_machine.snapshot().sleep_disable_depth > 0;
}

bool is_saver_active()
{
    return s_machine.snapshot().state ==
           platform::ui::screen_power::State::WakePreview;
}

void handle_input()
{
    s_machine.dispatch(platform::ui::screen_power::Event::Input, now_ms());
}

void handle_confirm_input()
{
    s_machine.dispatch(platform::ui::screen_power::Event::ConfirmInput, now_ms());
}

void handle_input_release()
{
    s_machine.dispatch(platform::ui::screen_power::Event::InputRelease, now_ms());
}

void wake_for_modal()
{
    s_machine.dispatch(platform::ui::screen_power::Event::ModalWake, now_ms());
}

void record_activity()
{
    s_machine.dispatch(platform::ui::screen_power::Event::Activity, now_ms());
}

void disable_sleep()
{
    s_machine.dispatch(platform::ui::screen_power::Event::DisableSleep, now_ms());
}

void enable_sleep()
{
    s_machine.dispatch(platform::ui::screen_power::Event::EnableSleep, now_ms());
}

} // namespace platform::ui::screen
