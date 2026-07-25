#include "ui/loop_shell.h"

namespace ui::loop_shell
{

void tick(const Hooks& hooks)
{
    const uint32_t now_ms = hooks.now_ms ? hooks.now_ms() : 0;

    if (hooks.is_overlay_active && hooks.is_overlay_active())
    {
        if (hooks.display_tick_if_due)
        {
            hooks.display_tick_if_due(now_ms);
        }
        if (hooks.yield_now)
        {
            hooks.yield_now();
        }
        if (hooks.sleep_ms)
        {
            hooks.sleep_ms(hooks.overlay_sleep_ms);
        }
        return;
    }

    // The display is the frame-critical owner of the loop. Runtime work can
    // enter Wi-Fi/MQTT, storage, or radio paths and may block for a bounded
    // interval. Present the pending LVGL frame before handing control to
    // those services, otherwise a slow first runtime tick can leave the
    // device on a black boot screen even though setup has completed.
    if (hooks.display_tick_if_due)
    {
        hooks.display_tick_if_due(now_ms);
    }

    if (hooks.handle_power_button)
    {
        hooks.handle_power_button();
    }

    if (hooks.update_runtime)
    {
        hooks.update_runtime();
    }

    if (hooks.sleep_ms)
    {
        hooks.sleep_ms(hooks.idle_sleep_ms);
    }
}

} // namespace ui::loop_shell
