#if defined(ARDUINO_T_DECK_PRO) && defined(TRAIL_MATE_TDECK_PRO_A7682E)

#include "ui/tdeck_pro/a7682e_phone_page.h"

#include "platform/ui/a7682e_cellular_runtime.h"
#include "ui/app_runtime.h"
#include "ui/tdeck_pro/text_font.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ui::tdeck_pro::a7682e_phone_page
{
namespace
{

constexpr lv_coord_t kScreenWidth = 240;
constexpr lv_coord_t kScreenHeight = 320;
constexpr lv_coord_t kMargin = 8;
constexpr lv_coord_t kContentWidth = kScreenWidth - (kMargin * 2);
constexpr lv_coord_t kLineHeight = 17;
constexpr lv_coord_t kFieldHeight = 18;
constexpr size_t kMaxLines = 4;
constexpr size_t kMaxFields = 5;
constexpr size_t kMaxButtons = 7;

enum class Page : uint8_t
{
    Phone,
    Sms,
    Email,
    SettingsRadio,
    SettingsMail,
};

enum class ButtonAction : uint8_t
{
    Dial,
    Answer,
    HangUp,
    GoPhone,
    GoSms,
    GoEmail,
    GoSettings,
    GoSettingsMail,
    SaveSettings,
    Toggle4g,
    ToggleAutoAnswer,
    CycleAudioGain,
    CycleSmtpPort,
    CycleSmtpSecurity,
    SendSms,
    SendEmail,
    Back,
};

struct Field
{
    lv_obj_t* textarea = nullptr;
    const char* label = nullptr;
    char* value = nullptr;
    size_t capacity = 0;
    bool password = false;
};

struct Button
{
    lv_obj_t* object = nullptr;
    lv_obj_t* label = nullptr;
    ButtonAction action = ButtonAction::Back;
};

struct State
{
    lv_obj_t* root = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* page_label = nullptr;
    lv_obj_t* lines[kMaxLines]{};
    lv_obj_t* notice = nullptr;
    lv_obj_t* footer = nullptr;
    lv_obj_t* parent = nullptr;
    lv_timer_t* status_timer = nullptr;
    lv_timer_t* rebuild_timer = nullptr;
    Field fields[kMaxFields]{};
    Button buttons[kMaxButtons]{};
    size_t field_count = 0;
    size_t button_count = 0;
    Page page = Page::Phone;
    bool editing_field = false;
    platform::ui::a7682e::Config config{};
    char phone_number[40]{};
    char sms_recipient[40]{};
    char sms_body[161]{};
    char email_recipient[96]{};
    char email_subject[96]{};
    char email_body[512]{};
    char notice_text[80]{};
};

State s_state;

bool valid(const lv_obj_t* object)
{
    return object != nullptr && lv_obj_is_valid(const_cast<lv_obj_t*>(object));
}

void copy_text(char* destination, size_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0)
    {
        return;
    }
    std::snprintf(destination, capacity, "%s", source != nullptr ? source : "");
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

lv_obj_t* create_text(lv_obj_t* parent, lv_coord_t width, lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, text_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, alignment, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void set_text(lv_obj_t* label, const char* text)
{
    if (!valid(label))
    {
        return;
    }
    lv_label_set_text(label, text != nullptr ? text : "");
}

void set_notice(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vsnprintf(s_state.notice_text, sizeof(s_state.notice_text), format, args);
    va_end(args);
    set_text(s_state.notice, s_state.notice_text);
}

void set_line(size_t index, const char* format, ...)
{
    if (index >= kMaxLines || !valid(s_state.lines[index]))
    {
        return;
    }
    char text[120]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    set_text(s_state.lines[index], text);
}

void clear_lines_from(size_t first)
{
    for (size_t index = first; index < kMaxLines; ++index)
    {
        set_text(s_state.lines[index], "");
    }
}

void store_field_values()
{
    for (size_t index = 0; index < s_state.field_count; ++index)
    {
        Field& field = s_state.fields[index];
        if (valid(field.textarea) && field.value != nullptr)
        {
            copy_text(field.value, field.capacity, lv_textarea_get_text(field.textarea));
        }
    }
}

void restore_application_group()
{
    if (app_g != nullptr)
    {
        lv_group_remove_all_objs(app_g);
        set_default_group(app_g);
    }
}

void render_status()
{
    const auto& status = platform::ui::a7682e::status();
    switch (s_state.page)
    {
    case Page::Phone:
        set_line(0,
                 "4G:%s  SIG:%d  %s",
                 platform::ui::a7682e::service_state_label(platform::ui::a7682e::service_state()),
                 static_cast<int>(status.rssi),
                 status.network_registered ? "NET" : "NO NET");
        set_line(1,
                 "CALL:%s %s",
                 platform::ui::a7682e::call_state_label(status.call_state),
                 status.incoming_number);
        set_line(2, "SIM:%s  OP:%s", status.sim_ready ? "READY" : "WAIT", status.operator_name);
        set_line(3, "%s", status.last_event);
        break;
    case Page::Sms:
        set_line(0, "SMS %s  RX:%lu", status.modem_ready ? "READY" : "WAIT", static_cast<unsigned long>(status.received_sms_count));
        set_line(1, "LAST:%s", status.last_sms_sender);
        set_line(2, "%s", status.last_sms_body);
        set_line(3, "%s", status.last_event);
        break;
    case Page::Email:
        set_line(0, "EMAIL %s", status.smtps_available ? "SMTP READY" : "CONFIGURE SMTP");
        set_line(1, "APN:%s", s_state.config.apn[0] != '\0' ? s_state.config.apn : "AUTO");
        set_line(2, "SERVER:%s", s_state.config.smtp_host);
        set_line(3, "%s", status.last_event);
        break;
    case Page::SettingsRadio:
        set_line(0, "4G:%s  AUTO ANSWER:%s", s_state.config.enabled ? "ON" : "OFF", s_state.config.auto_answer ? "ON" : "OFF");
        set_line(1, "SPEAKER:%u MIC:%u", static_cast<unsigned>(s_state.config.speaker_gain), static_cast<unsigned>(s_state.config.microphone_gain));
        set_line(2, "SMSC:%s", s_state.config.smsc[0] != '\0' ? s_state.config.smsc : "AUTO");
        set_line(3, "APN password is stored locally");
        break;
    case Page::SettingsMail:
        set_line(0, "SMTP TLS:%u PORT:%u", static_cast<unsigned>(s_state.config.smtp_security), static_cast<unsigned>(s_state.config.smtp_port));
        set_line(1, "FROM:%s", s_state.config.smtp_from);
        set_line(2, "TO:%s", s_state.config.smtp_default_recipient);
        set_line(3, "Use an app password, not account password");
        break;
    }
}

void status_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (valid(s_state.root))
    {
        render_status();
    }
}

void on_field_event(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    if ((key == 'w' || key == 'W') && !s_state.editing_field)
    {
        lv_group_focus_prev(app_g);
        lv_event_stop_processing(event);
    }
    else if ((key == 's' || key == 'S') && !s_state.editing_field)
    {
        lv_group_focus_next(app_g);
        lv_event_stop_processing(event);
    }
    else if (key == LV_KEY_ENTER)
    {
        s_state.editing_field = !s_state.editing_field;
        if (app_g != nullptr)
        {
            lv_group_set_editing(app_g, s_state.editing_field);
        }
        lv_event_stop_processing(event);
    }
    else if (key == LV_KEY_ESC)
    {
        s_state.editing_field = false;
        if (app_g != nullptr)
        {
            lv_group_set_editing(app_g, false);
        }
        lv_event_stop_processing(event);
    }
}

void add_field(const char* label,
               char* value,
               size_t capacity,
               lv_coord_t top,
               size_t max_length,
               bool password = false,
               const char* accepted_chars = nullptr)
{
    if (s_state.field_count >= kMaxFields || !valid(s_state.root))
    {
        return;
    }
    Field& field = s_state.fields[s_state.field_count++];
    field.label = label;
    field.value = value;
    field.capacity = capacity;
    field.password = password;

    lv_obj_t* name = create_text(s_state.root, 62);
    lv_obj_set_pos(name, kMargin, top + 2);
    set_text(name, label);

    field.textarea = lv_textarea_create(s_state.root);
    lv_obj_set_pos(field.textarea, 70, top);
    lv_obj_set_size(field.textarea, 162, kFieldHeight);
    lv_obj_set_style_text_font(field.textarea, text_font(), LV_PART_MAIN);
    lv_obj_set_style_text_color(field.textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(field.textarea, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field.textarea, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field.textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field.textarea, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(field.textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(field.textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_right(field.textarea, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_top(field.textarea, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(field.textarea, 0, LV_PART_MAIN);
    lv_textarea_set_one_line(field.textarea, true);
    lv_textarea_set_max_length(field.textarea, max_length);
    lv_textarea_set_password_mode(field.textarea, password);
    if (accepted_chars != nullptr)
    {
        lv_textarea_set_accepted_chars(field.textarea, accepted_chars);
    }
    lv_textarea_set_text(field.textarea, value != nullptr ? value : "");
    lv_obj_add_event_cb(field.textarea, on_field_event, LV_EVENT_KEY, nullptr);
    lv_group_add_obj(app_g, field.textarea);
}

Button* button_for(lv_obj_t* object)
{
    for (size_t index = 0; index < s_state.button_count; ++index)
    {
        if (s_state.buttons[index].object == object)
        {
            return &s_state.buttons[index];
        }
    }
    return nullptr;
}

void apply_button_focus(Button& button, bool focused)
{
    if (!valid(button.object) || !valid(button.label))
    {
        return;
    }
    lv_obj_set_style_bg_color(button.object, focused ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(button.label, focused ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
}

void rebuild_page(Page page);

void rebuild_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    s_state.rebuild_timer = nullptr;
    rebuild_page(s_state.page);
}

void schedule_rebuild(Page page)
{
    s_state.page = page;
    if (s_state.rebuild_timer != nullptr)
    {
        return;
    }
    s_state.rebuild_timer = lv_timer_create(rebuild_timer_cb, 1, nullptr);
    if (s_state.rebuild_timer != nullptr)
    {
        lv_timer_set_repeat_count(s_state.rebuild_timer, 1);
    }
}

bool save_settings()
{
    store_field_values();
    if (!platform::ui::a7682e::save_config(s_state.config))
    {
        set_notice("SETTINGS SAVE FAILED");
        return false;
    }
    set_notice("SETTINGS SAVED");
    return true;
}

void run_action(ButtonAction action)
{
    store_field_values();
    switch (action)
    {
    case ButtonAction::Dial:
        set_notice(platform::ui::a7682e::dial(s_state.phone_number) ? "DIAL REQUESTED" : "DIAL REJECTED");
        break;
    case ButtonAction::Answer:
        set_notice(platform::ui::a7682e::answer() ? "ANSWER REQUESTED" : "NO INCOMING CALL");
        break;
    case ButtonAction::HangUp:
        set_notice(platform::ui::a7682e::hang_up() ? "HANGUP REQUESTED" : "NO ACTIVE CALL");
        break;
    case ButtonAction::GoPhone:
        schedule_rebuild(Page::Phone);
        return;
    case ButtonAction::GoSms:
        schedule_rebuild(Page::Sms);
        return;
    case ButtonAction::GoEmail:
        schedule_rebuild(Page::Email);
        return;
    case ButtonAction::GoSettings:
        schedule_rebuild(Page::SettingsRadio);
        return;
    case ButtonAction::GoSettingsMail:
        schedule_rebuild(Page::SettingsMail);
        return;
    case ButtonAction::SaveSettings:
        (void)save_settings();
        break;
    case ButtonAction::Toggle4g:
        s_state.config.enabled = !s_state.config.enabled;
        (void)save_settings();
        schedule_rebuild(Page::SettingsRadio);
        return;
    case ButtonAction::ToggleAutoAnswer:
        s_state.config.auto_answer = !s_state.config.auto_answer;
        (void)save_settings();
        schedule_rebuild(Page::SettingsRadio);
        return;
    case ButtonAction::CycleAudioGain:
        s_state.config.speaker_gain = static_cast<uint8_t>((s_state.config.speaker_gain + 1U) % 16U);
        s_state.config.microphone_gain = static_cast<uint8_t>((s_state.config.microphone_gain + 1U) % 16U);
        (void)save_settings();
        schedule_rebuild(Page::SettingsRadio);
        return;
    case ButtonAction::CycleSmtpPort:
        if (s_state.config.smtp_port == 465)
        {
            s_state.config.smtp_port = 587;
        }
        else if (s_state.config.smtp_port == 587)
        {
            s_state.config.smtp_port = 25;
        }
        else
        {
            s_state.config.smtp_port = 465;
        }
        (void)save_settings();
        schedule_rebuild(Page::SettingsMail);
        return;
    case ButtonAction::CycleSmtpSecurity:
        s_state.config.smtp_security = static_cast<uint8_t>((s_state.config.smtp_security + 1U) % 3U);
        (void)save_settings();
        schedule_rebuild(Page::SettingsMail);
        return;
    case ButtonAction::SendSms:
        set_notice(platform::ui::a7682e::send_sms(s_state.sms_recipient, s_state.sms_body) ? "SMS QUEUED" : "SMS REJECTED");
        break;
    case ButtonAction::SendEmail:
        set_notice(platform::ui::a7682e::send_email(s_state.email_recipient,
                                                    s_state.email_subject,
                                                    s_state.email_body)
                       ? "EMAIL QUEUED"
                       : "EMAIL REJECTED");
        break;
    case ButtonAction::Back:
        ui_request_exit_to_menu();
        return;
    }
    render_status();
}

void on_button_event(lv_event_t* event)
{
    Button* button = button_for(lv_event_get_target_obj(event));
    if (button == nullptr)
    {
        return;
    }
    switch (lv_event_get_code(event))
    {
    case LV_EVENT_FOCUSED:
        apply_button_focus(*button, true);
        break;
    case LV_EVENT_DEFOCUSED:
        apply_button_focus(*button, false);
        break;
    case LV_EVENT_CLICKED:
        run_action(button->action);
        break;
    case LV_EVENT_KEY:
        if (lv_event_get_key(event) == 'w' || lv_event_get_key(event) == 'W')
        {
            lv_group_focus_prev(app_g);
            lv_event_stop_processing(event);
        }
        else if (lv_event_get_key(event) == 's' || lv_event_get_key(event) == 'S')
        {
            lv_group_focus_next(app_g);
            lv_event_stop_processing(event);
        }
        else if (lv_event_get_key(event) == LV_KEY_ESC)
        {
            ui_request_exit_to_menu();
        }
        break;
    default:
        break;
    }
}

void add_button(const char* label, ButtonAction action, lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    if (s_state.button_count >= kMaxButtons || !valid(s_state.root))
    {
        return;
    }
    Button& button = s_state.buttons[s_state.button_count++];
    button.action = action;
    button.object = lv_btn_create(s_state.root);
    lv_obj_set_pos(button.object, x, y);
    lv_obj_set_size(button.object, width, 18);
    lv_obj_set_style_bg_color(button.object, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button.object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button.object, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button.object, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(button.object, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button.object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button.object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(button.object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button.object, on_button_event, LV_EVENT_ALL, nullptr);
    button.label = create_text(button.object, width - 2, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(button.label);
    set_text(button.label, label);
    lv_group_add_obj(app_g, button.object);
}

void create_base(Page page)
{
    s_state.page = page;
    s_state.field_count = 0;
    s_state.button_count = 0;
    s_state.editing_field = false;
    s_state.root = lv_obj_create(s_state.parent);
    lv_obj_set_pos(s_state.root, 0, 0);
    lv_obj_set_size(s_state.root, kScreenWidth, kScreenHeight);
    style_paper(s_state.root);

    s_state.title = create_text(s_state.root, 142);
    lv_obj_set_pos(s_state.title, kMargin, kMargin);
    set_text(s_state.title, "4G PHONE");
    s_state.page_label = create_text(s_state.root, 82, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(s_state.page_label, 150, kMargin);

    const char* page_text = "PHONE";
    if (page == Page::Sms) page_text = "SMS";
    if (page == Page::Email) page_text = "EMAIL";
    if (page == Page::SettingsRadio) page_text = "4G SET";
    if (page == Page::SettingsMail) page_text = "MAIL SET";
    set_text(s_state.page_label, page_text);

    lv_obj_t* header_rule = lv_obj_create(s_state.root);
    lv_obj_set_pos(header_rule, kMargin, 32);
    lv_obj_set_size(header_rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(header_rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header_rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_rule, 0, LV_PART_MAIN);

    for (size_t index = 0; index < kMaxLines; ++index)
    {
        s_state.lines[index] = create_text(s_state.root, kContentWidth);
        lv_obj_set_pos(s_state.lines[index], kMargin, 42 + static_cast<lv_coord_t>(index) * kLineHeight);
    }

    s_state.notice = create_text(s_state.root, kContentWidth);
    lv_obj_set_pos(s_state.notice, kMargin, 114);
    lv_obj_set_style_text_color(s_state.notice, lv_color_black(), LV_PART_MAIN);

    s_state.footer = create_text(s_state.root, kContentWidth);
    lv_obj_set_pos(s_state.footer, kMargin, 280);
    lv_obj_set_style_bg_color(s_state.footer, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_state.footer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_state.footer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(s_state.footer, 2, LV_PART_MAIN);
    set_text(s_state.footer, "W/S NAV  ENT EDIT  ESC BACK");
}

void destroy_page()
{
    if (s_state.rebuild_timer != nullptr)
    {
        lv_timer_del(s_state.rebuild_timer);
        s_state.rebuild_timer = nullptr;
    }
    if (s_state.status_timer != nullptr)
    {
        lv_timer_del(s_state.status_timer);
        s_state.status_timer = nullptr;
    }
    if (valid(s_state.root))
    {
        lv_obj_del(s_state.root);
    }
    s_state.root = nullptr;
    restore_application_group();
}

void rebuild_page(Page page)
{
    store_field_values();
    if (s_state.status_timer != nullptr)
    {
        lv_timer_del(s_state.status_timer);
        s_state.status_timer = nullptr;
    }
    if (valid(s_state.root))
    {
        lv_obj_del(s_state.root);
    }
    restore_application_group();
    create_base(page);
    switch (page)
    {
    case Page::Phone:
        add_field("NUMBER", s_state.phone_number, sizeof(s_state.phone_number), 132, 39, false, "+0123456789*#");
        add_button("DIAL", ButtonAction::Dial, 8, 160, 68);
        add_button("ANSWER", ButtonAction::Answer, 82, 160, 72);
        add_button("HANG", ButtonAction::HangUp, 160, 160, 72);
        add_button("SMS", ButtonAction::GoSms, 8, 184, 52);
        add_button("EMAIL", ButtonAction::GoEmail, 66, 184, 64);
        add_button("4G SET", ButtonAction::GoSettings, 136, 184, 96);
        add_button("BACK", ButtonAction::Back, 8, 298, 58);
        break;
    case Page::Sms:
        add_field("TO", s_state.sms_recipient, sizeof(s_state.sms_recipient), 132, 39, false, "+0123456789*#");
        add_field("TEXT", s_state.sms_body, sizeof(s_state.sms_body), 154, 160);
        add_button("SEND SMS", ButtonAction::SendSms, 8, 184, 90);
        add_button("PHONE", ButtonAction::GoPhone, 104, 184, 64);
        add_button("EMAIL", ButtonAction::GoEmail, 174, 184, 58);
        add_button("BACK", ButtonAction::Back, 8, 298, 58);
        break;
    case Page::Email:
        add_field("TO", s_state.email_recipient, sizeof(s_state.email_recipient), 132, 95);
        add_field("SUBJ", s_state.email_subject, sizeof(s_state.email_subject), 154, 95);
        add_field("BODY", s_state.email_body, sizeof(s_state.email_body), 176, 255);
        add_button("SEND", ButtonAction::SendEmail, 8, 208, 62);
        add_button("PHONE", ButtonAction::GoPhone, 76, 208, 64);
        add_button("SET", ButtonAction::GoSettingsMail, 146, 208, 52);
        add_button("BACK", ButtonAction::Back, 8, 298, 58);
        break;
    case Page::SettingsRadio:
        add_field("APN", s_state.config.apn, sizeof(s_state.config.apn), 132, 63);
        add_field("APN USER", s_state.config.apn_user, sizeof(s_state.config.apn_user), 154, 47);
        add_field("APN PASS", s_state.config.apn_password, sizeof(s_state.config.apn_password), 176, 63, true);
        add_field("SMSC", s_state.config.smsc, sizeof(s_state.config.smsc), 198, 31, false, "+0123456789");
        add_button(s_state.config.enabled ? "4G OFF" : "4G ON", ButtonAction::Toggle4g, 8, 226, 70);
        add_button(s_state.config.auto_answer ? "AUTO ON" : "AUTO OFF", ButtonAction::ToggleAutoAnswer, 84, 226, 76);
        add_button("GAIN", ButtonAction::CycleAudioGain, 166, 226, 66);
        add_button("SAVE", ButtonAction::SaveSettings, 8, 250, 58);
        add_button("MAIL", ButtonAction::GoSettingsMail, 72, 250, 84);
        add_button("BACK", ButtonAction::Back, 8, 298, 58);
        break;
    case Page::SettingsMail:
        add_field("HOST", s_state.config.smtp_host, sizeof(s_state.config.smtp_host), 132, 95);
        add_field("USER", s_state.config.smtp_user, sizeof(s_state.config.smtp_user), 154, 95);
        add_field("PASS", s_state.config.smtp_password, sizeof(s_state.config.smtp_password), 176, 95, true);
        add_field("FROM", s_state.config.smtp_from, sizeof(s_state.config.smtp_from), 198, 95);
        add_field("DEFAULT TO", s_state.config.smtp_default_recipient, sizeof(s_state.config.smtp_default_recipient), 220, 95);
        add_button("SAVE", ButtonAction::SaveSettings, 8, 248, 58);
        add_button("PORT", ButtonAction::CycleSmtpPort, 72, 248, 52);
        add_button("TLS", ButtonAction::CycleSmtpSecurity, 130, 248, 48);
        add_button("4G SET", ButtonAction::GoSettings, 184, 248, 48);
        add_button("EMAIL", ButtonAction::GoEmail, 72, 270, 80);
        add_button("BACK", ButtonAction::Back, 8, 298, 58);
        break;
    }
    render_status();
    set_text(s_state.notice, s_state.notice_text);
    if (s_state.button_count > 0)
    {
        lv_group_focus_obj(s_state.buttons[0].object);
    }
    s_state.status_timer = lv_timer_create(status_timer_cb, 1000, nullptr);
}

} // namespace

void enter(lv_obj_t* parent)
{
    if (parent == nullptr)
    {
        return;
    }
    restore_application_group();
    s_state = State{};
    s_state.parent = parent;
    s_state.config = platform::ui::a7682e::config();
    rebuild_page(Page::Phone);
}

void exit(lv_obj_t* parent)
{
    (void)parent;
    store_field_values();
    destroy_page();
    s_state = State{};
}

} // namespace ui::tdeck_pro::a7682e_phone_page

#endif
