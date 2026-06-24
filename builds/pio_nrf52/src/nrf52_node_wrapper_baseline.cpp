#include "nrf52_node_app_shell.h"

namespace
{

bool g_wrapper_valid = false;

} // namespace

extern "C" void setup()
{
    trailmate::apps::nrf52_node::Nrf52NodeAppShell shell;
    g_wrapper_valid = shell.validate();
}

extern "C" void loop()
{
    (void)g_wrapper_valid;
}
