#pragma once

namespace platform::ui::a7682e
{

// Registers the board/runtime implementation of the generic 240x320 cellular
// UI port.  The caller is intentionally part of platform startup rather than
// the screen package, so the screen never learns modem or board details.
void install_screen_240x320_port();

} // namespace platform::ui::a7682e
