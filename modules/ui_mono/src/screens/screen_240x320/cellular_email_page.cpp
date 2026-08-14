#include "cellular_page_internal.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

void buildEmailPage()
{
    State& current = state();
    addField("TO", current.email_recipient, sizeof(current.email_recipient), 132, 95);
    addField("SUBJ", current.email_subject, sizeof(current.email_subject), 154, 95);
    addField("BODY", current.email_body, sizeof(current.email_body), 176, 255);
    addButton("SEND", ButtonAction::SendEmail, 8, 208, 62);
    addButton("PHONE", ButtonAction::GoPhone, 76, 208, 64);
    addButton("MAIL SET", ButtonAction::GoMailSettings, 146, 208, 86);
    addButton("BACK", ButtonAction::Back, 8, 298, 58);
}

void renderEmailStatus()
{
    State& current = state();
    if (current.port != nullptr)
    {
        current.port->readStatus(current.status);
    }
    const CellularStatus& status = current.status;
    setLine(0, "EMAIL %s", status.smtps_available ? "SMTP READY" : "CONFIGURE SMTP");
    setLine(1, "APN:%s", current.settings.apn[0] != '\0' ? current.settings.apn : "AUTO");
    setLine(2, "SERVER:%s", current.settings.smtp_host);
    setLine(3, "%s", status.last_event);
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
