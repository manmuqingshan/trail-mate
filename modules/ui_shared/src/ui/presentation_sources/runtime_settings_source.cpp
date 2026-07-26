#include "ui/presentation_sources/runtime_settings_source.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "platform/ui/gps_runtime.h"
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
                 app::configFacade().getConfig().gps_enabled ? "ON" : "OFF");
    out.sections[0].options[0].control =
        ui::settings::SettingControlKind::Toggle;
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
