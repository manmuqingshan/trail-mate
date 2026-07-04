#pragma once

#include "app/app_facades.h"
#include "ble/app_phone_facade.h"
#include "ble/ble_manager.h"
#include "chat/domain/chat_types.h"
#include "meshtastic/admin.pb.h"
#include "meshtastic/channel.pb.h"
#include "meshtastic/config.pb.h"
#include "meshtastic/device_ui.pb.h"
#include "meshtastic/localonly.pb.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/module_config.pb.h"
#include "meshtastic/telemetry.pb.h"
#include "phone/meshtastic/meshtastic_phone_session.h"
#include "platform/shared/ble/phone_ble_runtime.h"
#include <atomic>
#include <bluefruit.h>
#include <cstdint>
#include <memory>
#include <string>

namespace ble
{

class MeshtasticBleObserverBridge;

class MeshtasticBleService final : public BleService,
                                   public phone::meshtastic::MeshtasticPhoneTransport,
                                   public platform::shared::ble_bridge::IPhoneBleRuntime
{
  public:
    MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshtasticBleService() override;

    void start() override;
    void stop() override;
    void update() override;
    bool isRunning() const override;
    void setDeviceName(const std::string& name) override;

    void handleIncomingTextFromApp(const chat::MeshIncomingText& msg);
    void handleOutgoingTextFromApp(const chat::MeshIncomingText& msg);
    void handleIncomingDataFromApp(const chat::MeshIncomingData& msg);

    bool handleToRadio(const uint8_t* data, size_t len);
    bool enqueueToRadio(const uint8_t* data, size_t len);
    void flushPendingFromNumNotify();
    void handleFromRadioReadRequest(uint16_t conn_handle, uint16_t offset, uintptr_t chr_ptr);
    void handleConnectEvent(uint16_t conn_handle);
    void handleDisconnectEvent(uint16_t conn_handle, uint8_t reason);
    void handleFromNumCccdWrite(uint16_t conn_handle, uint16_t value);
    void handlePairPasskeyDisplay(uint16_t conn_handle, const uint8_t passkey[6], bool match_request);
    void handlePairComplete(uint16_t conn_handle, uint8_t auth_status);
    void handleSecured(uint16_t conn_handle);

    bool getPairingStatus(BlePairingStatus* out) const override;
    bool isBleConnected() const override;
    void notifyFromNum(uint32_t from_num) override;
    bool isPhoneBleConnected() const override;
    uint32_t pendingPhoneBlePasskey() const override;
    void requestPhoneHighThroughputConnection() override;
    void requestPhoneLowerPowerConnection() override;
    void requestPhoneDisconnect() override;
    void onPhoneBluetoothConfigChanged() override;
    void onPhoneModuleConfigChanged() override;

  private:
    struct PendingToRadioFrame
    {
        size_t len = 0;
        uint8_t buf[meshtastic_ToRadio_size] = {};
    };

    struct PublishedFromRadioSlot
    {
        phone::meshtastic::MeshtasticBleFrame frame{};
        bool notified = false;
    };

    enum class PendingGapEventType : uint8_t
    {
        Connect,
        Disconnect,
    };

    struct PendingGapEvent
    {
        PendingGapEventType type = PendingGapEventType::Disconnect;
        uint16_t conn_handle = BLE_CONN_HANDLE_INVALID;
        uint8_t reason = 0;
    };

    void enqueueGapEvent(PendingGapEventType type, uint16_t conn_handle, uint8_t reason);
    void processPendingGapEvents();
    void applyConnectEvent(uint16_t conn_handle);
    void applyDisconnectEvent(uint16_t conn_handle, uint8_t reason);
    void processPendingToRadio();
    void processPendingPairingRequest();
    void clearToPhoneQueue();
    void requestFromRadioPublish(const char* reason);
    uint32_t nextFromNumNotifyValue();
    void fillPublishedFromRadioSlots();
    void flushPendingFromRadioReadAuthorize();
    void clearPendingFromRadioReadAuthorize();
    void writeEmptyFromRadioRead(uint8_t reason);
    bool writePublishedFromRadioForRead(uint16_t conn_handle);
    void releasePublishedFromRadioHead();
    void syncMqttProxySettings();
    void markConfigSavePending(bool bluetooth_changed, bool module_changed);
    void flushPendingConfigSaves(bool force = false);
    void applyBleSecurity();
    bool processPendingPhoneDisconnect();
    void checkPhoneSessionLiveness(uint32_t now_ms);
    void logFromRadioState(const char* tag) const;
    void logSessionState(const char* tag, uint32_t detail = 0);
    void requestPairingIfNeeded(uint16_t conn_handle);
    uint32_t effectivePasskey() const;
    void logDeferredBleEvents();
    void loadRememberedPhonePeer();
    void rememberPhonePeer(uint16_t conn_handle, const char* reason);
    void startPhoneAdvertising(bool prefer_directed);

    meshtastic_Config_BluetoothConfig ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig module_config_ = meshtastic_LocalModuleConfig_init_zero;
    AppPhoneFacade phone_facade_;
    std::unique_ptr<MeshtasticBleObserverBridge> observer_bridge_;
    std::string device_name_;
    ::BLEService service_;
    ::BLECharacteristic to_radio_;
    ::BLECharacteristic from_radio_;
    ::BLECharacteristic from_num_;
    ::BLECharacteristic log_radio_;
    bool active_ = false;
    bool gatt_initialized_ = false;
    bool observers_registered_ = false;
    bool connected_ = false;
    bool from_num_notify_enabled_ = false;
    uint16_t conn_handle_ = BLE_CONN_HANDLE_INVALID;
    std::unique_ptr<phone::meshtastic::MeshtasticPhoneSession> phone_session_;
    std::atomic<uint32_t> pending_passkey_{0};
    std::atomic<uint32_t> configured_passkey_{0};

    static constexpr uint8_t kPendingToRadioCapacity = 6;
    PendingToRadioFrame pending_to_radio_[kPendingToRadioCapacity]{};
    PendingToRadioFrame pending_to_radio_work_{};
    volatile uint8_t pending_to_radio_head_ = 0;
    volatile uint8_t pending_to_radio_tail_ = 0;
    volatile uint8_t pending_to_radio_count_ = 0;

    static constexpr uint8_t kPublishedFromRadioCapacity = 3;
    PublishedFromRadioSlot published_from_radio_[kPublishedFromRadioCapacity]{};
    phone::meshtastic::MeshtasticBleFrame session_frame_scratch_{};
    volatile uint8_t published_from_radio_head_ = 0;
    volatile uint8_t published_from_radio_tail_ = 0;
    volatile uint8_t published_from_radio_count_ = 0;
    volatile bool from_radio_publish_requested_ = false;
    uint32_t from_num_notify_counter_ = 0;

    volatile bool pairing_request_pending_ = false;
    volatile uint16_t pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile bool pending_phone_disconnect_request_ = false;

    static constexpr uint8_t kPendingGapEventCapacity = 4;
    PendingGapEvent pending_gap_events_[kPendingGapEventCapacity]{};
    volatile uint8_t pending_gap_event_head_ = 0;
    volatile uint8_t pending_gap_event_tail_ = 0;
    volatile uint8_t pending_gap_event_count_ = 0;
    volatile uint8_t pending_gap_event_drop_count_ = 0;

    volatile bool pending_connect_log_ = false;
    volatile bool pending_disconnect_log_ = false;
    volatile bool pending_from_num_cccd_log_ = false;
    volatile bool pending_pair_complete_log_ = false;
    volatile bool pending_secured_log_ = false;
    volatile bool pending_from_radio_auth_log_ = false;

    volatile uint16_t pending_connect_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_disconnect_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint8_t pending_disconnect_reason_ = 0;
    volatile uint16_t pending_from_num_cccd_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_from_num_cccd_value_ = 0;
    volatile uint16_t pending_pair_complete_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint8_t pending_pair_complete_status_ = 0;
    volatile uint16_t pending_secured_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_from_radio_auth_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint16_t pending_from_radio_auth_offset_ = 0;
    volatile uintptr_t pending_from_radio_auth_chr_ = 0;
    volatile uintptr_t pending_from_radio_auth_svc_ = 0;

    volatile bool pending_from_radio_read_log_ = false;
    volatile bool pending_from_radio_empty_log_ = false;
    volatile bool pending_from_radio_read_authorize_ = false;
    volatile uint8_t pending_from_radio_empty_reason_ = 0;
    volatile uint16_t pending_from_radio_read_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    volatile uint32_t pending_from_radio_read_from_num_ = 0;
    volatile uint32_t pending_from_radio_read_due_ms_ = 0;
    volatile uint16_t pending_from_radio_read_len_ = 0;

    bool bluetooth_config_save_pending_ = false;
    bool module_config_save_pending_ = false;
    uint32_t config_save_due_ms_ = 0;
    uint32_t last_ble_activity_ms_ = 0;
    uint32_t ble_session_seq_ = 0;
    uint32_t session_started_ms_ = 0;
    uint32_t last_secured_ms_ = 0;
    uint32_t last_from_num_cccd_ms_ = 0;
    uint32_t last_to_radio_ms_ = 0;
    uint32_t last_heartbeat_ms_ = 0;
    uint32_t last_want_config_ms_ = 0;
    uint32_t last_from_radio_read_ms_ = 0;
    uint32_t last_from_num_notify_ms_ = 0;
    uint32_t next_connected_session_log_ms_ = 0;
    uint32_t next_ble_idle_log_ms_ = 0;
    uint32_t next_liveness_log_ms_ = 0;
    ble_gap_addr_t remembered_phone_peer_ = {};
    bool remembered_phone_peer_valid_ = false;
    bool remembered_phone_peer_bonded_ = false;
    bool directed_advertising_active_ = false;
    bool directed_advertising_attempted_ = false;
    uint32_t directed_advertising_until_ms_ = 0;
};

} // namespace ble
