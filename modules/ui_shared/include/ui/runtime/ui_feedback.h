#pragma once

#include <stdint.h>

namespace ui::feedback
{

enum class Severity : uint8_t
{
    Info,
    Success,
    Warning,
    Error
};

struct NoticeIntent
{
    const char* text = "";
    uint32_t duration_ms = 3000;
    Severity severity = Severity::Info;
};

class IFeedbackPresenter
{
  public:
    virtual ~IFeedbackPresenter() = default;

    virtual void init() = 0;
    virtual void show_notice(const NoticeIntent& intent) = 0;
    virtual void hide_notice() = 0;
};

void set_presenter(IFeedbackPresenter* presenter);
IFeedbackPresenter& presenter();

void init();
bool is_ready();
bool show_notice(const NoticeIntent& intent);
bool show_notice(const char* text, uint32_t duration_ms = 3000);
bool hide_notice();

} // namespace ui::feedback
