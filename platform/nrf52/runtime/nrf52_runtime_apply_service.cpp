#include "platform/nrf52/runtime/nrf52_runtime_apply_service.h"

#if !TRAILMATE_NRF52_BLE_DISABLED
#include "ble/ble_manager.h"
#endif
#include "board/BoardBase.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/usecase/chat_service.h"
#include "platform/nrf52/debug/nrf52_debug_console.h"

#include <cstring>

namespace platform::nrf52::runtime
{
namespace
{

const char* protocolLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT";
}

} // namespace

void RuntimeApplyService::applyMesh(app::AppConfig& config,
                                    chat::IMeshAdapter* mesh_router,
                                    chat::ChatService* chat_service,
                                    ble::BleManager* ble_manager,
                                    BoardBase* board) const
{
    platform::nrf52::debug_console::printf("[nrf52][cfg] applyMesh start proto=%u ok_to_mqtt=%u ignore_mqtt=%u\n",
                                           static_cast<unsigned>(config.mesh_protocol),
                                           config.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                                           config.meshtastic_config.ignore_mqtt ? 1U : 0U);

    if (mesh_router)
    {
        auto* router = static_cast<chat::MeshAdapterRouterCore*>(mesh_router);
        router->setActiveProtocol(config.mesh_protocol);
        mesh_router->applyConfig(config.activeMeshConfig());
    }
    if (board)
    {
        board->applyRadioConfig(config.mesh_protocol, config.activeMeshConfig());
    }
    if (chat_service)
    {
        chat_service->setActiveProtocol(config.mesh_protocol);
    }
#if !TRAILMATE_NRF52_BLE_DISABLED
    if (ble_manager)
    {
        ble_manager->applyProtocol(config.mesh_protocol);
    }
#else
    (void)ble_manager;
#endif

    platform::nrf52::debug_console::printf("[nrf52][cfg] applyMesh end\n");

    const chat::MeshConfig& mesh = config.activeMeshConfig();
    platform::nrf52::debug_console::printf("[nrf52] radio cfg %s region=%u preset=%u ch=%u tx=%d hop=%u\n",
                                           protocolLabel(config.mesh_protocol),
                                           static_cast<unsigned>(mesh.region),
                                           static_cast<unsigned>(mesh.modem_preset),
                                           static_cast<unsigned>(mesh.channel_num),
                                           static_cast<int>(mesh.tx_power),
                                           static_cast<unsigned>(mesh.hop_limit));
}

void RuntimeApplyService::applyUserInfo(const chat::runtime::EffectiveSelfIdentity& previous_identity,
                                        const chat::runtime::EffectiveSelfIdentity& current_identity,
                                        chat::IMeshAdapter* mesh_router,
                                        ble::BleManager* ble_manager) const
{
    if (mesh_router)
    {
        mesh_router->setUserInfo(current_identity.long_name, current_identity.short_name);
    }

    const bool ble_identity_changed =
        std::strcmp(previous_identity.long_name, current_identity.long_name) != 0 ||
        std::strcmp(previous_identity.short_name, current_identity.short_name) != 0;
#if !TRAILMATE_NRF52_BLE_DISABLED
    if (ble_identity_changed && ble_manager && ble_manager->isEnabled())
    {
        ble_manager->setEnabled(false);
        ble_manager->setEnabled(true);
    }
#else
    (void)ble_manager;
    (void)ble_identity_changed;
#endif
}

void RuntimeApplyService::applyPosition(const app::AppConfig& config,
                                        BoardBase* board) const
{
    if (board)
    {
        board->applyGpsConfig(config);
    }
}

} // namespace platform::nrf52::runtime
