#include <cassert>

#include "platform/ui/screen_power_state_machine.h"

namespace
{
using platform::ui::screen_power::Event;
using platform::ui::screen_power::State;
using platform::ui::screen_power::StateMachine;
} // namespace

int main()
{
    StateMachine machine;
    machine.dispatch(Event::Initialize, 100);

    assert(machine.snapshot().state == State::Awake);
    assert(machine.timeout_ms() == StateMachine::kDefaultTimeoutMs);

    auto sleep_effects = machine.dispatch(Event::Tick, 60100);
    assert(sleep_effects.sleep_display);
    assert(machine.snapshot().state == State::Sleeping);

    auto wake_effects = machine.dispatch(Event::WakeInput, 60200);
    assert(wake_effects.wake_display);
    assert(wake_effects.show_saver);
    assert(machine.snapshot().state == State::WakePreview);

    auto duplicate_press = machine.dispatch(Event::Input, 60210);
    assert(!duplicate_press.show_main_menu);
    assert(machine.snapshot().state == State::WakePreview);

    machine.dispatch(Event::InputRelease, 60220);
    auto guarded_press = machine.dispatch(Event::Input, 60300);
    assert(!guarded_press.show_main_menu);
    assert(machine.snapshot().state == State::WakePreview);

    auto confirm_effects = machine.dispatch(Event::Input, 60600);
    assert(confirm_effects.hide_saver);
    assert(confirm_effects.show_main_menu);
    assert(machine.snapshot().state == State::Awake);

    machine.dispatch(Event::Tick, 120700);
    assert(machine.snapshot().state == State::Sleeping);
    machine.dispatch(Event::WakeInput, 120800);
    auto timeout_effects = machine.dispatch(Event::Tick, 123800);
    assert(timeout_effects.sleep_display);
    assert(timeout_effects.hide_saver);
    assert(machine.snapshot().state == State::Sleeping);

    machine.dispatch(Event::DisableSleep, 123900);
    assert(machine.snapshot().state == State::Awake);
    assert(machine.snapshot().sleep_disable_depth == 1);
    assert(!machine.dispatch(Event::Tick, 999999).sleep_display);
    machine.dispatch(Event::EnableSleep, 124200);
    assert(machine.snapshot().sleep_disable_depth == 0);

    machine.set_timeout_ms(1);
    assert(machine.timeout_ms() == StateMachine::kDefaultTimeoutMs);
    machine.set_timeout_ms(400000);
    assert(machine.timeout_ms() == StateMachine::kMaxTimeoutMs);

    return 0;
}
