/**
 * @file notification_runtime.h
 * @brief Product notification owner for ESP Arduino builds.
 */

#pragma once

#include "app/app_facades.h"
#include "board/BoardBase.h"

#include <cstdint>

namespace platform::esp::arduino_common::notification
{

enum class AlertKind : uint8_t
{
    Message,
    Contact,
    Preview,
};

bool message_alerts_enabled();
bool vibration_enabled();
bool play_alert(BoardBase& board, AlertKind kind);
bool play_alert(app::IAppFacade& app_context, AlertKind kind);

} // namespace platform::esp::arduino_common::notification
