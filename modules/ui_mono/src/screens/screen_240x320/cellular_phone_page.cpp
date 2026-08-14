#include "cellular_page_internal.h"

namespace ui::mono::screens::screen_240x320::cellular_page::detail
{

void buildPhonePage()
{
    State& current = state();
    addField("NUMBER", current.phone_number, sizeof(current.phone_number), 132, 39, false, "+0123456789*#");
    addButton("DIAL", ButtonAction::Dial, 8, 160, 68);
    addButton("ANSWER", ButtonAction::Answer, 82, 160, 72);
    addButton("HANG", ButtonAction::HangUp, 160, 160, 72);
    addButton("SMS", ButtonAction::GoSms, 8, 184, 52);
    addButton("EMAIL", ButtonAction::GoEmail, 66, 184, 64);
    addButton("RADIO SET", ButtonAction::GoRadioSettings, 136, 184, 96);
    addButton("BACK", ButtonAction::Back, 8, 298, 58);
}

void renderPhoneStatus()
{
    State& current = state();
    if (current.port != nullptr)
    {
        current.port->readStatus(current.status);
    }
    const CellularStatus& status = current.status;
    setLine(0,
            "CELL:%s SIG:%d %s",
            status.service_state[0] != '\0' ? status.service_state : "OFF",
            static_cast<int>(status.rssi),
            status.network_registered ? "NET" : "NO NET");
    setLine(1,
            "CALL:%s %s",
            cellularCallStateLabel(status.call_state),
            status.incoming_number);
    setLine(2, "SIM:%s OP:%s", status.sim_ready ? "READY" : "WAIT", status.operator_name);
    setLine(3, "%s", status.last_event);
}

} // namespace ui::mono::screens::screen_240x320::cellular_page::detail
