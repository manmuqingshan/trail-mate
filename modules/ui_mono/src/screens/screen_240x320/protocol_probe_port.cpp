#include "ui/mono/screens/screen_240x320/protocol_probe_port.h"

namespace ui::mono::screens::screen_240x320
{
namespace
{

ProtocolProbePort* s_protocol_probe_port = nullptr;

} // namespace

void installProtocolProbePort(ProtocolProbePort* port)
{
    s_protocol_probe_port = port;
}

ProtocolProbePort* protocolProbePort()
{
    return s_protocol_probe_port;
}

} // namespace ui::mono::screens::screen_240x320
