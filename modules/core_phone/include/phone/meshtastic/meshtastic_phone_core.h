#pragma once

#include "chat/domain/chat_types.h"
#include "chat/infra/meshtastic/mt_node_payload.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/connection_status.pb.h"
#include "meshtastic/device_ui.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/module_config.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "phone/common/phone_app_facade.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace phone::meshtastic
{

enum class MeshtasticBleFrameKind : uint8_t
{
    None,
    Config,
    Liveness,
    QueueStatus,
    AdminResponse,
    NodeInfo,
    Packet,
    MqttProxy,
};

enum class MeshtasticBleFramePriority : uint8_t
{
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
};

struct MeshtasticBleFrame
{
    uint16_t len = 0;
    MeshtasticBleFrameKind kind = MeshtasticBleFrameKind::None;
    MeshtasticBleFramePriority priority = MeshtasticBleFramePriority::P3;
    uint32_t from_num = 0;
    uint8_t buf[meshtastic_FromRadio_size] = {};
};

struct MeshtasticGpsFix
{
    bool valid = false;
    double lat = 0.0;
    double lng = 0.0;
    bool has_alt = false;
    double alt_m = 0.0;
    bool has_speed = false;
    double speed_mps = 0.0;
    bool has_course = false;
    double course_deg = 0.0;
    uint8_t satellites = 0;
};

class MeshtasticPhoneTransport
{
  public:
    virtual ~MeshtasticPhoneTransport() = default;
    virtual bool isBleConnected() const = 0;
    virtual void notifyFromNum(uint32_t from_num) = 0;
};

class MeshtasticPhoneBluetoothConfigHooks
{
  public:
    virtual ~MeshtasticPhoneBluetoothConfigHooks() = default;
    virtual bool loadBluetoothConfig(meshtastic_Config_BluetoothConfig* out) const
    {
        (void)out;
        return false;
    }
    virtual void saveBluetoothConfig(const meshtastic_Config_BluetoothConfig& config)
    {
        (void)config;
    }
};

class MeshtasticPhoneModuleConfigHooks
{
  public:
    virtual ~MeshtasticPhoneModuleConfigHooks() = default;
    virtual bool loadModuleConfig(meshtastic_LocalModuleConfig* out) const
    {
        (void)out;
        return false;
    }
    virtual void saveModuleConfig(const meshtastic_LocalModuleConfig& config)
    {
        (void)config;
    }
};

class MeshtasticPhoneConfigLifecycleHooks
{
  public:
    virtual ~MeshtasticPhoneConfigLifecycleHooks() = default;
    virtual void onConfigStart() {}
    virtual void onConfigComplete() {}
};

class MeshtasticPhoneStatusHooks
{
  public:
    virtual ~MeshtasticPhoneStatusHooks() = default;
    virtual bool loadDeviceConnectionStatus(meshtastic_DeviceConnectionStatus* out) const
    {
        (void)out;
        return false;
    }
};

class MeshtasticPhoneMqttHooks
{
  public:
    virtual ~MeshtasticPhoneMqttHooks() = default;
    virtual bool handleMqttProxyToRadio(const meshtastic_MqttClientProxyMessage& msg)
    {
        (void)msg;
        return false;
    }
    virtual bool pollMqttProxyToPhone(meshtastic_MqttClientProxyMessage* out)
    {
        (void)out;
        return false;
    }
    virtual bool hasMqttProxyToPhone() const
    {
        return false;
    }
};

class MeshtasticPhoneDeviceRuntimeHooks
{
  public:
    virtual ~MeshtasticPhoneDeviceRuntimeHooks() = default;
    virtual bool loadTimezoneTzdef(char* out, size_t out_len) const
    {
        (void)out;
        (void)out_len;
        return false;
    }
    virtual void saveTimezoneTzdef(const char* tzdef)
    {
        (void)tzdef;
    }
    virtual int getTimezoneOffsetMinutes() const
    {
        return 0;
    }
    virtual void setTimezoneOffsetMinutes(int offset_min)
    {
        (void)offset_min;
    }
    virtual int getTimezoneProfileId() const
    {
        return 0;
    }
    virtual void setTimezoneProfileId(int profile_id)
    {
        (void)profile_id;
    }
    virtual bool getGpsFix(MeshtasticGpsFix* out) const
    {
        if (!out)
        {
            return false;
        }
        *out = {};
        return false;
    }
};

class MeshtasticPhoneCore
{
  public:
    enum class PhoneApiPhase : uint8_t
    {
        SendNothing,
        ConfigFlow,
        SendPackets,
    };

    MeshtasticPhoneCore(IPhoneAppFacade& app, MeshtasticPhoneTransport& transport,
                        MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks = nullptr,
                        MeshtasticPhoneModuleConfigHooks* module_config_hooks = nullptr,
                        MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks = nullptr,
                        MeshtasticPhoneStatusHooks* status_hooks = nullptr,
                        MeshtasticPhoneMqttHooks* mqtt_hooks = nullptr,
                        MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks = nullptr);

    void reset();
    void pumpIncomingAppData();
    void onIncomingText(const chat::MeshIncomingText& msg);
    void onIncomingData(const chat::MeshIncomingData& msg);
    bool handleToRadio(const uint8_t* data, size_t len);
    bool popToPhone(MeshtasticBleFrame* out);
    bool isSendingPackets() const;
    bool isConfigFlowActive() const;
    void debugLogMemoryLayout(const char* stage) const;
    PhoneApiPhase phoneApiPhase() const;

  private:
    enum class OutputPriority : uint8_t
    {
        P0 = 0,
        P1 = 1,
        P2 = 2,
        P3 = 3,
    };

    enum class OutputEventKind : uint8_t
    {
        None,
        QueueStatus,
        NodeInfo,
        Packet,
    };

    struct OutputEvent
    {
        OutputEventKind kind = OutputEventKind::None;
        OutputPriority priority = OutputPriority::P3;
        uint32_t notify_id = 0;
        uint32_t coalesce_key = 0;

        union Payload
        {
            meshtastic_QueueStatus queue_status;
            meshtastic_NodeInfo node_info;
            meshtastic_MeshPacket packet;

            Payload() {}
        } payload;

        OutputEvent()
        {
            clear();
        }

        void clear()
        {
            kind = OutputEventKind::None;
            priority = OutputPriority::P3;
            notify_id = 0;
            coalesce_key = 0;
            std::memset(&payload, 0, sizeof(payload));
        }
    };

    struct OutputPushReport
    {
        enum class Result : uint8_t
        {
            Enqueued,
            Replaced,
            DroppedExisting,
            DroppedNew,
        };

        Result result = Result::Enqueued;
        OutputPriority dropped_priority = OutputPriority::P3;
        OutputEventKind dropped_kind = OutputEventKind::None;
    };

    template <size_t Capacity>
    class OutputQueue
    {
      public:
        void clear()
        {
            for (auto& item : items_)
            {
                item.clear();
            }
            count_ = 0;
        }

        bool empty() const
        {
            return count_ == 0;
        }

        size_t size() const
        {
            return count_;
        }

        constexpr size_t capacity() const
        {
            return Capacity;
        }

        bool push(const OutputEvent& event, OutputPushReport* report = nullptr)
        {
            OutputPushReport local_report{};
            if (event.kind == OutputEventKind::None)
            {
                local_report.result = OutputPushReport::Result::DroppedNew;
                if (report)
                {
                    *report = local_report;
                }
                return false;
            }

            const size_t coalesce_index = findCoalesce(event);
            if (coalesce_index < count_)
            {
                items_[coalesce_index] = event;
                local_report.result = OutputPushReport::Result::Replaced;
                if (report)
                {
                    *report = local_report;
                }
                return true;
            }

            if (count_ < Capacity)
            {
                items_[count_++] = event;
                local_report.result = OutputPushReport::Result::Enqueued;
                if (report)
                {
                    *report = local_report;
                }
                return true;
            }

            const size_t victim_index = findDropVictim(event.priority);
            if (victim_index < count_)
            {
                local_report.result = OutputPushReport::Result::DroppedExisting;
                local_report.dropped_priority = items_[victim_index].priority;
                local_report.dropped_kind = items_[victim_index].kind;
                removeAt(victim_index);
                items_[count_++] = event;
                if (report)
                {
                    *report = local_report;
                }
                return true;
            }

            local_report.result = OutputPushReport::Result::DroppedNew;
            local_report.dropped_priority = event.priority;
            local_report.dropped_kind = event.kind;
            if (report)
            {
                *report = local_report;
            }
            return false;
        }

        bool pop(OutputEvent* out)
        {
            if (!out || empty())
            {
                return false;
            }

            const size_t index = selectPopIndex();
            *out = items_[index];
            removeAt(index);
            return true;
        }

        const OutputEvent* peek() const
        {
            if (empty())
            {
                return nullptr;
            }

            return &items_[selectPopIndex()];
        }

      private:
        static uint8_t priorityRank(OutputPriority priority)
        {
            return static_cast<uint8_t>(priority);
        }

        static uint8_t kindPopRank(OutputEventKind kind)
        {
            switch (kind)
            {
            case OutputEventKind::QueueStatus:
                return 0;
            case OutputEventKind::NodeInfo:
                return 1;
            case OutputEventKind::Packet:
                return 2;
            default:
                return 3;
            }
        }

        size_t findCoalesce(const OutputEvent& event) const
        {
            if (event.coalesce_key == 0)
            {
                return Capacity;
            }
            for (size_t index = 0; index < count_; ++index)
            {
                const auto& item = items_[index];
                if (item.kind == event.kind && item.coalesce_key == event.coalesce_key)
                {
                    return index;
                }
            }
            return Capacity;
        }

        size_t findDropVictim(OutputPriority incoming) const
        {
            size_t victim = Capacity;
            uint8_t victim_priority = priorityRank(incoming);
            for (size_t index = 0; index < count_; ++index)
            {
                const uint8_t candidate_priority = priorityRank(items_[index].priority);
                if (candidate_priority < priorityRank(incoming))
                {
                    continue;
                }
                if (victim == Capacity || candidate_priority > victim_priority)
                {
                    victim = index;
                    victim_priority = candidate_priority;
                }
            }
            return victim;
        }

        size_t selectPopIndex() const
        {
            size_t selected = 0;
            for (size_t index = 1; index < count_; ++index)
            {
                const auto& candidate = items_[index];
                const auto& current = items_[selected];
                if (priorityRank(candidate.priority) < priorityRank(current.priority))
                {
                    selected = index;
                    continue;
                }
                if (candidate.priority == current.priority &&
                    kindPopRank(candidate.kind) < kindPopRank(current.kind))
                {
                    selected = index;
                }
            }
            return selected;
        }

        void removeAt(size_t index)
        {
            if (index >= count_)
            {
                return;
            }
            for (size_t cursor = index + 1U; cursor < count_; ++cursor)
            {
                items_[cursor - 1U] = items_[cursor];
            }
            --count_;
            items_[count_].clear();
        }

        std::array<OutputEvent, Capacity> items_{};
        size_t count_ = 0;
    };

    static constexpr size_t kPhoneOutputQueueDepth = 6;
    static constexpr size_t kNodeProjectionCacheDepth = 8;
    static constexpr uint8_t kMqttProxyMaxP2Deferrals = 4;

    struct NodeProjectionCacheEntry
    {
        uint32_t node_id = 0;
        uint32_t signature = 0;
    };

    bool handleToRadioPacket(meshtastic_MeshPacket& packet);
    bool handleAdmin(meshtastic_MeshPacket& packet);
    bool handleLocalSelfPacket(meshtastic_MeshPacket& packet);
    bool encodeFromRadio(meshtastic_FromRadio& from, uint32_t from_num, MeshtasticBleFrame* out,
                         MeshtasticBleFrameKind kind, MeshtasticBleFramePriority priority);
    bool popConfigSnapshotFrame(MeshtasticBleFrame* out);
    void enqueueKnownNodeInfoProjection(chat::NodeId node_id);
    bool enqueueMetadataNodeInfoProjection(const chat::MeshIncomingData& msg);
    bool shouldProjectNodeInfo(chat::NodeId node_id, uint32_t signature);
    bool enqueueOutputEvent(const OutputEvent& event, const char* reason);
    OutputPriority priorityForPacket(const meshtastic_MeshPacket& packet) const;
    uint32_t coalesceKeyForPacket(const meshtastic_MeshPacket& packet, OutputPriority priority) const;
    static MeshtasticBleFramePriority framePriorityForOutput(OutputPriority priority);
    static MeshtasticBleFrameKind frameKindForPacket(const meshtastic_MeshPacket& packet);
    void enqueueQueueStatus(uint32_t packet_id, bool ok);
    void enqueueConfigSnapshot(uint32_t config_nonce);
    void setPhoneApiPhase(PhoneApiPhase phase, const char* reason);
    bool canHandleMqttProxy() const;
    bool canEmitSteadyStateFrame() const;
    bool hasDeferredSideEffects() const;
    bool shouldEmitMqttProxyBeforeOutput() const;
    void recordMqttProxyDeferral(const OutputEvent& event);
    bool popMqttProxyFrame(MeshtasticBleFrame* out);
    void notifyFromNum(uint32_t from_num);
    void fillMyInfo(meshtastic_MyNodeInfo* out) const;
    void fillSelfNodeInfo(meshtastic_NodeInfo* out) const;
    void fillNodeInfoFromEntry(const PhoneNodeView& entry, meshtastic_NodeInfo* out) const;
    void fillNodeInfoFromDecodedPayload(const chat::meshtastic::DecodedNodePayload& node,
                                        meshtastic_NodeInfo* out) const;
    void fillMetadata(meshtastic_DeviceMetadata* out) const;
    void fillDeviceUi(meshtastic_DeviceUIConfig* out) const;
    void fillChannel(uint8_t idx, meshtastic_Channel* out) const;
    void fillConfig(meshtastic_AdminMessage_ConfigType type, meshtastic_Config* out) const;
    void fillModuleConfig(meshtastic_AdminMessage_ModuleConfigType type, meshtastic_ModuleConfig* out) const;
    bool canEmitSendNothingLivenessFrame() const;
    meshtastic_MyNodeInfo buildMyInfo() const;
    meshtastic_NodeInfo buildSelfNodeInfo() const;
    meshtastic_NodeInfo buildNodeInfoFromEntry(const PhoneNodeView& entry) const;
    meshtastic_DeviceMetadata buildMetadata() const;
    meshtastic_DeviceMetrics buildDeviceMetrics() const;
    meshtastic_LocalStats buildLocalStats() const;
    meshtastic_DeviceUIConfig buildDeviceUi() const;
    meshtastic_Channel buildChannel(uint8_t idx) const;
    meshtastic_Config buildConfig(meshtastic_AdminMessage_ConfigType type) const;
    meshtastic_ModuleConfig buildModuleConfig(meshtastic_AdminMessage_ModuleConfigType type) const;
    void fillPacketFromText(const chat::MeshIncomingText& msg, meshtastic_MeshPacket* out) const;
    void fillPacketFromData(const chat::MeshIncomingData& msg, meshtastic_MeshPacket* out) const;

    IPhoneAppFacade& app_;
    MeshtasticPhoneTransport& transport_;
    MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks_ = nullptr;
    MeshtasticPhoneModuleConfigHooks* module_config_hooks_ = nullptr;
    MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks_ = nullptr;
    MeshtasticPhoneStatusHooks* status_hooks_ = nullptr;
    MeshtasticPhoneMqttHooks* mqtt_hooks_ = nullptr;
    MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks_ = nullptr;
    uint32_t config_nonce_ = 0;
    size_t config_node_index_ = 0;
    uint8_t config_channel_index_ = 0;
    uint8_t config_type_index_ = 0;
    uint8_t config_module_type_index_ = 0;
    uint32_t config_request_seq_ = 0;
    uint32_t from_radio_id_ = 0;
    PhoneApiPhase phone_api_phase_ = PhoneApiPhase::SendNothing;
    bool deferred_config_save_pending_ = false;
    bool deferred_module_config_save_pending_ = false;
    bool deferred_bluetooth_config_apply_pending_ = false;
    bool admin_edit_transaction_open_ = false;
    bool admin_edit_transaction_dirty_ = false;
    bool admin_edit_transaction_module_dirty_ = false;
    bool admin_edit_transaction_bluetooth_dirty_ = false;
    bool admin_edit_transaction_restart_pending_ = false;
    bool restart_pending_ = false;
    uint8_t mqtt_proxy_deferral_count_ = 0;
    OutputQueue<kPhoneOutputQueueDepth> output_queue_;
    OutputEvent output_event_scratch_{};
    std::array<NodeProjectionCacheEntry, kNodeProjectionCacheDepth> node_projection_cache_{};
    size_t node_projection_cache_next_ = 0;
    meshtastic_Config_BluetoothConfig bluetooth_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    char admin_canned_messages_[160] = {};
    char admin_ringtone_[96] = {};
    meshtastic_ToRadio to_radio_scratch_ = meshtastic_ToRadio_init_zero;
    meshtastic_AdminMessage admin_req_scratch_ = meshtastic_AdminMessage_init_zero;
    meshtastic_AdminMessage admin_resp_scratch_ = meshtastic_AdminMessage_init_zero;
    meshtastic_MeshPacket reply_packet_scratch_ = meshtastic_MeshPacket_init_zero;
    meshtastic_MqttClientProxyMessage mqtt_proxy_scratch_ = meshtastic_MqttClientProxyMessage_init_zero;
    meshtastic_Data node_metadata_decode_scratch_ = meshtastic_Data_init_zero;
    meshtastic_FromRadio from_radio_scratch_ = meshtastic_FromRadio_init_zero;
};

} // namespace phone::meshtastic
