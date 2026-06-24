#include "nrf52_node_arduino_entry.h"

// T-Echo-Lite owns the board-level Arduino entrypoint. App runtime sources are
// compiled by the nRF52 app shell library to keep one definition per symbol.

extern "C" void setup()
{
    trailmate::apps::nrf52_node::arduino_entry::setup();
}

extern "C" void loop()
{
    trailmate::apps::nrf52_node::arduino_entry::loop();
}
