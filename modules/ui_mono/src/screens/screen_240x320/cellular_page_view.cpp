#include "cellular_page_internal.h"

#include "ui/app_runtime.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{
namespace
{

Button* buttonFor(lv_obj_t* object)
{
    State& current = state();
    for (size_t index = 0; index < current.button_count; ++index)
    {
        if (current.buttons[index].object == object)
        {
            return &current.buttons[index];
        }
    }
    return nullptr;
}

void applyButtonFocus(Button& button, bool focused)
{
    if (!valid(button.object) || !valid(button.label))
    {
        return;
    }
    lv_obj_set_style_bg_color(button.object, focused ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(button.label, focused ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
}

void onFieldEvent(lv_event_t* event)
{
    if (lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }

    State& current = state();
    const uint32_t key = lv_event_get_key(event);
    if ((key == 'w' || key == 'W') && !current.editing_field)
    {
        lv_group_focus_prev(app_g);
        lv_event_stop_processing(event);
    }
    else if ((key == 's' || key == 'S') && !current.editing_field)
    {
        lv_group_focus_next(app_g);
        lv_event_stop_processing(event);
    }
    else if (key == LV_KEY_ENTER)
    {
        current.editing_field = !current.editing_field;
        if (app_g != nullptr)
        {
            lv_group_set_editing(app_g, current.editing_field);
        }
        lv_event_stop_processing(event);
    }
    else if (key == LV_KEY_ESC ||
             (!current.editing_field && (key == LV_KEY_BACKSPACE || key == '\b')))
    {
        if (current.editing_field)
        {
            current.editing_field = false;
            if (app_g != nullptr)
            {
                lv_group_set_editing(app_g, false);
            }
        }
        else
        {
            runAction(ButtonAction::Back);
        }
        lv_event_stop_processing(event);
    }
}

void onButtonEvent(lv_event_t* event)
{
    Button* button = buttonFor(lv_event_get_target_obj(event));
    if (button == nullptr)
    {
        return;
    }

    switch (lv_event_get_code(event))
    {
    case LV_EVENT_FOCUSED:
        applyButtonFocus(*button, true);
        break;
    case LV_EVENT_DEFOCUSED:
        applyButtonFocus(*button, false);
        break;
    case LV_EVENT_CLICKED:
        runAction(button->action);
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
            runAction(ButtonAction::Back);
            lv_event_stop_processing(event);
        }
        break;
    }
    default:
        break;
    }
}

} // namespace

bool valid(const lv_obj_t* object)
{
    return object != nullptr && lv_obj_is_valid(const_cast<lv_obj_t*>(object));
}

void copyText(char* destination, size_t capacity, const char* source)
{
    if (destination == nullptr || capacity == 0)
    {
        return;
    }
    std::snprintf(destination, capacity, "%s", source != nullptr ? source : "");
}

void stylePaper(lv_obj_t* object)
{
    lv_obj_set_style_bg_color(object, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(object, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(object, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(object, 0, LV_PART_MAIN);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* createText(lv_obj_t* parent, lv_coord_t width, lv_text_align_t alignment)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_align(label, alignment, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

void setText(lv_obj_t* label, const char* text)
{
    if (!valid(label))
    {
        return;
    }
    const char* const safe = text != nullptr ? text : "";
    const char* const previous = lv_label_get_text(label);
    if (previous != nullptr && std::strcmp(previous, safe) == 0)
    {
        return;
    }
    lv_label_set_text(label, safe);
}

void setLine(size_t index, const char* format, ...)
{
    if (index >= kMaxLines)
    {
        return;
    }
    State& current = state();
    va_list args;
    va_start(args, format);
    std::vsnprintf(current.scratch, sizeof(current.scratch), format, args);
    va_end(args);
    setText(current.lines[index], current.scratch);
}

void clearLinesFrom(size_t first)
{
    State& current = state();
    for (size_t index = first; index < kMaxLines; ++index)
    {
        setText(current.lines[index], "");
    }
}

void setNotice(const char* format, ...)
{
    State& current = state();
    va_list args;
    va_start(args, format);
    std::vsnprintf(current.notice_text, sizeof(current.notice_text), format, args);
    va_end(args);
    setText(current.notice, current.notice_text);
}

void storeFieldValues()
{
    State& current = state();
    for (size_t index = 0; index < current.field_count; ++index)
    {
        Field& field = current.fields[index];
        if (valid(field.textarea) && field.value != nullptr)
        {
            copyText(field.value, field.capacity, lv_textarea_get_text(field.textarea));
        }
    }
}

void restoreApplicationGroup()
{
    if (app_g != nullptr)
    {
        lv_group_remove_all_objs(app_g);
        lv_group_set_editing(app_g, false);
        set_default_group(app_g);
    }
}

void addField(const char* label,
              char* value,
              size_t capacity,
              lv_coord_t top,
              size_t max_length,
              bool password,
              const char* accepted_chars)
{
    State& current = state();
    if (current.field_count >= kMaxFields || !valid(current.root))
    {
        return;
    }

    Field& field = current.fields[current.field_count++];
    field.value = value;
    field.capacity = capacity;

    lv_obj_t* name = createText(current.root, 62);
    lv_obj_set_pos(name, kMargin, top + 2);
    setText(name, label);

    field.textarea = lv_textarea_create(current.root);
    lv_obj_set_pos(field.textarea, 70, top);
    lv_obj_set_size(field.textarea, 162, kFieldHeight);
    lv_obj_set_style_text_font(field.textarea, LV_FONT_DEFAULT, LV_PART_MAIN);
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
    lv_obj_add_event_cb(field.textarea, onFieldEvent, LV_EVENT_KEY, nullptr);
    if (app_g != nullptr)
    {
        lv_group_add_obj(app_g, field.textarea);
    }
}

void addButton(const char* label, ButtonAction action, lv_coord_t x, lv_coord_t y, lv_coord_t width)
{
    State& current = state();
    if (current.button_count >= kMaxButtons || !valid(current.root))
    {
        return;
    }

    Button& button = current.buttons[current.button_count++];
    button.action = action;
    button.object = lv_btn_create(current.root);
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
    lv_obj_add_event_cb(button.object, onButtonEvent, LV_EVENT_ALL, nullptr);
    button.label = createText(button.object, width - 2, LV_TEXT_ALIGN_CENTER);
    lv_obj_center(button.label);
    setText(button.label, label);
    if (app_g != nullptr)
    {
        lv_group_add_obj(app_g, button.object);
    }
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
