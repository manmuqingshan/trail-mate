#pragma once

#include "ui/mono/screens/screen_240x320/cellular_port.h"

#include "lvgl.h"

#include <cstddef>
#include <cstdint>

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

constexpr lv_coord_t kScreenWidth = 240;
constexpr lv_coord_t kScreenHeight = 320;
constexpr lv_coord_t kMargin = 8;
constexpr lv_coord_t kContentWidth = kScreenWidth - (kMargin * 2);
constexpr lv_coord_t kLineHeight = 17;
constexpr lv_coord_t kFieldHeight = 18;
constexpr size_t kMaxLines = 4;
constexpr size_t kMaxFields = 5;
constexpr size_t kMaxButtons = 9;
constexpr size_t kMaxPageHistory = 4;

enum class Page : uint8_t
{
    Phone,
    Sms,
    Email,
    RadioSettings,
    MailSettings,
};

enum class ButtonAction : uint8_t
{
    Dial,
    Answer,
    HangUp,
    GoPhone,
    GoSms,
    GoEmail,
    GoRadioSettings,
    GoMailSettings,
    SaveSettings,
    ToggleCellular,
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
    char* value = nullptr;
    size_t capacity = 0;
};

struct Button
{
    lv_obj_t* object = nullptr;
    lv_obj_t* label = nullptr;
    ButtonAction action = ButtonAction::Back;
};

struct State
{
    lv_obj_t* parent = nullptr;
    lv_obj_t* root = nullptr;
    lv_obj_t* title = nullptr;
    lv_obj_t* page_label = nullptr;
    lv_obj_t* lines[kMaxLines]{};
    lv_obj_t* notice = nullptr;
    lv_obj_t* footer = nullptr;
    Field fields[kMaxFields]{};
    Button buttons[kMaxButtons]{};
    size_t field_count = 0;
    size_t button_count = 0;
    Page page_history[kMaxPageHistory]{};
    size_t page_history_count = 0;
    bool editing_field = false;
    Page page = Page::Phone;
    lv_timer_t* rebuild_timer = nullptr;
    CellularPort* port = nullptr;
    CellularSettings settings{};
    CellularStatus status{};
    char phone_number[40]{};
    char sms_recipient[40]{};
    char sms_body[161]{};
    char email_recipient[96]{};
    char email_subject[96]{};
    char email_body[256]{};
    char notice_text[96]{};
    char scratch[256]{};
};

State& state();

bool valid(const lv_obj_t* object);
void copyText(char* destination, size_t capacity, const char* source);
void stylePaper(lv_obj_t* object);
lv_obj_t* createText(lv_obj_t* parent, lv_coord_t width, lv_text_align_t alignment = LV_TEXT_ALIGN_LEFT);
void setText(lv_obj_t* label, const char* text);
void setLine(size_t index, const char* format, ...);
void clearLinesFrom(size_t first);
void setNotice(const char* format, ...);
void renderStatus();
void storeFieldValues();
void restoreApplicationGroup();
void addField(const char* label,
              char* value,
              size_t capacity,
              lv_coord_t top,
              size_t max_length,
              bool password = false,
              const char* accepted_chars = nullptr);
void addButton(const char* label, ButtonAction action, lv_coord_t x, lv_coord_t y, lv_coord_t width);
void scheduleRebuild(Page page);
void navigateTo(Page page);
bool navigateBack();
void rebuildPage(Page page);
void destroyPage();
void runAction(ButtonAction action);

void buildPhonePage();
void buildSmsPage();
void buildEmailPage();
void buildRadioSettingsPage();
void buildMailSettingsPage();
void renderPhoneStatus();
void renderSmsStatus();
void renderEmailStatus();
void renderRadioSettingsStatus();
void renderMailSettingsStatus();

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
