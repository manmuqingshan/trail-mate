/**
 * @file app_context.cpp
 * @brief ESP Arduino application context implementation.
 */

#include "app/app_context.h"

#include <Arduino.h>

#include "app/app_config_change_detection.h"
#include "ble/ble_manager.h"
#include "board/BoardBase.h"
#include "board/GpsBoard.h"
#include "board/LoraBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/runtime/self_identity_policy.h"
#include "platform/esp/arduino_common/app_tasks.h"
#include "platform/esp/arduino_common/memory_diag.h"
#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_group_config_runtime.h"
#include "sys/event_bus.h"
#include "ui/chat_ui_runtime_proxy.h"
#include "ui/ui_boot.h"

#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>

namespace app
{
namespace
{
constexpr uint32_t kConfigSaveTaskStackBytes = 4 * 1024;
constexpr UBaseType_t kConfigSaveTaskPriority = 1;
constexpr TickType_t kConfigSaveMutexWait = pdMS_TO_TICKS(20);
constexpr TickType_t kConfigSaveDebounceTicks = pdMS_TO_TICKS(250);
constexpr TickType_t kConfigSaveRetryDelayTicks = pdMS_TO_TICKS(1000);

void normalize_reticulum_interface_strategy(AppConfig& config)
{
    chat::MeshConfig& reticulum = config.reticulumConfig();
    switch (reticulum.reticulum_interface_policy)
    {
    case chat::ReticulumInterfacePolicy::LoRaOnly:
        reticulum.reticulum_lora_enabled = true;
        reticulum.reticulum_wifi_gateway_enabled = false;
        break;
    case chat::ReticulumInterfacePolicy::WifiGatewayOnly:
        reticulum.reticulum_lora_enabled = false;
        reticulum.reticulum_wifi_gateway_enabled = true;
        break;
    case chat::ReticulumInterfacePolicy::All:
    default:
        reticulum.reticulum_interface_policy = chat::ReticulumInterfacePolicy::All;
        reticulum.reticulum_lora_enabled = true;
        reticulum.reticulum_wifi_gateway_enabled = true;
        break;
    }
}

void sync_reticulum_group_config(AppConfig& config)
{
    if (!chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(config.mesh_protocol)))
    {
        return;
    }
    normalize_reticulum_interface_strategy(config);

    const auto status = ::platform::ui::reticulum_groups::load(
        config.reticulumConfig().reticulum_groups,
        chat::kReticulumGroupDestinationMaxCount);
    std::printf("[RTGroupConfig] sync sd=%u loaded=%u file=%u message=%s detail=%s\n",
                status.sd_present ? 1U : 0U,
                status.loaded ? 1U : 0U,
                status.file_present ? 1U : 0U,
                status.message,
                status.detail);
}
} // namespace

AppContext& AppContext::getInstance()
{
    static AppContext* instance = []() -> AppContext*
    {
        void* storage = heap_caps_malloc_prefer(sizeof(AppContext),
                                                2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (storage == nullptr)
        {
            storage = ::operator new(sizeof(AppContext));
        }
        return new (storage) AppContext();
    }();
    return *instance;
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
    return platform_bindings_.create_mesh_backend(protocol,
                                                  *lora_board_,
                                                  mesh_peer_directory_.get());
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

    deferred_storage_starter_ = chat_services.start_deferred_storage;
    deferred_storage_store_context_ =
        chat_services.deferred_storage_store_context;
    deferred_storage_peer_context_ =
        chat_services.deferred_storage_peer_context;
    chat_model_ = std::move(chat_services.model);
    chat_store_ = std::move(chat_services.store);
    mesh_peer_directory_ = std::move(chat_services.mesh_peer_directory);
    ::platform::ui::reticulum_directory::bind_mesh_peer_directory(
        mesh_peer_directory_.get());
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

    if (!mesh_peer_directory_)
    {
        Serial.printf("[AppContext] protocol peer repository unavailable\n");
        return;
    }

    auto contact_services =
        platform_bindings_.create_contact_services(*mesh_peer_directory_);
    if (!contact_services.isValid())
    {
        Serial.printf("[AppContext] contact service bundle invalid\n");
        return;
    }

    contact_service_ = std::move(contact_services.service);
    if (contact_service_)
    {
        contact_service_->setActiveProtocol(config_.mesh_protocol);
    }
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
    enqueueConfigSave(AppConfigChangeSet::none());
}

void AppContext::requestSaveConfig()
{
    enqueueConfigSave(AppConfigChangeSet::none());
}

void AppContext::saveConfig(AppConfigChangeSet changes)
{
    enqueueConfigSave(changes);
}

void AppContext::requestSaveConfig(AppConfigChangeSet changes)
{
    enqueueConfigSave(changes);
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

void AppContext::enqueueConfigSave(AppConfigChangeSet requested_changes)
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

        AppConfigChangeSet detected_changes =
            config_save_baseline_valid_
                ? detectAppConfigChanges(active_config_save_, config_)
                : AppConfigChangeSet::allPersisted();
        detected_changes.mergeIn(requested_changes);
        if (detected_changes.empty())
        {
            xSemaphoreGive(config_save_mutex_);
            Serial.println("[AppCfg][SAVE_ASYNC] noop");
            return;
        }

        pending_config_save_ = config_;
        pending_config_changes_.mergeIn(detected_changes);
        ++pending_config_save_generation_;
        const uint32_t generation = pending_config_save_generation_;
        const AppConfigChangeSet queued_changes = pending_config_changes_;
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

        Serial.printf("[AppCfg][SAVE_ASYNC] queued gen=%lu changes=0x%08lx\n",
                      static_cast<unsigned long>(generation),
                      static_cast<unsigned long>(queued_changes.bits()));
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
            active_config_save_ = pending_config_save_;
            active_config_changes_ = pending_config_changes_;
            pending_config_changes_ = AppConfigChangeSet::none();
            generation = pending_config_save_generation_;
            config_save_pending_ = false;
            config_save_busy_ = true;
            xSemaphoreGive(config_save_mutex_);

            Serial.printf("[AppCfg][SAVE_ASYNC] flush begin gen=%lu changes=0x%08lx\n",
                          static_cast<unsigned long>(generation),
                          static_cast<unsigned long>(active_config_changes_.bits()));
            const bool ok = platform_bindings_.save_app_config
                                ? platform_bindings_.save_app_config(active_config_save_,
                                                                     active_config_changes_)
                                : false;

            bool has_more = false;
            if (xSemaphoreTake(config_save_mutex_, portMAX_DELAY) == pdTRUE)
            {
                config_save_busy_ = false;
                config_save_failed_ = !ok;
                if (ok)
                {
                    completed_config_save_generation_ = generation;
                    config_save_baseline_valid_ = true;
                }
                else if (!config_save_pending_)
                {
                    pending_config_save_ = active_config_save_;
                    pending_config_changes_ = AppConfigChangeSet::allPersisted();
                    config_save_pending_ = true;
                }
                else
                {
                    pending_config_changes_ =
                        pending_config_changes_.merged(AppConfigChangeSet::allPersisted());
                }
                if (!ok)
                {
                    config_save_baseline_valid_ = false;
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
    sync_reticulum_group_config(config_);
    if (!chat::infra::isReticulumMeshProtocol(
            chat::infra::normalizeMeshProtocol(config_.mesh_protocol)))
    {
        AppTasks::setRadioReceiveSuppressed(false);
    }
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
        mesh_router_->broadcastSelfIdentity();
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
#if defined(TRAIL_MATE_ENABLE_BLE) && TRAIL_MATE_ENABLE_BLE
    return config_.ble_enabled;
#else
    return false;
#endif
}

bool AppContext::init(BoardBase& board, LoraBoard* lora_board, GpsBoard* gps_board, MotionBoard* motion_board,
                      bool use_mock_adapter, uint32_t disable_hw_init)
{
    const uint32_t init_started_ms = millis();
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
    active_config_save_ = config_;
    config_save_baseline_valid_ = true;
    const uint32_t after_config_ms = millis();
    Serial.printf("[AppContext] phase=load_config elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_config_ms - init_started_ms),
                  static_cast<unsigned long>(after_config_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_load_config");
    ::ui::boot::set_log_line("Initializing GPS services...");
    initGpsRuntime(disable_hw_init);
    const uint32_t after_gps_ms = millis();
    Serial.printf("[AppContext] phase=gps elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_gps_ms - after_config_ms),
                  static_cast<unsigned long>(after_gps_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_gps_runtime");
    initTrackRecorder();
    const uint32_t after_track_ms = millis();
    Serial.printf("[AppContext] phase=track elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_track_ms - after_gps_ms),
                  static_cast<unsigned long>(after_track_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_track_recorder");
    ::ui::boot::set_log_line("Initializing chat runtime...");
    initChatRuntime(use_mock_adapter);
    const uint32_t after_chat_ms = millis();
    Serial.printf("[AppContext] phase=chat elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_chat_ms - after_track_ms),
                  static_cast<unsigned long>(after_chat_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_chat_runtime");
    ::ui::boot::set_log_line("Initializing team services...");
    initTeamServices();
    const uint32_t after_team_ms = millis();
    Serial.printf("[AppContext] phase=team elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_team_ms - after_chat_ms),
                  static_cast<unsigned long>(after_team_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_team_services");
    ::ui::boot::set_log_line("Initializing contacts...");
    initContactServices();
    const uint32_t after_contacts_ms = millis();
    Serial.printf("[AppContext] phase=contacts elapsed_ms=%lu total_ms=%lu\n",
                  static_cast<unsigned long>(after_contacts_ms - after_team_ms),
                  static_cast<unsigned long>(after_contacts_ms - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_contact_services");
    if (platform_bindings_.finalize_startup)
    {
        ::ui::boot::set_log_line("Finalizing startup...");
        platform_bindings_.finalize_startup(*this);
    }
    Serial.printf("[AppContext] phase=finalize total_ms=%lu\n",
                  static_cast<unsigned long>(millis() - init_started_ms));
    platform::esp::arduino_common::memory_diag::logHeapSnapshot("appctx.after_finalize_startup");

    return true;
}

void AppContext::startDeferredStorage()
{
    if (deferred_storage_started_)
    {
        return;
    }
    deferred_storage_started_ = true;
    if (deferred_storage_starter_)
    {
        deferred_storage_starter_(deferred_storage_store_context_,
                                  deferred_storage_peer_context_,
                                  config_.mesh_protocol);
    }
}

bool AppContext::switchMeshProtocol(chat::MeshProtocol protocol, bool persist)
{
    if (!mesh_router_ || !lora_board_)
    {
        return false;
    }

    const chat::MeshProtocol normalized = chat::infra::normalizeMeshProtocol(protocol);
    if (!chat::infra::isValidMeshProtocol(normalized))
    {
        return false;
    }

    std::unique_ptr<chat::IMeshAdapter> backend = createMeshBackend(normalized);
    if (!backend)
    {
        return false;
    }
    if (!AppTasks::pauseRadioTasks())
    {
        return false;
    }

    const chat::MeshProtocol previous_protocol = config_.mesh_protocol;
    config_.mesh_protocol = normalized;
    sync_reticulum_group_config(config_);
    if (!chat::infra::isReticulumMeshProtocol(normalized))
    {
        AppTasks::setRadioReceiveSuppressed(false);
    }

    backend->applyConfig(config_.activeMeshConfig());

    char long_name[sizeof(config_.node_name)];
    char short_name[sizeof(config_.short_name)];
    getEffectiveUserInfo(long_name, sizeof(long_name),
                         short_name, sizeof(short_name));
    backend->setUserInfo(long_name, short_name);
    backend->setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    backend->setPrivacyConfig(config_.privacy_encrypt_mode);

    if (!mesh_router_->installBackend(normalized, std::move(backend)))
    {
        config_.mesh_protocol = previous_protocol;
        sync_reticulum_group_config(config_);
        mesh_router_->applyConfig(config_.activeMeshConfig());
        AppTasks::resumeRadioTasks();
        return false;
    }

    if (chat_service_)
    {
        chat_service_->setActiveProtocol(normalized);
    }
    if (contact_service_)
    {
        contact_service_->setActiveProtocol(normalized);
    }

    if (persist)
    {
        saveConfig(AppConfigChangeSet::mesh());
    }
    AppTasks::resumeRadioTasks();
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
    if (mesh_peer_directory_ &&
        !::platform::ui::reticulum_call::resource_preempt_active())
    {
        (void)mesh_peer_directory_->flush();
    }
    if (deferred_storage_started_ &&
        platform_bindings_.deferred_storage_ready &&
        ::platform::esp::arduino_common::storage::consume_hydration_ready())
    {
        platform_bindings_.deferred_storage_ready(*this);
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
#if defined(TRAIL_MATE_ENABLE_BLE) && TRAIL_MATE_ENABLE_BLE
    config_.ble_enabled = enabled;
    if (ble_manager_)
    {
        ble_manager_->setEnabled(enabled);
    }
#else
    (void)enabled;
    config_.ble_enabled = false;
#endif
    saveConfig();
}

chat::NodeId AppContext::getSelfNodeId() const
{
    return platform_bindings_.get_self_node_id ? platform_bindings_.get_self_node_id() : 0;
}

void AppContext::clearNodeDb()
{
    if (mesh_peer_directory_)
    {
        (void)mesh_peer_directory_->clearProtocol(config_.mesh_protocol);
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
