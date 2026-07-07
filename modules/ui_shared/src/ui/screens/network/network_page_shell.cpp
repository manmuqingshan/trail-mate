#include "ui/screens/network/network_page_shell.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/ui_theme.h"

#include <cstdint>

#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif

namespace
{

struct NetworkPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* status_value = nullptr;
};

NetworkPageState g_state;

constexpr uint32_t kBg = 0xF8FAFC;
constexpr uint32_t kPanel = 0xFFFFFF;
constexpr uint32_t kLine = 0xD6DEE7;
constexpr uint32_t kText = 0x18212F;
constexpr uint32_t kMuted = 0x607086;
constexpr uint32_t kAccent = 0x2C7A7B;
constexpr uint32_t kAccentDark = 0x1E5B5C;
constexpr uint32_t kWarn = 0xB45309;

const char* safe_tr(const char* text)
{
    return ::ui::i18n::tr(text);
}

void request_exit()
{
    ui_request_exit_to_menu();
}

void back_event_cb(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        if (key != LV_KEY_ESC && key != LV_KEY_BACKSPACE)
        {
            return;
        }
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }
    request_exit();
}

void set_label(lv_obj_t* label, const char* text, uint32_t color)
{
    if (!label)
    {
        return;
    }
    const char* value = text ? text : "";
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    ::ui::fonts::apply_localized_font(label, value, &lv_font_montserrat_14);
}

void refresh_values()
{
    if (!app::hasAppFacade())
    {
        set_label(g_state.status_value, safe_tr("Reticulum inactive"), kMuted);
        return;
    }

    const app::AppConfig& config = app::configFacade().getConfig();
    if (!chat::infra::isReticulumMeshProtocol(config.mesh_protocol))
    {
        set_label(g_state.status_value, safe_tr("Reticulum inactive"), kMuted);
        return;
    }

    set_label(g_state.status_value,
              config.reticulumConfig().reticulum_anonymous_peer
                  ? safe_tr("Anonymous Peer")
                  : safe_tr("Reticulum active"),
              config.reticulumConfig().reticulum_anonymous_peer ? kWarn : kAccentDark);
}

void refresh_event_cb(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        if (key != LV_KEY_ENTER && key != LV_KEY_RIGHT)
        {
            return;
        }
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }
    refresh_values();
}

lv_obj_t* create_header_button(lv_obj_t* parent,
                               const char* text,
                               lv_event_cb_t cb,
                               int width)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, width, 34);
    lv_obj_set_style_radius(btn, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAccent), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAccentDark), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, nullptr);

    lv_obj_t* label = lv_label_create(btn);
    set_label(label, text, 0xFFFFFF);
    lv_obj_center(label);
    if (app_g)
    {
        lv_group_add_obj(app_g, btn);
    }
    return btn;
}

lv_obj_t* create_info_row(lv_obj_t* parent, const char* title)
{
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(kPanel), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(row, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(row);
    set_label(title_label, safe_tr(title), kMuted);
    lv_obj_set_width(title_label, LV_PCT(100));

    lv_obj_t* value = lv_label_create(row);
    set_label(value, "--", kText);
    lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(value, LV_PCT(100));
    lv_obj_set_style_text_font(value, &lv_font_montserrat_14, LV_PART_MAIN);
    return value;
}

} // namespace

namespace network::ui::shell
{

void enter(void* /*user_data*/, lv_obj_t* parent)
{
    if (!parent || (g_state.root && lv_obj_is_valid(g_state.root)))
    {
        return;
    }

    const auto& profile = ::ui::page_profile::current();
    g_state.root = lv_obj_create(parent);
    lv_obj_set_size(g_state.root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(g_state.root, lv_color_hex(kBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_state.root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_state.root, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.root, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.root, LV_FLEX_FLOW_COLUMN);

    lv_obj_t* header = lv_obj_create(g_state.root);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 38);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header,
                          LV_FLEX_ALIGN_SPACE_BETWEEN,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* back_btn = create_header_button(header,
                                              LV_SYMBOL_LEFT,
                                              back_event_cb,
                                              profile.large_touch_hitbox ? 48 : 40);

    lv_obj_t* title = lv_label_create(header);
    set_label(title, safe_tr("Network"), kText);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, LV_PART_MAIN);

    create_header_button(header,
                         LV_SYMBOL_REFRESH,
                         refresh_event_cb,
                         profile.large_touch_hitbox ? 48 : 40);

    lv_obj_t* content = lv_obj_create(g_state.root);
    lv_obj_set_width(content, LV_PCT(100));
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    g_state.status_value = create_info_row(content, "Status");

    lv_obj_add_event_cb(g_state.root, back_event_cb, LV_EVENT_KEY, nullptr);
    if (app_g)
    {
        lv_group_focus_obj(back_btn);
        set_default_group(app_g);
    }

    refresh_values();
}

void exit(void* /*user_data*/, lv_obj_t* /*parent*/)
{
    if (g_state.root && lv_obj_is_valid(g_state.root))
    {
        lv_obj_del(g_state.root);
    }
    g_state = NetworkPageState{};
}

} // namespace network::ui::shell
