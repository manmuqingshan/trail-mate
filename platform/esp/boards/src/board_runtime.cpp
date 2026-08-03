#include "platform/esp/boards/board_runtime.h"

#include <cstdio>
#include <sys/time.h>

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/platform_esp_board_runtime.h"
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/platform_esp_board_runtime.h"
#elif defined(ARDUINO_T_DECK_PRO)
#include "boards/tdeck_pro/platform_esp_board_runtime.h"
#elif defined(ARDUINO_T_DECK)
#include "boards/tdeck/platform_esp_board_runtime.h"
#elif defined(ARDUINO_T_WATCH_S3)
#include "boards/twatchs3/platform_esp_board_runtime.h"
#else
#include "boards/tlora_pager/platform_esp_board_runtime.h"
#endif

#if defined(ARDUINO_T_DECK_PRO) || defined(ARDUINO_T_DECK) || \
    defined(ARDUINO_T_LORA_PAGER)
#include "platform/esp/common/shared_spi_coordinator.h"
#endif

namespace platform::esp::boards
{

void initializeBoard(bool waking_from_sleep)
{
    detail::initializeBoard(waking_from_sleep);
}

void initializeBoardDisplayHardware(bool waking_from_sleep)
{
    detail::initializeBoardDisplayHardware(waking_from_sleep);
}

void initializeBoardServices(bool waking_from_sleep)
{
    detail::initializeBoardServices(waking_from_sleep);
}

void initializeDisplay()
{
    detail::initializeDisplay();
}

bool initializeStorage()
{
    if (!storageStartupGateSatisfied())
    {
        std::printf("[SPI][STARTUP] storage mount deferred: first display transaction incomplete\n");
        return false;
    }
    return detail::initializeStorage();
}

bool tryResolveAppContextInitHandles(AppContextInitHandles* out_handles)
{
    return detail::tryResolveAppContextInitHandles(out_handles);
}

AppContextInitHandles resolveAppContextInitHandles()
{
    return detail::resolveAppContextInitHandles();
}

bool lockDisplay(uint32_t timeout_ms)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return detail::lockDisplay(timeout_ms);
#else
    (void)timeout_ms;
    return true;
#endif
}

void unlockDisplay()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    detail::unlockDisplay();
#endif
}

bool syncSystemTimeFromBoardRtc()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return detail::syncSystemTimeFromBoardRtc();
#else
    return false;
#endif
}

bool applySystemTimeAndSyncBoardRtc(std::time_t epoch_seconds, const char* source)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return detail::applySystemTimeAndSyncBoardRtc(epoch_seconds, source);
#else
    (void)source;
    timeval tv{};
    tv.tv_sec = epoch_seconds;
    tv.tv_usec = 0;
    return settimeofday(&tv, nullptr) == 0;
#endif
}

BoardIdentity defaultIdentity()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return detail::defaultIdentity();
#else
    return {};
#endif
}

BoardStorageCapabilities storageCapabilities()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || \
    defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return {StorageBusTopology::Sdmmc};
#elif defined(ARDUINO_T_DECK_PRO) || defined(ARDUINO_T_DECK) || \
    defined(ARDUINO_T_LORA_PAGER)
    return {StorageBusTopology::SharedDisplaySpi};
#else
    return {StorageBusTopology::DedicatedSpi};
#endif
}

bool storageStartupGateSatisfied()
{
    if (!storageCapabilities().requiresDisplayTransactionGate())
    {
        return true;
    }
#if defined(ARDUINO_T_DECK_PRO) || defined(ARDUINO_T_DECK) || \
    defined(ARDUINO_T_LORA_PAGER)
    return platform::esp::common::shared_spi_coordinator()
               .displayFrameCompletions() > 0U;
#else
    // A shared-display-SPI IDF board must provide a board-local completion
    // signal before hydration can be enabled.
    return false;
#endif
}

} // namespace platform::esp::boards
