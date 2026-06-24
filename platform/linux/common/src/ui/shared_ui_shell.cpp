#include "ui/shared_ui_shell.h"

#include "ui/app_catalog.h"
#include "ui/app_runtime.h"
#include "ui/callback_app_screen.h"
#include "ui/screens/chat/chat_page_shell.h"
#include "ui/screens/contacts/contacts_page_shell.h"
#include "ui/screens/extensions/extensions_page_shell.h"
#include "ui/screens/gnss/gnss_skyplot_page_shell.h"
#include "ui/screens/gps/gps_page_shell.h"
#include "ui/screens/settings/settings_page_shell.h"
#include "ui/screens/team/team_page_shell.h"
#include "ui/screens/tracker/tracker_page_shell.h"
#include "ui/screens/walkie_talkie/walkie_talkie_page_shell.h"
#include "ui/startup_ui_shell.h"
#include "ui/ui_boot.h"

namespace trailmate::cardputer_zero::linux_ui
{
namespace
{

extern "C"
{
    extern const lv_image_dsc_t Chat;
    extern const lv_image_dsc_t contact;
    extern const lv_image_dsc_t gps_icon;
    extern const lv_image_dsc_t Satellite;
    extern const lv_image_dsc_t Setting;
    extern const lv_image_dsc_t team_icon;
    extern const lv_image_dsc_t tracker_icon;
    extern const lv_image_dsc_t ext;
    extern const lv_image_dsc_t walkie_talkie;
}

struct PageSpec
{
    const char* stable_id = nullptr;
    const char* title = nullptr;
    const lv_image_dsc_t* icon = nullptr;
};

PageSpec s_chat_spec{"chat", "Chat", &Chat};
PageSpec s_map_spec{"map", "Map", &gps_icon};
PageSpec s_contacts_spec{"contacts", "Contacts", &contact};
PageSpec s_sky_plot_spec{"sky_plot", "Sky Plot", &Satellite};
PageSpec s_team_spec{"team", "Team", &team_icon};
PageSpec s_tracker_spec{"tracker", "Tracker", &tracker_icon};
PageSpec s_extensions_spec{"extensions", "Extensions", &ext};
PageSpec s_walkie_spec{"walkie_talkie", "Walkie Talkie", &walkie_talkie};

gps::ui::shell::RouteSpec s_map_route{
    nullptr,
    gps::ui::shell::Projection::Map,
};

void gps_route_enter(void* user_data, lv_obj_t* parent)
{
    gps::ui::shell::enter_route(
        static_cast<const gps::ui::shell::RouteSpec*>(user_data),
        parent);
}

void gps_route_exit(void* user_data, lv_obj_t* parent)
{
    gps::ui::shell::exit_route(
        static_cast<const gps::ui::shell::RouteSpec*>(user_data),
        parent);
}

ui::CallbackAppScreen s_chat_app{
    s_chat_spec.stable_id,
    s_chat_spec.title,
    s_chat_spec.icon,
    chat::ui::shell::enter,
    chat::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_map_app{
    s_map_spec.stable_id,
    s_map_spec.title,
    s_map_spec.icon,
    gps_route_enter,
    gps_route_exit,
    &s_map_route,
};

ui::CallbackAppScreen s_contacts_app{
    s_contacts_spec.stable_id,
    s_contacts_spec.title,
    s_contacts_spec.icon,
    contacts::ui::shell::enter,
    contacts::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_sky_plot_app{
    s_sky_plot_spec.stable_id,
    s_sky_plot_spec.title,
    s_sky_plot_spec.icon,
    gnss::ui::shell::enter,
    gnss::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_team_app{
    s_team_spec.stable_id,
    s_team_spec.title,
    s_team_spec.icon,
    team::ui::shell::enter,
    team::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_tracker_app{
    s_tracker_spec.stable_id,
    s_tracker_spec.title,
    s_tracker_spec.icon,
    tracker::ui::shell::enter,
    tracker::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_extensions_app{
    s_extensions_spec.stable_id,
    s_extensions_spec.title,
    s_extensions_spec.icon,
    extensions::ui::shell::enter,
    extensions::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_walkie_app{
    s_walkie_spec.stable_id,
    s_walkie_spec.title,
    s_walkie_spec.icon,
    walkie_page::ui::shell::enter,
    walkie_page::ui::shell::exit,
    nullptr,
};

ui::CallbackAppScreen s_settings_app{
    "settings",
    "Settings",
    &Setting,
    settings::ui::shell::enter,
    settings::ui::shell::exit,
    nullptr,
};

AppScreen* s_apps[] = {
    &s_chat_app,
    &s_map_app,
    &s_sky_plot_app,
    &s_contacts_app,
    &s_team_app,
    &s_tracker_app,
    &s_walkie_app,
    &s_extensions_app,
    &s_settings_app,
};

ui::StaticAppCatalogState& catalogState()
{
    static ui::StaticAppCatalogState state = ui::makeStaticAppCatalogState(s_apps);
    return state;
}

[[nodiscard]] ui::startup_ui_shell::Hooks buildHooks()
{
    ui::startup_ui_shell::Hooks hooks{};
    hooks.apps = ui::makeStaticAppCatalog(&catalogState());
    hooks.ux_pack_id = "cardputer_compact";
    return hooks;
}

constexpr unsigned int kMenuBuildDelayMs = 140U;
constexpr unsigned int kThemeReadyDelayMs = 280U;
constexpr unsigned int kFinalizeDelayMs = 420U;

} // namespace

bool SharedUiShellStartup::begin()
{
    if (phase_ != Phase::Idle)
    {
        return true;
    }

    started_at_ms_ = lv_tick_get();
    if (!ui::startup_ui_shell::beginBootUi(buildHooks(), false, "Loading language packs..."))
    {
        return false;
    }
    ui::startup_ui_shell::prepareBootResources();

    phase_ = Phase::BootVisible;
    return true;
}

bool SharedUiShellStartup::runPhase(Phase next_phase, const char* log_line)
{
    if (log_line != nullptr && log_line[0] != '\0')
    {
        ui::boot::set_log_line(log_line);
    }
    phase_ = next_phase;
    return true;
}

bool SharedUiShellStartup::tick()
{
    if (phase_ == Phase::Idle)
    {
        return begin();
    }

    if (phase_ == Phase::Finalized)
    {
        return true;
    }

    const unsigned int elapsed = lv_tick_elaps(started_at_ms_);
    if (phase_ == Phase::BootVisible && elapsed >= kMenuBuildDelayMs)
    {
        ui::boot::set_log_line("Building menu shell...");
        if (!ui::startup_ui_shell::initializeMenuSkeleton(buildHooks()))
        {
            return false;
        }
        return runPhase(Phase::MenuBuilt, nullptr);
    }

    if (phase_ == Phase::MenuBuilt && elapsed >= kThemeReadyDelayMs)
    {
        return runPhase(Phase::ThemeReady, "Preparing navigation and apps...");
    }

    if (phase_ == Phase::ThemeReady && elapsed >= kFinalizeDelayMs)
    {
        ui::boot::set_log_line("Starting Trail Mate...");
        if (!ui::startup_ui_shell::finalizeStartup(buildHooks(), false))
        {
            return false;
        }
        return runPhase(Phase::Finalized, nullptr);
    }

    return true;
}

bool SharedUiShellStartup::ready() const noexcept
{
    return phase_ == Phase::Finalized;
}

} // namespace trailmate::cardputer_zero::linux_ui
