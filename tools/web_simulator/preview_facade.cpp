// In-memory platform composition for the native website preview.
// Chat/contact models and services remain the production implementations.
#include "app/app_facade_access.h"
#include "app/app_config.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
class PreviewRadio final : public chat::IMeshAdapter {
public:
    bool sendText(chat::ChannelId, const std::string&, chat::MessageId* id, chat::NodeId) override { if(id)*id=++next_id; return true; }
    bool pollIncomingText(chat::MeshIncomingText*) override {return false;}
    bool sendAppData(chat::ChannelId,uint32_t,const uint8_t*,size_t,chat::NodeId,bool,chat::MessageId,bool) override {return true;}
    bool pollIncomingData(chat::MeshIncomingData*) override {return false;}
    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override {return true;}
    bool pollIncomingRawPacket(uint8_t*,size_t&,size_t) override {return false;}
    chat::NodeId getNodeId() const override {return 0xa1b35b7c;}
    chat::MeshCapabilities getCapabilities() const override {chat::MeshCapabilities c;c.supports_unicast_text=true;c.supports_node_info=true;return c;}
private: uint32_t next_id=100;
};
class MemoryBlob final : public chat::IMeshPeerDirectoryBlobStore {
public:
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(std::vector<uint8_t>& out) override {out=data;return data.empty()?chat::MeshPeerDirectoryBlobLoadResult::Missing:chat::MeshPeerDirectoryBlobLoadResult::Loaded;}
    bool saveBlob(const uint8_t* bytes,std::size_t size) override {data.assign(bytes,bytes+size);return true;}
    void clearBlob() override {data.clear();}
private: std::vector<uint8_t> data;
};
class PreviewApp final : public app::IAppFacade {
public:
    PreviewApp(): directory(blob),contacts(directory),chat(model,radio,store) {
        directory.beginEmpty();contacts.begin();
        contacts.updateNodeInfo(0x12345678,"ALEX","Alex",8,-94,1789100000);
        contacts.addContact(0x12345678,"Alex");
        contacts.updateNodeInfo(0x23456789,"MORG","Morgan",6,-102,1789100000);
        contacts.addContact(0x23456789,"Morgan");
        std::snprintf(config.node_name,sizeof(config.node_name),"Trail Mate");
        std::snprintf(config.short_name,sizeof(config.short_name),"TM");
    }
    app::AppConfigEdit beginConfigEdit() override {return {&config,nullptr,nullptr,nullptr};}
    void saveConfig() override {}
    void saveConfig(app::AppConfigChangeSet) override {}
    void applyMeshConfig() override {}
    void applyUserInfo() override {}
    void applyPositionConfig() override {}
    void applyNetworkLimits() override {}
    void applyPrivacyConfig() override {}
    void applyChatDefaults() override {}
    chat::MeshProtocol getMeshProtocol() const override {return static_cast<chat::MeshProtocol>(config.mesh_protocol);}
    void getEffectiveUserInfo(char* a,std::size_t n,char* b,std::size_t m) const override {std::snprintf(a,n,"%s",config.node_name);std::snprintf(b,m,"%s",config.short_name);}
    bool switchMeshProtocol(chat::MeshProtocol p,bool) override {config.mesh_protocol=p;contacts.setActiveProtocol(p);return true;}
    chat::ChatService& getChatService() override {return chat;}
    chat::contacts::ContactService& getContactService() override {return contacts;}
    chat::IMeshAdapter* getMeshAdapter() override {return &radio;}
    const chat::IMeshAdapter* getMeshAdapter() const override {return &radio;}
    chat::NodeId getSelfNodeId() const override {return radio.getNodeId();}
    team::TeamController* getTeamController() override {return nullptr;}
    team::TeamPairingService* getTeamPairing() override {return nullptr;}
    team::TeamService* getTeamService() override {return nullptr;}
    const team::TeamService* getTeamService() const override {return nullptr;}
    team::TeamTrackSampler* getTeamTrackSampler() override {return nullptr;}
    void setTeamModeActive(bool) override {}
    void broadcastNodeInfo() override {}
    void clearNodeDb() override {directory.clear();}
    void clearMessageDb() override {store.clearAll();model.clearAll();}
    ble::BleManager* getBleManager() override {return nullptr;}
    const ble::BleManager* getBleManager() const override {return nullptr;}
    bool isBleEnabled() const override {return ble_enabled;}
    void setBleEnabled(bool enabled) override {ble_enabled=enabled;}
    void restartDevice() override {}
    chat::ui::IChatUiRuntime* getChatUiRuntime() override {return ui_runtime;}
    void setChatUiRuntime(chat::ui::IChatUiRuntime* value) override {ui_runtime=value;}
    BoardBase* getBoard() override {return nullptr;}
    const BoardBase* getBoard() const override {return nullptr;}
    void updateCoreServices() override {}
    void tickEventRuntime() override {}
    void dispatchPendingEvents(std::size_t) override {}
protected:
    const app::AppConfig& getConfig() const override {return config;}
private:
    app::AppConfig config{};
    bool ble_enabled=false;
    chat::ui::IChatUiRuntime* ui_runtime=nullptr;
    MemoryBlob blob;
    chat::MeshPeerDirectoryCore directory;
    chat::contacts::ContactService contacts;
    PreviewRadio radio;
    chat::ChatModel model;
    chat::RamStore store;
    chat::ChatService chat;
};
}
void preview_bind_app() {static PreviewApp app; app::bindAppFacade(app);}
