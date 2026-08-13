#include "ui/presentation_sources/runtime_settings_source.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/gps_runtime.h"
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
#include "platform/ui/a7682e_cellular_runtime.h"
#endif
#include "sys/clock.h"

#include <cstring>

namespace ui::presentation_sources
{
namespace
{

bool keyEquals(const ui::settings::SettingsPatchView& patch, const char* key)
{
    return std::strcmp(patch.key.c_str(), key) == 0;
}

bool parseBool(const char* text, bool& out)
{
    if (!text)
    {
        return false;
    }
    if (std::strcmp(text, "1") == 0 || std::strcmp(text, "true") == 0 ||
        std::strcmp(text, "TRUE") == 0 || std::strcmp(text, "on") == 0 ||
        std::strcmp(text, "ON") == 0)
    {
        out = true;
        return true;
    }
    if (std::strcmp(text, "0") == 0 || std::strcmp(text, "false") == 0 ||
        std::strcmp(text, "FALSE") == 0 || std::strcmp(text, "off") == 0 ||
        std::strcmp(text, "OFF") == 0)
    {
        out = false;
        return true;
    }
    return false;
}

} // namespace

bool RuntimeSettingsSource::buildSettingsSnapshot(
    ui::settings::SettingsSnapshot& out) const
{
    out = ui::settings::SettingsSnapshot{};
    out.header.valid = true;
    out.header.version = 1;
    out.header.generated_at_ms = sys::millis_now();

    out.section_count = 1;
    ui::copyText(out.sections[0].title, "GPS");
    out.sections[0].option_count = 1;
    ui::copyText(out.sections[0].options[0].key, "gps_enabled");
    ui::copyText(out.sections[0].options[0].label, "GPS Enabled");
    ui::copyText(out.sections[0].options[0].value_label,
                 app::configFacade().readConfig().gps_enabled ? "ON" : "OFF");
    out.sections[0].options[0].control =
        ui::settings::SettingControlKind::Toggle;
#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    const auto& cellular = ::platform::ui::a7682e::status();
    out.section_count = 2;
    ui::copyText(out.sections[1].title, "4G Cellular");
    out.sections[1].option_count = 2;
    ui::copyText(out.sections[1].options[0].key, "cellular_enabled");
    ui::copyText(out.sections[1].options[0].label, "4G Enabled");
    ui::copyText(out.sections[1].options[0].value_label,
                 cellular.enabled ? "ON" : "OFF");
    out.sections[1].options[0].control = ui::settings::SettingControlKind::Toggle;
    ui::copyText(out.sections[1].options[1].key, "cellular_status");
    ui::copyText(out.sections[1].options[1].label, "4G Status");
    ui::copyText(out.sections[1].options[1].value_label,
                 ::platform::ui::a7682e::service_state_label(
                     ::platform::ui::a7682e::service_state()));
    out.sections[1].options[1].control = ui::settings::SettingControlKind::Action;
    out.sections[1].options[1].enabled = false;
#endif
    return true;
}

ui::UiActionResult RuntimeSettingsActionSink::applySetting(
    const ui::settings::SettingsPatchView& patch)
{
    if (keyEquals(patch, "gps_enabled") || keyEquals(patch, "gps.enabled"))
    {
        bool enabled = false;
        if (!parseBool(patch.value.c_str(), enabled))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }

        app::IAppFacade& app_ctx = app::appFacade();
        auto edit = app_ctx.beginConfigEdit();
        if (!edit)
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
        }
        edit.config().gps_enabled = enabled;
        edit.commit(app::AppConfigChangeSet::gps());
        ::platform::ui::gps::set_enabled(enabled);
        return ui::UiActionResult::success();
    }

#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)
    if (keyEquals(patch, "cellular_enabled") || keyEquals(patch, "cellular.enabled"))
    {
        bool enabled = false;
        if (!parseBool(patch.value.c_str(), enabled))
        {
            return ui::UiActionResult::fail(ui::UiActionFailure::InvalidInput);
        }
        return ::platform::ui::a7682e::set_enabled(enabled)
                   ? ui::UiActionResult::success()
                   : ui::UiActionResult::fail(ui::UiActionFailure::NotReady);
    }
#endif

    return ui::UiActionResult::fail(ui::UiActionFailure::Unsupported);
}

RuntimeSettingsSource& runtime_settings_source()
{
    static RuntimeSettingsSource source;
    return source;
}

RuntimeSettingsActionSink& runtime_settings_action_sink()
{
    static RuntimeSettingsActionSink sink;
    return sink;
}

} // namespace ui::presentation_sources
