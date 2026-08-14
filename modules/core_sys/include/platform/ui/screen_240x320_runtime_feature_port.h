#pragma once

namespace platform::ui
{

// Installs the ESP runtime projection used by generic 240x320 monochrome
// pages. It deliberately contains no board-specific display or input logic.
void install_screen_240x320_runtime_feature_port();

} // namespace platform::ui
