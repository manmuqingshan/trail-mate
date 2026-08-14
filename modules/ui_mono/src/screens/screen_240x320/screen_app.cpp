#include "screen_app_internal.h"

#include "ui/assets/fonts/font_utils.h"
#include "ui/localization.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ui::mono::screens::screen_240x320::detail
{

ScreenState s_state;

bool valid(const lv_obj_t* object)
{
    return object != nullptr && lv_obj_is_valid(const_cast<lv_obj_t*>(object));
}

void style_paper(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_text(lv_obj_t* parent, lv_coord_t width, lv_text_align_t align)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, align, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void set_text(lv_obj_t* label, const char* text)
{
    if (!valid(label))
    {
        return;
    }

    const char* const safe = text ? text : "";
    const char* const previous = lv_label_get_text(label);
    if (previous != nullptr && std::strcmp(previous, safe) == 0)
    {
        return;
    }
    lv_label_set_text(label, safe);
    ::ui::fonts::apply_localized_font(label, safe, LV_FONT_DEFAULT);
}

void set_line(size_t index, const char* text)
{
    if (index >= kMaxLines)
    {
        return;
    }
    set_text(s_state.lines[index], text);
}

void set_linef(size_t index, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(s_state.scratch, sizeof(s_state.scratch), format, args);
    va_end(args);
    set_line(index, s_state.scratch);
}

void clear_lines_from(size_t index)
{
    for (; index < kMaxLines; ++index)
    {
        set_line(index, "");
    }
}

void set_notice(const char* text)
{
    std::snprintf(s_state.notice, sizeof(s_state.notice), "%s", text ? text : "");
}

void set_action(size_t index, const char* label, Action action)
{
    if (index >= s_state.action_count)
    {
        return;
    }

    ActionButton& entry = s_state.actions[index];
    entry.action = action;
    set_text(entry.label, ::ui::i18n::tr(label));
}

void set_action_visible(size_t index, bool visible)
{
    if (index >= s_state.action_count || !valid(s_state.actions[index].button))
    {
        return;
    }

    ActionButton& entry = s_state.actions[index];
    if (visible)
    {
        lv_obj_clear_flag(entry.button, LV_OBJ_FLAG_HIDDEN);
        if (app_g != nullptr && lv_obj_get_group(entry.button) != app_g)
        {
            lv_group_add_obj(app_g, entry.button);
        }
        return;
    }
    if (app_g != nullptr && lv_obj_get_group(entry.button) == app_g)
    {
        lv_group_remove_obj(entry.button);
    }
    lv_obj_add_flag(entry.button, LV_OBJ_FLAG_HIDDEN);
}

void focus_action(size_t index)
{
    if (index < s_state.action_count && valid(s_state.actions[index].button))
    {

        lv_group_focus_obj(s_state.actions[index].button);
    }
}

void apply_button_focus(ActionButton& action, bool focused)
{
    if (!valid(action.button) || !valid(action.label))
    {
        return;
    }

    lv_obj_set_style_bg_color(action.button, focused ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(action.label, focused ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
}

ActionButton* action_for(lv_obj_t* button)
{
    for (size_t index = 0; index < s_state.action_count; ++index)
    {
        if (s_state.actions[index].button == button)
        {
            return &s_state.actions[index];
        }
    }
    return nullptr;
}

void run_action(Action action)
{
    if (action == Action::Refresh)
    {
        set_notice("UPDATED");
        refresh_page();
        return;
    }
    if (run_page_action(action))
    {
        refresh_page();
        return;
    }
    if (action == Action::Back)
    {
        ui_request_exit_to_menu();
    }
}

void on_button_event(lv_event_t* event)
{
    lv_obj_t* button = lv_event_get_target_obj(event);
    ActionButton* action = action_for(button);
    if (action == nullptr)
    {
        return;
    }

    switch (lv_event_get_code(event))
    {
    case LV_EVENT_FOCUSED:
        apply_button_focus(*action, true);
        break;
    case LV_EVENT_DEFOCUSED:
        apply_button_focus(*action, false);
        break;
    case LV_EVENT_CLICKED:
        run_action(action->action);
        break;
    case LV_EVENT_KEY:
    {
        const uint32_t key = lv_event_get_key(event);
        if (key == 'w' || key == 'W')
        {
            lv_group_focus_prev(app_g);
            lv_event_stop_processing(event);
        }
        else if (key == 's' || key == 'S')
        {
            lv_group_focus_next(app_g);
            lv_event_stop_processing(event);
        }
        else if (key == LV_KEY_BACKSPACE || key == '\b' || key == LV_KEY_ESC)
        {
            run_action(Action::Back);
            lv_event_stop_processing(event);
        }
        break;
    }
    default:
        break;
    }
}

void add_action(const char* label, Action action, lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    if (s_state.action_count >= kMaxActions || !valid(s_state.root))
    {
        return;
    }

    ActionButton& entry = s_state.actions[s_state.action_count++];
    entry.action = action;
    entry.button = lv_btn_create(s_state.root);
    lv_obj_set_pos(entry.button, x, y);
    lv_obj_set_size(entry.button, width, 18);
    lv_obj_set_style_bg_color(entry.button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(entry.button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(entry.button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(entry.button, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(entry.button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(entry.button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(entry.button, 0, LV_PART_MAIN);
    lv_obj_clear_flag(entry.button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(entry.button, on_button_event, LV_EVENT_ALL, nullptr);

    entry.label = create_text(entry.button, width - 2, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(entry.label);
    set_text(entry.label, ::ui::i18n::tr(label));
    lv_group_add_obj(app_g, entry.button);
}

void create_root(lv_obj_t* parent, ScreenApp& adapter)
{
    s_state = ScreenState{};
    reset_page_state(adapter.page_kind());
    s_state.adapter = &adapter;
    s_state.root = lv_obj_create(parent);
    lv_obj_set_pos(s_state.root, 0, 0);
    lv_obj_set_size(s_state.root, kScreenWidth, kScreenHeight);
    style_paper(s_state.root);

    s_state.title = create_text(s_state.root, 160);
    lv_obj_set_pos(s_state.title, kMargin, kMargin);
    set_text(s_state.title, adapter.name());

    s_state.subtitle = create_text(s_state.root, 56, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_state.subtitle, kScreenWidth - kMargin - 56, kMargin);
    set_text(s_state.subtitle, "TEXT UI");

    lv_obj_t* rule = lv_obj_create(s_state.root);
    lv_obj_set_pos(rule, kMargin, kHeaderRuleY);
    lv_obj_set_size(rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rule, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t index = 0; index < kMaxLines; ++index)
    {
        s_state.lines[index] = create_text(s_state.root, kContentWidth);
        lv_obj_set_pos(s_state.lines[index], kMargin, kBodyTop + static_cast<lv_coord_t>(index) * kLineHeight);
    }

    create_page_actions(adapter.page_kind());

    lv_obj_t* footer_rule = lv_obj_create(s_state.root);
    lv_obj_set_pos(footer_rule, kMargin, kFooterRuleY);
    lv_obj_set_size(footer_rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(footer_rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(footer_rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(footer_rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(footer_rule, LV_OBJ_FLAG_SCROLLABLE);

    s_state.footer = create_text(s_state.root, kContentWidth);
    lv_obj_set_pos(s_state.footer, kMargin, kFooterTop);
    lv_obj_set_height(s_state.footer, kLineHeight);
    lv_obj_set_style_bg_color(s_state.footer, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_state.footer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_state.footer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_state.footer, 2, LV_PART_MAIN);
    set_text(s_state.footer, "W/S ENT ACT  BKSP BACK");
    if (adapter.page_kind() != PageKind::Map && adapter.page_kind() != PageKind::Chat &&
        adapter.page_kind() != PageKind::Team && adapter.page_kind() != PageKind::Contacts &&
        adapter.page_kind() != PageKind::Settings &&
        adapter.page_kind() != PageKind::Extensions &&
        adapter.page_kind() != PageKind::ProtocolProbe)
    {
        add_action("BACK", Action::Back, kMargin, kButtonTop, 58);
    }

    if (s_state.action_count > 0)
    {
        lv_group_focus_obj(s_state.actions[0].button);
    }

    refresh_page();
}

void destroy_root(lv_obj_t* parent)
{
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == PageKind::Map)
    {
        // The map viewport owns a tile-loader timer and LVGL children.  It
        // must release both before the generic root is deleted.  Resetting
        // also releases its PSRAM-backed page snapshot.
        reset_map_page_state();
    }
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == PageKind::SkyPlot)
    {
        // The sky plot owns a live GNSS refresh timer and its polar-chart
        // children.  Release them before the generic root is deleted.
        destroy_sky_plot_page();
    }
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == PageKind::Chat)
    {
        reset_direct_chat_flow();
    }
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == PageKind::Team)
    {
        reset_team_page_state();
    }
    if (s_state.adapter != nullptr && s_state.adapter->page_kind() == PageKind::Settings)
    {
        reset_settings_page_state();
    }
    if (app_g != nullptr)
    {
        lv_group_remove_all_objs(app_g);
    }
    if (valid(s_state.root))
    {
        lv_obj_del(s_state.root);
    }
    s_state = ScreenState{};
    (void)parent;
}

} // namespace ui::mono::screens::screen_240x320::detail

namespace ui::mono::screens::screen_240x320
{

ScreenApp::ScreenApp(const char* stable_id,
                     const char* name,
                     PageKind page_kind,
                     ui::AppLaunchMode launch_mode)
    : stable_id_(stable_id ? stable_id : ""),
      name_(name ? name : ""),
      page_kind_(page_kind),
      launch_mode_(launch_mode)
{
}

const char* ScreenApp::stable_id() const
{
    return stable_id_;
}

const char* ScreenApp::name() const
{
    return name_;
}

const lv_image_dsc_t* ScreenApp::icon() const
{
    return nullptr;
}

ui::AppLaunchMode ScreenApp::launch_mode() const
{
    return launch_mode_;
}

void ScreenApp::enter(lv_obj_t* parent)
{
    if (parent == nullptr)
    {
        return;
    }
    if (app_g != nullptr)
    {
        lv_group_remove_all_objs(app_g);
        set_default_group(app_g);
    }
    detail::create_root(parent, *this);
}

void ScreenApp::exit(lv_obj_t* parent)
{
    // Protocol probing owns an active scan; stop it when its projection exits.
    if (page_kind_ == PageKind::ProtocolProbe)
    {
        if (::ui::mono::screens::screen_240x320::ProtocolProbePort* const port =
                ::ui::mono::screens::screen_240x320::protocolProbePort())
        {
            port->stop();
        }
    }
    detail::destroy_root(parent);
}

} // namespace ui::mono::screens::screen_240x320
