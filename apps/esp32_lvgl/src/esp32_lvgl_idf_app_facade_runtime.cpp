#include "esp32_lvgl_idf_app_facade_runtime.h"

#if defined(ESP_PLATFORM)
#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "app/app_facades.h"
#include "board/BoardBase.h"
#include "chat/infra/contact_store_core.h"
#include "chat/infra/node_store_blob_format.h"
#include "chat/infra/node_store_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_contact_blob_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/ports/i_node_blob_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "platform/esp/boards/board_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#endif

namespace trailmate::apps::esp32_lvgl::idf_app_facade_runtime
{
namespace
{

#if defined(ESP_PLATFORM)
constexpr const char* kIdfStoreTag = "idf-app-store";
constexpr const char* kIdfContactsFile = "/contacts.dat";
constexpr const char* kIdfNodesFile = "/nodes.bin";

std::string makeSdPath(const char* relative)
{
    const char* mount = platform::esp::idf_common::bsp_runtime::sdcard_mount_point();
    std::string path = mount ? mount : "";
    if (path.empty() || !relative || !relative[0])
    {
        return path;
    }
    if (path.back() == '/' && relative[0] == '/')
    {
        path.pop_back();
    }
    else if (path.back() != '/' && relative[0] != '/')
    {
        path.push_back('/');
    }
    path += relative;
    return path;
}

bool readSdFile(const char* relative, std::vector<uint8_t>& out)
{
    out.clear();
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        ESP_LOGW(kIdfStoreTag, "load skipped: sd not ready path=%s", relative);
        return false;
    }

    const std::string path = makeSdPath(relative);
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return false;
    }

    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size <= 0)
    {
        std::fclose(file);
        return false;
    }
    if (std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    out.resize(static_cast<size_t>(size));
    const size_t read = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    if (read != out.size())
    {
        out.clear();
        return false;
    }
    return true;
}

bool removeSdFile(const char* relative)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }
    const std::string path = makeSdPath(relative);
    const std::string temp_path = path + ".tmp";
    std::remove(temp_path.c_str());
    std::remove(path.c_str());
    return true;
}

bool writeSdFileAtomic(const char* relative, const uint8_t* data, size_t len)
{
    if (len == 0)
    {
        return removeSdFile(relative);
    }
    if (!data)
    {
        return false;
    }
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        ESP_LOGW(kIdfStoreTag, "save skipped: sd not ready path=%s len=%u",
                 relative,
                 static_cast<unsigned>(len));
        return false;
    }

    const std::string path = makeSdPath(relative);
    const std::string temp_path = path + ".tmp";
    std::remove(temp_path.c_str());

    FILE* file = std::fopen(temp_path.c_str(), "wb");
    if (!file)
    {
        ESP_LOGW(kIdfStoreTag, "save open failed path=%s", temp_path.c_str());
        return false;
    }

    const size_t written = std::fwrite(data, 1, len, file);
    const int close_result = std::fclose(file);
    if (written != len || close_result != 0)
    {
        std::remove(temp_path.c_str());
        ESP_LOGW(kIdfStoreTag, "save write failed path=%s want=%u got=%u close=%d",
                 temp_path.c_str(),
                 static_cast<unsigned>(len),
                 static_cast<unsigned>(written),
                 close_result);
        return false;
    }

    std::remove(path.c_str());
    if (std::rename(temp_path.c_str(), path.c_str()) != 0)
    {
        std::remove(temp_path.c_str());
        ESP_LOGW(kIdfStoreTag, "save rename failed tmp=%s path=%s",
                 temp_path.c_str(),
                 path.c_str());
        return false;
    }
    return true;
}

class IdfSdNodeBlobStore final : public chat::contacts::INodeBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        std::vector<uint8_t> file;
        if (!readSdFile(kIdfNodesFile, file) ||
            file.size() <= sizeof(chat::contacts::NodeStoreSdHeader))
        {
            out.clear();
            return false;
        }

        chat::contacts::NodeStoreSdHeader header{};
        std::memcpy(&header, file.data(), sizeof(header));
        const uint8_t* payload = file.data() + sizeof(header);
        const size_t payload_len = file.size() - sizeof(header);
        if (chat::contacts::validateNodeStoreSdBlob(header, payload, payload_len) !=
            chat::contacts::NodeBlobValidation::Ok)
        {
            out.clear();
            ESP_LOGW(kIdfStoreTag,
                     "node load invalid path=%s ver=%u count=%u len=%u",
                     kIdfNodesFile,
                     static_cast<unsigned>(header.ver),
                     static_cast<unsigned>(header.count),
                     static_cast<unsigned>(payload_len));
            return false;
        }

        std::vector<chat::contacts::NodeEntry> entries;
        if (!chat::contacts::NodeStoreCore::decodeBlob(entries, payload, payload_len, header.ver))
        {
            out.clear();
            ESP_LOGW(kIdfStoreTag, "node load decode failed path=%s ver=%u",
                     kIdfNodesFile,
                     static_cast<unsigned>(header.ver));
            return false;
        }

        chat::contacts::NodeStoreCore::encodeBlob(out, entries);
        ESP_LOGI(kIdfStoreTag,
                 "node load source=sd path=%s count=%u len=%u",
                 kIdfNodesFile,
                 static_cast<unsigned>(entries.size()),
                 static_cast<unsigned>(out.size()));
        return !out.empty();
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        if (len == 0)
        {
            removeSdFile(kIdfNodesFile);
            return true;
        }
        if (!chat::contacts::isValidNodeBlobSize(len) ||
            chat::contacts::nodeBlobEntryCount(len) > chat::contacts::NodeStoreCore::kMaxNodes)
        {
            return false;
        }

        chat::contacts::NodeStoreSdHeader header =
            chat::contacts::makeNodeStoreSdHeader(data, len);
        std::vector<uint8_t> file(sizeof(header) + len);
        std::memcpy(file.data(), &header, sizeof(header));
        std::memcpy(file.data() + sizeof(header), data, len);
        const bool ok = writeSdFileAtomic(kIdfNodesFile, file.data(), file.size());
        ESP_LOGI(kIdfStoreTag,
                 "node save target=sd path=%s count=%u len=%u ok=%u",
                 kIdfNodesFile,
                 static_cast<unsigned>(header.count),
                 static_cast<unsigned>(len),
                 ok ? 1U : 0U);
        return ok;
    }

    void clearBlob() override
    {
        (void)removeSdFile(kIdfNodesFile);
    }
};

class IdfSdContactBlobStore final : public chat::IContactBlobStore
{
  public:
    bool loadBlob(std::vector<uint8_t>& out) override
    {
        const bool ok = readSdFile(kIdfContactsFile, out);
        if (ok)
        {
            ESP_LOGI(kIdfStoreTag,
                     "contacts load source=sd path=%s len=%u",
                     kIdfContactsFile,
                     static_cast<unsigned>(out.size()));
        }
        return ok;
    }

    bool saveBlob(const uint8_t* data, size_t len) override
    {
        const bool ok = writeSdFileAtomic(kIdfContactsFile, data, len);
        ESP_LOGI(kIdfStoreTag,
                 "contacts save target=sd path=%s len=%u ok=%u",
                 kIdfContactsFile,
                 static_cast<unsigned>(len),
                 ok ? 1U : 0U);
        return ok;
    }
};

class IdfNullMeshAdapter final : public chat::IMeshAdapter
{
  public:
    bool sendText(chat::ChannelId channel,
                  const std::string& text,
                  chat::MessageId* out_msg_id,
                  chat::NodeId peer = 0) override
    {
        (void)channel;
        (void)text;
        (void)peer;
        if (out_msg_id)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    chat::MeshSendResult sendTextDetailed(chat::ChannelId channel,
                                          const std::string& text,
                                          chat::MessageId forced_msg_id = 0,
                                          chat::NodeId peer = 0) override
    {
        (void)channel;
        (void)text;
        (void)peer;
        const chat::MessageId msg_id = forced_msg_id != 0 ? forced_msg_id : next_msg_id_++;
        return chat::MeshSendResult::fail(chat::MeshOperationFailure::NotReady, msg_id);
    }

    bool pollIncomingText(chat::MeshIncomingText* out) override
    {
        (void)out;
        return false;
    }

    bool sendAppData(chat::ChannelId channel,
                     uint32_t portnum,
                     const uint8_t* payload,
                     size_t len,
                     chat::NodeId dest = 0,
                     bool want_ack = false,
                     chat::MessageId packet_id = 0,
                     bool want_response = false) override
    {
        (void)channel;
        (void)portnum;
        (void)payload;
        (void)len;
        (void)dest;
        (void)want_ack;
        (void)packet_id;
        (void)want_response;
        return false;
    }

    bool pollIncomingData(chat::MeshIncomingData* out) override
    {
        (void)out;
        return false;
    }

    chat::NodeId getNodeId() const override
    {
        return self_node_id_;
    }

    void applyConfig(const chat::MeshConfig& config) override
    {
        active_config_ = config;
    }

    void setUserInfo(const char* long_name, const char* short_name) override
    {
        copyString(long_name_, sizeof(long_name_), long_name);
        copyString(short_name_, sizeof(short_name_), short_name);
    }

    void setNetworkLimits(bool duty_cycle_enabled, uint8_t util_percent) override
    {
        duty_cycle_enabled_ = duty_cycle_enabled;
        util_percent_ = util_percent;
    }

    void setPrivacyConfig(uint8_t encrypt_mode) override
    {
        encrypt_mode_ = encrypt_mode;
    }

    bool isReady() const override
    {
        return false;
    }

    bool pollIncomingRawPacket(uint8_t* out_data, size_t& out_len, size_t max_len) override
    {
        (void)out_data;
        (void)max_len;
        out_len = 0;
        return false;
    }

    void setSelfNodeId(chat::NodeId node_id)
    {
        self_node_id_ = node_id;
    }

  private:
    static void copyString(char* dst, size_t dst_len, const char* src)
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
        std::strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
    }

    chat::MessageId next_msg_id_ = 1;
    chat::NodeId self_node_id_ = 0;
    chat::MeshConfig active_config_{};
    char long_name_[32] = {};
    char short_name_[16] = {};
    bool duty_cycle_enabled_ = true;
    uint8_t util_percent_ = 0;
    uint8_t encrypt_mode_ = 1;
};

class IdfAppFacadeRuntime final : public app::IAppFacade
{
  public:
    bool begin(BoardBase& board)
    {
        if (initialized_)
        {
            return true;
        }

        board_ = &board;
        config_ = app::AppConfig{};

        const auto identity = platform::esp::boards::defaultIdentity();
        copyString(config_.node_name, sizeof(config_.node_name), identity.long_name);
        copyString(config_.short_name, sizeof(config_.short_name), identity.short_name);

        mesh_adapter_.setSelfNodeId(resolveSelfNodeId());
        applyMeshConfig();
        applyUserInfo();
        applyNetworkLimits();
        applyPrivacyConfig();

        node_store_.setAutoSaveEnabled(false);
        contact_service_.begin();
        chat_service_.setActiveProtocol(config_.mesh_protocol);
        chat_service_.switchChannel(config_.chat_channel == 1 ? chat::ChannelId::SECONDARY
                                                              : chat::ChannelId::PRIMARY);
        initialized_ = true;
        return true;
    }

    app::AppConfig& getConfig() override { return config_; }
    const app::AppConfig& getConfig() const override { return config_; }

    void saveConfig() override {}

    void applyMeshConfig() override
    {
        mesh_adapter_.applyConfig(config_.activeMeshConfig());
        if (board_)
        {
            board_->applyRadioConfig(config_.mesh_protocol, config_.activeMeshConfig());
        }
    }

    void applyUserInfo() override
    {
        char long_name[sizeof(config_.node_name)] = {};
        char short_name[sizeof(config_.short_name)] = {};
        getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
        mesh_adapter_.setUserInfo(long_name, short_name);
    }

    void applyPositionConfig() override {}

    void applyNetworkLimits() override
    {
        mesh_adapter_.setNetworkLimits(config_.net_duty_cycle, config_.net_channel_util);
    }

    void applyPrivacyConfig() override
    {
        mesh_adapter_.setPrivacyConfig(config_.privacy_encrypt_mode);
    }

    void applyChatDefaults() override
    {
        chat_service_.switchChannel(config_.chat_channel == 1 ? chat::ChannelId::SECONDARY
                                                              : chat::ChannelId::PRIMARY);
    }

    chat::MeshProtocol getMeshProtocol() const override
    {
        return config_.mesh_protocol;
    }

    void getEffectiveUserInfo(char* out_long,
                              std::size_t long_len,
                              char* out_short,
                              std::size_t short_len) const override
    {
        const auto identity = platform::esp::boards::defaultIdentity();
        copyString(out_long,
                   long_len,
                   config_.node_name[0] != '\0' ? config_.node_name : identity.long_name);
        copyString(out_short,
                   short_len,
                   config_.short_name[0] != '\0' ? config_.short_name : identity.short_name);
    }

    bool switchMeshProtocol(chat::MeshProtocol protocol, bool persist = true) override
    {
        config_.mesh_protocol = protocol;
        chat_service_.setActiveProtocol(protocol);
        applyMeshConfig();
        if (persist)
        {
            saveConfig();
        }
        return true;
    }

    chat::ChatService& getChatService() override { return chat_service_; }
    chat::contacts::ContactService& getContactService() override { return contact_service_; }
    chat::IMeshAdapter* getMeshAdapter() override { return &mesh_adapter_; }
    const chat::IMeshAdapter* getMeshAdapter() const override { return &mesh_adapter_; }
    chat::NodeId getSelfNodeId() const override { return mesh_adapter_.getNodeId(); }

    team::TeamController* getTeamController() override { return nullptr; }
    team::TeamPairingService* getTeamPairing() override { return nullptr; }
    team::TeamService* getTeamService() override { return nullptr; }
    const team::TeamService* getTeamService() const override { return nullptr; }
    team::TeamTrackSampler* getTeamTrackSampler() override { return nullptr; }
    void setTeamModeActive(bool active) override { team_mode_active_ = active; }

    void broadcastNodeInfo() override {}

    void clearNodeDb() override
    {
        node_store_.clear();
        node_blob_store_.clearBlob();
    }

    void clearMessageDb() override
    {
        chat_service_.clearAllMessages();
    }

    ble::BleManager* getBleManager() override { return nullptr; }
    const ble::BleManager* getBleManager() const override { return nullptr; }
    bool isBleEnabled() const override { return config_.ble_enabled; }
    void setBleEnabled(bool enabled) override { config_.ble_enabled = enabled; }

    void restartDevice() override
    {
        esp_restart();
    }

    chat::ui::IChatUiRuntime* getChatUiRuntime() override
    {
        return chat_ui_runtime_;
    }

    void setChatUiRuntime(chat::ui::IChatUiRuntime* runtime) override
    {
        chat_ui_runtime_ = runtime;
    }

    BoardBase* getBoard() override { return board_; }
    const BoardBase* getBoard() const override { return board_; }

    void updateCoreServices() override
    {
        mesh_adapter_.processSendQueue();
        chat_service_.processIncoming();
        chat_service_.flushStore();
    }

    void tickEventRuntime() override {}
    void dispatchPendingEvents(std::size_t max_events = 32) override
    {
        (void)max_events;
    }

    bool initialized() const { return initialized_; }

  private:
    static void copyString(char* dst, size_t dst_len, const char* src)
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
        std::strncpy(dst, src, dst_len - 1);
        dst[dst_len - 1] = '\0';
    }

    static chat::NodeId resolveSelfNodeId()
    {
        uint8_t mac[6] = {};
        if (esp_efuse_mac_get_default(mac) == ESP_OK)
        {
            return (static_cast<chat::NodeId>(mac[2]) << 24) |
                   (static_cast<chat::NodeId>(mac[3]) << 16) |
                   (static_cast<chat::NodeId>(mac[4]) << 8) |
                   static_cast<chat::NodeId>(mac[5]);
        }
        return 0x544D5034UL; // "TMP4"
    }

    bool initialized_ = false;
    bool team_mode_active_ = false;
    BoardBase* board_ = nullptr;
    app::AppConfig config_{};
    IdfSdNodeBlobStore node_blob_store_{};
    IdfSdContactBlobStore contact_blob_store_{};
    chat::contacts::NodeStoreCore node_store_{node_blob_store_};
    chat::contacts::ContactStoreCore contact_store_{contact_blob_store_};
    chat::contacts::ContactService contact_service_{node_store_, contact_store_};
    chat::ChatModel chat_model_{};
    chat::RamStore chat_store_{};
    IdfNullMeshAdapter mesh_adapter_{};
    chat::ChatService chat_service_{chat_model_, mesh_adapter_, chat_store_};
    chat::ui::IChatUiRuntime* chat_ui_runtime_ = nullptr;
};

IdfAppFacadeRuntime s_runtime{};
#endif

} // namespace

bool initialize(const platform::esp::boards::AppContextInitHandles& handles,
                const Esp32LvglRuntimeConfig& config)
{
#if defined(ESP_PLATFORM)
    if (!handles.isValid())
    {
        ESP_LOGE(config.log_tag, "IDF AppFacade runtime cannot start without board handles");
        return false;
    }

    if (app::hasAppFacade())
    {
        return true;
    }

    if (!s_runtime.begin(*handles.board))
    {
        ESP_LOGE(config.log_tag, "IDF AppFacade runtime initialization failed for %s", config.target_name);
        return false;
    }

    app::bindAppFacade(s_runtime);
    ESP_LOGI(config.log_tag,
             "IDF AppFacade runtime bound for %s self=%08lX mesh_backend=not_ready",
             config.target_name,
             static_cast<unsigned long>(s_runtime.getSelfNodeId()));
    return true;
#else
    (void)handles;
    (void)config;
    return false;
#endif
}

bool isInitialized()
{
#if defined(ESP_PLATFORM)
    return s_runtime.initialized();
#else
    return false;
#endif
}

} // namespace trailmate::apps::esp32_lvgl::idf_app_facade_runtime
