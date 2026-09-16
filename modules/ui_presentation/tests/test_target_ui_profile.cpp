#include "ui_presentation/target_ui_profile.h"

#include <cassert>
#include <cstring>

int main()
{
    std::size_t count = 0;
    const auto* profiles = ui::presentation::allTargetUiProfiles(&count);
    assert(profiles != nullptr);
    assert(count == 9);

    const auto* tab5 = ui::presentation::findTargetUiProfile("tab5_touch_ui");
    assert(tab5 != nullptr);
    assert(tab5->display_class == ui::presentation::DisplayClass::LargeTouch);
    assert(tab5->input_class == ui::presentation::InputClass::TouchOnly);
    assert(std::strcmp(tab5->page_manifest_id, "tab5_touch_manifest") == 0);

    const auto* deck = ui::presentation::findTargetUiProfile("deck_wide_ui");
    assert(deck != nullptr);
    assert(deck->has_physical_keyboard);
    assert(deck->has_trackball);

    const auto* touch = ui::presentation::findTargetUiProfile("deck_touch_ui");
    assert(touch && touch->input_class == ui::presentation::InputClass::TouchOnly);
    assert(touch->screen_width == 320 && touch->screen_height == 240);
    assert(touch->has_soft_keyboard && touch->has_touch);
    assert(!touch->has_physical_keyboard && !touch->has_trackball);
    assert(std::strcmp(touch->page_manifest_id, deck->page_manifest_id) == 0);

    const auto* uconsole = ui::presentation::findTargetUiProfile("uconsole_desktop_ui");
    assert(uconsole != nullptr);
    assert(uconsole->has_pointer);
    assert(uconsole->has_side_nav);

    const auto* cardputer = ui::presentation::findTargetUiProfile("cardputer_compact_ui");
    assert(cardputer != nullptr);
    assert(cardputer->screen_width == 320);
    assert(cardputer->screen_height == 170);
    assert(cardputer->display_class == ui::presentation::DisplayClass::WideLandscape);
    assert(cardputer->input_class == ui::presentation::InputClass::Keyboard);
    assert(cardputer->layout_class == ui::presentation::LayoutClass::CardputerCompact);
    assert(cardputer->has_physical_keyboard);
    assert(!cardputer->has_touch);
    assert(std::strcmp(cardputer->page_manifest_id, "cardputer_compact_manifest") == 0);

    const auto* node = ui::presentation::findTargetUiProfile("node_headless_ui");
    assert(node != nullptr);
    assert(node->display_class == ui::presentation::DisplayClass::Headless);
    assert(node->layout_class == ui::presentation::LayoutClass::HeadlessNode);

    assert(ui::presentation::findTargetUiProfile("unknown") == nullptr);
    return 0;
}
