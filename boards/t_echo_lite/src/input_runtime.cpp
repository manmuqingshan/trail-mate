#include "boards/t_echo_lite/input_runtime.h"

#include "boards/t_echo_lite/board_profile.h"

#include <Arduino.h>

namespace boards::t_echo_lite
{
namespace
{

char textForKey(BoardInputKey key)
{
    switch (key)
    {
    case BoardInputKey::Star:
        return '*';
    case BoardInputKey::Hash:
        return '#';
    case BoardInputKey::Digit0:
        return '0';
    case BoardInputKey::Digit1:
        return '1';
    case BoardInputKey::Digit2:
        return '2';
    case BoardInputKey::Digit3:
        return '3';
    case BoardInputKey::Digit4:
        return '4';
    case BoardInputKey::Digit5:
        return '5';
    case BoardInputKey::Digit6:
        return '6';
    case BoardInputKey::Digit7:
        return '7';
    case BoardInputKey::Digit8:
        return '8';
    case BoardInputKey::Digit9:
        return '9';
    default:
        return '\0';
    }
}

BoardInputKey keyForMatrixEvent(uint8_t key_number)
{
    switch (key_number)
    {
    case 1:
        return BoardInputKey::Yes;
    case 2:
        return BoardInputKey::Star;
    case 3:
        return BoardInputKey::Digit0;
    case 4:
        return BoardInputKey::Hash;
    case 11:
        return BoardInputKey::No;
    case 12:
        return BoardInputKey::Digit7;
    case 13:
        return BoardInputKey::Digit8;
    case 14:
        return BoardInputKey::Digit9;
    case 21:
        return BoardInputKey::Down;
    case 22:
        return BoardInputKey::Digit4;
    case 23:
        return BoardInputKey::Digit5;
    case 24:
        return BoardInputKey::Digit6;
    case 31:
        return BoardInputKey::Center;
    case 32:
        return BoardInputKey::Digit1;
    case 33:
        return BoardInputKey::Digit2;
    case 34:
        return BoardInputKey::Digit3;
    case 41:
        return BoardInputKey::Up;
    case 42:
        return BoardInputKey::Escape;
    case 43:
        return BoardInputKey::Home;
    case 44:
        return BoardInputKey::Mail;
    default:
        return BoardInputKey::None;
    }
}

} // namespace

bool InputRuntime::ensureKeypadReady()
{
    if (keypad_initialized_)
    {
        return keypad_online_;
    }
    keypad_initialized_ = true;

    auto& board = TEchoLiteBoard::instance();
    TEchoLiteBoard::I2cGuard guard(board, 150);
    if (!guard)
    {
        return false;
    }

    const auto& keyboard = kBoardProfile.keyboard;
    if (keyboard.interrupt_pin >= 0)
    {
        pinMode(keyboard.interrupt_pin, INPUT_PULLUP);
    }

    keypad_online_ = keypad_.begin(keyboard.address, &board.i2cWire());
    if (!keypad_online_)
    {
        return false;
    }

    keypad_.matrix(keyboard.rows, keyboard.columns);
    keypad_.enableDebounce();
    keypad_.enableInterrupts();
    keypad_.flush();
    return true;
}

bool InputRuntime::pollSnapshot(BoardInputSnapshot* out_snapshot) const
{
    if (!out_snapshot)
    {
        return false;
    }
    *out_snapshot = snapshot_;
    return snapshot_.any_activity;
}

uint16_t InputRuntime::debounceMs() const
{
    return kBoardProfile.inputs.debounce_ms;
}

bool InputRuntime::mapKeyEvent(uint8_t event_code, BoardInputEvent* out_event)
{
    if (!out_event)
    {
        return false;
    }

    const bool pressed = (event_code & 0x80U) != 0;
    uint8_t key_number = event_code & 0x7FU;
    if (key_number == 0)
    {
        return false;
    }

    const BoardInputKey key = keyForMatrixEvent(key_number);
    if (key == BoardInputKey::None)
    {
        return false;
    }

    out_event->key = key;
    out_event->pressed = pressed;
    out_event->timestamp_ms = millis();
    out_event->text = pressed ? textForKey(key) : '\0';
    return true;
}

bool InputRuntime::pollEvent(BoardInputEvent* out_event)
{
    if (out_event)
    {
        *out_event = BoardInputEvent{};
    }

    if (!ensureKeypadReady())
    {
        return false;
    }

    auto& board = TEchoLiteBoard::instance();
    TEchoLiteBoard::I2cGuard guard(board, 50);
    if (!guard || keypad_.available() == 0)
    {
        snapshot_.any_activity = false;
        return false;
    }

    const uint8_t event_code = keypad_.getEvent();
    BoardInputEvent event{};
    if (!mapKeyEvent(event_code, &event))
    {
        snapshot_.any_activity = false;
        return false;
    }

    if (event.pressed)
    {
        last_activity_ms_ = event.timestamp_ms;
        snapshot_.any_activity = true;
    }
    else
    {
        snapshot_.any_activity = false;
    }

    if (out_event)
    {
        *out_event = event;
    }
    return true;
}

} // namespace boards::t_echo_lite
