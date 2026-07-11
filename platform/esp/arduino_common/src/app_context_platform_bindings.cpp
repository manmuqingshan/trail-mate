#include "platform/esp/arduino_common/app_context_platform_bindings.h"
#include "platform/esp/arduino_common/app_config_store.h"

#include <Arduino.h>

#include "app/app_facades.h"
#include "board/GpsBoard.h"
#include "board/MotionBoard.h"
#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/infra/store/ram_store.h"
#include "chat/usecase/contact_service.h"
#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"
#include "platform/esp/arduino_common/chat/infra/contact_store.h"
#include "platform/esp/arduino_common/chat/infra/mesh_adapter_router.h"
#include "platform/esp/arduino_common/chat/infra/meshtastic/node_store.h"
#include "platform/esp/arduino_common/chat/infra/protocol_factory.h"
#include "platform/esp/arduino_common/chat/infra/store/sd_store.h"
#include "platform/esp/arduino_common/device_identity.h"
#include "platform/esp/arduino_common/gps/gps_service.h"
#include "platform/esp/arduino_common/gps/track_recorder.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/arduino_common/team/crypto/team_crypto.h"
#include "platform/esp/arduino_common/team/event/team_app_data_event_bus_bridge.h"
#include "platform/esp/arduino_common/team/event/team_event_bus_sink.h"
#include "platform/esp/arduino_common/team/event/team_pairing_event_bus_sink.h"
#include "platform/esp/arduino_common/team_platform_bundle.h"
#include "platform/esp/common/shared_spi_lock.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "team/usecase/team_controller.h"
#include "team/usecase/team_track_sampler.h"
#include "ui/ui_common.h"

#include "freertos/FreeRTOS.h"

#include <new>
#include <vector>

namespace
{

constexpr TickType_t kMeshPeerDirectorySdWait = pdMS_TO_TICKS(50);
constexpr const char* kMeshPeerDirectoryDir = "/mesh";
constexpr const char* kMeshPeerDirectoryPath = "/mesh/peers.bin";
constexpr const char* kMeshPeerDirectoryTempPath = "/mesh/peers.tmp";
constexpr std::size_t kMeshPeerDirectoryMaxBlobBytes = 768U * 1024U;

gps::GpsReceiverInitConfig make_receiver_init_config(const app::AppConfig& config)
{
    gps::GpsReceiverInitConfig init{};
    init.baud = config.gps_init_baud;
    init.probe_ms = config.gps_init_probe_ms;
    init.profile = config.gps_init_profile;
    init.rxm_policy = config.gps_init_rxm_policy;
    init.gnss_policy = config.gps_init_gnss_policy;
    init.nmea_policy = config.gps_init_nmea_policy;
    return init;
}

std::unique_ptr<chat::IMeshAdapter> create_mesh_runtime()
{
    return std::unique_ptr<chat::IMeshAdapter>(new chat::MeshAdapterRouter());
}

void init_gps_runtime(GpsBoard* gps_board,
                      MotionBoard* motion_board,
                      uint32_t disable_hw_init,
                      const app::AppConfig& config)
{
    if (!gps_board || !motion_board)
    {
        return;
    }

    auto& gps_service = gps::GpsService::getInstance();
    gps_service.begin(*gps_board,
                      *motion_board,
                      disable_hw_init,
                      config.gps_interval_ms,
                      config.motion_config,
                      make_receiver_init_config(config));
    gps_service.setEnabled(config.gps_enabled);
    gps_service.setCollectionInterval(config.gps_interval_ms);
    gps_service.setPowerStrategy(config.gps_strategy);
    gps_service.setGnssConfig(config.gps_mode, config.gps_sat_mask);
    gps_service.setExternalNmeaConfig(config.external_nmea_output_hz, config.external_nmea_sentence_mask);
}

void apply_position_config(const app::AppConfig& config)
{
    gps::GpsService::getInstance().setReceiverInitConfig(make_receiver_init_config(config));
    gps::GpsService::getInstance().setEnabled(config.gps_enabled);
    gps::GpsService::getInstance().setCollectionInterval(config.gps_interval_ms);
    gps::GpsService::getInstance().setPowerStrategy(config.gps_strategy);
    gps::GpsService::getInstance().setGnssConfig(config.gps_mode, config.gps_sat_mask);
    gps::GpsService::getInstance().setExternalNmeaConfig(config.external_nmea_output_hz,
                                                         config.external_nmea_sentence_mask);
}

void init_track_recorder(const app::AppConfig& config)
{
    auto& recorder = gps::TrackRecorder::getInstance();
    recorder.setFormat(static_cast<gps::TrackFormat>(config.map_track_format));
    if (config.map_track_interval == 99)
    {
        recorder.setDistanceOnly(true);
        recorder.setIntervalSeconds(0);
    }
    else
    {
        recorder.setDistanceOnly(false);
        recorder.setIntervalSeconds(static_cast<uint32_t>(config.map_track_interval));
    }
    if (recorder.restoreActiveSession())
    {
        Serial.printf("[Tracker] active session restored path=%s\n",
                      recorder.currentPath().c_str());
    }
    recorder.setAutoRecording(config.map_track_enabled);
}

void set_team_mode_active(bool active)
{
    gps::GpsService::getInstance().setTeamModeActive(active);
}

std::unique_ptr<chat::IChatStore> create_chat_store()
{
    if (::platform::esp::arduino_common::storage::sd_card_ready())
    {
        std::unique_ptr<chat::SdStore> sd_store(new chat::SdStore());
        if (sd_store && sd_store->isReady())
        {
            Serial.printf("[AppContext] chat store=SdStore backend=%s layout=/chat/index.bin+/chat/*.log\n",
                          ::platform::esp::arduino_common::storage::sd_card_backend_name());
            return std::unique_ptr<chat::IChatStore>(sd_store.release());
        }
        Serial.printf("[AppContext] chat store=RamStore reason=sd_store_unavailable\n");
        return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
    }

    Serial.printf("[AppContext] chat store=RamStore reason=sd_not_ready\n");
    return std::unique_ptr<chat::IChatStore>(new chat::RamStore());
}

bool ensure_mesh_peer_directory_dir()
{
    using namespace ::platform::esp::arduino_common::storage;
    return sd_exists(kMeshPeerDirectoryDir) || sd_mkdir(kMeshPeerDirectoryDir);
}

class EspSdMeshPeerDirectoryBlobStore final
    : public chat::IMeshPeerDirectoryBlobStore
{
  public:
    chat::MeshPeerDirectoryBlobLoadResult loadBlob(
        std::vector<uint8_t>& out) override
    {
        using namespace ::platform::esp::arduino_common::storage;
        out.clear();
        if (!sd_card_ready())
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Unavailable;
        }
        if (!sd_exists(kMeshPeerDirectoryPath))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Missing;
        }

        ::platform::esp::common::SharedSpiLockGuard spi_guard(
            kMeshPeerDirectorySdWait,
            "mesh_peer_dir_load");
        if (!spi_guard.locked())
        {
            return chat::MeshPeerDirectoryBlobLoadResult::Unavailable;
        }

        SdRuntimeFile file;
        if (!file.open(kMeshPeerDirectoryPath, "r"))
        {
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }
        const uint64_t size = file.size();
        if (size == 0 || size > kMeshPeerDirectoryMaxBlobBytes)
        {
            file.close();
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }
        out.resize(static_cast<std::size_t>(size));
        const int read = file.read(out.data(), out.size());
        file.close();
        if (read < 0 || static_cast<std::size_t>(read) != out.size())
        {
            out.clear();
            return chat::MeshPeerDirectoryBlobLoadResult::IoError;
        }
        return chat::MeshPeerDirectoryBlobLoadResult::Loaded;
    }

    bool saveBlob(const uint8_t* data, std::size_t len) override
    {
        using namespace ::platform::esp::arduino_common::storage;
        if (!sd_card_ready() || (!data && len > 0) ||
            len > kMeshPeerDirectoryMaxBlobBytes || !ensure_mesh_peer_directory_dir())
        {
            return false;
        }

        ::platform::esp::common::SharedSpiLockGuard spi_guard(
            kMeshPeerDirectorySdWait,
            "mesh_peer_dir_save");
        if (!spi_guard.locked())
        {
            return false;
        }

        if (sd_exists(kMeshPeerDirectoryTempPath))
        {
            sd_remove(kMeshPeerDirectoryTempPath);
        }

        SdRuntimeFile file;
        if (!file.open(kMeshPeerDirectoryTempPath, "w"))
        {
            return false;
        }
        bool ok = true;
        if (len > 0)
        {
            ok = file.write(data, len) == len;
        }
        file.close();
        if (!ok)
        {
            sd_remove(kMeshPeerDirectoryTempPath);
            return false;
        }

        if (sd_exists(kMeshPeerDirectoryPath))
        {
            sd_remove(kMeshPeerDirectoryPath);
        }
        if (!sd_rename(kMeshPeerDirectoryTempPath, kMeshPeerDirectoryPath))
        {
            sd_remove(kMeshPeerDirectoryTempPath);
            return false;
        }
        return true;
    }

    void clearBlob() override
    {
        using namespace ::platform::esp::arduino_common::storage;
        if (!sd_card_ready())
        {
            return;
        }
        if (sd_exists(kMeshPeerDirectoryTempPath))
        {
            sd_remove(kMeshPeerDirectoryTempPath);
        }
        if (sd_exists(kMeshPeerDirectoryPath))
        {
            sd_remove(kMeshPeerDirectoryPath);
        }
    }
};

class EspSdMeshPeerDirectory final : public chat::IMeshPeerDirectory
{
  public:
    EspSdMeshPeerDirectory()
        : core_(blob_store_)
    {
    }

    chat::MeshPeerDirectoryStatus begin() override
    {
        return core_.begin();
    }

    chat::MeshPeerDirectoryStatus record(
        const chat::MeshPeerRecord& record) override
    {
        return core_.record(record);
    }

    chat::MeshPeerDirectoryStatus find(
        const chat::MeshPeerIdentity& identity,
        chat::MeshPeerRecord& out_record) override
    {
        return core_.find(identity, out_record);
    }

    chat::MeshPeerDirectoryStatus findByNodeId(
        chat::MeshProtocol protocol,
        chat::NodeId node_id,
        chat::MeshPeerRecord& out_record) override
    {
        return core_.findByNodeId(protocol, node_id, out_record);
    }

    chat::MeshPeerDirectoryStatus loadRecent(
        chat::MeshProtocol protocol,
        chat::MeshPeerRecord* out_records,
        std::size_t max_records,
        std::size_t* out_count) override
    {
        return core_.loadRecent(protocol, out_records, max_records, out_count);
    }

    chat::MeshPeerDirectoryStatus search(chat::MeshProtocol protocol,
                                         const char* query,
                                         chat::MeshPeerRecord* out_records,
                                         std::size_t max_records,
                                         std::size_t* out_count) override
    {
        return core_.search(protocol, query, out_records, max_records, out_count);
    }

    chat::MeshPeerDirectoryStatus setUserFlags(
        const chat::MeshPeerIdentity& identity,
        const chat::MeshPeerUserFlags& flags) override
    {
        return core_.setUserFlags(identity, flags);
    }

    chat::MeshPeerDirectoryStatus remove(
        const chat::MeshPeerIdentity& identity) override
    {
        return core_.remove(identity);
    }

    chat::MeshPeerDirectoryStatus clearProtocol(
        chat::MeshProtocol protocol) override
    {
        return core_.clearProtocol(protocol);
    }

    chat::MeshPeerDirectoryCapacity capacityFor(
        chat::MeshProtocol protocol) const override
    {
        return core_.capacityFor(protocol);
    }

    chat::MeshPeerDirectoryStatus flush() override
    {
        return core_.flush();
    }

  private:
    EspSdMeshPeerDirectoryBlobStore blob_store_;
    chat::MeshPeerDirectoryCore core_;
};

std::unique_ptr<chat::IMeshPeerDirectory> create_mesh_peer_directory()
{
    std::unique_ptr<chat::IMeshPeerDirectory> directory(
        new (std::nothrow) EspSdMeshPeerDirectory());
    if (!directory)
    {
        return directory;
    }
    const auto status = directory->begin();
    Serial.printf("[MeshPeerDirectory] backend=sd path=%s status=%u\n",
                  kMeshPeerDirectoryPath,
                  static_cast<unsigned>(status.code));
    return directory;
}

std::unique_ptr<chat::IMeshAdapter> create_mesh_backend(chat::MeshProtocol protocol,
                                                        LoraBoard& lora_board,
                                                        chat::IMeshPeerDirectory* peer_directory)
{
    return chat::ProtocolFactory::createAdapter(protocol, lora_board, peer_directory);
}

app::ContactServicesBundle create_contact_services()
{
    app::ContactServicesBundle bundle;
    bundle.node_store = std::unique_ptr<chat::contacts::INodeStore>(new chat::meshtastic::NodeStore());
    bundle.contact_store = std::unique_ptr<chat::contacts::IContactStore>(new chat::contacts::ContactStore());
    if (!bundle.node_store || !bundle.contact_store)
    {
        return bundle;
    }

    bundle.service = std::unique_ptr<chat::contacts::ContactService>(
        new chat::contacts::ContactService(*bundle.node_store, *bundle.contact_store));
    if (bundle.service)
    {
        bundle.service->begin();
        Serial.printf("[ContactService] startup nodes=%u nicknames=%u\n",
                      static_cast<unsigned>(bundle.node_store->getEntries().size()),
                      static_cast<unsigned>(bundle.contact_store->getCount()));
    }
    return bundle;
}

std::unique_ptr<chat::ChatService::IncomingMessageObserver> create_chat_message_observer(chat::ChatService& service)
{
    return std::unique_ptr<chat::ChatService::IncomingMessageObserver>(new chat::infra::ChatEventBusBridge(service));
}

app::ChatServicesBundle create_chat_services(const app::AppConfig& config,
                                             LoraBoard* lora_board,
                                             bool use_mock_adapter)
{
    (void)use_mock_adapter;

    app::ChatServicesBundle bundle;
    bundle.model = std::unique_ptr<chat::ChatModel>(new chat::ChatModel());
    if (!bundle.model)
    {
        return bundle;
    }
    bundle.model->setPolicy(config.chat_policy);

    bundle.store = create_chat_store();
    bundle.mesh_peer_directory = create_mesh_peer_directory();
    bundle.mesh_runtime = create_mesh_runtime();
    if (!bundle.store || !bundle.mesh_peer_directory || !bundle.mesh_runtime)
    {
        return bundle;
    }

    if (lora_board)
    {
        std::unique_ptr<chat::IMeshAdapter> backend =
            create_mesh_backend(config.mesh_protocol,
                                *lora_board,
                                bundle.mesh_peer_directory.get());
        if (backend)
        {
            backend->applyConfig(config.activeMeshConfig());
            if (!bundle.mesh_runtime->installBackend(config.mesh_protocol, std::move(backend)))
            {
                Serial.printf("[APP] WARNING: Failed to install mesh adapter backend\n");
            }
        }
    }

    bundle.service = std::unique_ptr<chat::ChatService>(
        new chat::ChatService(*bundle.model, *bundle.mesh_runtime, *bundle.store, config.mesh_protocol));
    if (!bundle.service)
    {
        return bundle;
    }

    bundle.incoming_message_observer = create_chat_message_observer(*bundle.service);
    return bundle;
}

std::unique_ptr<team::ITeamCrypto> create_team_crypto()
{
    return std::unique_ptr<team::ITeamCrypto>(new team::infra::TeamCrypto());
}

std::unique_ptr<team::ITeamEventSink> create_team_event_sink()
{
    return std::unique_ptr<team::ITeamEventSink>(new team::infra::TeamEventBusSink());
}

std::unique_ptr<team::TeamService::UnhandledAppDataObserver> create_team_app_data_observer()
{
    return std::unique_ptr<team::TeamService::UnhandledAppDataObserver>(new team::infra::TeamAppDataEventBusBridge());
}

std::unique_ptr<team::ITeamPairingEventSink> create_team_pairing_event_sink()
{
    return std::unique_ptr<team::ITeamPairingEventSink>(new team::infra::TeamPairingEventBusSink());
}

app::TeamServicesBundle create_team_services(chat::IMeshAdapter& mesh_adapter)
{
    app::TeamServicesBundle bundle;
    bundle.crypto = create_team_crypto();
    bundle.event_sink = create_team_event_sink();
    bundle.app_data_observer = create_team_app_data_observer();
    bundle.pairing_event_sink = create_team_pairing_event_sink();
    if (!bundle.crypto || !bundle.event_sink || !bundle.app_data_observer || !bundle.pairing_event_sink)
    {
        return bundle;
    }

    auto platform_bundle = platform::esp::arduino_common::createTeamPlatformBundle(*bundle.pairing_event_sink);
    bundle.runtime = std::move(platform_bundle.runtime);
    bundle.track_source = std::move(platform_bundle.track_source);
    bundle.pairing_transport = std::move(platform_bundle.pairing_transport);
    bundle.pairing_service = std::move(platform_bundle.pairing_service);
    if (!bundle.runtime || !bundle.track_source || !bundle.pairing_transport || !bundle.pairing_service)
    {
        return bundle;
    }

    bundle.service = std::unique_ptr<team::TeamService>(
        new team::TeamService(*bundle.crypto, mesh_adapter, *bundle.event_sink, *bundle.runtime));
    if (!bundle.service)
    {
        return bundle;
    }
    bundle.service->setUnhandledAppDataObserver(bundle.app_data_observer.get());

    bundle.controller = std::unique_ptr<team::TeamController>(new team::TeamController(*bundle.service));
    bundle.track_sampler = std::unique_ptr<team::TeamTrackSampler>(
        new team::TeamTrackSampler(*bundle.runtime, *bundle.track_source));
    return bundle;
}

void finalize_startup(app::IAppFacade& app_facade)
{
    (void)ui_get_timezone_offset_min();

    team::TeamController* team_controller = app_facade.getTeamController();
    if (team_controller)
    {
        team::ui::TeamUiSnapshot snap;
        if (team::ui::team_ui_snapshot_store().load(snap) &&
            snap.has_team_id && snap.has_team_psk && snap.security_round > 0)
        {
            if (team_controller->setKeysFromPsk(snap.team_id,
                                                snap.security_round,
                                                snap.team_psk.data(),
                                                snap.team_psk.size()))
            {
                Serial.printf("[Team] keys restored from store key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
            else
            {
                Serial.printf("[Team] keys restore failed key_id=%lu\n",
                              static_cast<unsigned long>(snap.security_round));
            }
        }
    }

    team::TeamService* team_service = app_facade.getTeamService();
    app_facade.setTeamModeActive(team_service && team_service->hasKeys());
}

chat::NodeId get_self_node_id()
{
    return platform::esp::arduino_common::device_identity::getSelfNodeId();
}

} // namespace

namespace platform::esp::arduino_common
{

app::AppContextPlatformBindings makeAppContextPlatformBindings()
{
    app::AppContextPlatformBindings bindings{};
    bindings.load_app_config = app::loadAppConfig;
    bindings.save_app_config = app::saveAppConfig;
    bindings.load_message_tone_volume = app::loadMessageToneVolume;
    bindings.init_gps_runtime = init_gps_runtime;
    bindings.apply_position_config = apply_position_config;
    bindings.init_track_recorder = init_track_recorder;
    bindings.set_team_mode_active = set_team_mode_active;
    bindings.finalize_startup = finalize_startup;
    bindings.create_chat_services = create_chat_services;
    bindings.create_mesh_backend = create_mesh_backend;
    bindings.create_contact_services = create_contact_services;
    bindings.create_team_services = create_team_services;
    bindings.get_self_node_id = get_self_node_id;
    return bindings;
}

} // namespace platform::esp::arduino_common
