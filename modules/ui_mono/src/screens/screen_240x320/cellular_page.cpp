#include "cellular_page_internal.h"

#include "ui/mono/screens/screen_240x320/cellular_page.h"

#include "ui/app_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace ui::mono::screens::screen_240x320::cellular_page
{
namespace detail
{
namespace
{

State* s_state = nullptr;
bool s_state_allocation_failed_logged = false;

State* allocateState()
{
#if defined(ESP_PLATFORM)
    void* const storage = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void* const storage = std::malloc(sizeof(State));
#endif
    return storage ? new (storage) State{} : nullptr;
}

bool ensureState()
{
    if (s_state != nullptr)
    {
        return true;
    }

    s_state = allocateState();
    if (s_state != nullptr)
    {
        return true;
    }

    if (!s_state_allocation_failed_logged)
    {
        s_state_allocation_failed_logged = true;
        std::printf("[UI][Cellular] page enter denied reason=psram_state_alloc bytes=%u\n",
                    static_cast<unsigned>(sizeof(State)));
    }
    return false;
}

void releaseState()
{
    if (s_state == nullptr)
    {
        return;
    }

    s_state->~State();
#if defined(ESP_PLATFORM)
    heap_caps_free(s_state);
#else
    std::free(s_state);
#endif
    s_state = nullptr;
}

void rebuildTimer(lv_timer_t*)
{
    state().rebuild_timer = nullptr;
    rebuildPage(state().page);
}

void createBase(Page page)
{
    State& current = state();
    current.page = page;
    current.field_count = 0;
    current.button_count = 0;
    current.editing_field = false;
    current.root = lv_obj_create(current.parent);
    lv_obj_set_pos(current.root, 0, 0);
    lv_obj_set_size(current.root, kScreenWidth, kScreenHeight);
    stylePaper(current.root);

    current.title = createText(current.root, 142);
    lv_obj_set_pos(current.title, kMargin, kMargin);
    setText(current.title, "CELLULAR");
    current.page_label = createText(current.root, 82, LV_TEXT_ALIGN_RIGHT);
    lv_obj_set_pos(current.page_label, 150, kMargin);

    const char* page_text = "PHONE";
    if (page == Page::Sms) page_text = "SMS";
    if (page == Page::Email) page_text = "EMAIL";
    if (page == Page::RadioSettings) page_text = "RADIO SET";
    if (page == Page::MailSettings) page_text = "MAIL SET";
    setText(current.page_label, page_text);

    lv_obj_t* header_rule = lv_obj_create(current.root);
    lv_obj_set_pos(header_rule, kMargin, 32);
    lv_obj_set_size(header_rule, kContentWidth, 1);
    lv_obj_set_style_bg_color(header_rule, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header_rule, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_rule, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header_rule, LV_OBJ_FLAG_SCROLLABLE);

    for (size_t index = 0; index < kMaxLines; ++index)
    {
        current.lines[index] = createText(current.root, kContentWidth);
        lv_obj_set_pos(current.lines[index], kMargin, 42 + static_cast<lv_coord_t>(index) * kLineHeight);
    }

    current.notice = createText(current.root, kContentWidth);
    lv_obj_set_pos(current.notice, kMargin, 114);
    current.footer = createText(current.root, kContentWidth);
    lv_obj_set_pos(current.footer, kMargin, 280);
    lv_obj_set_style_bg_color(current.footer, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(current.footer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(current.footer, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(current.footer, 2, LV_PART_MAIN);
    setText(current.footer, "W/S NAV ENT EDIT BKSP BACK");
}

} // namespace

State& state()
{
    return *s_state;
}

void destroyPage()
{
    if (s_state == nullptr)
    {
        return;
    }
    State& current = state();
    if (current.rebuild_timer != nullptr)
    {
        lv_timer_del(current.rebuild_timer);
        current.rebuild_timer = nullptr;
    }
    if (valid(current.root))
    {
        lv_obj_del(current.root);
    }
    current.root = nullptr;
    restoreApplicationGroup();
}

void scheduleRebuild(Page page)
{
    State& current = state();
    current.page = page;
    if (current.rebuild_timer == nullptr)
    {
        current.rebuild_timer = lv_timer_create(rebuildTimer, 1, nullptr);
        if (current.rebuild_timer != nullptr)
        {
            lv_timer_set_repeat_count(current.rebuild_timer, 1);
        }
    }
}

void navigateTo(Page page)
{
    State& current = state();
    if (page == current.page)
    {
        return;
    }

    if (current.page_history_count < kMaxPageHistory)
    {
        current.page_history[current.page_history_count++] = current.page;
    }
    else
    {
        // Preserve the immediate return target when navigation exceeds the
        // fixed allocation. Cellular page history is intentionally bounded
        // because it lives alongside form drafts on an ESP target.
        for (size_t index = 1; index < kMaxPageHistory; ++index)
        {
            current.page_history[index - 1] = current.page_history[index];
        }
        current.page_history[kMaxPageHistory - 1] = current.page;
    }
    scheduleRebuild(page);
}

bool navigateBack()
{
    State& current = state();
    current.editing_field = false;
    if (app_g != nullptr)
    {
        lv_group_set_editing(app_g, false);
    }
    if (current.page_history_count == 0)
    {
        return false;
    }

    const Page parent = current.page_history[--current.page_history_count];
    scheduleRebuild(parent);
    return true;
}

void rebuildPage(Page page)
{
    State& current = state();
    storeFieldValues();
    if (valid(current.root))
    {
        lv_obj_del(current.root);
    }
    restoreApplicationGroup();
    createBase(page);

    switch (page)
    {
    case Page::Phone:
        buildPhonePage();
        break;
    case Page::Sms:
        buildSmsPage();
        break;
    case Page::Email:
        buildEmailPage();
        break;
    case Page::RadioSettings:
        buildRadioSettingsPage();
        break;
    case Page::MailSettings:
        buildMailSettingsPage();
        break;
    }

    renderStatus();
    setText(current.notice, current.notice_text);
    if (current.button_count > 0)
    {
        lv_group_focus_obj(current.buttons[0].object);
    }
}

} // namespace detail

void enter(lv_obj_t* parent)
{
    if (parent == nullptr || !detail::ensureState())
    {
        return;
    }

    detail::State& current = detail::state();
    current = detail::State{};
    current.parent = parent;
    current.port = cellularPort();
    if (current.port != nullptr)
    {
        current.port->readSettings(current.settings);
        detail::copyText(current.email_recipient,
                         sizeof(current.email_recipient),
                         current.settings.smtp_default_recipient);
    }
    detail::rebuildPage(detail::Page::Phone);
}

void exit(lv_obj_t*)
{
    detail::destroyPage();
    detail::releaseState();
}

} // namespace ui::mono::screens::screen_240x320::cellular_page
