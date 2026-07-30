#pragma once

#include "app/app_facades.h"
#include "ble/app_phone_facade.h"
#include "ble/ble_manager.h"
#include "chat/usecase/chat_service.h"
#include "phone/meshcore/meshcore_phone_core.h"
#include "phone/meshtastic/meshtastic_phone_core.h"
#include "platform/esp/arduino_common/chat/infra/meshcore/meshcore_adapter.h"
#include "team/usecase/team_service.h"
#include <NimBLEDevice.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ble
{

class MeshCoreBleService : public BleService,
                           public phone::meshcore::MeshCorePhoneHooks,
                           public chat::ChatService::IncomingTextObserver,
                           public team::TeamService::IncomingDataObserver
{
  public:
    MeshCoreBleService(app::IAppBleFacade& ctx, const std::string& device_name);
    ~MeshCoreBleService() override;

    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, std::size_t size) noexcept;

    bool start() override;
    void stop() override;
    void update() override;

    void onIncomingText(const chat::MeshIncomingText& msg) override;
    void onIncomingData(const chat::MeshIncomingData& msg) override;

  private:
    friend class MeshCoreRxCallbacks;
    friend class MeshCoreTxCallbacks;
    friend class MeshCoreServerCallbacks;

    struct Frame
    {
        static constexpr size_t kMaxLen = 172;

        uint8_t len = 0;
        std::array<uint8_t, kMaxLen> buf{};

        bool assign(const uint8_t* data, size_t size)
        {
            if (!data || size == 0 || size > buf.size())
            {
                return false;
            }
            len = static_cast<uint8_t>(size);
            memcpy(buf.data(), data, size);
            return true;
        }
    };

    template <size_t Capacity>
    class FrameQueue
    {
      public:
        bool empty() const { return count_ == 0; }
        size_t size() const { return count_; }

        void clear()
        {
            for (size_t index = 0; index < count_; ++index)
            {
                frames_[index] = Frame{};
            }
            count_ = 0;
        }

        const Frame* front() const
        {
            return count_ == 0 ? nullptr : &frames_[0];
        }

        void pop_front()
        {
            removeAt(0);
        }

        bool pushDropOldest(const uint8_t* data, size_t len, bool* out_dropped = nullptr)
        {
            if (out_dropped)
            {
                *out_dropped = false;
            }
            if (!data || len == 0 || len > Frame::kMaxLen)
            {
                return false;
            }
            if (count_ >= Capacity)
            {
                removeAt(0);
                if (out_dropped)
                {
                    *out_dropped = true;
                }
            }
            return frames_[count_++].assign(data, len);
        }

      private:
        void removeAt(size_t index)
        {
            if (index >= count_)
            {
                return;
            }
            for (size_t move = index + 1; move < count_; ++move)
            {
                frames_[move - 1] = frames_[move];
            }
            --count_;
            frames_[count_] = Frame{};
        }

        std::array<Frame, Capacity> frames_{};
        size_t count_ = 0;
    };

    struct ContactRecord
    {
        uint8_t pubkey[chat::meshcore::MeshCoreIdentity::kPubKeySize] = {};
        uint8_t type = 0;
        uint8_t flags = 0;
        uint8_t out_path_len = 0;
        uint8_t out_path_profile = 0;
        uint8_t out_path_hash_bytes = 1;
        uint8_t out_path[64] = {};
        char name[32] = {};
        uint32_t last_advert = 0;
        int32_t lat = 0;
        int32_t lon = 0;
        uint32_t lastmod = 0;
    } __attribute__((packed));

    app::IAppBleFacade& ctx_;
    meshtastic_Config_BluetoothConfig phone_ble_config_ = meshtastic_Config_BluetoothConfig_init_zero;
    meshtastic_LocalModuleConfig phone_module_config_ = meshtastic_LocalModuleConfig_init_zero;
    AppPhoneFacade phone_facade_;
    std::string device_name_;
    NimBLEServer* server_ = nullptr;
    NimBLEService* service_ = nullptr;
    NimBLECharacteristic* rx_char_ = nullptr;
    NimBLECharacteristic* tx_char_ = nullptr;
    bool connected_ = false;
    bool tx_subscribed_ = false;
    uint16_t conn_handle_ = 0;
    bool conn_handle_valid_ = false;
    uint16_t negotiated_mtu_ = 23;

    static constexpr size_t kOutboundFrameQueueDepth = 24;
    static constexpr size_t kRxFrameQueueDepth = 8;
    static constexpr size_t kOfflineFrameQueueDepth = 12;
    static constexpr size_t kKnownPeerHashDepth = 64;
    static constexpr size_t kConnectionDepth = 8;
    FrameQueue<kOutboundFrameQueueDepth> outbound_;
    FrameQueue<kRxFrameQueueDepth> rx_queue_;
    FrameQueue<kOfflineFrameQueueDepth> offline_queue_;
    std::vector<ContactRecord> manual_contacts_;
    std::array<uint8_t, kKnownPeerHashDepth> known_peer_hashes_{};
    size_t known_peer_hash_count_ = 0;

    uint32_t last_write_ms_ = 0;

    uint8_t app_target_ver_ = 0;
    uint8_t cmd_frame_[173] = {};

    bool manual_add_contacts_ = false;
    uint8_t telemetry_mode_base_ = 0;
    uint8_t telemetry_mode_loc_ = 0;
    uint8_t telemetry_mode_env_ = 0;
    uint8_t advert_loc_policy_ = 0;
    uint8_t multi_acks_ = 0;

    uint32_t ble_pin_ = 0;
    uint32_t active_ble_pin_ = 0;
    std::atomic<uint32_t> pending_passkey_{0};
    int16_t last_rssi_dbm_x10_ = 0;
    int16_t last_snr_db_x10_ = 0;
    uint32_t stats_tx_packets_ = 0;
    uint32_t stats_rx_packets_ = 0;
    uint32_t stats_tx_flood_ = 0;
    uint32_t stats_tx_direct_ = 0;
    uint32_t stats_rx_flood_ = 0;
    uint32_t stats_rx_direct_ = 0;
    int32_t advert_lat_ = 0;
    int32_t advert_lon_ = 0;
    uint32_t pending_login_ = 0;
    uint32_t pending_status_ = 0;
    uint32_t pending_status_tag_ = 0;
    uint32_t pending_telemetry_ = 0;
    uint32_t pending_req_ = 0;
    uint32_t pending_discovery_ = 0;

    struct ConnectionEntry
    {
        bool used = false;
        uint32_t prefix4 = 0;
        uint32_t expires_ms = 0;
        uint16_t keep_alive_secs = 0;
    };
    std::array<ConnectionEntry, kConnectionDepth> connections_{};
    std::unique_ptr<phone::meshcore::MeshCorePhoneCore> shared_core_;

    void setupService();
    bool startAdvertising();
    void handleIncomingFrames();
    void handleCmdFrame(size_t len);
    void enqueueFrame(const uint8_t* data, size_t len);
    void enqueueOffline(const uint8_t* data, size_t len);
    void sendPendingFrames();
    void sendOfflineTickle();

    void loadManualContacts();
    void saveManualContacts();
    void loadBlePin();
    void saveBlePin();
    uint32_t effectiveBlePin() const;
    void refreshBlePin();
    void noteLinkStats(int16_t rssi_dbm_x10, int16_t snr_db_x10);
    void noteRxMeta(const chat::RxMeta& rx_meta);
    void noteEventRx(int8_t rssi_dbm, int8_t snr_qdb);
    void noteSentRoute(bool sent_flood);
    void enqueueRawDataPush(const uint8_t* payload, size_t len, const chat::RxMeta* meta);
    bool handleCustomVarSet(const char* key, const char* value);
    void appendCustomVar(std::string& out, const char* key, const char* value) const;
    bool rememberKnownPeerHash(uint8_t peer_hash);
    void clearKnownPeerHashes();
    void pruneConnections(uint32_t now_ms);
    void upsertConnection(const ConnectionEntry& entry);
    bool hasConnectionPrefix(uint32_t prefix4, uint32_t now_ms) const;
    void removeConnectionPrefix(uint32_t prefix4);
    void clearConnections();
    ContactRecord* findManualContact(const uint8_t* pubkey);
    const ContactRecord* findManualContactByPrefix(const uint8_t* prefix, size_t len) const;
    bool resolveContactByPubkey(const uint8_t* pubkey,
                                chat::meshcore::MeshCoreAdapter::PeerInfo* out_peer,
                                const ContactRecord** out_manual) const;
    void upsertManualContact(const ContactRecord& record);
    bool removeManualContact(const uint8_t* pubkey);
    static bool decodeContactPayload(const uint8_t* frame, size_t len,
                                     ContactRecord* out, uint32_t* out_lastmod);

    void clearPendingRequests();

    bool buildContactFrame(const chat::meshcore::MeshCoreAdapter::PeerInfo& peer,
                           uint8_t code, Frame& out);
    bool lookupPeerByPrefix(const uint8_t* prefix, size_t len,
                            chat::meshcore::MeshCoreAdapter::PeerInfo* out) const;
    chat::meshcore::MeshCoreAdapter* meshCoreAdapter();
    const chat::meshcore::MeshCoreAdapter* meshCoreAdapter() const;
    uint32_t deriveNodeIdFromPubkey(const uint8_t* pubkey, size_t len) const;
    bool shouldUseSharedCore(uint8_t cmd) const;
    bool handleViaSharedCore(size_t len);
    phone::meshcore::MeshCorePhoneBatteryInfo getBatteryInfo() const override;
    phone::meshcore::MeshCorePhoneLocation getAdvertLocation() const override;
    uint32_t getReportedBlePin() const override;
    uint8_t getAdvertLocationPolicy() const override;
    uint8_t getTelemetryModeBits() const override;
    bool getManualAddContacts() const override;
    bool resolvePeerPublicKey(const uint8_t* in_pubkey, size_t in_len,
                              uint8_t* out_pubkey, size_t out_len) const override;
    void onPendingBinaryRequest(uint32_t tag) override;
    void onPendingTelemetryRequest(uint32_t tag) override;
    void onPendingPathDiscoveryRequest(uint32_t tag) override;
    void onSentRoute(bool sent_flood) override;
    bool lookupAdvertPath(const uint8_t* pubkey, size_t len,
                          uint32_t* out_ts, uint8_t* out_path, size_t* inout_len) const override;
    bool lookupAdvertPathEx(const uint8_t* pubkey, size_t len,
                            phone::meshcore::MeshCorePhoneAdvertPath* out) const override;
    bool hasActiveConnection(const uint8_t* prefix, size_t len) const override;
    void logoutActiveConnection(const uint8_t* prefix, size_t len) override;
    bool getRadioStats(phone::meshcore::MeshCorePhoneRadioStats* out) const override;
    bool getPacketStats(phone::meshcore::MeshCorePhonePacketStats* out) const override;
    bool setAdvertLocation(int32_t lat, int32_t lon) override;
    bool upsertContactFromFrame(const uint8_t* frame, size_t len) override;
    bool removeContact(const uint8_t* pubkey, size_t len) override;
    bool exportContact(const uint8_t* pubkey, size_t len, uint8_t* out, size_t* out_len) const override;
    bool importContact(const uint8_t* frame, size_t len) override;
    bool shareContact(const uint8_t* pubkey, size_t len) override;
    bool popOfflineMessage(uint8_t* out, size_t* out_len) override;
    bool setTuningParams(const phone::meshcore::MeshCorePhoneTuningParams& params) override;
    bool getTuningParams(phone::meshcore::MeshCorePhoneTuningParams* out) const override;
    bool setOtherParams(uint8_t manual_add_contacts, uint8_t telemetry_bits,
                        bool has_multi_acks, uint8_t advert_loc_policy, uint8_t multi_acks) override;
    bool setDevicePin(uint32_t pin) override;
    bool getCustomVars(std::string* out) const override;
    bool setCustomVar(const char* key, const char* value) override;
    std::size_t meshCoreContactCount() const override;
    bool getMeshCoreContactByIndex(std::size_t index,
                                   phone::meshcore::MeshCorePhoneContactView* out) const override;
    bool resolveMeshCoreContactNodeId(const uint8_t* prefix,
                                      std::size_t len,
                                      uint32_t* out_node_id) const override;
    void onFactoryReset() override;
};

} // namespace ble
