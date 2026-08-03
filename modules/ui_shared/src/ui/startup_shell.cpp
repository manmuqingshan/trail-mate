#include "ui/startup_shell.h"

#include <cstdint>
#include <cstdio>
#include <ctime>

#include "lvgl.h"
#include "platform/ui/screen_runtime.h"
#include "platform/ui/time_runtime.h"
#include "sys/clock.h"
#include "ui/app_runtime.h"
#include "ui/components/screen_saver_overlay.h"
#include "ui/localization.h"
#include "ui/menu/menu_layout.h"
#include "ui/menu/menu_runtime.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_boot.h"
#include "ui/ui_common.h"
#include "ui/ui_status.h"
#include "ui/watch_face.h"
#include "ui_lvgl_ux_packs/ux/ux_menu_provider.h"

namespace ui::startup_shell
{

bool format_menu_time(char* out, size_t out_len);

namespace
{

ui::menu::MenuModel s_ux_menu_model;

#ifndef TRAIL_MATE_BOOT_UI_SYNC_PRESENT
#define TRAIL_MATE_BOOT_UI_SYNC_PRESENT 1
#endif

#if TRAIL_MATE_BOOT_UI_SYNC_PRESENT
constexpr uint8_t kBootPresentFrameCount = 4;
constexpr uint32_t kBootPresentFrameDelayMs = 16;
#endif

bool resolve_display_time(struct tm* out_tm)
{
    if (!out_tm)
    {
        return false;
    }

    if (::platform::ui::time::localtime_now(out_tm))
    {
        return true;
    }

    const std::time_t now = std::time(nullptr);
    if (now <= 0)
    {
        return false;
    }

    const std::time_t local = ::platform::ui::time::apply_timezone_offset(now);
    const std::tm* tmp = std::gmtime(&local);
    if (!tmp)
    {
        return false;
    }

    *out_tm = *tmp;
    return true;
}

bool s_boot_presentation_completed = false;

void present_boot_overlay_now()
{
#if TRAIL_MATE_BOOT_UI_SYNC_PRESENT
    for (uint8_t frame = 0; frame < kBootPresentFrameCount; ++frame)
    {
        if (lv_obj_t* top = lv_layer_top())
        {
            lv_obj_invalidate(top);
        }
        lv_timer_handler();
        lv_refr_now(nullptr);
        if (frame + 1U < kBootPresentFrameCount)
        {
            sys::sleep_ms(kBootPresentFrameDelayMs);
        }
    }
    s_boot_presentation_completed = true;
#else
    if (lv_obj_t* top = lv_layer_top())
    {
        lv_obj_invalidate(top);
    }
    s_boot_presentation_completed = false;
#endif
}

void present_screen_saver_now()
{
    ui::components::screen_saver_overlay::request_present();
}

void initScreenSaverOverlay()
{
    ui::components::screen_saver_overlay::Hooks overlay_hooks{};
    overlay_hooks.format_time = format_menu_time;
    overlay_hooks.read_unread_count = ui::status::get_total_unread;
    ui::components::screen_saver_overlay::init(overlay_hooks);
}

} // namespace

bool format_menu_time(char* out, size_t out_len)
{
    if (!out || out_len < 6)
    {
        return false;
    }

    struct tm info
    {
    };
    if (!resolve_display_time(&info))
    {
        return false;
    }
    strftime(out, out_len, "%H:%M", &info);
    return true;
}

menu_runtime::Hooks::WatchFaceHooks defaultWatchFaceHooks()
{
    menu_runtime::Hooks::WatchFaceHooks hooks{};
    hooks.create = watch_face_create;
    hooks.set_time = watch_face_set_time;
    hooks.set_node_id = watch_face_set_node_id;
    hooks.show = watch_face_show;
    hooks.is_ready = watch_face_is_ready;
    hooks.set_unlock_cb = watch_face_set_unlock_cb;
    return hooks;
}

menu_runtime::Hooks buildMenuRuntimeHooks(const Hooks& hooks)
{
    menu_runtime::Hooks runtime_hooks{};
    runtime_hooks.format_time = format_menu_time;
    runtime_hooks.show_main_menu = hooks.show_main_menu;

    const auto default_watch_face = defaultWatchFaceHooks();
    runtime_hooks.watch_face = hooks.watch_face;
    if (!runtime_hooks.watch_face.create)
    {
        runtime_hooks.watch_face.create = default_watch_face.create;
    }
    if (!runtime_hooks.watch_face.set_time)
    {
        runtime_hooks.watch_face.set_time = default_watch_face.set_time;
    }
    if (!runtime_hooks.watch_face.set_node_id)
    {
        runtime_hooks.watch_face.set_node_id = default_watch_face.set_node_id;
    }
    if (!runtime_hooks.watch_face.show)
    {
        runtime_hooks.watch_face.show = default_watch_face.show;
    }
    if (!runtime_hooks.watch_face.is_ready)
    {
        runtime_hooks.watch_face.is_ready = default_watch_face.is_ready;
    }
    if (!runtime_hooks.watch_face.set_unlock_cb)
    {
        runtime_hooks.watch_face.set_unlock_cb = default_watch_face.set_unlock_cb;
    }
    return runtime_hooks;
}

platform::ui::screen::Hooks buildScreenSleepHooks(const Hooks& hooks)
{
    platform::ui::screen::Hooks runtime_hooks{};
    runtime_hooks.format_time = format_menu_time;
    runtime_hooks.read_unread_count = ui::status::get_total_unread;
    runtime_hooks.show_main_menu = hooks.show_main_menu;
    runtime_hooks.on_wake_from_sleep = ui::components::screen_saver_overlay::show;
    runtime_hooks.show_screen_saver = ui::components::screen_saver_overlay::show;
    runtime_hooks.hide_screen_saver = ui::components::screen_saver_overlay::hide;
    runtime_hooks.present_screen_saver = present_screen_saver_now;
    return runtime_hooks;
}

void setBootLogLine(const char* line)
{
    std::printf("[BOOT][UI] log line=%s\n", line ? line : "");
    std::fflush(stdout);
    ui::boot::set_log_line(line);
    present_boot_overlay_now();
}

void beginBootUi(bool waking_from_sleep, const char* initial_line)
{
    std::printf("[BOOT][UI] show waking=%d line=%s\n",
                waking_from_sleep ? 1 : 0,
                initial_line ? initial_line : "");
    std::fflush(stdout);
    if (!waking_from_sleep)
    {
        ui::boot::show();
        setBootLogLine(initial_line);
    }
}

void prepareBootResources()
{
    ::ui::i18n::reload_language();
    ui::feedback::init();
}

void initializeShell(const Hooks& hooks)
{
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(lv_screen_active(), 0, 0);

    ui::menu_layout::InitOptions menu_options{};
    menu_options.messaging = hooks.messaging;
    menu_options.apps = hooks.apps;
    if (hooks.ux_pack_id != nullptr &&
        ui_lvgl_ux::buildMenuForUxPack(hooks.ux_pack_id, s_ux_menu_model))
    {
        menu_options.ux_menu = &s_ux_menu_model;
    }
    ui::menu_layout::init(menu_options);

    ui::menu_runtime::init(
        lv_screen_active(), main_screen, ui::menu_layout::menuPanel(), buildMenuRuntimeHooks(hooks));
    initScreenSaverOverlay();

    if (hooks.set_max_brightness)
    {
        hooks.set_max_brightness();
    }

    ui::menu_runtime::showWatchFace();
    platform::ui::screen::init(buildScreenSleepHooks(hooks));
}

void finalizeStartup(bool waking_from_sleep)
{
    if (!waking_from_sleep && !s_boot_presentation_completed)
    {
        std::printf("[BOOT][UI] first_frame_retry reason=no_display_transaction\n");
        std::fflush(stdout);
        present_boot_overlay_now();
    }
    std::printf("[BOOT][UI] ready waking=%d\n", waking_from_sleep ? 1 : 0);
    std::fflush(stdout);
    std::printf("[BOOT][UI] presentation_completed=%d\n",
                s_boot_presentation_completed ? 1 : 0);
    std::fflush(stdout);
    if (waking_from_sleep)
    {
        platform::ui::screen::record_activity();
    }

    ui::boot::mark_ready();
}

} // namespace ui::startup_shell
