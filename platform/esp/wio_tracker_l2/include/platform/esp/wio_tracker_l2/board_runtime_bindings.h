#pragma once

#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/wio_tracker_l2/wio_tracker_l2_board.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{
inline void initializeBoard(bool waking_from_sleep)
{
    auto& board = ::boards::wio_tracker_l2::WioTrackerL2Board::instance();
    board.begin(NO_HW_GPS | NO_HW_SD);
    if (waking_from_sleep)
    {
        board.wakeUp();
    }
}
inline void initializeBoardDisplayHardware(bool waking_from_sleep) { initializeBoard(waking_from_sleep); }
inline void initializeBoardServices(bool) {}
inline void initializeDisplay()
{
    beginLvglHelper(::boards::wio_tracker_l2::WioTrackerL2Board::instance());
}
inline bool initializeStorage()
{
    return ::boards::wio_tracker_l2::WioTrackerL2Board::instance().ensureSDReady();
}
inline AppContextInitHandles resolveAppContextInitHandles()
{
    auto& board = ::boards::wio_tracker_l2::WioTrackerL2Board::instance();
    return {&board, &board, &board, nullptr};
}
inline bool tryResolveAppContextInitHandles(AppContextInitHandles* handles)
{
    if (!handles) return false;
    *handles = resolveAppContextInitHandles();
    return true;
}
} // namespace platform::esp::boards::detail
