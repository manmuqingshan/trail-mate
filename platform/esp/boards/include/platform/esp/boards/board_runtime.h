#pragma once

#include <cstdint>
#include <ctime>

class BoardBase;
class GpsBoard;
class LoraBoard;
class MotionBoard;

namespace platform::esp::boards
{

struct AppContextInitHandles
{
    AppContextInitHandles(BoardBase* board_in = nullptr, LoraBoard* lora_board_in = nullptr,
                          GpsBoard* gps_board_in = nullptr, MotionBoard* motion_board_in = nullptr)
        : board(board_in), lora_board(lora_board_in), gps_board(gps_board_in), motion_board(motion_board_in)
    {
    }

    bool isValid() const
    {
        return board != nullptr;
    }

    BoardBase* board;
    LoraBoard* lora_board;
    GpsBoard* gps_board;
    MotionBoard* motion_board;
};

struct BoardIdentity
{
    const char* long_name = "TrailMate";
    const char* short_name = "TM";
    const char* ble_name = "trail-mate";
};

void initializeBoard(bool waking_from_sleep);
void initializeDisplay();
bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles);
AppContextInitHandles resolveAppContextInitHandles();
bool lockDisplay(uint32_t timeout_ms);
void unlockDisplay();
bool syncSystemTimeFromBoardRtc();
bool applySystemTimeAndSyncBoardRtc(std::time_t epoch_seconds, const char* source);
BoardIdentity defaultIdentity();

} // namespace platform::esp::boards
