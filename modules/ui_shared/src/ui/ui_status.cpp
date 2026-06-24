/**
 * @file ui_status.cpp
 * @brief Global UI status indicators (top bar icons + menu badges)
 */

#include "ui/ui_status.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#if !defined(TRAIL_MATE_FINAL_IDF_NO_APP_FACADE)
#include "chat/usecase/chat_service.h"
#endif
#include "platform/ui/lora_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "platform/ui/wifi_runtime.h"
#include "sys/clock.h"
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
#include "platform/ui/team_ui_snapshot_store.h"
#endif
#include "ui/presentation_sources/runtime_gps_status_source.h"
#include "ui_presentation/gps/gps_status_model.h"

#include <cstdio>

#if __has_include("ble/ble_manager.h")
#include "ble/ble_manager.h"
#define TRAIL_MATE_UI_STATUS_HAS_BLE_MANAGER_HEADER 1
#else
#define TRAIL_MATE_UI_STATUS_HAS_BLE_MANAGER_HEADER 0
#endif

extern "C"
{
    extern const lv_image_dsc_t gps_topbar;
    extern const lv_image_dsc_t message_topbar;
    extern const lv_image_dsc_t route_topbar;
    extern const lv_image_dsc_t wifi_topbar;
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
    extern const lv_image_dsc_t team_topbar;
#endif
    extern const lv_image_dsc_t tracker_topbar;
    extern const lv_image_dsc_t ble_topbar;
    extern const lv_image_dsc_t lora_mod_topbar;
    extern const lv_image_dsc_t fsk_mod_topbar;
    extern const lv_image_dsc_t walkie_monitor_topbar;
}

namespace ui
{
namespace status
{

namespace
{
struct StatusSnapshot
{
    bool route_active = false;
    bool track_recording = false;
    bool gps_enabled = false;
    bool wifi_enabled = false;
    bool team_active = false;
    bool ble_enabled = false;
    bool radio_mod_visible = false;
    bool radio_mod_fsk = false;
    bool walkie_monitor = false;
    int unread = 0;
};

struct TeamSnapshotCache
{
    bool valid = false;
    uint32_t last_refresh_ms = 0;
    bool team_active = false;
    int team_unread = 0;
};

lv_timer_t* s_status_timer = nullptr;
lv_obj_t* s_menu_status_row = nullptr;
lv_obj_t* s_menu_route_icon = nullptr;
lv_obj_t* s_menu_tracker_icon = nullptr;
lv_obj_t* s_menu_gps_icon = nullptr;
lv_obj_t* s_menu_wifi_icon = nullptr;
lv_obj_t* s_menu_team_icon = nullptr;
lv_obj_t* s_menu_msg_icon = nullptr;
lv_obj_t* s_menu_ble_icon = nullptr;
lv_obj_t* s_menu_radio_mod_icon = nullptr;
lv_obj_t* s_menu_walkie_monitor_icon = nullptr;
lv_obj_t* s_chat_badge = nullptr;
lv_obj_t* s_chat_badge_label = nullptr;
TeamSnapshotCache s_team_cache;
constexpr uint32_t kTeamSnapshotRefreshMs = 5000;
bool s_menu_active = true;

::ui::gps::GpsStatusModel& gpsStatusModel()
{
    static ::ui::gps::GpsStatusModel model(
        ::ui::presentation_sources::runtime_gps_status_source());
    return model;
}

bool obj_valid(lv_obj_t* obj)
{
    return obj && lv_obj_is_valid(obj);
}

void refresh_team_cache(bool force = false)
{
#if defined(GAT562_NO_TEAM) && GAT562_NO_TEAM
    (void)force;
    s_team_cache.team_active = false;
    s_team_cache.team_unread = 0;
    s_team_cache.valid = true;
    s_team_cache.last_refresh_ms = sys::millis_now();
    return;
#else
    const uint32_t now = sys::millis_now();
    if (!force && s_team_cache.valid && (now - s_team_cache.last_refresh_ms) < kTeamSnapshotRefreshMs)
    {
        return;
    }

    team::ui::TeamUiSnapshot snap;
    if (team::ui::team_ui_snapshot_store().load(snap))
    {
        s_team_cache.team_active = snap.in_team;
        s_team_cache.team_unread = static_cast<int>(snap.team_chat_unread);
    }
    else
    {
        s_team_cache.team_active = false;
        s_team_cache.team_unread = 0;
    }
    s_team_cache.valid = true;
    s_team_cache.last_refresh_ms = now;
#endif
}

StatusSnapshot collect_status()
{
    StatusSnapshot snap{};

    if (app::hasAppFacade())
    {
        const auto& cfg = app::configFacade().getConfig();
        snap.route_active = cfg.route_enabled && (cfg.route_path[0] != '\0');
#if !defined(TRAIL_MATE_FINAL_IDF_NO_APP_FACADE)
        snap.ble_enabled = app::runtimeFacade().isBleEnabled();
#if TRAIL_MATE_UI_STATUS_HAS_BLE_MANAGER_HEADER
        if (auto* ble = app::runtimeFacade().getBleManager())
        {
            snap.ble_enabled = ble->isEnabled();
        }
#endif
#endif
    }

    snap.track_recording = platform::ui::tracker::is_recording();
    const auto gps = gpsStatusModel().snapshot();
    snap.gps_enabled = gps.header.valid && gps.receiver_enabled;
    snap.wifi_enabled = platform::ui::wifi::status().enabled;
    const auto walkie = platform::ui::walkie::get_status();
    snap.radio_mod_visible = platform::ui::lora::is_supported() ||
                             platform::ui::walkie::is_supported() ||
                             walkie.active || walkie.monitor_enabled;
    snap.radio_mod_fsk = walkie.active;
    snap.walkie_monitor = walkie.monitor_enabled;

    refresh_team_cache();
    snap.team_active = s_team_cache.team_active;

    snap.unread = get_total_unread();
    return snap;
}

void apply_icon(lv_obj_t* icon, const lv_image_dsc_t* src, bool visible)
{
    if (!obj_valid(icon))
    {
        return;
    }
    if (src)
    {
        lv_image_set_src(icon, src);
    }
    if (visible)
    {
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
}

void apply_menu_icons(const StatusSnapshot& snap)
{
    if (!obj_valid(s_menu_status_row))
    {
        return;
    }

    apply_icon(s_menu_route_icon, &route_topbar, snap.route_active);
    apply_icon(s_menu_tracker_icon, &tracker_topbar, snap.track_recording);
    apply_icon(s_menu_gps_icon, &gps_topbar, snap.gps_enabled);
    apply_icon(s_menu_wifi_icon, &wifi_topbar, snap.wifi_enabled);
#if !defined(GAT562_NO_TEAM) || !GAT562_NO_TEAM
    apply_icon(s_menu_team_icon, &team_topbar, snap.team_active);
#else
    apply_icon(s_menu_team_icon, nullptr, false);
#endif
    apply_icon(s_menu_msg_icon, &message_topbar, snap.unread > 0);
    apply_icon(s_menu_ble_icon, &ble_topbar, snap.ble_enabled);
    apply_icon(s_menu_radio_mod_icon,
               snap.radio_mod_fsk ? &fsk_mod_topbar : &lora_mod_topbar,
               snap.radio_mod_visible);
    apply_icon(s_menu_walkie_monitor_icon, &walkie_monitor_topbar, snap.walkie_monitor);

    const bool any = snap.route_active || snap.track_recording || snap.gps_enabled ||
                     snap.wifi_enabled || snap.team_active || snap.ble_enabled ||
                     snap.radio_mod_visible || snap.walkie_monitor || (snap.unread > 0);
    if (any)
    {
        lv_obj_clear_flag(s_menu_status_row, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_menu_status_row, LV_OBJ_FLAG_HIDDEN);
    }
}

void apply_menu_badge(const StatusSnapshot& snap)
{
    if (!obj_valid(s_chat_badge) || !obj_valid(s_chat_badge_label))
    {
        return;
    }
    if (snap.unread <= 0)
    {
        lv_obj_add_flag(s_chat_badge, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    char buf[12];
    snprintf(buf, sizeof(buf), "%d", snap.unread);
    lv_label_set_text(s_chat_badge_label, buf);
    lv_obj_clear_flag(s_chat_badge, LV_OBJ_FLAG_HIDDEN);
}

void status_timer_cb(lv_timer_t* /*timer*/)
{
    if (!s_menu_active)
    {
        return;
    }

    StatusSnapshot snap = collect_status();

    apply_menu_icons(snap);
    apply_menu_badge(snap);
}
} // namespace

void init()
{
    if (s_status_timer)
    {
        return;
    }
    s_status_timer = lv_timer_create(status_timer_cb, 1000, nullptr);
    if (s_status_timer)
    {
        lv_timer_set_repeat_count(s_status_timer, -1);
    }
    status_timer_cb(nullptr);
}

void register_menu_status_row(lv_obj_t* row,
                              lv_obj_t* route_icon,
                              lv_obj_t* tracker_icon,
                              lv_obj_t* gps_icon,
                              lv_obj_t* wifi_icon,
                              lv_obj_t* team_icon,
                              lv_obj_t* msg_icon,
                              lv_obj_t* ble_icon,
                              lv_obj_t* radio_mod_icon,
                              lv_obj_t* walkie_monitor_icon)
{
    s_menu_status_row = row;
    s_menu_route_icon = route_icon;
    s_menu_tracker_icon = tracker_icon;
    s_menu_gps_icon = gps_icon;
    s_menu_wifi_icon = wifi_icon;
    s_menu_team_icon = team_icon;
    s_menu_msg_icon = msg_icon;
    s_menu_ble_icon = ble_icon;
    s_menu_radio_mod_icon = radio_mod_icon;
    s_menu_walkie_monitor_icon = walkie_monitor_icon;
    status_timer_cb(nullptr);
}

void register_chat_badge(lv_obj_t* badge_bg, lv_obj_t* badge_label)
{
    s_chat_badge = badge_bg;
    s_chat_badge_label = badge_label;
    status_timer_cb(nullptr);
}

void force_update()
{
    refresh_team_cache(true);
    status_timer_cb(nullptr);
}

void set_menu_active(bool active)
{
    s_menu_active = active;
    if (s_status_timer == nullptr)
    {
        return;
    }

    if (active)
    {
        lv_timer_resume(s_status_timer);
        status_timer_cb(nullptr);
    }
    else
    {
        lv_timer_pause(s_status_timer);
    }
}

int get_total_unread()
{
    int unread = 0;
#if !defined(TRAIL_MATE_FINAL_IDF_NO_APP_FACADE)
    if (app::hasAppFacade())
    {
        chat::ChatService& chat = app::messagingFacade().getChatService();
        unread = chat.getTotalUnread();
    }
#endif
    refresh_team_cache();
    return unread + s_team_cache.team_unread;
}

} // namespace status
} // namespace ui
