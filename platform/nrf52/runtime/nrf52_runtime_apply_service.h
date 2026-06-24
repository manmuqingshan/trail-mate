#pragma once

#include "app/app_config.h"
#include "chat/runtime/self_identity_policy.h"

#ifndef TRAILMATE_NRF52_BLE_DISABLED
#define TRAILMATE_NRF52_BLE_DISABLED 1
#endif

namespace ble
{
class BleManager;
}

namespace chat
{
class ChatService;
class IMeshAdapter;
} // namespace chat

class BoardBase;

namespace platform::nrf52::runtime
{

class RuntimeApplyService
{
  public:
    void applyMesh(app::AppConfig& config,
                   chat::IMeshAdapter* mesh_router,
                   chat::ChatService* chat_service,
                   ble::BleManager* ble_manager,
                   BoardBase* board) const;

    void applyUserInfo(const chat::runtime::EffectiveSelfIdentity& previous_identity,
                       const chat::runtime::EffectiveSelfIdentity& current_identity,
                       chat::IMeshAdapter* mesh_router,
                       ble::BleManager* ble_manager) const;

    void applyPosition(const app::AppConfig& config,
                       BoardBase* board) const;
};

} // namespace platform::nrf52::runtime
