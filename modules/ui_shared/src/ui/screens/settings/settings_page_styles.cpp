/**
 * @file settings_page_styles.cpp
 * @brief Settings styles implementation
 */

#include "ui/screens/settings/settings_page_styles.h"
#include "ui/components/info_card.h"
#include "ui/components/two_pane_styles.h"

#if defined(ESP_PLATFORM)
#include "esp_log.h"
#endif

namespace settings::ui::style
{
namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kLogTag = "settings-style";
#endif

bool s_inited = false;
lv_style_t s_modal_bg;
lv_style_t s_modal_panel;
lv_style_t s_modal_btn_checked;
lv_style_t s_value_box;

#if defined(ARDUINO_T_DECK_PRO)
void apply_mono_button(lv_obj_t* button)
{
    if (!button) return;

    lv_obj_set_style_bg_color(button, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(button, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button,
                              lv_color_black(),
                              LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(button,
                                lv_color_white(),
                                LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(button,
                              lv_color_black(),
                              LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_text_color(button,
                                lv_color_white(),
                                LV_PART_MAIN | LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(button,
                              lv_color_black(),
                              LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(button,
                                lv_color_white(),
                                LV_PART_MAIN | LV_STATE_CHECKED);
}

void apply_mono_modal_button(lv_obj_t* button)
{
    apply_mono_button(button);
    lv_obj_set_style_bg_color(button, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_text_color(button, lv_color_white(), LV_PART_MAIN);
}
#endif

} // namespace

void init_once()
{
    ::ui::components::two_pane_styles::init_once();
    if (s_inited) return;
    s_inited = true;

#if defined(ESP_PLATFORM)
    ESP_LOGI(kLogTag, "init_once");
#endif

    lv_style_init(&s_modal_bg);
    lv_style_set_bg_opa(&s_modal_bg, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_bg,
                          lv_color_hex(::ui::components::two_pane_styles::kSidePanelBg));

    lv_style_init(&s_modal_panel);
    lv_style_set_bg_opa(&s_modal_panel, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_panel,
                          lv_color_hex(::ui::components::two_pane_styles::kMainPanelBg));
    lv_style_set_border_width(&s_modal_panel, 2);
    lv_style_set_border_color(&s_modal_panel,
                              lv_color_hex(::ui::components::two_pane_styles::kBorder));
    lv_style_set_radius(&s_modal_panel, 8);

    lv_style_init(&s_modal_btn_checked);
    lv_style_set_bg_opa(&s_modal_btn_checked, LV_OPA_COVER);
    lv_style_set_bg_color(&s_modal_btn_checked,
                          lv_color_hex(::ui::components::two_pane_styles::kAccent));

    lv_style_init(&s_value_box);
    lv_style_set_bg_opa(&s_value_box, LV_OPA_COVER);
    lv_style_set_bg_color(&s_value_box,
                          lv_color_hex(::ui::components::two_pane_styles::kSidePanelBg));
    lv_style_set_border_width(&s_value_box, 1);
    lv_style_set_border_color(&s_value_box,
                              lv_color_hex(::ui::components::two_pane_styles::kBorder));
    lv_style_set_radius(&s_value_box, 8);
}

void apply_panel_side(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_side(obj);
#if defined(ARDUINO_T_DECK_PRO)
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN);
#endif
}

void apply_panel_main(lv_obj_t* obj)
{
    ::ui::components::two_pane_styles::apply_panel_main(obj);
#if defined(ARDUINO_T_DECK_PRO)
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
#endif
}

void apply_btn_basic(lv_obj_t* btn)
{
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
#if defined(ARDUINO_T_DECK_PRO)
    apply_mono_button(btn);
#endif
}

void apply_btn_filter(lv_obj_t* btn)
{
    ::ui::components::two_pane_styles::apply_btn_filter(btn);
#if defined(ARDUINO_T_DECK_PRO)
    apply_mono_button(btn);
#endif
}

void apply_list_item(lv_obj_t* item)
{
    if (::ui::components::info_card::use_tdeck_layout())
    {
        ::ui::components::info_card::apply_item_style(item);
#if defined(ARDUINO_T_DECK_PRO)
        apply_mono_button(item);
#endif
        return;
    }
    ::ui::components::two_pane_styles::apply_list_item(item);
#if defined(ARDUINO_T_DECK_PRO)
    apply_mono_button(item);
#endif
}

void apply_label_primary(lv_obj_t* label)
{
#if !defined(ARDUINO_T_DECK_PRO)
    ::ui::components::two_pane_styles::apply_label_primary(label);
#else
    (void)label;
#endif
}

void apply_label_muted(lv_obj_t* label)
{
#if !defined(ARDUINO_T_DECK_PRO)
    ::ui::components::two_pane_styles::apply_label_muted(label);
#else
    (void)label;
#endif
}

void apply_value_box(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_value_box, 0);
#if defined(ARDUINO_T_DECK_PRO)
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
#endif
}

void apply_modal_bg(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_modal_bg, 0);
#if defined(ARDUINO_T_DECK_PRO)
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
#endif
}

void apply_modal_panel(lv_obj_t* obj)
{
    init_once();
    lv_obj_add_style(obj, &s_modal_panel, 0);
#if defined(ARDUINO_T_DECK_PRO)
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
#endif
}

void apply_btn_modal(lv_obj_t* btn)
{
    init_once();
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
    lv_obj_add_style(btn, &s_modal_btn_checked, LV_PART_MAIN | LV_STATE_CHECKED);
#if defined(ARDUINO_T_DECK_PRO)
    apply_mono_modal_button(btn);
#endif
}

} // namespace settings::ui::style
