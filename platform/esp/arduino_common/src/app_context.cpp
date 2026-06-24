/**
 * @file app_context.cpp
 * @brief ESP Arduino application context implementation.
 */

#include "app/app_context.h"

#include <Arduino.h>

#include "ble/ble_manager.h"
#include "board/BoardBase.h"
#include "board/GpsBoard.h"
#include "board/LoraBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"
#include "platform/esp/arduino_common/memory_diag.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime_proxy.h"
#include "ui/ui_boot.h"

#include <cstdio>
#include <cstring>

namespace app
{
namespace
{
constexpr uint32_t kConfigSaveTaskStackBytes = 8 * 1024;
constexpr UBaseType_t kConfigSaveTaskPriority = 1;
constexpr TickType_t kConfigSaveMutexWait = pdMS_TO_TICKS(20);
constexpr TickType_t kConfigSaveDebounceTicks = pdMS_TO_TICKS(250);
constexpr TickType_t kConfigSaveRetryDelayTicks = pdMS_TO_TICKS(1000);
} // namespace

AppContext& AppContext::getInstance()
{
    static AppContext instance;
    return instance;
}

AppContext::AppContext()
    : chat_ui_runtime_proxy_(new chat::ui::GlobalChatUiRuntime())
{
}

AppContext::~AppContext() = default;

void AppContext::configurePlatformBindings(const AppContextPlatformBindings& bindings)
{
    platform_bindings_ = bindings;
}

void AppContext::assignBoards(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board,
                              MotionBoard* motion_board)
{
    board_ = &board;
    lora_board_ = lora_board;
    gps_board_ = gps_board;
    motion_board_ = motion_board;
    const uint8_t tone_volume = platform_bindings_.load_message_tone_volume ? platform_bindings_.load_message_tone_volume() : 45;
    board_->setMessageToneVolume(tone_volume);
}

void AppContext::initGpsRuntime(uint32_t disable_hw_init)
{
    if (platform_bindings_.init_gps_runtime)
    {
        platform_bindings_.init_gps_runtime(gps_board_, motion_board_, disable_hw_init, config_);
    }
}

void AppContext::initTrackRecorder()
{
    if (platform_bindings_.init_track_recorder)
    {
        platform_bindings_.init_track_recorder(config_);
    }
}

std::unique_ptr<chat::IMeshAdapter> AppContext::createMeshBackend(chat::MeshProtocol protocol) const
{
    if (!lora_board_ || !platform_bindings_.create_mesh_backend)
    {
        return nullptr;
    }
    return platform_bindings_.create_mesh_backend(protocol, *lora_board_);
}

void AppContext::initChatRuntime(bool use_mock_adapter)
{
    if (!platform_bindings_.create_chat_services)
    {
        Serial.printf("[AppContext] chat platform bindings missing\n");
        return;
    }

    auto chat_services = platform_bindings_.create_chat_services(config_, lora_board_, use_mock_adapter);
    if (!chat_services.isValid())
    {
        Serial.printf("[AppContext] chat service bundle invalid\n");
        return;
    }

    chat_model_ = std::move(chat_services.model);
    chat_store_ = std::move(chat_services.store);
    mesh_router_ = std::move(chat_services.mesh_runtime);
    chat_service_ = std::move(chat_services.service);
    chat_event_bus_bridge_ = std::move(chat_services.incoming_message_observer);

    applyUserInfo();
    applyNetworkLimits();
    applyPrivacyConfig();
    applyChatDefaults();
}

void AppContext::initTeamServices()
{
    if (!mesh_router_)
    {
        Serial.printf("[Team] mesh router unavailable, skip team services\n");
        return;
    }

    if (!platform_bindings_.create_team_services)
    {
        if (platform_bindings_.set_team_mode_active)
        {
            platform_bindings_.set_team_mode_active(false);
        }
        return;
    }

    auto team_services = platform_bindings_.create_team_services(*mesh_router_);
    if (!team_services.isValid())
    {
        Serial.printf("[Team] service bundle invalid\n");
        return;
    }

    team_crypto_ = std::move(team_services.crypto);
    team_event_sink_ = std::move(team_services.event_sink);
    team_app_data_bridge_ = std::move(team_services.app_data_observer);
    team_pairing_event_sink_ = std::move(team_services.pairing_event_sink);
    team_runtime_ = std::move(team_services.runtime);
    team_track_source_ = std::move(team_services.track_source);
    team_pairing_transport_ = std::move(team_services.pairing_transport);
    team_pairing_service_ = std::move(team_services.pairing_service);
    team_service_ = std::move(team_services.service);
    team_controller_ = std::move(team_services.controller);
    team_track_sampler_ = std::move(team_services.track_sampler);
}

void AppContext::initContactServices()
{
    if (!platform_bindings_.create_contact_services)
    {
        Serial.printf("[AppContext] contact platform bindings missing\n");
        return;
    }

    auto contact_services = platform_bindings_.create_contact_services();
    if (!contact_services.isValid())
    {
        Serial.printf("[AppContext] contact service bundle invalid\n");
        return;
    }

    node_store_ = std::move(contact_services.node_store);
    contact_store_ = std::move(contact_services.contact_store);
    contact_service_ = std::move(contact_services.service);
}

chat::IMeshAdapter* AppContext::getMeshAdapter()
{
    return mesh_router_.get();
}

const chat::IMeshAdapter* AppContext::getMeshAdapter() const
{
    return mesh_router_.get();
}

chat::ui::IChatUiRuntime* AppContext::getChatUiRuntime()
{
    return chat_ui_runtime_proxy_.get();
}

void AppContext::setChatUiRuntime(chat::ui::IChatUiRuntime* runtime)
{
    if (chat_ui_runtime_proxy_)
    {
        chat_ui_runtime_proxy_->setActiveRuntime(runtime);
    }
}

void AppContext::saveConfig()
{
    enqueueConfigSave();
}

void AppContext::requestSaveConfig()
{
    enqueueConfigSave();
}

void AppContext::ensureConfigSaveWorker()
{
    if (config_save_mutex_ == nullptr)
    {
        config_save_mutex_ = xSemaphoreCreateMutex();
    }
    if (config_save_queue_ == nullptr)
    {
        config_save_queue_ = xQueueCreate(1, sizeof(uint8_t));
    }
    if (config_save_task_ == nullptr &&
        config_save_mutex_ != nullptr &&
        config_save_queue_ != nullptr)
    {
        BaseType_t ok = xTaskCreate(configSaveTaskEntry,
                                    "app_cfg_io",
                                    kConfigSaveTaskStackBytes,
                                    this,
                                    kConfigSaveTaskPriority,
                                    &config_save_task_);
        if (ok != pdPASS)
        {
            Serial.printf("[AppCfg][SAVE_ASYNC] task_create_failed rc=%ld\n",
                          static_cast<long>(ok));
            config_save_task_ = nullptr;
        }
    }
}

void AppContext::enqueueConfigSave()
{
    if (platform_bindings_.save_app_config)
    {
        ensureConfigSaveWorker();
        if (config_save_mutex_ == nullptr ||
            config_save_queue_ == nullptr ||
            config_save_task_ == nullptr)
        {
            Serial.println("[AppCfg][SAVE_ASYNC] unavailable");
            return;
        }

        if (xSemaphoreTake(config_save_mutex_, kConfigSaveMutexWait) != pdTRUE)
        {
            Serial.println("[AppCfg][SAVE_ASYNC] enqueue_busy");
            return;
        }

        pending_config_save_ = config_;
        ++pending_config_save_generation_;
        const uint32_t generation = pending_config_save_generation_;
        config_save_pending_ = true;
        config_save_failed_ = false;
        xSemaphoreGive(config_save_mutex_);

        const uint8_t signal = 1;
        if (xQueueOverwrite(config_save_queue_, &signal) != pdTRUE)
        {
            Serial.printf("[AppCfg][SAVE_ASYNC] signal_failed gen=%lu\n",
                          static_cast<unsigned long>(generation));
            return;
        }

        Serial.printf("[AppCfg][SAVE_ASYNC] queued gen=%lu\n",
                      static_cast<unsigned long>(generation));
    }
}

void AppContext::configSaveLoop()
{
    uint8_t signal = 0;
    for (;;)
    {
        if (xQueueReceive(config_save_queue_, &signal, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        vTaskDelay(kConfigSaveDebounceTicks);
        for (;;)
        {
            AppConfig snapshot{};
            uint32_t generation = 0;

            if (xSemaphoreTake(config_save_mutex_, portMAX_DELAY) != pdTRUE)
            {
                break;
            }
            if (!config_save_pending_)
            {
                config_save_busy_ = false;
                xSemaphoreGive(config_save_mutex_);
                break;
            }
            snapshot = pending_config_save_;
            generation = pending_config_save_generation_;
            config_save_pending_ = false;
            config_save_busy_ = true;
            xSemaphoreGive(config_save_mutex_);

            Serial.printf("[AppCfg][SAVE_ASYNC] flush begin gen=%lu\n",
                          static_cast<unsigned long>(generation));
            const bool ok = platform_bindings_.save_app_config
                                ? platform_bindings_.save_app_config(snapshot)
                                : false;

            bool has_more = false;
            if (xSemaphoreTake(config_save_mutex_, portMAX_DELAY) == pdTRUE)
            {
                config_save_busy_ = false;
                config_save_failed_ = !ok;
                if (ok)
                {
                    completed_config_save_generation_ = generation;
                }
                else if (!config_save_pending_)
                {
                    pending_config_save_ = snapshot;
                    config_save_pending_ = true;
                }
                has_more = config_save_pending_;
                xSemaphoreGive(config_save_mutex_);
            }

            Serial.printf("[AppCfg][SAVE_ASYNC] flush done gen=%lu ok=%u more=%u\n",
                          static_cast<unsigned long>(generation),
                          ok ? 1U : 0U,
                          has_more ? 1U : 0U);
            if (!ok)
            {
                vTaskDelay(kConfigSaveRetryDelayTicks);
            }
            if (!has_more)
            {
                break;
            }
            vTaskDelay(kConfigSaveDebounceTicks);
        }
    }
}

void AppContext::configSaveTaskEntry(void* context)
{
    auto* self = static_cast<AppContext*>(context);
    if (self)
    {
        self->configSaveLoop();
    }
    vTaskDelete(nullptr);
}

void AppContext::applyMeshConfig()
{
    if (mesh_router_)
    {
        if (mesh_router_->backendProtocol() != config_.mesh_protocol)
        {
            (void)switchMeshProtocol(config_.mesh_protocol, false);
        }
        else
        {
            mesh_router_->applyConfig(config_.activeMeshConfig());
        }
    }
    if (chat_service_)
    {
        chat_service_->setActiveProtocol(config_.mesh_protocol);
    }
}

void AppContext::applyUserInfo()
{
    if (mesh_router_)
    {
        char long_name[sizeof(config_.node_name)];
        char short_name[sizeof(config_.short_name)];
        getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
        mesh_router_->setUserInfo(long_name, short_name);
    }
}

void AppContext::broadcastNodeInfo()
{
    if (mesh_router_)
    {
        mesh_router_->requestNodeInfo(0xFFFFFFFF, false);
    }
}

void AppContext::applyNetworkLimits()
{
    if (mesh_router_)
    {
        mesh_router_->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }
}

void AppContext::applyPrivacyConfig()
{
    if (mesh_router_)
    {
        mesh_router_->setPrivacyConfig(config_.privacy_encrypt_mode);
    }
}

bool AppContext::isBleEnabled() const
{
    return config_.ble_enabled;
}

bool AppContext::init(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board, MotionBoard* motion_board,
                      bool use_mock_adapter, uint32_t disable_hw_init)
{
    if (!platform_bindings_.isValid())
    {
        Serial.printf("[AppContext] ERROR: platform bindings not configured\n");
        return false;
    }

    assignBoards(board, lora_board, gps_board, motion_board);
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_assign_boards");

    if (!sys::EventBus::init())
    {
        return false;
    }
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_event_bus");

    if (platform_bindings_.load_app_config)
    {
        ::ui::boot::set_log_line("Loading app config...");
        platform_bindings_.load_app_config(config_);
    }
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_load_config");
    ::ui::boot::set_log_line("Initializing GPS services...");
    initGpsRuntime(disable_hw_init);
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_gps_runtime");
    initTrackRecorder();
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_track_recorder");
    ::ui::boot::set_log_line("Initializing chat runtime...");
    initChatRuntime(use_mock_adapter);
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_chat_runtime");
    ::ui::boot::set_log_line("Initializing team services...");
    initTeamServices();
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_team_services");
    ::ui::boot::set_log_line("Initializing contacts...");
    initContactServices();
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_contact_services");
    if (platform_bindings_.finalize_startup)
    {
        ::ui::boot::set_log_line("Finalizing startup...");
        platform_bindings_.finalize_startup(*this);
    }
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_finalize_startup");

    return true;
}

bool AppContext::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!mesh_router_ || !lora_board_)
    {
        return false;
    }

    if (!chat::infra::isValidMeshProtocol(protocol))
    {
        return false;
    }

    std::unique_ptr<chat::IMeshAdapter> backend = createMeshBackend(protocol);
    if (!backend)
    {
        return false;
    }

    const chat::MeshProtocol previous_protocol = config_.mesh_protocol;
    config_.mesh_protocol = protocol;

    backend->applyConfig(config_.activeMeshConfig());

    char long_name[sizeof(config_.node_name)];
    char short_name[sizeof(config_.short_name)];
    getEffectiveUserInfo(long_name, sizeof(long_name),
                         short_name, sizeof(short_name));
    backend->setUserInfo(long_name, short_name);
    backend->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    backend->setPrivacyConfig(config_.privacy_encrypt_mode);

    if (!mesh_router_->installBackend(protocol, std::move(backend)))
    {
        config_.mesh_protocol = previous_protocol;
        return false;
    }

    if (chat_service_)
    {
        chat_service_->setActiveProtocol(protocol);
    }

    if (persist)
    {
        saveConfig();
    }
    return true;
}

void AppContext::applyPositionConfig()
{
    if (platform_bindings_.apply_position_config)
    {
        platform_bindings_.apply_position_config(config_);
    }
}

void AppContext::getEffectiveUserInfo(char* out_long, size_t long_len,
                                      char* out_short, size_t short_len) const
{
    if (!out_long || long_len == 0 || !out_short || short_len == 0)
    {
        return;
    }

    chat::runtime::SelfIdentityInput input{};
    input.node_id = getSelfNodeId();
    input.configured_long_name = config_.node_name;
    input.configured_short_name = config_.short_name;
    input.fallback_long_prefix = "lilygo";
    input.fallback_ble_prefix = "TrailMate";
    input.allow_short_hex_fallback = true;

    chat::runtime::EffectiveSelfIdentity identity{};
    (void)chat::runtime::resolveEffectiveSelfIdentity(input, &identity);

    strncpy(out_long, identity.long_name, long_len - 1);
    out_long[long_len - 1] = '\0';
    strncpy(out_short, identity.short_name, short_len - 1);
    out_short[short_len - 1] = '\0';
}

void AppContext::updateCoreServices()
{
    if (event_runtime_hooks_.update_core_services)
    {
        event_runtime_hooks_.update_core_services(*this);
    }
}

void AppContext::tickEventRuntime()
{
    if (event_runtime_hooks_.tick)
    {
        event_runtime_hooks_.tick(*this);
    }
}

void AppContext::dispatchPendingEvents(size_t max_events)
{
    sys::Event* event = nullptr;
    for (size_t processed = 0;
         processed < max_events && sys::EventBus::subscribe(&event, 0);)
    {
        if (!event)
        {
            continue;
        }
        ++processed;

        if (event_runtime_hooks_.dispatch_event && event_runtime_hooks_.dispatch_event(*this, event))
        {
            continue;
        }

        if (event_runtime_hooks_.handle_event && event_runtime_hooks_.handle_event(*this, event))
        {
            continue;
        }

        delete event;
    }
}

void AppContext::attachEventRuntimeHooks(const AppEventRuntimeHooks& hooks)
{
    event_runtime_hooks_ = hooks;
}

void AppContext::attachBleManager(std::unique_ptr<ble::BleManager> ble_manager)
{
    ble_manager_ = std::move(ble_manager);
}

void AppContext::setBleEnabled(bool enabled)
{
    config_.ble_enabled = enabled;
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
    saveConfig();
}

chat::NodeId AppContext::getSelfNodeId() const
{
    return platform_bindings_.get_self_node_id ? platform_bindings_.get_self_node_id() : 0;
}

void AppContext::clearNodeDb()
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

void AppContext::clearMessageDb()
{
    if (chat_service_)
    {
        chat_service_->clearAllMessages();
    }
    else if (chat_model_)
    {
        chat_model_->clearAll();
        if (chat_store_)
        {
            chat_store_->clearAll();
        }
    }
}

} // namespace app
