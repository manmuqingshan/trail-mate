#include "../../include/ble/meshtastic_ble.h"

#include "ble/ble_uuids.h"
#include "ble/meshtastic_ble_observer_bridge.h"
#include "ble/meshtastic_ble_persistence_bridge.h"
#include "chat/ble/meshtastic_defaults.h"
#include "chat/ble/meshtastic_phone_config_bridge.h"
#include "platform/nrf52/arduino_common/chat/infra/meshtastic/meshtastic_radio_adapter.h"
#include "platform/shared/ble/app_config_phone_snapshot_bridge.h"
#include "platform/shared/ble/meshtastic_phone_runtime_bridge.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace ble
{
namespace
{
constexpr uint32_t kDefaultBleFixedPin = 654321;
constexpr uint32_t kConfigSaveDebounceMs = 1500UL;

bool usbSerialWritable(std::size_t len)
{
    return static_cast<bool>(Serial) && Serial.dtr() != 0 && Serial.availableForWrite() >= static_cast<int>(len);
}

void bleLogBoth(const char* fmt, ...)
{
    char buffer[192] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (usbSerialWritable(std::strlen(buffer) + 2U))
    {
        Serial.println(buffer);
    }
    Serial2.println(buffer);
}

uint32_t parsePasskeyDigits(const uint8_t passkey[6])
{
    if (!passkey)
    {
        return 0;
    }

    char digits[7] = {};
    for (size_t i = 0; i < 6; ++i)
    {
        const uint8_t ch = passkey[i];
        if (ch < '0' || ch > '9')
        {
            return 0;
        }
        digits[i] = static_cast<char>(ch);
    }
    return static_cast<uint32_t>(std::strtoul(digits, nullptr, 10));
}

MeshtasticBleService* s_active_service = nullptr;

void onBleConnect(uint16_t conn_handle)
{
    if (s_active_service)
    {
        s_active_service->handleConnectEvent(conn_handle);
    }
}

void onBleDisconnect(uint16_t conn_handle, uint8_t)
{
    if (!s_active_service)
    {
        return;
    }
    s_active_service->handleDisconnectEvent(conn_handle);
}

bool onPairPasskeyDisplay(uint16_t conn_handle, uint8_t const passkey[6], bool match_request)
{
    if (s_active_service)
    {
        s_active_service->handlePairPasskeyDisplay(conn_handle, passkey, match_request);
    }
    return true;
}

void onPairComplete(uint16_t conn_handle, uint8_t auth_status)
{
    if (s_active_service)
    {
        s_active_service->handlePairComplete(conn_handle, auth_status);
    }
}

void onSecured(uint16_t conn_handle)
{
    if (s_active_service)
    {
        s_active_service->handleSecured(conn_handle);
    }
}

void prepareBluefruit(const std::string& device_name)
{
    bleLogBoth("[BLE][nrf52][mt] bluefruit begin name=%s", device_name.c_str());
    Bluefruit.autoConnLed(false);
    Bluefruit.configPrphBandwidth(BANDWIDTH_MAX);
    Bluefruit.begin();
    Bluefruit.setName(device_name.c_str());
    Bluefruit.Periph.setConnectCallback(onBleConnect);
    Bluefruit.Periph.setDisconnectCallback(onBleDisconnect);
    bleLogBoth("[BLE][nrf52][mt] bluefruit ready");
}

void startAdvertising(::BLEService& service)
{
    (void)service;
    Bluefruit.Advertising.stop();
    Bluefruit.Advertising.clearData();
    Bluefruit.ScanResponse.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addService(service);
    Bluefruit.ScanResponse.addName();
    Bluefruit.ScanResponse.addTxPower();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 668);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
    bleLogBoth("[BLE][nrf52][mt] advertising started running=%u",
               Bluefruit.Advertising.isRunning() ? 1U : 0U);
}

void disconnectAll()
{
    for (uint8_t index = 0; index < BLE_MAX_CONNECTION; ++index)
    {
        if (Bluefruit.connected(index))
        {
            Bluefruit.disconnect(index);
        }
    }
}

void authorizeRead(uint16_t conn_handle)
{
    ble_gatts_rw_authorize_reply_params_t reply = {.type = BLE_GATTS_AUTHORIZE_TYPE_READ};
    reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
    sd_ble_gatts_rw_authorize_reply(conn_handle, &reply);
}

void onToRadioWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len)
{
    if (!s_active_service || !data || len == 0)
    {
        return;
    }
    (void)s_active_service->enqueueToRadio(data, len);
}

void onFromNumCccdWrite(uint16_t conn_handle, BLECharacteristic*, uint16_t value)
{
    if (s_active_service)
    {
        s_active_service->handleFromNumCccdWrite(conn_handle, value);
    }
}

void onFromRadioAuthorize(uint16_t conn_handle, BLECharacteristic* chr, ble_gatts_evt_read_t* request)
{
    if (!chr || !request)
    {
        authorizeRead(conn_handle);
        return;
    }

    bleLogBoth("[BLE][nrf52][mt][auth] conn=%u offset=%u chr=%p svc=%p",
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned>(request->offset),
               static_cast<void*>(chr),
               static_cast<void*>(s_active_service));

    if (request->offset == 0)
    {
        if (s_active_service)
        {
            (void)s_active_service->writeNextFromRadioForRead();
        }
    }

    authorizeRead(conn_handle);
}

} // namespace

MeshtasticBleService::MeshtasticBleService(app::IAppBleFacade& ctx, const std::string& device_name)
    : phone_facade_(ctx, ble_config_, module_config_, this),
      observer_bridge_(new MeshtasticBleObserverBridge(ctx, *this)),
      device_name_(device_name),
      service_(::BLEUuid(MESH_SERVICE_UUID)),
      to_radio_(::BLEUuid(TORADIO_UUID)),
      from_radio_(::BLEUuid(FROMRADIO_UUID)),
      from_num_(::BLEUuid(FROMNUM_UUID)),
      log_radio_(::BLEUuid(LOGRADIO_UUID))
{
    ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    ble_config_.enabled = phone_facade_.isBleEnabled();
    ble_config_.mode = meshtastic_Config_BluetoothConfig_PairingMode_RANDOM_PIN;
    ble_config_.fixed_pin = 0;

    meshtastic_config_bridge::PersistedState persisted{};
    const bool persisted_ok = loadMeshtasticBlePersistedState(&persisted);

    bleLogBoth("[BLE][nrf52][mt] persisted load ok=%u has_bt=%u has_mod=%u",
               persisted_ok ? 1U : 0U,
               persisted.has_bluetooth ? 1U : 0U,
               persisted.has_module ? 1U : 0U);

    logMeshtasticBlePersistenceStatus();

    meshtastic_config_bridge::initializeConfigState(
        persisted, phone_facade_.isBleEnabled(), phone_facade_.getSelfNodeId(), &ble_config_, &module_config_);

    if (persisted.has_bluetooth)
    {
        bleLogBoth("[BLE][nrf52][mt] loaded bluetooth cfg mode=%u pin=%06lu",
                   static_cast<unsigned>(ble_config_.mode),
                   static_cast<unsigned long>(ble_config_.fixed_pin));
    }
    if (persisted.has_module)
    {
        bleLogBoth("[BLE][nrf52][mt] loaded module cfg mqtt enabled=%u proxy=%u root=%s",
                   module_config_.has_mqtt && module_config_.mqtt.enabled ? 1U : 0U,
                   module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled ? 1U : 0U,
                   module_config_.mqtt.root);
    }

    phone_session_.reset(new phone::meshtastic::MeshtasticPhoneSession(phone_facade_,
                                                                       *this,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_,
                                                                       &phone_facade_));
}

MeshtasticBleService::~MeshtasticBleService()
{
    stop();
}

void MeshtasticBleService::logFromRadioState(const char* tag) const
{
    bleLogBoth("[BLE][nrf52][mt] fromRadio tag=%s pending_from_num=%d "
               "notify_enabled=%d connected=%d config_active=%d",
               tag ? tag : "?",
               pending_from_num_valid_ ? 1 : 0,
               from_num_notify_enabled_ ? 1 : 0,
               connected_ ? 1 : 0,
               (phone_session_ && phone_session_->isConfigFlowActive()) ? 1 : 0);
}

void MeshtasticBleService::start()
{
    s_active_service = this;
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    from_num_notify_enabled_ = false;
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pending_from_num_valid_ = false;
    pending_from_num_ = 0;
    pending_connect_log_ = false;
    pending_disconnect_log_ = false;
    pending_from_num_cccd_log_ = false;
    pending_pair_complete_log_ = false;
    pending_secured_log_ = false;
    pending_from_radio_read_log_ = false;
    pending_from_radio_empty_log_ = false;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    last_ble_activity_ms_ = millis();

    prepareBluefruit(device_name_);
    applyBleSecurity();

    service_.begin();
    bleLogBoth("[BLE][nrf52][mt] service begin");

    to_radio_.setProperties(CHR_PROPS_WRITE);
    to_radio_.setPermission(SECMODE_OPEN, SECMODE_OPEN);
    to_radio_.setFixedLen(0);
    to_radio_.setMaxLen(meshtastic_ToRadio_size);
    to_radio_.setWriteCallback(onToRadioWrite, false);
    to_radio_.begin();

    from_radio_.setProperties(CHR_PROPS_READ);
    from_radio_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    from_radio_.setFixedLen(0);
    from_radio_.setMaxLen(meshtastic_FromRadio_size);
    from_radio_.setReadAuthorizeCallback(onFromRadioAuthorize, false);
    from_radio_.begin();
    {
        uint8_t empty = 0;
        from_radio_.write(&empty, 0);
    }

    from_num_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    from_num_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    from_num_.setFixedLen(4);
    from_num_.write32(0);
    from_num_.setCccdWriteCallback(onFromNumCccdWrite, false);
    from_num_.begin();

    log_radio_.setProperties(CHR_PROPS_NOTIFY | CHR_PROPS_READ);
    log_radio_.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
    log_radio_.setFixedLen(0);
    log_radio_.setMaxLen(96);
    log_radio_.begin();
    bleLogBoth("[BLE][nrf52][mt] chars ready");

    if (observer_bridge_)
    {
        observer_bridge_->registerObservers();
    }

    startAdvertising(service_);
    active_ = true;
    pending_passkey_.store(0);
    syncMqttProxySettings();
    bleLogBoth("[BLE][nrf52][mt] service active");
    logFromRadioState("start_done");
}

void MeshtasticBleService::stop()
{
    if (observer_bridge_)
    {
        observer_bridge_->unregisterObservers();
    }

    disconnectAll();
    Bluefruit.Advertising.stop();
    flushPendingConfigSaves(true);
    if (phone_session_)
    {
        phone_session_->close();
    }
    active_ = false;
    connected_ = false;
    from_num_notify_enabled_ = false;
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_passkey_.store(0);
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pending_from_num_valid_ = false;
    pending_from_num_ = 0;
    pending_connect_log_ = false;
    pending_disconnect_log_ = false;
    pending_from_num_cccd_log_ = false;
    pending_pair_complete_log_ = false;
    pending_secured_log_ = false;
    pending_from_radio_read_log_ = false;
    pending_from_radio_empty_log_ = false;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    if (s_active_service == this)
    {
        s_active_service = nullptr;
    }
}

void MeshtasticBleService::update()
{
    if (!active_)
    {
        return;
    }

    syncMqttProxySettings();

    if (phone_session_)
    {
        phone_session_->pumpIncomingAppData();
    }

    processPendingPairingRequest();
    processPendingToRadio();
    flushPendingFromNumNotify();
    logDeferredBleEvents();
    flushPendingConfigSaves(false);

    if (!Bluefruit.connected() && !Bluefruit.Advertising.isRunning())
    {
        Bluefruit.Advertising.start(0);
        bleLogBoth("[BLE][nrf52][mt] advertising restarted");
    }
}

void MeshtasticBleService::handleIncomingTextFromApp(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        phone_session_->onIncomingText(msg);
        if (phone_session_->isSendingPackets())
        {
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::handleOutgoingTextFromApp(const chat::MeshIncomingText& msg)
{
    if (phone_session_)
    {
        Serial2.printf("[BLE][nrf52][mt] local text mirror id=%08lX from=%08lX to=%08lX len=%u\n",
                       static_cast<unsigned long>(msg.msg_id),
                       static_cast<unsigned long>(msg.from),
                       static_cast<unsigned long>(msg.to),
                       static_cast<unsigned>(msg.text.size()));
        phone_session_->onIncomingText(msg);
        if (phone_session_->isSendingPackets())
        {
            notifyFromNum(0);
        }
    }
}

void MeshtasticBleService::handleIncomingDataFromApp(const chat::MeshIncomingData& msg)
{
    if (phone_session_)
    {
        bleLogBoth("[BLE][nrf52][mt] onIncomingData from=%08lX to=%08lX pkt=%08lX port=%u len=%u",
                   static_cast<unsigned long>(msg.from),
                   static_cast<unsigned long>(msg.to),
                   static_cast<unsigned long>(msg.packet_id),
                   static_cast<unsigned>(msg.portnum),
                   static_cast<unsigned>(msg.payload.size()));
        phone_session_->onIncomingData(msg);
        if (phone_session_->isSendingPackets())
        {
            notifyFromNum(0);
        }
    }
}

bool MeshtasticBleService::isRunning() const
{
    return active_ && (Bluefruit.connected() || Bluefruit.Advertising.isRunning());
}

void MeshtasticBleService::setDeviceName(const std::string& name)
{
    device_name_ = name;
}

bool MeshtasticBleService::handleToRadio(const uint8_t* data, size_t len)
{
    last_ble_activity_ms_ = millis();
    Serial2.printf("[BLE][nrf52][mt] handleToRadio len=%u connected=%u\n",
                   static_cast<unsigned>(len),
                   connected_ ? 1U : 0U);
    return phone_session_ ? phone_session_->handleToRadio(data, len) : false;
}

bool MeshtasticBleService::writeNextFromRadioForRead()
{
    uint8_t empty = 0;
    if (!active_ || !connected_ || !phone_session_)
    {
        from_radio_.write(&empty, 0);
        pending_from_radio_empty_log_ = true;
        return false;
    }

    auto& session_frame = session_frame_scratch_;
    std::memset(&session_frame, 0, sizeof(session_frame));
    if (!phone_session_->popToPhone(&session_frame))
    {
        from_radio_.write(&empty, 0);
        pending_from_radio_empty_log_ = true;
        return false;
    }

    if (session_frame.len == 0 || session_frame.len > meshtastic_FromRadio_size)
    {
        bleLogBoth("[BLE][nrf52][mt] drop invalid from_radio frame from_num=%08lX len=%u max=%u",
                   static_cast<unsigned long>(session_frame.from_num),
                   static_cast<unsigned>(session_frame.len),
                   static_cast<unsigned>(meshtastic_FromRadio_size));
        from_radio_.write(&empty, 0);
        pending_from_radio_empty_log_ = true;
        return false;
    }

    from_radio_.write(session_frame.buf, session_frame.len);
    pending_from_radio_read_len_ = static_cast<uint16_t>(session_frame.len);
    pending_from_radio_read_from_num_ = session_frame.from_num;
    pending_from_radio_read_log_ = true;
    if (pending_from_num_valid_ && pending_from_num_ == session_frame.from_num)
    {
        pending_from_num_valid_ = false;
    }
    bleLogBoth("[BLE][nrf52][mt][flow] from_radio authorize frame from_num=%08lX len=%u",
               static_cast<unsigned long>(session_frame.from_num),
               static_cast<unsigned>(session_frame.len));
    return true;
}

bool MeshtasticBleService::enqueueToRadio(const uint8_t* data, size_t len)
{
    if (!data || len == 0 || len > meshtastic_ToRadio_size)
    {
        return false;
    }

    noInterrupts();
    if (pending_to_radio_count_ >= kPendingToRadioCapacity)
    {
        interrupts();
        Serial2.printf("[BLE][nrf52][mt] to_radio queue full len=%u\n", static_cast<unsigned>(len));
        return false;
    }

    PendingToRadioFrame& frame = pending_to_radio_[pending_to_radio_tail_];
    std::memcpy(frame.buf, data, len);
    frame.len = len;
    pending_to_radio_tail_ = static_cast<uint8_t>((pending_to_radio_tail_ + 1U) % kPendingToRadioCapacity);
    ++pending_to_radio_count_;
    interrupts();
    return true;
}

void MeshtasticBleService::processPendingToRadio()
{
    PendingToRadioFrame frame{};
    while (true)
    {
        noInterrupts();
        if (pending_to_radio_count_ == 0)
        {
            interrupts();
            return;
        }

        frame = pending_to_radio_[pending_to_radio_head_];
        pending_to_radio_head_ = static_cast<uint8_t>((pending_to_radio_head_ + 1U) % kPendingToRadioCapacity);
        --pending_to_radio_count_;
        interrupts();

        (void)handleToRadio(frame.buf, frame.len);
    }
}

void MeshtasticBleService::processPendingPairingRequest()
{
    if (!pairing_request_pending_)
    {
        return;
    }

    const uint16_t conn_handle = pending_pairing_conn_handle_;
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    requestPairingIfNeeded(conn_handle);
}

void MeshtasticBleService::clearToPhoneQueue()
{
    from_num_notify_counter_ = 0;
    session_frame_scratch_ = phone::meshtastic::MeshtasticBleFrame{};
}

void MeshtasticBleService::handleConnectEvent(uint16_t conn_handle)
{
    connected_ = true;
    conn_handle_ = conn_handle;
    last_ble_activity_ms_ = millis();
    from_num_notify_enabled_ = false;
    clearToPhoneQueue();
    pairing_request_pending_ = true;
    pending_pairing_conn_handle_ = conn_handle;
    pending_connect_conn_handle_ = conn_handle;
    pending_connect_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] link-up conn=%u adv=%u",
               static_cast<unsigned>(conn_handle),
               Bluefruit.Advertising.isRunning() ? 1U : 0U);
}

void MeshtasticBleService::handleDisconnectEvent(uint16_t conn_handle)
{
    connected_ = false;
    from_num_notify_enabled_ = false;
    conn_handle_ = BLE_CONN_HANDLE_INVALID;
    last_ble_activity_ms_ = millis();
    if (phone_session_)
    {
        phone_session_->close();
    }
    pending_passkey_.store(0);
    pending_to_radio_head_ = 0;
    pending_to_radio_tail_ = 0;
    pending_to_radio_count_ = 0;
    clearToPhoneQueue();
    pairing_request_pending_ = false;
    pending_pairing_conn_handle_ = BLE_CONN_HANDLE_INVALID;
    pending_disconnect_conn_handle_ = conn_handle;
    pending_disconnect_log_ = true;
    flushPendingConfigSaves(true);
}

void MeshtasticBleService::handleFromNumCccdWrite(uint16_t conn_handle, uint16_t value)
{
    conn_handle_ = conn_handle;
    last_ble_activity_ms_ = millis();
    from_num_notify_enabled_ = (value != 0U);
    pending_from_num_cccd_conn_handle_ = conn_handle;
    pending_from_num_cccd_value_ = value;
    pending_from_num_cccd_log_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_num subscribed=%u conn=%u value=0x%04X",
               from_num_notify_enabled_ ? 1U : 0U,
               static_cast<unsigned>(conn_handle),
               static_cast<unsigned>(value));
}

void MeshtasticBleService::handlePairPasskeyDisplay(uint16_t conn_handle, const uint8_t passkey[6], bool match_request)
{
    const uint32_t parsed = parsePasskeyDigits(passkey);
    pending_passkey_.store(parsed);
    (void)match_request;
    (void)conn_handle;
}

void MeshtasticBleService::handlePairComplete(uint16_t conn_handle, uint8_t auth_status)
{
    pending_passkey_.store(0);
    pending_pair_complete_conn_handle_ = conn_handle;
    pending_pair_complete_status_ = auth_status;
    pending_pair_complete_log_ = true;
}

void MeshtasticBleService::handleSecured(uint16_t conn_handle)
{
    pending_passkey_.store(0);
    pending_secured_conn_handle_ = conn_handle;
    pending_secured_log_ = true;
}

bool MeshtasticBleService::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }

    *out = BlePairingStatus{};
    out->available = phone_facade_.isBleEnabled();
    out->requires_passkey = ble_config_.mode != meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN;
    out->is_fixed_pin = ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN;
    out->is_connected = isBleConnected();
    out->passkey = effectivePasskey();
    out->is_pairing_active = out->requires_passkey && out->passkey != 0;
    return true;
}

bool MeshtasticBleService::isBleConnected() const
{
    return connected_ && Bluefruit.connected();
}

bool MeshtasticBleService::isPhoneBleConnected() const
{
    return isBleConnected();
}

uint32_t MeshtasticBleService::pendingPhoneBlePasskey() const
{
    return effectivePasskey();
}

void MeshtasticBleService::requestPhoneHighThroughputConnection()
{
}

void MeshtasticBleService::requestPhoneLowerPowerConnection()
{
}

void MeshtasticBleService::onPhoneBluetoothConfigChanged()
{
    markConfigSavePending(true, false);
    bleLogBoth("[BLE][nrf52][mt] saveBluetoothConfig requested mode=%u pin=%06lu enabled=%u",
               static_cast<unsigned>(ble_config_.mode),
               static_cast<unsigned long>(ble_config_.fixed_pin),
               ble_config_.enabled ? 1U : 0U);
    flushPendingConfigSaves(true);
}

void MeshtasticBleService::onPhoneModuleConfigChanged()
{
    markConfigSavePending(false, true);
    syncMqttProxySettings();
    bleLogBoth("[BLE][nrf52][mt] saveModuleConfig requested");
    flushPendingConfigSaves(true);
}

void MeshtasticBleService::notifyFromNum(uint32_t from_num)
{
    pending_from_num_ = from_num;
    pending_from_num_valid_ = true;
    bleLogBoth("[BLE][nrf52][mt][flow] from_num pending source=%08lX", static_cast<unsigned long>(from_num));
}

void MeshtasticBleService::flushPendingFromNumNotify()
{
    if (!pending_from_num_valid_)
    {
        return;
    }

    const uint32_t from_num = pending_from_num_;

    if (!active_ || !connected_)
    {
        pending_from_num_valid_ = false;
        bleLogBoth("[BLE][nrf52][mt][flow] from_num skip=%08lX reason=inactive active=%u connected=%u",
                   static_cast<unsigned long>(from_num),
                   active_ ? 1U : 0U,
                   connected_ ? 1U : 0U);
        return;
    }

    if (!from_num_notify_enabled_ || conn_handle_ == BLE_CONN_HANDLE_INVALID)
    {
        bleLogBoth("[BLE][nrf52][mt][flow] from_num skip=%08lX reason=not-subscribed notify=%u conn=%u",
                   static_cast<unsigned long>(from_num),
                   from_num_notify_enabled_ ? 1U : 0U,
                   static_cast<unsigned>(conn_handle_));
        return;
    }

    pending_from_num_valid_ = false;
    uint32_t notify_value = ++from_num_notify_counter_;
    if (notify_value == 0)
    {
        notify_value = ++from_num_notify_counter_;
    }
    from_num_.write32(notify_value);
    const bool ok = from_num_.notify32(conn_handle_, notify_value);
    bleLogBoth("[BLE][nrf52][mt][flow] from_num notify value=%08lX source=%08lX conn=%u ok=%u cccd=0x%04X",
               static_cast<unsigned long>(notify_value),
               static_cast<unsigned long>(from_num),
               static_cast<unsigned>(conn_handle_),
               ok ? 1U : 0U,
               static_cast<unsigned>(from_num_.getCccd(conn_handle_)));
    if (!ok && Bluefruit.connected())
    {
        const bool fallback_ok = from_num_.notify32(notify_value);
        bleLogBoth("[BLE][nrf52][mt][flow] from_num notify fallback ok=%u", fallback_ok ? 1U : 0U);
    }
}

void MeshtasticBleService::markConfigSavePending(bool bluetooth_changed, bool module_changed)
{
    if (bluetooth_changed)
    {
        bluetooth_config_save_pending_ = true;
    }
    if (module_changed)
    {
        module_config_save_pending_ = true;
    }
    config_save_due_ms_ = millis() + kConfigSaveDebounceMs;
}

void MeshtasticBleService::flushPendingConfigSaves(bool force)
{
    if (!bluetooth_config_save_pending_ && !module_config_save_pending_)
    {
        return;
    }

    const uint32_t now_ms = millis();
    if (!force)
    {
        if (phone_session_ && phone_session_->isConfigFlowActive())
        {
            return;
        }
        if (static_cast<int32_t>(now_ms - config_save_due_ms_) < 0)
        {
            return;
        }
        if (connected_ && (now_ms - last_ble_activity_ms_) < kConfigSaveDebounceMs)
        {
            return;
        }
    }

    meshtastic_Config_BluetoothConfig persisted_bluetooth = ble_config_;
    meshtastic_config_bridge::normalizeBluetoothConfig(&persisted_bluetooth);
    persisted_bluetooth.enabled = phone_facade_.isBleEnabled();
    meshtastic_LocalModuleConfig persisted_module = module_config_;
    meshtastic_config_bridge::normalizeModuleConfig(&persisted_module);
    const bool needs_save = bluetooth_config_save_pending_ || module_config_save_pending_;
    const bool persisted = needs_save ? saveMeshtasticBlePersistedState(persisted_bluetooth, persisted_module) : true;
    ble_config_.enabled = persisted_bluetooth.enabled;

    bleLogBoth("[BLE][nrf52][mt] current mem persisted=%u bluetooth_config_save_pending_=%u module_config_save_pending_=%u needs_save=%u",
               persisted ? 1U : 0U,
               bluetooth_config_save_pending_ ? 1U : 0U,
               module_config_save_pending_ ? 1U : 0U,
               needs_save ? 1U : 0U);

    if (bluetooth_config_save_pending_)
    {
        bleLogBoth("[BLE][nrf52][mt] flush bluetooth cfg persisted=%u mode=%u pin=%06lu enabled=%u",
                   persisted ? 1U : 0U,
                   static_cast<unsigned>(persisted_bluetooth.mode),
                   static_cast<unsigned long>(persisted_bluetooth.fixed_pin),
                   persisted_bluetooth.enabled ? 1U : 0U);
        if (persisted)
        {
            bluetooth_config_save_pending_ = false;
        }
    }

    if (module_config_save_pending_)
    {
        bleLogBoth("[BLE][nrf52][mt] flush module cfg persisted=%u enabled=%u proxy=%u root=%s",
                   persisted ? 1U : 0U,
                   module_config_.has_mqtt && module_config_.mqtt.enabled ? 1U : 0U,
                   module_config_.has_mqtt && module_config_.mqtt.proxy_to_client_enabled ? 1U : 0U,
                   module_config_.mqtt.root);
        if (persisted)
        {
            module_config_save_pending_ = false;
        }
    }

    if (!persisted)
    {
        config_save_due_ms_ = millis() + kConfigSaveDebounceMs;
    }
}

void MeshtasticBleService::syncMqttProxySettings()
{
    phone_facade_.syncMeshtasticMqttProxySettings(module_config_);
}

void MeshtasticBleService::applyBleSecurity()
{
    pending_passkey_.store(0);

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        Bluefruit.Security.setMITM(false);
        Bluefruit.Security.setIOCaps(false, false, false);
        Bluefruit.Security.setPairPasskeyCallback(nullptr);
        Bluefruit.Security.setPairCompleteCallback(nullptr);
        Bluefruit.Security.setSecuredCallback(nullptr);
        Serial2.printf("[BLE][nrf52][mt] security mode=no_pin\n");
        return;
    }

    Bluefruit.Security.setIOCaps(true, false, false);
    Bluefruit.Security.setMITM(true);
    Bluefruit.Security.setPairPasskeyCallback(onPairPasskeyDisplay);
    Bluefruit.Security.setPairCompleteCallback(onPairComplete);
    Bluefruit.Security.setSecuredCallback(onSecured);

    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
    {
        const uint32_t fixed_pin = ble_config_.fixed_pin != 0 ? ble_config_.fixed_pin : kDefaultBleFixedPin;
        ble_config_.fixed_pin = fixed_pin;
        char digits[7] = {};
        std::snprintf(digits, sizeof(digits), "%06lu", static_cast<unsigned long>(fixed_pin));
        (void)Bluefruit.Security.setPIN(digits);
        Serial2.printf("[BLE][nrf52][mt] security mode=fixed pin=%s\n", digits);
        return;
    }

    Serial2.printf("[BLE][nrf52][mt] security mode=random_pin\n");
}

void MeshtasticBleService::requestPairingIfNeeded(uint16_t conn_handle)
{
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN)
    {
        return;
    }
    Serial2.printf("[BLE][nrf52][mt] pairing wait-for-central conn=%u mode=%u\n",
                   static_cast<unsigned>(conn_handle),
                   static_cast<unsigned>(ble_config_.mode));
}

void MeshtasticBleService::logDeferredBleEvents()
{
    if (pending_connect_log_)
    {
        pending_connect_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] connected conn=%u mode=%u",
                   static_cast<unsigned>(pending_connect_conn_handle_),
                   static_cast<unsigned>(ble_config_.mode));
    }

    if (pending_disconnect_log_)
    {
        pending_disconnect_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] disconnected conn=%u",
                   static_cast<unsigned>(pending_disconnect_conn_handle_));
    }

    if (pending_from_num_cccd_log_)
    {
        pending_from_num_cccd_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_num cccd conn=%u value=0x%04X enabled=%u",
                   static_cast<unsigned>(pending_from_num_cccd_conn_handle_),
                   static_cast<unsigned>(pending_from_num_cccd_value_),
                   from_num_notify_enabled_ ? 1U : 0U);
    }

    if (pending_pair_complete_log_)
    {
        pending_pair_complete_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] pair complete status=%u conn=%u",
                   static_cast<unsigned>(pending_pair_complete_status_),
                   static_cast<unsigned>(pending_pair_complete_conn_handle_));
    }

    if (pending_secured_log_)
    {
        pending_secured_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] secured conn=%u",
                   static_cast<unsigned>(pending_secured_conn_handle_));
    }

    if (pending_from_radio_read_log_)
    {
        pending_from_radio_read_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_radio read len=%u from_num=%08lX",
                   static_cast<unsigned>(pending_from_radio_read_len_),
                   static_cast<unsigned long>(pending_from_radio_read_from_num_));
    }

    if (pending_from_radio_empty_log_)
    {
        pending_from_radio_empty_log_ = false;
        bleLogBoth("[BLE][nrf52][mt] from_radio read empty");
    }
}

uint32_t MeshtasticBleService::effectivePasskey() const
{
    const uint32_t pending = pending_passkey_.load();
    if (pending != 0)
    {
        return pending;
    }
    if (ble_config_.mode == meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN)
    {
        return ble_config_.fixed_pin;
    }
    return 0;
}

} // namespace ble
