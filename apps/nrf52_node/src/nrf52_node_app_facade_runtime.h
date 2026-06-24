#pragma once

#include "app/app_config.h"
#include "app/app_facades.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/runtime/self_identity_provider.h"
#include "nrf52_node_target_board.h"
#include "platform/nrf52/runtime/nrf52_runtime_apply_service.h"

#include <memory>

#ifndef TRAILMATE_NRF52_BLE_DISABLED
#define TRAILMATE_NRF52_BLE_DISABLED 1
#endif

#if !TRAILMATE_NRF52_BLE_DISABLED
#include "ble/ble_manager.h"
#endif

namespace chat
{
class ChatModel;
class ChatService;
class IChatStore;
class IMeshAdapter;
namespace contacts
{
class INodeStore;
class IContactStore;
class ContactService;
} // namespace contacts
} // namespace chat

namespace platform::nrf52::arduino_common
{
class SelfIdentityBridge;
}

namespace trailmate::apps::nrf52_node
{

class AppFacadeRuntime final : public app::IAppBleFacade
#if !TRAILMATE_NRF52_BLE_DISABLED
    ,
                               public ble::IBleRuntimeContext
#endif
{
  public:
    static AppFacadeRuntime& instance();
    ~AppFacadeRuntime();

    bool initialize();
    bool isInitialized() const;

    bool installMeshBackend(chat::MeshProtocol protocol,
                            std::unique_ptr<chat::IMeshAdapter> backend);

    app::AppConfig& getConfig() override;
    const app::AppConfig& getConfig() const override;
    chat::MeshProtocol getMeshProtocol() const override;
    void saveConfig() override;
    void applyMeshConfig() override;
    void applyUserInfo() override;
    void applyPositionConfig() override;
    void applyNetworkLimits() override;
    void applyPrivacyConfig() override;
    void applyChatDefaults() override;
    void getEffectiveUserInfo(char* out_long, std::size_t long_len,
                              char* out_short, std::size_t short_len) const override;
    bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) override;

    chat::ChatService& getChatService() override;
    chat::contacts::ContactService& getContactService() override;
    chat::IMeshAdapter* getMeshAdapter() override;
    const chat::IMeshAdapter* getMeshAdapter() const override;
    chat::NodeId getSelfNodeId() const override;

    team::TeamController* getTeamController() override;
    team::TeamPairingService* getTeamPairing() override;
    team::TeamService* getTeamService() override;
    const team::TeamService* getTeamService() const override;
    team::TeamTrackSampler* getTeamTrackSampler() override;
    void setTeamModeActive(bool active) override;

    void broadcastNodeInfo() override;
    void clearNodeDb() override;
    void clearMessageDb() override;

    ble::BleManager* getBleManager() override;
    const ble::BleManager* getBleManager() const override;
    bool isBleEnabled() const override;
    void setBleEnabled(bool enabled) override;
    void restartDevice() override;
    chat::contacts::INodeStore* getNodeStore() override;
    const chat::contacts::INodeStore* getNodeStore() const override;
    bool getDeviceMacAddress(uint8_t out_mac[6]) const override;
    bool syncCurrentEpochSeconds(uint32_t epoch_seconds) override;
    void resetMeshConfig() override;
    chat::ui::IChatUiRuntime* getChatUiRuntime() override;
    void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) override;
    BoardBase* getBoard() override;
    const BoardBase* getBoard() const override;

    void updateCoreServices() override;
    void tickEventRuntime() override;
    void dispatchPendingEvents(std::size_t max_events = 32) override;
    bool takeChatSendResultFeedback(chat::MessageId* out_msg_id, bool* out_success);

#if !TRAILMATE_NRF52_BLE_DISABLED
    const app::AppConfig& bleConfig() const override;
    bool bleEnabled() const override;
    void bleEffectiveUserInfo(char* out_long, std::size_t long_len,
                              char* out_short, std::size_t short_len) const override;
    chat::NodeId bleSelfNodeId() const override;
    app::IAppBleFacade& bleAppFacade() override;
#endif

    const chat::runtime::EffectiveSelfIdentity& effectiveIdentity() const;

  private:
    AppFacadeRuntime();

    void initializeStores();
    void initializeChatRuntime();
    void refreshEffectiveIdentity();
    void syncSelfPositionFromGps();
    bool consumePostSaveApplySkip(uint8_t bit, const char* label);
    void markPostSaveApplySkips(uint8_t mask);
    void clearPostSaveApplySkips();
    chat::NodeId resolveSelfNodeId() const;
    const chat::runtime::SelfIdentityProvider* identityProvider() const;

    bool initialized_ = false;
    app::AppConfig config_{};
    std::unique_ptr<platform::nrf52::arduino_common::SelfIdentityBridge> identity_bridge_;
    mutable chat::runtime::EffectiveSelfIdentity effective_identity_{};

    std::unique_ptr<chat::contacts::INodeStore> node_store_;
    std::unique_ptr<chat::contacts::IContactStore> contact_store_;
    std::unique_ptr<chat::contacts::ContactService> contact_service_;
    std::unique_ptr<chat::ChatModel> chat_model_;
    std::unique_ptr<chat::IChatStore> chat_store_;
    std::unique_ptr<chat::IMeshAdapter> mesh_router_;
    std::unique_ptr<chat::ChatService> chat_service_;
#if !TRAILMATE_NRF52_BLE_DISABLED
    std::unique_ptr<ble::BleManager> ble_manager_;
#endif
    std::unique_ptr<platform::nrf52::runtime::RuntimeApplyService> apply_service_;
    target_board::Board* board_ = nullptr;
    chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
    bool config_save_pending_ = false;
    uint8_t post_save_apply_skip_mask_ = 0;
    uint32_t last_chat_store_flush_ms_ = 0;
    bool pending_chat_send_result_feedback_ = false;
    chat::MessageId pending_chat_send_result_msg_id_ = 0;
    bool pending_chat_send_result_success_ = false;
};

} // namespace trailmate::apps::nrf52_node
