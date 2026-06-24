#pragma once

#include "boards/t_echo_lite/t_echo_lite_board.h"

#include <Adafruit_TCA8418.h>

namespace boards::t_echo_lite
{

class InputRuntime
{
  public:
    bool pollSnapshot(BoardInputSnapshot* out_snapshot) const;
    uint16_t debounceMs() const;
    bool pollEvent(BoardInputEvent* out_event);

  private:
    bool ensureKeypadReady();
    static bool mapKeyEvent(uint8_t event_code, BoardInputEvent* out_event);

    Adafruit_TCA8418 keypad_{};
    bool keypad_initialized_ = false;
    bool keypad_online_ = false;
    mutable BoardInputSnapshot snapshot_{};
    uint32_t last_activity_ms_ = 0;
};

} // namespace boards::t_echo_lite
