/**
 * @file notification_runtime.cpp
 * @brief Product notification owner for ESP Arduino builds.
 */

#include "platform/esp/arduino_common/notification_runtime.h"

#include "platform/ui/settings_store.h"

#include <cstdint>

namespace platform::esp::arduino_common::notification
{
namespace
{

constexpr const char* kSettingsNs = "settings";
constexpr const char* kMessageAlertsKey = "chat_message_alerts";
constexpr const char* kVibrationEnabledKey = "vibration_enabled";

} // namespace

bool message_alerts_enabled()
{
    return platform::ui::settings_store::get_int(kSettingsNs,
                                                 kMessageAlertsKey,
                                                 1) != 0;
}

bool vibration_enabled()
{
    return platform::ui::settings_store::get_bool(kSettingsNs,
                                                  kVibrationEnabledKey,
                                                  true);
}

bool play_alert(BoardBase& board, AlertKind kind)
{
    if (vibration_enabled() && kind != AlertKind::Preview)
    {
        board.vibrator();
    }

    board.playMessageTone();
    return true;
}

bool play_alert(app::IAppFacade& app_context, AlertKind kind)
{
    BoardBase* board = app_context.getBoard();
    if (!board)
    {
        return false;
    }
    return play_alert(*board, kind);
}

} // namespace platform::esp::arduino_common::notification
