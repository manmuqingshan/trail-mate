#include "platform/ui/wifi_runtime.h"
#include "screen_app_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

constexpr size_t kVisibleSettingRows = 8;
constexpr size_t kFilterCount = 7;
constexpr size_t kMaxFilteredOptions = 36;
constexpr size_t kMaxWifiNetworks = 8;

enum class SettingsFilter : unsigned char
{
    Profile,
    Mesh,
    Radio,
    Wifi,
    Location,
    Device,
    Maintenance,
};

struct SettingsEditorState
{
    lv_obj_t* body = nullptr;
    lv_obj_t* heading = nullptr;
    lv_obj_t* help = nullptr;
    lv_obj_t* textarea = nullptr;
};

struct SettingsPageState
{
    enum class Route : unsigned char
    {
        SectionList,
        OptionList,
        OptionDetail,
        TextEdit,
    };

    ::ui::settings::SettingsSnapshot snapshot{};
    SettingsEditorState editor{};
    size_t selected_option = 0;
    SettingsFilter selected_filter = SettingsFilter::Profile;
    const ::ui::settings::SettingsOption* filtered_options[kMaxFilteredOptions]{};
    size_t filtered_option_count = 0;
    ::ui::settings::SettingsOption wifi_options[8]{};
    ::platform::ui::wifi::ScanResult wifi_scan_results[kMaxWifiNetworks]{};
    size_t wifi_scan_count = 0;
    size_t selected_wifi_network = 0;
    Route route = Route::SectionList;
};

SettingsPageState* s_settings_page_state_storage = nullptr;
bool s_settings_page_state_allocation_failed_logged = false;

void destroy_settings_editor();

SettingsPageState* allocate_settings_page_state()
{
#if defined(ESP_PLATFORM)
    void* const storage =
        heap_caps_malloc(sizeof(SettingsPageState), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const storage = std::malloc(sizeof(SettingsPageState));
#endif
    return storage ? new (storage) SettingsPageState{} : nullptr;
}

bool ensure_settings_page_state()
{
    if (s_settings_page_state_storage != nullptr)
    {
        return true;
    }

    s_settings_page_state_storage = allocate_settings_page_state();
    if (s_settings_page_state_storage != nullptr)
    {
        return true;
    }

    if (!s_settings_page_state_allocation_failed_logged)
    {
        s_settings_page_state_allocation_failed_logged = true;
        std::printf("[UI][Settings] page enter denied reason=psram_state_alloc bytes=%u\n",
                    static_cast<unsigned>(sizeof(SettingsPageState)));
    }
    return false;
}

void release_settings_page_state()
{
    if (s_settings_page_state_storage == nullptr)
    {
        return;
    }

    destroy_settings_editor();
    s_settings_page_state_storage->~SettingsPageState();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_settings_page_state_storage);
#else
    std::free(s_settings_page_state_storage);
#endif
    s_settings_page_state_storage = nullptr;
}

// The settings snapshot holds every category and option.  It is useful only
// while Settings is open, so retain it in PSRAM for that page lifetime rather
// than reserve internal DRAM from boot.
#define s_settings_page_state (*s_settings_page_state_storage)
#define s_settings_editor (s_settings_page_state.editor)

bool load_settings_snapshot()
{
    s_settings_page_state.snapshot = {};
    return ::ui::presentation_sources::runtime_settings_source().buildSettingsSnapshot(
        s_settings_page_state.snapshot);
}

const char* filter_label(SettingsFilter filter)
{
    switch (filter)
    {
    case SettingsFilter::Profile:
        return "PROFILE";
    case SettingsFilter::Mesh:
        return "MESH";
    case SettingsFilter::Radio:
        return "RADIO";
    case SettingsFilter::Wifi:
        return "WI-FI";
    case SettingsFilter::Location:
        return "LOCATION";
    case SettingsFilter::Device:
        return "DEVICE";
    case SettingsFilter::Maintenance:
    default:
        return "MAINTENANCE";
    }
}

SettingsFilter previous_filter(SettingsFilter filter)
{
    const unsigned value = static_cast<unsigned>(filter);
    return static_cast<SettingsFilter>(value == 0 ? kFilterCount - 1U : value - 1U);
}

SettingsFilter next_filter(SettingsFilter filter)
{
    const unsigned value = static_cast<unsigned>(filter);
    return static_cast<SettingsFilter>((value + 1U) % kFilterCount);
}

const ::ui::settings::SettingsSection* find_source_section(const char* title)
{
    for (size_t index = 0; index < s_settings_page_state.snapshot.section_count; ++index)
    {
        const ::ui::settings::SettingsSection& section = s_settings_page_state.snapshot.sections[index];
        if (std::strcmp(section.title.c_str(), title) == 0)
        {
            return &section;
        }
    }
    return nullptr;
}

void append_filtered_option(const ::ui::settings::SettingsOption* option)
{
    if (option != nullptr && s_settings_page_state.filtered_option_count < kMaxFilteredOptions)
    {
        s_settings_page_state.filtered_options[s_settings_page_state.filtered_option_count++] = option;
    }
}

void append_source_section(const char* title)
{
    const ::ui::settings::SettingsSection* const section = find_source_section(title);
    if (section == nullptr)
    {
        return;
    }
    for (size_t index = 0; index < section->option_count; ++index)
    {
        append_filtered_option(&section->options[index]);
    }
}

void set_wifi_option(size_t index,
                     const char* key,
                     const char* label,
                     const char* value,
                     ::ui::settings::SettingControlKind control,
                     bool enabled)
{
    if (index >= (sizeof(s_settings_page_state.wifi_options) /
                  sizeof(s_settings_page_state.wifi_options[0])))
    {
        return;
    }
    ::ui::settings::SettingsOption& option = s_settings_page_state.wifi_options[index];
    option = ::ui::settings::SettingsOption{};
    ::ui::copyText(option.key, key);
    ::ui::copyText(option.label, label);
    ::ui::copyText(option.value_label, value ? value : "");
    option.control = control;
    option.enabled = enabled;
}

void rebuild_wifi_options()
{
    using ::platform::ui::wifi::Config;
    using ::platform::ui::wifi::Status;

    Config config{};
    (void)::platform::ui::wifi::load_config(config);
    const Status status = ::platform::ui::wifi::status();
    if (!status.supported)
    {
        set_wifi_option(0,
                        "wifi_status",
                        "Wi-Fi Status",
                        "UNSUPPORTED",
                        ::ui::settings::SettingControlKind::Action,
                        false);
        return;
    }

    char status_text[48] = {};
    if (status.connected && status.ip[0] != '\0')
    {
        std::snprintf(status_text, sizeof(status_text), "%s", status.ip);
    }
    else if (status.message[0] != '\0')
    {
        std::snprintf(status_text, sizeof(status_text), "%s", status.message);
    }
    else if (status.scanning)
    {
        std::snprintf(status_text, sizeof(status_text), "SCANNING");
    }
    else
    {
        std::snprintf(status_text, sizeof(status_text), status.enabled ? "READY" : "OFF");
    }

    char network_text[48] = {};
    if (s_settings_page_state.wifi_scan_count != 0)
    {
        const size_t index = s_settings_page_state.selected_wifi_network %
                             s_settings_page_state.wifi_scan_count;
        const auto& network = s_settings_page_state.wifi_scan_results[index];
        std::snprintf(network_text, sizeof(network_text), "%s", network.ssid);
    }
    else
    {
        std::snprintf(network_text, sizeof(network_text), "SCAN FIRST");
    }

    set_wifi_option(0,
                    "wifi_enabled",
                    "Wi-Fi Enabled",
                    status.enabled ? "ON" : "OFF",
                    ::ui::settings::SettingControlKind::Toggle,
                    true);
    set_wifi_option(1,
                    "wifi_status",
                    "Status",
                    status_text,
                    ::ui::settings::SettingControlKind::Action,
                    false);
    set_wifi_option(2,
                    "wifi_scan",
                    "Scan Networks",
                    "OPEN",
                    ::ui::settings::SettingControlKind::Action,
                    true);
    set_wifi_option(3,
                    "wifi_network",
                    "Detected Network",
                    network_text,
                    ::ui::settings::SettingControlKind::Choice,
                    s_settings_page_state.wifi_scan_count != 0);
    set_wifi_option(4,
                    "wifi_ssid",
                    "SSID",
                    config.ssid[0] != '\0' ? config.ssid : "UNSET",
                    ::ui::settings::SettingControlKind::Text,
                    true);
    set_wifi_option(5,
                    "wifi_password",
                    "Password",
                    config.password[0] != '\0' ? "HIDDEN" : "UNSET",
                    ::ui::settings::SettingControlKind::Text,
                    true);
    set_wifi_option(6,
                    "wifi_connect",
                    "Connect",
                    status.connected ? "CONNECTED" : "OPEN",
                    ::ui::settings::SettingControlKind::Action,
                    config.ssid[0] != '\0');
    set_wifi_option(7,
                    "wifi_disconnect",
                    "Disconnect",
                    "OPEN",
                    ::ui::settings::SettingControlKind::Action,
                    status.connected);
}

void append_wifi_options()
{
    rebuild_wifi_options();
    const size_t count = ::platform::ui::wifi::is_supported()
                             ? sizeof(s_settings_page_state.wifi_options) /
                                   sizeof(s_settings_page_state.wifi_options[0])
                             : 1U;
    for (size_t index = 0; index < count; ++index)
    {
        append_filtered_option(&s_settings_page_state.wifi_options[index]);
    }
}

void rebuild_filtered_options()
{
    s_settings_page_state.filtered_option_count = 0;
    for (auto& option : s_settings_page_state.filtered_options)
    {
        option = nullptr;
    }

    switch (s_settings_page_state.selected_filter)
    {
    case SettingsFilter::Profile:
        append_source_section("Profile");
        break;
    case SettingsFilter::Mesh:
        append_source_section("Channels");
        append_source_section("Chat");
        append_source_section("Network");
        break;
    case SettingsFilter::Radio:
        append_source_section("Radio");
        break;
    case SettingsFilter::Wifi:
        append_wifi_options();
        break;
    case SettingsFilter::Location:
        append_source_section("GPS");
        append_source_section("Map");
        break;
    case SettingsFilter::Device:
        append_source_section("Device");
        break;
    case SettingsFilter::Maintenance:
    default:
        break;
    }
}

const ::ui::settings::SettingsOption* selected_option()
{
    if (s_settings_page_state.selected_option >= s_settings_page_state.filtered_option_count)
    {
        return nullptr;
    }
    return s_settings_page_state.filtered_options[s_settings_page_state.selected_option];
}

void reset_option_selection()
{
    if (s_settings_page_state.filtered_option_count == 0)
    {
        s_settings_page_state.selected_option = 0;
        return;
    }
    if (s_settings_page_state.selected_option >= s_settings_page_state.filtered_option_count)
    {
        s_settings_page_state.selected_option = s_settings_page_state.filtered_option_count - 1U;
    }
}

const char* control_text(::ui::settings::SettingControlKind control)
{
    switch (control)
    {
    case ::ui::settings::SettingControlKind::Toggle:
        return "TOGGLE";
    case ::ui::settings::SettingControlKind::Choice:
        return "CHOICE";
    case ::ui::settings::SettingControlKind::Number:
        return "NUMBER";
    case ::ui::settings::SettingControlKind::Text:
        return "TEXT";
    case ::ui::settings::SettingControlKind::Action:
    default:
        return "ACTION";
    }
}

bool is_cellular_configuration(const ::ui::settings::SettingsOption* option)
{
    return option != nullptr && std::strcmp(option->key.c_str(), "cellular_config") == 0;
}

bool is_wifi_option(const ::ui::settings::SettingsOption* option)
{
    return option != nullptr && std::strncmp(option->key.c_str(), "wifi_", 5) == 0;
}

bool wifi_key_equals(const ::ui::settings::SettingsOption& option, const char* key)
{
    return std::strcmp(option.key.c_str(), key) == 0;
}

bool apply_wifi_setting(const ::ui::settings::SettingsOption& option,
                        const char* value,
                        const char* success_notice)
{
    using ::platform::ui::wifi::Config;

    Config config{};
    (void)::platform::ui::wifi::load_config(config);
    const char* const safe_value = value ? value : "";

    if (wifi_key_equals(option, "wifi_enabled"))
    {
        config.enabled = !::platform::ui::wifi::status().enabled;
        const bool saved = ::platform::ui::wifi::save_config(config);
        const bool applied = ::platform::ui::wifi::apply_enabled(config.enabled);
        if (!config.enabled)
        {
            s_settings_page_state.wifi_scan_count = 0;
            s_settings_page_state.selected_wifi_network = 0;
        }
        set_notice(saved && (applied || !config.enabled) ? success_notice : "WI-FI CHANGE FAILED");
        return true;
    }

    if (wifi_key_equals(option, "wifi_scan"))
    {
        size_t result_count = 0;
        const bool scanned = ::platform::ui::wifi::scan(s_settings_page_state.wifi_scan_results,
                                                        kMaxWifiNetworks,
                                                        result_count);
        s_settings_page_state.wifi_scan_count = scanned ? result_count : 0;
        s_settings_page_state.selected_wifi_network = 0;
        if (scanned)
        {
            for (size_t index = 0; index < result_count; ++index)
            {
                if (std::strcmp(s_settings_page_state.wifi_scan_results[index].ssid,
                                config.ssid) == 0)
                {
                    s_settings_page_state.selected_wifi_network = index;
                    break;
                }
            }
        }
        set_notice(!scanned            ? "WI-FI SCAN FAILED"
                   : result_count == 0 ? "NO NETWORKS FOUND"
                                       : "NETWORKS FOUND");
        return true;
    }

    if (wifi_key_equals(option, "wifi_network"))
    {
        if (s_settings_page_state.wifi_scan_count == 0)
        {
            set_notice("SCAN NETWORKS FIRST");
            return true;
        }
        const bool previous = std::strcmp(safe_value, "previous") == 0;
        const size_t count = s_settings_page_state.wifi_scan_count;
        s_settings_page_state.selected_wifi_network = previous
                                                          ? (s_settings_page_state.selected_wifi_network == 0
                                                                 ? count - 1U
                                                                 : s_settings_page_state.selected_wifi_network - 1U)
                                                          : (s_settings_page_state.selected_wifi_network + 1U) % count;
        const auto& result = s_settings_page_state.wifi_scan_results[s_settings_page_state.selected_wifi_network];
        std::snprintf(config.ssid, sizeof(config.ssid), "%s", result.ssid);
        Config saved_profile{};
        if (::platform::ui::wifi::find_saved_config(config.ssid, saved_profile))
        {
            saved_profile.enabled = config.enabled;
            config = saved_profile;
        }
        const bool saved = ::platform::ui::wifi::save_config(config);
        set_notice(saved ? "NETWORK SELECTED" : "NETWORK SAVE FAILED");
        return true;
    }

    if (wifi_key_equals(option, "wifi_ssid"))
    {
        if (safe_value[0] == '\0')
        {
            set_notice("SSID REQUIRED");
            return true;
        }
        std::snprintf(config.ssid, sizeof(config.ssid), "%s", safe_value);
        set_notice(::platform::ui::wifi::save_config(config) ? success_notice : "WI-FI SAVE FAILED");
        return true;
    }

    if (wifi_key_equals(option, "wifi_password"))
    {
        std::snprintf(config.password, sizeof(config.password), "%s", safe_value);
        set_notice(::platform::ui::wifi::save_config(config) ? success_notice : "WI-FI SAVE FAILED");
        return true;
    }

    if (wifi_key_equals(option, "wifi_connect"))
    {
        if (config.ssid[0] == '\0')
        {
            set_notice("SSID REQUIRED");
            return true;
        }
        config.enabled = true;
        const bool saved = ::platform::ui::wifi::save_config(config);
        const bool enabled = ::platform::ui::wifi::apply_enabled(true);
        const bool connected = enabled && ::platform::ui::wifi::connect(&config);
        set_notice(saved && connected ? "WI-FI CONNECTING" : "WI-FI CONNECT FAILED");
        return true;
    }

    if (wifi_key_equals(option, "wifi_disconnect"))
    {
        ::platform::ui::wifi::disconnect();
        set_notice("WI-FI DISCONNECTED");
        return true;
    }

    return false;
}

void destroy_settings_editor()
{
    if (app_g != nullptr && valid(s_settings_editor.textarea) &&
        lv_obj_get_group(s_settings_editor.textarea) == app_g)
    {
        lv_group_remove_obj(s_settings_editor.textarea);
    }
    if (valid(s_settings_editor.body))
    {
        lv_obj_del(s_settings_editor.body);
    }
    s_settings_editor = SettingsEditorState{};
}

void style_text_editor(lv_obj_t* textarea)
{
    lv_obj_set_style_text_font(textarea, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(textarea, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(textarea, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_right(textarea, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_top(textarea, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(textarea, 3, LV_PART_MAIN);
}

void render_text_editor(const ::ui::settings::SettingsOption& option)
{
    set_text(s_state.title, "SETTINGS");
    set_text(s_state.subtitle, "EDIT TEXT");
    clear_lines_from(0);

    if (!valid(s_settings_editor.body))
    {
        s_settings_editor.body = lv_obj_create(s_state.root);
        lv_obj_set_pos(s_settings_editor.body, 0, kHeaderRuleY + 1);
        lv_obj_set_size(s_settings_editor.body, kScreenWidth, kActionTop - kHeaderRuleY - 3);
        style_paper(s_settings_editor.body);

        s_settings_editor.heading = create_text(s_settings_editor.body, kContentWidth);
        lv_obj_set_pos(s_settings_editor.heading, kMargin, 8);

        s_settings_editor.textarea = lv_textarea_create(s_settings_editor.body);
        lv_obj_set_pos(s_settings_editor.textarea, kMargin, 30);
        lv_obj_set_size(s_settings_editor.textarea, kContentWidth, 112);
        style_text_editor(s_settings_editor.textarea);
        lv_textarea_set_max_length(s_settings_editor.textarea, 63);
        lv_textarea_set_placeholder_text(s_settings_editor.textarea, "TYPE NEW VALUE");

        // Never preload credentials.  Other editable values are safe to show
        // and starting with the current value makes a small correction useful.
        const bool hidden = std::strcmp(option.value_label.c_str(), "HIDDEN") == 0;
        const bool unset = std::strcmp(option.value_label.c_str(), "UNSET") == 0;
        lv_textarea_set_text(s_settings_editor.textarea, (hidden || unset) ? "" : option.value_label.c_str());

        s_settings_editor.help = create_text(s_settings_editor.body, kContentWidth);
        lv_obj_set_pos(s_settings_editor.help, kMargin, 150);
        set_text(s_settings_editor.help, "BACK CANCELS  SAVE WRITES VALUE");

        if (app_g != nullptr)
        {
            lv_group_add_obj(app_g, s_settings_editor.textarea);
            lv_group_focus_obj(s_settings_editor.textarea);
        }
    }

    std::snprintf(s_state.scratch, sizeof(s_state.scratch), "EDIT: %s", option.label.c_str());
    set_text(s_settings_editor.heading, s_state.scratch);
}

void set_actions_visible(size_t count)
{
    for (size_t index = 0; index < s_state.action_count; ++index)
    {
        set_action_visible(index, index < count);
    }
}

bool apply_selected_setting(const char* value, const char* success_notice)
{
    const ::ui::settings::SettingsOption* const option = selected_option();
    if (option == nullptr || !option->enabled)
    {
        set_notice("OPTION UNAVAILABLE");
        return true;
    }

    if (is_wifi_option(option))
    {
        return apply_wifi_setting(*option, value, success_notice);
    }

    ::ui::settings::SettingsPatchView patch{};
    ::ui::copyText(patch.key, option->key.c_str());
    ::ui::copyText(patch.value, value ? value : "");
    const auto result = ::ui::presentation_sources::runtime_settings_action_sink().applySetting(patch);
    set_notice(result.ok ? success_notice : "CHANGE REJECTED");
    return true;
}

void launch_cellular_settings_async(void*)
{
    if (!::ui::menu_layout::launchAppByStableId("cellular"))
    {
        set_notice("4G SETTINGS UNAVAILABLE");
    }
}

} // namespace

void reset_settings_page_state()
{
    release_settings_page_state();
}

void render_settings()
{
    if (!ensure_settings_page_state())
    {
        set_text(s_state.title, "SETTINGS");
        set_text(s_state.subtitle, "MEMORY");
        set_line(0, "PSRAM REQUIRED FOR SETTINGS");
        set_line(1, "RETURN AFTER MEMORY RECOVERS");
        clear_lines_from(2);
        return;
    }
    if (!load_settings_snapshot())
    {
        destroy_settings_editor();
        set_text(s_state.title, "SETTINGS");
        set_text(s_state.subtitle, "OFFLINE");
        set_line(0, "SETTINGS RUNTIME UNAVAILABLE");
        set_line(1, "BACK TO RETURN");
        clear_lines_from(2);
        return;
    }

    rebuild_filtered_options();
    reset_option_selection();

    if (s_settings_page_state.route == SettingsPageState::Route::SectionList)
    {
        destroy_settings_editor();
        set_text(s_state.title, "SETTINGS");
        set_text(s_state.subtitle, "FILTERS");
        set_linef(0,
                  "FILTER %u/%u",
                  static_cast<unsigned>(s_settings_page_state.selected_filter) + 1U,
                  static_cast<unsigned>(kFilterCount));
        size_t line = 1;
        for (size_t index = 0; index < kFilterCount && line < 9; ++index)
        {
            const SettingsFilter filter = static_cast<SettingsFilter>(index);
            set_linef(line++, "%c %s", filter == s_settings_page_state.selected_filter ? '>' : ' ', filter_label(filter));
        }
        clear_lines_from(line);
        set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "PREV/NEXT SELECT  OPEN FILTER");
        return;
    }

    if (s_settings_page_state.route == SettingsPageState::Route::OptionList)
    {
        destroy_settings_editor();
        set_text(s_state.title, "SETTINGS");
        set_text(s_state.subtitle, "OPTIONS");
        const size_t first = s_settings_page_state.selected_option < kVisibleSettingRows
                                 ? 0
                                 : s_settings_page_state.selected_option - kVisibleSettingRows + 1U;
        set_linef(0,
                  "%s %u/%u",
                  filter_label(s_settings_page_state.selected_filter),
                  static_cast<unsigned>(s_settings_page_state.selected_option + 1U),
                  static_cast<unsigned>(s_settings_page_state.filtered_option_count));
        size_t line = 1;
        for (size_t index = first;
             index < s_settings_page_state.filtered_option_count && line < 9;
             ++index)
        {
            const ::ui::settings::SettingsOption* const option =
                s_settings_page_state.filtered_options[index];
            if (option == nullptr)
            {
                continue;
            }
            set_linef(line++,
                      "%c %s: %s",
                      index == s_settings_page_state.selected_option ? '>' : ' ',
                      option->label.c_str(),
                      option->value_label.c_str());
        }
        clear_lines_from(line);
        set_line(9,
                 s_state.notice[0] != '\0'
                     ? s_state.notice
                 : s_settings_page_state.filtered_option_count == 0 ? "NO ITEMS FOR THIS TARGET"
                                                                    : "PREV/NEXT SELECT  OPEN OPTION");
        return;
    }

    const ::ui::settings::SettingsOption* const option = selected_option();
    if (s_settings_page_state.route == SettingsPageState::Route::TextEdit)
    {
        if (option == nullptr || option->control != ::ui::settings::SettingControlKind::Text)
        {
            destroy_settings_editor();
            s_settings_page_state.route = SettingsPageState::Route::OptionDetail;
            render_settings();
            return;
        }
        render_text_editor(*option);
        return;
    }

    destroy_settings_editor();
    set_text(s_state.title, "SETTINGS");
    set_text(s_state.subtitle, "DETAIL");
    if (option == nullptr)
    {
        set_line(0, "OPTION UNAVAILABLE");
        set_line(1, "BACK TO OPTIONS");
        clear_lines_from(2);
        return;
    }
    set_linef(0, "%s", option->label.c_str());
    set_linef(1, "VALUE %s", option->value_label.c_str());
    set_linef(2, "TYPE %s", control_text(option->control));
    set_linef(3, "KEY %s", option->key.c_str());
    set_line(4, option->enabled ? "OPTION AVAILABLE" : "OPTION DISABLED");
    switch (option->control)
    {
    case ::ui::settings::SettingControlKind::Toggle:
        set_line(5, "TOGGLE WRITES RUNTIME CONFIG");
        break;
    case ::ui::settings::SettingControlKind::Choice:
    case ::ui::settings::SettingControlKind::Number:
        set_line(5, "PREV/NEXT WRITES RUNTIME CONFIG");
        break;
    case ::ui::settings::SettingControlKind::Text:
        set_line(5, "EDIT OPENS TEXT INPUT");
        break;
    case ::ui::settings::SettingControlKind::Action:
    default:
        set_line(5, "OPEN LAUNCHES CONFIGURATION");
        break;
    }
    set_line(6, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO OPTIONS");
    clear_lines_from(7);
}

void add_settings_actions()
{
    add_action("PREV", Action::SettingsPrevious, kMargin, kActionTop, 48);
    add_action("NEXT", Action::SettingsNext, 62, kActionTop, 48);
    add_action("OPEN", Action::SettingsOpen, 116, kActionTop, 54);
    add_action("BACK", Action::Back, 176, kActionTop, 56);
}

bool handle_settings_action(Action action)
{
    if (!ensure_settings_page_state())
    {
        set_notice("SETTINGS MEMORY UNAVAILABLE");
        return action != Action::Back;
    }
    if (!load_settings_snapshot())
    {
        set_notice("SETTINGS RUNTIME UNAVAILABLE");
        return action != Action::Back;
    }
    rebuild_filtered_options();
    reset_option_selection();

    if (s_settings_page_state.route == SettingsPageState::Route::SectionList)
    {
        switch (action)
        {
        case Action::SettingsPrevious:
            s_settings_page_state.selected_filter = previous_filter(s_settings_page_state.selected_filter);
            set_notice("");
            return true;
        case Action::SettingsNext:
            s_settings_page_state.selected_filter = next_filter(s_settings_page_state.selected_filter);
            set_notice("");
            return true;
        case Action::SettingsOpen:
            s_settings_page_state.route = SettingsPageState::Route::OptionList;
            s_settings_page_state.selected_option = 0;
            rebuild_filtered_options();
            set_notice("FILTER OPEN");
            return true;
        default:
            return false;
        }
    }

    if (s_settings_page_state.route == SettingsPageState::Route::OptionList)
    {
        switch (action)
        {
        case Action::Back:
            s_settings_page_state.route = SettingsPageState::Route::SectionList;
            set_notice("SETTINGS FILTERS");
            return true;
        case Action::SettingsPrevious:
        case Action::SettingsNext:
            if (s_settings_page_state.filtered_option_count == 0)
            {
                set_notice("NO ITEMS FOR THIS TARGET");
                return true;
            }
            s_settings_page_state.selected_option = action == Action::SettingsPrevious
                                                        ? (s_settings_page_state.selected_option == 0
                                                               ? s_settings_page_state.filtered_option_count - 1U
                                                               : s_settings_page_state.selected_option - 1U)
                                                        : (s_settings_page_state.selected_option + 1U) %
                                                              s_settings_page_state.filtered_option_count;
            set_notice("");
            return true;
        case Action::SettingsOpen:
            if (s_settings_page_state.filtered_option_count == 0)
            {
                set_notice("NO OPTION SELECTED");
                return true;
            }
            s_settings_page_state.route = SettingsPageState::Route::OptionDetail;
            set_notice("OPTION DETAIL");
            return true;
        default:
            return false;
        }
    }

    if (s_settings_page_state.route == SettingsPageState::Route::TextEdit)
    {
        if (action == Action::Back)
        {
            destroy_settings_editor();
            s_settings_page_state.route = SettingsPageState::Route::OptionDetail;
            set_notice("EDIT CANCELLED");
            return true;
        }
        if (action == Action::SettingsPrevious)
        {
            if (valid(s_settings_editor.textarea))
            {
                lv_textarea_set_text(s_settings_editor.textarea, "");
                set_notice("TEXT CLEARED");
            }
            return true;
        }
        if (action == Action::SettingsOpen)
        {
            const char* const text = valid(s_settings_editor.textarea)
                                         ? lv_textarea_get_text(s_settings_editor.textarea)
                                         : "";
            const bool saved = apply_selected_setting(text, "VALUE SAVED");
            if (saved)
            {
                destroy_settings_editor();
                s_settings_page_state.route = SettingsPageState::Route::OptionDetail;
            }
            return true;
        }
        return false;
    }

    if (action == Action::Back)
    {
        s_settings_page_state.route = SettingsPageState::Route::OptionList;
        set_notice("SECTION OPTIONS");
        return true;
    }

    const ::ui::settings::SettingsOption* const option = selected_option();
    if (option == nullptr || !option->enabled)
    {
        set_notice("OPTION UNAVAILABLE");
        return true;
    }
    if (is_wifi_option(option))
    {
        if (option->control == ::ui::settings::SettingControlKind::Text &&
            action == Action::SettingsOpen)
        {
            s_settings_page_state.route = SettingsPageState::Route::TextEdit;
            set_notice("");
            return true;
        }
        if (option->control == ::ui::settings::SettingControlKind::Toggle &&
            action == Action::SettingsOpen)
        {
            return apply_selected_setting("toggle", "WI-FI UPDATED");
        }
        if (option->control == ::ui::settings::SettingControlKind::Choice &&
            (action == Action::SettingsPrevious || action == Action::SettingsNext))
        {
            return apply_selected_setting(action == Action::SettingsPrevious ? "previous" : "next",
                                          "NETWORK SELECTED");
        }
        if (option->control == ::ui::settings::SettingControlKind::Action &&
            action == Action::SettingsOpen)
        {
            return apply_selected_setting("run", "WI-FI UPDATED");
        }
        set_notice("ACTION NOT AVAILABLE");
        return true;
    }
    if (is_cellular_configuration(option) && action == Action::SettingsOpen)
    {
        if (lv_async_call(launch_cellular_settings_async, nullptr) != LV_RESULT_OK)
        {
            set_notice("4G SETTINGS UNAVAILABLE");
        }
        return true;
    }
    if (option->control == ::ui::settings::SettingControlKind::Toggle && action == Action::SettingsOpen)
    {
        return apply_selected_setting("toggle", "SETTING UPDATED");
    }
    if ((option->control == ::ui::settings::SettingControlKind::Choice ||
         option->control == ::ui::settings::SettingControlKind::Number) &&
        (action == Action::SettingsPrevious || action == Action::SettingsNext))
    {
        return apply_selected_setting(action == Action::SettingsPrevious ? "previous" : "next", "SETTING UPDATED");
    }
    if (option->control == ::ui::settings::SettingControlKind::Text && action == Action::SettingsOpen)
    {
        s_settings_page_state.route = SettingsPageState::Route::TextEdit;
        set_notice("");
        return true;
    }
    if (action == Action::OpenCellularSettings)
    {
        if (lv_async_call(launch_cellular_settings_async, nullptr) != LV_RESULT_OK)
        {
            set_notice("4G SETTINGS UNAVAILABLE");
        }
        return true;
    }
    set_notice("ACTION NOT AVAILABLE");
    return true;
}

void configure_settings_actions()
{
    if (s_state.action_count < 4 || !ensure_settings_page_state())
    {
        return;
    }
    if (s_settings_page_state.route == SettingsPageState::Route::SectionList ||
        s_settings_page_state.route == SettingsPageState::Route::OptionList)
    {
        set_action(0, "PREV", Action::SettingsPrevious);
        set_action(1, "NEXT", Action::SettingsNext);
        set_action(2, "OPEN", Action::SettingsOpen);
        set_action(3, "BACK", Action::Back);
        set_actions_visible(4);
        return;
    }
    if (s_settings_page_state.route == SettingsPageState::Route::TextEdit)
    {
        set_action(0, "CANCEL", Action::Back);
        set_action(1, "CLEAR", Action::SettingsPrevious);
        set_action(2, "SAVE", Action::SettingsOpen);
        set_actions_visible(3);
        if (app_g != nullptr && valid(s_settings_editor.textarea))
        {
            lv_group_focus_obj(s_settings_editor.textarea);
        }
        return;
    }

    const ::ui::settings::SettingsOption* const option = selected_option();
    if (option != nullptr && option->enabled &&
        (option->control == ::ui::settings::SettingControlKind::Choice ||
         option->control == ::ui::settings::SettingControlKind::Number))
    {
        set_action(0, "PREV", Action::SettingsPrevious);
        set_action(1, "NEXT", Action::SettingsNext);
        set_action(2, "BACK", Action::Back);
        set_actions_visible(3);
        return;
    }
    if (option != nullptr && option->enabled && option->control == ::ui::settings::SettingControlKind::Text)
    {
        set_action(0, "EDIT", Action::SettingsOpen);
        set_action(1, "BACK", Action::Back);
        set_actions_visible(2);
        return;
    }
    set_action(0, is_cellular_configuration(option) ? "OPEN 4G" : "TOGGLE", Action::SettingsOpen);
    set_action(1, "BACK", Action::Back);
    set_actions_visible(2);
}

} // namespace ui::mono::screens::screen_240x320::detail
