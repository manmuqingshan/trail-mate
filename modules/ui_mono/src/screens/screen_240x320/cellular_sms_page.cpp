#include "cellular_page_internal.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

void buildSmsPage()
{
    State& current = state();
    addField("TO", current.sms_recipient, sizeof(current.sms_recipient), 132, 39, false, "+0123456789*#");
    addField("TEXT", current.sms_body, sizeof(current.sms_body), 154, 160);
    addButton("SEND SMS", ButtonAction::SendSms, 8, 184, 90);
    addButton("PHONE", ButtonAction::GoPhone, 104, 184, 64);
    addButton("EMAIL", ButtonAction::GoEmail, 174, 184, 58);
    addButton("BACK", ButtonAction::Back, 8, 298, 58);
}

void renderSmsStatus()
{
    State& current = state();
    if (current.port != nullptr)
    {
        current.port->readStatus(current.status);
    }
    const CellularStatus& status = current.status;
    setLine(0, "SMS %s RX:%lu", status.modem_ready ? "READY" : "WAIT", static_cast<unsigned long>(status.received_sms_count));
    setLine(1, "LAST:%s", status.last_sms_sender);
    setLine(2, "%s", status.last_sms_body);
    setLine(3, "%s", status.last_event);
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
