#include "cellular_page_internal.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

void renderStatus()
{
    switch (state().page)
    {
    case Page::Phone:
        renderPhoneStatus();
        break;
    case Page::Sms:
        renderSmsStatus();
        break;
    case Page::Email:
        renderEmailStatus();
        break;
    case Page::RadioSettings:
        renderRadioSettingsStatus();
        break;
    case Page::MailSettings:
        renderMailSettingsStatus();
        break;
    }
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
