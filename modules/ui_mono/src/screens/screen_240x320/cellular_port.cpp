#include "ui/mono/screens/screen_240x320/cellular_port.h"

namespace ui::mono::screens::screen_240x320
{
namespace
{

CellularPort* s_cellular_port = nullptr;

} // namespace

void installCellularPort(CellularPort* port)
{
    s_cellular_port = port;
}

CellularPort* cellularPort()
{
    return s_cellular_port;
}

const char* cellularCallStateLabel(CellularCallState state)
{
    switch (state)
    {
    case CellularCallState::Idle:
        return "IDLE";
    case CellularCallState::Dialing:
        return "DIALING";
    case CellularCallState::Incoming:
        return "INCOMING";
    case CellularCallState::Active:
        return "ACTIVE";
    }
    return "UNKNOWN";
}

} // namespace ui::mono::screens::screen_240x320
