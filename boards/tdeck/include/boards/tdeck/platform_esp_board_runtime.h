#pragma once

#include "boards/tdeck/tdeck_board.h"
#include "platform/esp/boards/board_runtime.h"
#include "ui/LV_Helper.h"

namespace platform::esp::boards::detail
{

inline void initializeBoard(bool waking_from_sleep)
{
    // Defer GPS UART open until AppConfig is loaded. Defer SD until the boot
    // overlay is visible so slow card mount cannot leave the device on a black screen.
    ::boards::tdeck::instance.begin(NO_HW_GPS | NO_HW_SD);

    if (waking_from_sleep)
    {
        ::boards::tdeck::instance.wakeUp();
    }
}

inline void initializeDisplay()
{
    beginLvglHelper(static_cast<LilyGo_Display&>(::boards::tdeck::instance));
}

inline bool initializeStorage()
{
    return ::boards::tdeck::instance.ensureSDReady();
}

inline bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    if (!out_handles)
    {
        return false;
    }
    *out_handles = resolveAppContextInitHandles();
    return true;
}

inline AppContextInitHandles resolveAppContextInitHandles()
{
#if HAS_GPS
    return {&::boards::tdeck::board, &::boards::tdeck::instance, &::boards::tdeck::instance, &::boards::tdeck::instance};
#else
    return {&::boards::tdeck::board, &::boards::tdeck::instance, nullptr, nullptr};
#endif
}

} // namespace platform::esp::boards::detail
