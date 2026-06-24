#include "nrf52_node_app_facade_runtime.h"

#include "app/app_facade_access.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/mesh_adapter_router_core.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/store/ram_store.h"
#include "chat/runtime/self_identity_provider.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/nrf52/arduino_common/chat/infra/contact_store.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/node_store.h"
#include "platform/nrf52/arduino_common/chat/infra/radio_packet_io.h"
#include "platform/nrf52/arduino_common/chat/infra/store/internal_fs_store.h"
#include "platform/nrf52/arduino_common/device_identity.h"
#include "platform/nrf52/arduino_common/self_identity_bridge.h"
#include "platform/nrf52/arduino_common/sys/event_bus.h"
#include "platform/nrf52/debug/nrf52_debug_console.h"
#include "platform/nrf52/protocol/nrf52_protocol_factory.h"
#include "platform/nrf52/runtime/nrf52_runtime_apply_service.h"
#include "sys/clock.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace trailmate::apps::nrf52_node
{
namespace
{
constexpr uint32_t kChatStoreFlushIntervalMs = 2000UL;
constexpr uint8_t kSkipApplyMesh = 1U << 0;
constexpr uint8_t kSkipApplyUser = 1U << 1;
constexpr uint8_t kSkipApplyPosition = 1U << 2;
constexpr uint8_t kSkipApplyNetwork = 1U << 3;
constexpr uint8_t kSkipApplyPrivacy = 1U << 4;
constexpr uint8_t kSkipApplyChatDefaults = 1U << 5;
constexpr uint8_t kSkipApplyMaskAll = kSkipApplyMesh | kSkipApplyUser | kSkipApplyPosition | kSkipApplyNetwork |
                                      kSkipApplyPrivacy | kSkipApplyChatDefaults;

class ScopedGpsSuspend
{
  public:
    explicit ScopedGpsSuspend(target_board::Board* board)
        : board_(board),
          resume_(board_ && board_->gpsEnabled())
    {
        if (resume_)
        {
            board_->suspendGps();
        }
    }

    ~ScopedGpsSuspend()
    {
        if (resume_ && board_)
        {
            board_->resumeGps();
        }
    }

    ScopedGpsSuspend(const ScopedGpsSuspend&) = delete;
    ScopedGpsSuspend& operator=(const ScopedGpsSuspend&) = delete;

  private:
    target_board::Board* board_ = nullptr;
    bool resume_ = false;
};

platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter* getMeshtasticBackend(chat::IMeshAdapter* adapter)
{
    if (!adapter)
    {
        return nullptr;
    }

    chat::IMeshAdapter* backend = adapter->backendForProtocol(chat::MeshProtocol::Meshtastic);
    return backend
               ? static_cast<platform::nrf52::arduino_common::chat::meshtastic::MeshtasticRadioAdapter*>(backend)
               : nullptr;
}

template <typename T>
void copyString(const char* src, T* dst, size_t dst_len)
{
    if (!dst || dst_len == 0)
    {
        return;
    }

    if (!src)
    {
        dst[0] = '\0';
        return;
    }

    const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
    std::memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

bool buildNodePositionFromGpsState(const ::gps::GpsState& gps_state,
                                   ::chat::contacts::NodePosition* out_position)
{
    if (!out_position || !gps_state.valid)
    {
        return false;
    }

    ::chat::contacts::NodePosition position{};
    position.valid = true;
    position.latitude_i = static_cast<int32_t>(std::lround(gps_state.lat * 1e7));
    position.longitude_i = static_cast<int32_t>(std::lround(gps_state.lng * 1e7));
    position.has_altitude = gps_state.has_alt;
    position.altitude = gps_state.has_alt ? static_cast<int32_t>(std::lround(gps_state.alt_m)) : 0;
    position.timestamp = ::sys::epoch_seconds_now();
    *out_position = position;
    return true;
}

} // namespace

AppFacadeRuntime& AppFacadeRuntime::instance()
{
    static AppFacadeRuntime runtime;
    return runtime;
}

AppFacadeRuntime::AppFacadeRuntime()
    : apply_service_(new platform::nrf52::runtime::RuntimeApplyService())
{
}

AppFacadeRuntime::~AppFacadeRuntime() = default;

bool AppFacadeRuntime::initialize()
{
    if (initialized_)
    {
        return true;
    }

    board_ = &target_board::instance();
    if (board_)
    {
        (void)board_->begin();
    }
    (void)target_board::settings_store::loadAppConfig(config_);
    target_board::settings_store::normalizeConfig(config_);
#if TRAILMATE_NRF52_BLE_DISABLED
    config_.ble_enabled = false;
#endif
    initializeStores();
    const chat::NodeId resolved_self_node_id = resolveSelfNodeId();
    platform::nrf52::arduino_common::device_identity::setResolvedSelfNodeId(resolved_self_node_id);
    identity_bridge_ = std::unique_ptr<platform::nrf52::arduino_common::SelfIdentityBridge>(
        new platform::nrf52::arduino_common::SelfIdentityBridge(config_,
                                                                NRF_FICR->DEVICEADDR[0],
                                                                NRF_FICR->DEVICEADDR[1],
                                                                board_->defaultLongName(),
                                                                board_->defaultShortName()));
    identity_bridge_->setNodeId(resolved_self_node_id);
    refreshEffectiveIdentity();
    initializeChatRuntime();

    app::bindAppFacade(*this);
#if !TRAILMATE_NRF52_BLE_DISABLED
    ble_manager_ = std::unique_ptr<ble::BleManager>(new ble::BleManager(*this));
    if (config_.ble_enabled && ble_manager_)
    {
        ble_manager_->begin();
    }
#endif
    initialized_ = true;
    platform::nrf52::debug_console::printf("%s app facade ready node=%08lX\n",
                                           target_board::kLogTag,
                                           static_cast<unsigned long>(effective_identity_.node_id));
    return true;
}

bool AppFacadeRuntime::isInitialized() const
{
    return initialized_;
}

bool AppFacadeRuntime::installMeshBackend(chat::MeshProtocol protocol,
                                          std::unique_ptr<chat::IMeshAdapter> backend)
{
    if (!mesh_router_ || !backend)
    {
        return false;
    }

    if (!mesh_router_->installBackend(protocol, std::move(backend)))
    {
        return false;
    }

    if (protocol == config_.mesh_protocol)
    {
        applyMeshConfig();
        applyUserInfo();
        applyNetworkLimits();
        applyPrivacyConfig();
    }
    return true;
}

void AppFacadeRuntime::initializeStores()
{
    if (node_store_ && contact_store_ && contact_service_)
    {
        return;
    }

    auto node_store = std::unique_ptr<platform::nrf52::arduino_common::chat::meshtastic::NodeStore>(
        new platform::nrf52::arduino_common::chat::meshtastic::NodeStore());
    auto contact_store = std::unique_ptr<platform::nrf52::arduino_common::chat::infra::ContactStore>(
        new platform::nrf52::arduino_common::chat::infra::ContactStore());
    platform::nrf52::arduino_common::chat::infra::ContactStore* contact_store_ptr = contact_store.get();
    node_store->setProtectedNodeChecker([contact_store_ptr](uint32_t node_id)
                                        { return contact_store_ptr && contact_store_ptr->hasContactNode(node_id); });
    node_store_ = std::move(node_store);
    contact_store_ = std::move(contact_store);
    contact_service_ = std::unique_ptr<chat::contacts::ContactService>(
        new chat::contacts::ContactService(*node_store_, *contact_store_));

    if (node_store_)
    {
        node_store_->begin();
    }
    if (contact_store_)
    {
        contact_store_->begin();
    }
    if (contact_service_)
    {
        contact_service_->begin();
    }
}

void AppFacadeRuntime::initializeChatRuntime()
{
    chat_model_ = std::unique_ptr<chat::ChatModel>(new chat::ChatModel());
    chat_store_ = std::unique_ptr<chat::IChatStore>(
        new platform::nrf52::arduino_common::chat::infra::store::InternalFsStore());
    mesh_router_ = std::unique_ptr<chat::IMeshAdapter>(new chat::MeshAdapterRouterCore());
    chat_service_ = std::unique_ptr<chat::ChatService>(
        new chat::ChatService(*chat_model_, *mesh_router_, *chat_store_, config_.mesh_protocol));

    if (chat_model_)
    {
        chat_model_->setPolicy(config_.chat_policy);
    }

    (void)installMeshBackend(chat::MeshProtocol::Meshtastic,
                             platform::nrf52::protocol::createProtocolAdapter(chat::MeshProtocol::Meshtastic,
                                                                              identityProvider(),
                                                                              static_cast<platform::nrf52::arduino_common::chat::meshtastic::NodeStore*>(node_store_.get()),
                                                                              contact_service_.get()));
    (void)installMeshBackend(chat::MeshProtocol::MeshCore,
                             platform::nrf52::protocol::createProtocolAdapter(chat::MeshProtocol::MeshCore, identityProvider()));

    applyMeshConfig();
    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();
    applyChatDefaults();
}

void AppFacadeRuntime::refreshEffectiveIdentity()
{
    effective_identity_ = chat::runtime::EffectiveSelfIdentity{};
    if (!identity_bridge_)
    {
        return;
    }

    chat::runtime::SelfIdentityInput input{};
    if (!identity_bridge_->readSelfIdentityInput(&input))
    {
        return;
    }

    (void)chat::runtime::resolveEffectiveSelfIdentity(input, &effective_identity_);
}

const chat::runtime::SelfIdentityProvider* AppFacadeRuntime::identityProvider() const
{
    return identity_bridge_.get();
}

app::AppConfig& AppFacadeRuntime::getConfig()
{
    return config_;
}

const app::AppConfig& AppFacadeRuntime::getConfig() const
{
    return config_;
}

chat::MeshProtocol AppFacadeRuntime::getMeshProtocol() const
{
    return config_.mesh_protocol;
}

void AppFacadeRuntime::saveConfig()
{
    ScopedGpsSuspend suspend_gps(board_);
    clearPostSaveApplySkips();
#if TRAILMATE_NRF52_BLE_DISABLED
    config_.ble_enabled = false;
#endif
    platform::nrf52::debug_console::printf("%s[cfg] save start proto=%u ok_to_mqtt=%u ignore_mqtt=%u ble=%u\n",
                                           target_board::kLogTag,
                                           static_cast<unsigned>(config_.mesh_protocol),
                                           config_.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                                           config_.meshtastic_config.ignore_mqtt ? 1U : 0U,
                                           config_.ble_enabled ? 1U : 0U);
    target_board::settings_store::normalizeConfig(config_);
    platform::nrf52::debug_console::printf("%s[cfg] save post-normalize ok_to_mqtt=%u ignore_mqtt=%u\n",
                                           target_board::kLogTag,
                                           config_.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                                           config_.meshtastic_config.ignore_mqtt ? 1U : 0U);
    refreshEffectiveIdentity();
    platform::nrf52::debug_console::printf("%s[cfg] save post-identity\n", target_board::kLogTag);
    applyMeshConfig();
    platform::nrf52::debug_console::printf("%s[cfg] save post-applyMesh\n", target_board::kLogTag);
    applyUserInfo();
    platform::nrf52::debug_console::printf("%s[cfg] save post-applyUser\n", target_board::kLogTag);
    applyPositionConfig();
    platform::nrf52::debug_console::printf("%s[cfg] save post-applyPos\n", target_board::kLogTag);
    applyNetworkLimits();
    platform::nrf52::debug_console::printf("%s[cfg] save post-applyLimits\n", target_board::kLogTag);
    applyPrivacyConfig();
    platform::nrf52::debug_console::printf("%s[cfg] save post-applyPrivacy\n", target_board::kLogTag);
    applyChatDefaults();
    markPostSaveApplySkips(kSkipApplyMaskAll);
    target_board::settings_store::queueSaveAppConfig(config_);
    config_save_pending_ = true;
    platform::nrf52::debug_console::printf("%s[cfg] save deferred-store queued\n", target_board::kLogTag);
}

void AppFacadeRuntime::applyMeshConfig()
{
    if (consumePostSaveApplySkip(kSkipApplyMesh, "applyMesh"))
    {
        return;
    }
    if (apply_service_)
    {
        apply_service_->applyMesh(config_,
                                  mesh_router_.get(),
                                  chat_service_.get(),
#if TRAILMATE_NRF52_BLE_DISABLED
                                  nullptr,
#else
                                  ble_manager_.get(),
#endif
                                  board_);
    }
}

void AppFacadeRuntime::applyUserInfo()
{
    if (consumePostSaveApplySkip(kSkipApplyUser, "applyUser"))
    {
        return;
    }
    const chat::runtime::EffectiveSelfIdentity previous_identity = effective_identity_;
    refreshEffectiveIdentity();
    if (apply_service_)
    {
        apply_service_->applyUserInfo(previous_identity,
                                      effective_identity_,
                                      mesh_router_.get(),
#if TRAILMATE_NRF52_BLE_DISABLED
                                      nullptr);
#else
                                      ble_manager_.get());
#endif
    }
}

void AppFacadeRuntime::applyPositionConfig()
{
    if (consumePostSaveApplySkip(kSkipApplyPosition, "applyPos"))
    {
        return;
    }
    if (apply_service_)
    {
        apply_service_->applyPosition(config_, board_);
    }
}

chat::NodeId AppFacadeRuntime::resolveSelfNodeId() const
{
    return platform::nrf52::arduino_common::device_identity::resolveNodeId(
        NRF_FICR->DEVICEADDR[0],
        NRF_FICR->DEVICEADDR[1],
        NRF_FICR->DEVICEID[0],
        NRF_FICR->DEVICEID[1],
        node_store_.get());
}

void AppFacadeRuntime::applyNetworkLimits()
{
    if (consumePostSaveApplySkip(kSkipApplyNetwork, "applyLimits"))
    {
        return;
    }
    if (mesh_router_)
    {
        mesh_router_->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }
}

void AppFacadeRuntime::applyPrivacyConfig()
{
    if (consumePostSaveApplySkip(kSkipApplyPrivacy, "applyPrivacy"))
    {
        return;
    }
    if (mesh_router_)
    {
        mesh_router_->setPrivacyConfig(config_.privacy_encrypt_mode);
    }
}

void AppFacadeRuntime::applyChatDefaults()
{
    if (consumePostSaveApplySkip(kSkipApplyChatDefaults, "applyChatDefaults"))
    {
        return;
    }
    if (!chat_service_)
    {
        return;
    }

    const chat::ChannelId channel = (config_.chat_channel == 1)
                                        ? chat::ChannelId::SECONDARY
                                        : chat::ChannelId::PRIMARY;
    chat_service_->switchChannel(channel);
}

void AppFacadeRuntime::getEffectiveUserInfo(char* out_long, std::size_t long_len,
                                            char* out_short, std::size_t short_len) const
{
    const char* long_name = effective_identity_.long_name[0] != '\0'
                                ? effective_identity_.long_name
                                : (board_ ? board_->defaultLongName() : "");
    const char* short_name = effective_identity_.short_name[0] != '\0'
                                 ? effective_identity_.short_name
                                 : (board_ ? board_->defaultShortName() : "");
    copyString(long_name, out_long, long_len);
    copyString(short_name, out_short, short_len);
}

bool AppFacadeRuntime::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!chat::infra::isValidMeshProtocol(protocol))
    {
        return false;
    }

    ScopedGpsSuspend suspend_gps(board_);
    const bool protocol_changed = (config_.mesh_protocol != protocol);
    platform::nrf52::debug_console::printf("%s[cfg] switch proto=%u persist=%u changed=%u\n",
                                           target_board::kLogTag,
                                           static_cast<unsigned>(protocol),
                                           persist ? 1U : 0U,
                                           protocol_changed ? 1U : 0U);
    config_.mesh_protocol = protocol;
    target_board::settings_store::cacheAppConfig(config_);

    if (persist)
    {
        target_board::settings_store::normalizeConfig(config_);
        if (target_board::settings_store::saveAppConfig(config_))
        {
            config_save_pending_ = target_board::settings_store::hasDeferredSavePending();
            platform::nrf52::debug_console::printf("%s[cfg] switch persist save=ok deferred=%u\n",
                                                   target_board::kLogTag,
                                                   config_save_pending_ ? 1U : 0U);
            if (protocol_changed)
            {
                platform::nrf52::debug_console::printf("%s[cfg] switch persist rebooting for proto=%u\n",
                                                       target_board::kLogTag,
                                                       static_cast<unsigned>(protocol));
                restartDevice();
            }
        }
        else
        {
            target_board::settings_store::queueSaveAppConfig(config_);
            config_save_pending_ = true;
            platform::nrf52::debug_console::printf("%s[cfg] switch persist save=deferred\n", target_board::kLogTag);
        }
    }

    if (protocol_changed)
    {
        applyMeshConfig();
        applyUserInfo();
        applyNetworkLimits();
        applyPrivacyConfig();
    }
    return true;
}

chat::ChatService& AppFacadeRuntime::getChatService()
{
    return *chat_service_;
}

chat::contacts::ContactService& AppFacadeRuntime::getContactService()
{
    return *contact_service_;
}

chat::IMeshAdapter* AppFacadeRuntime::getMeshAdapter()
{
    return mesh_router_.get();
}

const chat::IMeshAdapter* AppFacadeRuntime::getMeshAdapter() const
{
    return mesh_router_.get();
}

chat::NodeId AppFacadeRuntime::getSelfNodeId() const
{
    return effective_identity_.node_id;
}

team::TeamController* AppFacadeRuntime::getTeamController()
{
    return nullptr;
}

team::TeamPairingService* AppFacadeRuntime::getTeamPairing()
{
    return nullptr;
}

team::TeamService* AppFacadeRuntime::getTeamService()
{
    return nullptr;
}

const team::TeamService* AppFacadeRuntime::getTeamService() const
{
    return nullptr;
}

team::TeamTrackSampler* AppFacadeRuntime::getTeamTrackSampler()
{
    return nullptr;
}

void AppFacadeRuntime::setTeamModeActive(bool active)
{
    (void)active;
}

void AppFacadeRuntime::broadcastNodeInfo()
{
    if (mesh_router_)
    {
        (void)mesh_router_->requestNodeInfo(0xFFFFFFFFUL, false);
    }
}

void AppFacadeRuntime::clearNodeDb()
{
    if (node_store_)
    {
        node_store_->clear();
    }
    if (contact_service_)
    {
        contact_service_->clearCache();
    }
}

void AppFacadeRuntime::clearMessageDb()
{
    if (chat_service_)
    {
        chat_service_->clearAllMessages();
    }
}

ble::BleManager* AppFacadeRuntime::getBleManager()
{
#if TRAILMATE_NRF52_BLE_DISABLED
    return nullptr;
#else
    return ble_manager_.get();
#endif
}

const ble::BleManager* AppFacadeRuntime::getBleManager() const
{
#if TRAILMATE_NRF52_BLE_DISABLED
    return nullptr;
#else
    return ble_manager_.get();
#endif
}

bool AppFacadeRuntime::isBleEnabled() const
{
#if TRAILMATE_NRF52_BLE_DISABLED
    return false;
#else
    return config_.ble_enabled;
#endif
}

void AppFacadeRuntime::setBleEnabled(bool enabled)
{
#if TRAILMATE_NRF52_BLE_DISABLED
    (void)enabled;
    if (config_.ble_enabled)
    {
        config_.ble_enabled = false;
        target_board::settings_store::queueSaveAppConfig(config_);
        config_save_pending_ = true;
    }
    return;
#else
    if (config_.ble_enabled == enabled)
    {
        if (ble_manager_)
        {
            ble_manager_->setEnabled(enabled);
        }
        return;
    }

    config_.ble_enabled = enabled;
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
    target_board::settings_store::queueSaveAppConfig(config_);
    config_save_pending_ = true;
#endif
}

void AppFacadeRuntime::restartDevice()
{
    NVIC_SystemReset();
}

chat::contacts::INodeStore* AppFacadeRuntime::getNodeStore()
{
    return node_store_.get();
}

const chat::contacts::INodeStore* AppFacadeRuntime::getNodeStore() const
{
    return node_store_.get();
}

bool AppFacadeRuntime::getDeviceMacAddress(uint8_t out_mac[6]) const
{
    if (!out_mac)
    {
        return false;
    }

    const auto mac = platform::nrf52::arduino_common::device_identity::getSelfMacAddress();
    std::copy(mac.begin(), mac.end(), out_mac);
    return true;
}

bool AppFacadeRuntime::syncCurrentEpochSeconds(uint32_t epoch_seconds)
{
    if (!board_ || epoch_seconds == 0)
    {
        return false;
    }

    board_->setCurrentEpochSeconds(epoch_seconds);
    return true;
}

void AppFacadeRuntime::resetMeshConfig()
{
    if (config_.mesh_protocol == chat::MeshProtocol::MeshCore)
    {
        config_.meshcore_config = chat::MeshConfig();
        config_.applyMeshCoreFactoryDefaults();
    }
    else
    {
        config_.meshtastic_config = chat::MeshConfig();
        config_.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }
    saveConfig();
    applyMeshConfig();
}

chat::ui::IChatUiRuntime* AppFacadeRuntime::getChatUiRuntime()
{
    return chat_ui_runtime_;
}

void AppFacadeRuntime::setChatUiRuntime(chat::ui::IChatUiRuntime* runtime)
{
    chat_ui_runtime_ = runtime;
}

BoardBase* AppFacadeRuntime::getBoard()
{
    return board_;
}

const BoardBase* AppFacadeRuntime::getBoard() const
{
    return board_;
}

void AppFacadeRuntime::updateCoreServices()
{
    clearPostSaveApplySkips();
    syncSelfPositionFromGps();
    if (chat_service_)
    {
        chat_service_->processIncoming();
        const uint32_t now_ms = ::sys::millis_now();
        if ((now_ms - last_chat_store_flush_ms_) >= kChatStoreFlushIntervalMs)
        {
            chat_service_->flushStore();
            if (node_store_)
            {
                (void)node_store_->flush();
            }
            if (auto* mt = getMeshtasticBackend(getMeshAdapter()))
            {
                mt->flushDeferredPersistence(false);
            }
            last_chat_store_flush_ms_ = now_ms;
        }
    }
#if !TRAILMATE_NRF52_BLE_DISABLED
    if (ble_manager_)
    {
        ble_manager_->update();
    }
#endif
}

bool AppFacadeRuntime::consumePostSaveApplySkip(uint8_t bit, const char* label)
{
    if ((post_save_apply_skip_mask_ & bit) == 0)
    {
        return false;
    }

    post_save_apply_skip_mask_ &= static_cast<uint8_t>(~bit);
    platform::nrf52::debug_console::printf("%s[cfg] %s skipped: already applied in save\n",
                                           target_board::kLogTag,
                                           label ? label : "apply");
    return true;
}

void AppFacadeRuntime::markPostSaveApplySkips(uint8_t mask)
{
    post_save_apply_skip_mask_ |= mask;
}

void AppFacadeRuntime::clearPostSaveApplySkips()
{
    post_save_apply_skip_mask_ = 0;
}

void AppFacadeRuntime::syncSelfPositionFromGps()
{
    if (!contact_service_ || !board_ || effective_identity_.node_id == 0)
    {
        return;
    }

    ::chat::contacts::NodePosition position{};
    if (!buildNodePositionFromGpsState(board_->gpsData(), &position))
    {
        return;
    }

    const ::chat::contacts::NodeInfo* existing = contact_service_->getNodeInfo(effective_identity_.node_id);
    if (existing && existing->position.valid &&
        existing->position.latitude_i == position.latitude_i &&
        existing->position.longitude_i == position.longitude_i &&
        existing->position.has_altitude == position.has_altitude &&
        existing->position.altitude == position.altitude)
    {
        return;
    }

    contact_service_->updateNodePosition(effective_identity_.node_id, position);
}

void AppFacadeRuntime::tickEventRuntime()
{
    if (!config_save_pending_)
    {
        return;
    }

    if (!target_board::settings_store::hasDeferredSavePending())
    {
        config_save_pending_ = false;
        return;
    }

    const bool flushed = target_board::settings_store::tickDeferredSave();
    if (flushed)
    {
        platform::nrf52::debug_console::printf("%s[cfg] deferred-store flush ok\n", target_board::kLogTag);
    }
    config_save_pending_ = target_board::settings_store::hasDeferredSavePending();
}

void AppFacadeRuntime::dispatchPendingEvents(std::size_t max_events)
{
    std::size_t handled = 0;
    sys::Event* event = nullptr;
    while (handled < max_events && sys::EventBus::subscribe(&event, 0))
    {
        ++handled;
        if (!event)
        {
            continue;
        }

        switch (event->type)
        {
        case sys::EventType::ChatSendResult:
        {
            auto* result = static_cast<sys::ChatSendResultEvent*>(event);
            const chat::ChatMessage* message =
                chat_service_ ? chat_service_->getMessage(result->msg_id) : nullptr;
            if (chat_service_ && message)
            {
                const bool local_outgoing = message->from == 0;
                chat_service_->handleSendResult(result->msg_id, result->success);
                if (local_outgoing)
                {
                    pending_chat_send_result_feedback_ = true;
                    pending_chat_send_result_msg_id_ = result->msg_id;
                    pending_chat_send_result_success_ = result->success;
                }
            }
            break;
        }
        default:
            break;
        }

        delete event;
        event = nullptr;
    }
}

bool AppFacadeRuntime::takeChatSendResultFeedback(chat::MessageId* out_msg_id, bool* out_success)
{
    if (!pending_chat_send_result_feedback_)
    {
        return false;
    }
    if (out_msg_id)
    {
        *out_msg_id = pending_chat_send_result_msg_id_;
    }
    if (out_success)
    {
        *out_success = pending_chat_send_result_success_;
    }
    pending_chat_send_result_feedback_ = false;
    pending_chat_send_result_msg_id_ = 0;
    pending_chat_send_result_success_ = false;
    return true;
}

#if !TRAILMATE_NRF52_BLE_DISABLED
const app::AppConfig& AppFacadeRuntime::bleConfig() const
{
    return config_;
}

bool AppFacadeRuntime::bleEnabled() const
{
    return isBleEnabled();
}

void AppFacadeRuntime::bleEffectiveUserInfo(char* out_long, std::size_t long_len,
                                            char* out_short, std::size_t short_len) const
{
    getEffectiveUserInfo(out_long, long_len, out_short, short_len);
}

chat::NodeId AppFacadeRuntime::bleSelfNodeId() const
{
    return getSelfNodeId();
}

app::IAppBleFacade& AppFacadeRuntime::bleAppFacade()
{
    return *this;
}
#endif

const chat::runtime::EffectiveSelfIdentity& AppFacadeRuntime::effectiveIdentity() const
{
    return effective_identity_;
}

} // namespace trailmate::apps::nrf52_node
