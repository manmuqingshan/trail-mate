#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"

#include "chat/ports/i_mesh_peer_directory.h"
#include "platform/esp/idf_common/bsp_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace platform::ui::reticulum_directory
{
namespace
{

constexpr const char* kReticulumDir = "/trailmate/reticulum";
constexpr const char* kAnnouncesPath = "/trailmate/reticulum/announces.bin";
constexpr const char* kLxmfAddressesPath = "/mesh/peers.bin";
constexpr uint32_t kAnnounceMagic = 0x52414E31UL; // RAN1
constexpr uint16_t kAnnounceVersion = 1;
constexpr std::size_t kMaxAnnounceRecords = 128;
constexpr UBaseType_t kAnnounceQueueDepth = 16;
constexpr uint32_t kAnnounceBatchDelayMs = 250;
constexpr uint32_t kAnnounceWorkerStackBytes = 5U * 1024U;

struct PersistedAnnounce
{
    uint8_t destination_hash[kReticulumHashSize] = {};
    uint8_t identity_hash[kReticulumHashSize] = {};
    uint8_t aspect = 0;
    uint8_t source = 0;
    uint8_t hops = 0xFF;
    uint8_t flags = 0;
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
    char display_name[kReticulumDisplayNameSize] = {};
};

struct AnnounceFileHeader
{
    uint32_t magic = kAnnounceMagic;
    uint16_t version = kAnnounceVersion;
    uint16_t record_size = sizeof(PersistedAnnounce);
    uint32_t count = 0;
    uint32_t checksum = 0;
};

enum class AnnounceLoadResult : uint8_t
{
    Loaded,
    Missing,
    Unavailable,
    IoError,
};

chat::IMeshPeerDirectory* s_mesh_peer_directory = nullptr;
QueueHandle_t s_announce_queue = nullptr;
TaskHandle_t s_announce_worker = nullptr;

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

bool zero_hash(const uint8_t* hash, std::size_t len)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool hash_equal(const uint8_t* lhs, const uint8_t* rhs, std::size_t len)
{
    return lhs && rhs && std::memcmp(lhs, rhs, len) == 0;
}

uint32_t fnv1a32(const void* data, std::size_t len)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t hash = 2166136261UL;
    for (std::size_t index = 0; index < len; ++index)
    {
        hash ^= bytes[index];
        hash *= 16777619UL;
    }
    return hash;
}

bool sd_ready()
{
    return platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready();
}

std::string make_sd_path(const char* relative)
{
    const char* mount =
        platform::esp::idf_common::bsp_runtime::sdcard_mount_point();
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

bool is_regular_file(const char* relative)
{
    if (!sd_ready())
    {
        return false;
    }
    const std::string path = make_sd_path(relative);
    struct stat info = {};
    return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

bool ensure_directory(const char* relative)
{
    const std::string path = make_sd_path(relative);
    struct stat info = {};
    if (::stat(path.c_str(), &info) == 0)
    {
        return S_ISDIR(info.st_mode);
    }
    if (::mkdir(path.c_str(), 0775) == 0)
    {
        return true;
    }
    return ::stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool ensure_reticulum_directory()
{
    return sd_ready() && ensure_directory("/trailmate") &&
           ensure_directory(kReticulumDir);
}

AnnounceLoadResult load_persisted_announces(
    std::vector<PersistedAnnounce>& out)
{
    out.clear();
    if (!sd_ready())
    {
        return AnnounceLoadResult::Unavailable;
    }

    const std::string path = make_sd_path(kAnnouncesPath);
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file)
    {
        return errno == ENOENT ? AnnounceLoadResult::Missing
                               : AnnounceLoadResult::IoError;
    }

    AnnounceFileHeader header{};
    if (std::fread(&header, 1, sizeof(header), file) != sizeof(header) ||
        header.magic != kAnnounceMagic ||
        header.version != kAnnounceVersion ||
        header.record_size != sizeof(PersistedAnnounce) ||
        header.count > kMaxAnnounceRecords)
    {
        std::fclose(file);
        return AnnounceLoadResult::IoError;
    }

    out.resize(header.count);
    const std::size_t payload_len = out.size() * sizeof(PersistedAnnounce);
    if ((payload_len != 0 &&
         std::fread(out.data(), 1, payload_len, file) != payload_len) ||
        std::fclose(file) != 0)
    {
        out.clear();
        return AnnounceLoadResult::IoError;
    }
    if (fnv1a32(out.data(), payload_len) != header.checksum)
    {
        out.clear();
        return AnnounceLoadResult::IoError;
    }
    return AnnounceLoadResult::Loaded;
}

bool save_persisted_announces(const std::vector<PersistedAnnounce>& records)
{
    if (records.size() > kMaxAnnounceRecords ||
        !ensure_reticulum_directory())
    {
        return false;
    }

    const std::string path = make_sd_path(kAnnouncesPath);
    const std::string temp_path = path + ".tmp";
    std::remove(temp_path.c_str());
    FILE* file = std::fopen(temp_path.c_str(), "wb");
    if (!file)
    {
        return false;
    }

    AnnounceFileHeader header{};
    header.count = static_cast<uint32_t>(records.size());
    const std::size_t payload_len = records.size() * sizeof(PersistedAnnounce);
    header.checksum = fnv1a32(records.data(), payload_len);
    bool ok = std::fwrite(&header, 1, sizeof(header), file) == sizeof(header);
    if (ok && payload_len != 0)
    {
        ok = std::fwrite(records.data(), 1, payload_len, file) == payload_len;
    }
    ok = std::fclose(file) == 0 && ok;
    if (!ok)
    {
        std::remove(temp_path.c_str());
        return false;
    }

    std::remove(path.c_str());
    if (std::rename(temp_path.c_str(), path.c_str()) != 0)
    {
        std::remove(temp_path.c_str());
        return false;
    }
    return true;
}

PersistedAnnounce make_persisted_announce(const AnnounceRecord& record)
{
    PersistedAnnounce out{};
    std::memcpy(out.destination_hash,
                record.destination_hash,
                sizeof(out.destination_hash));
    std::memcpy(out.identity_hash,
                record.identity_hash,
                sizeof(out.identity_hash));
    out.aspect = static_cast<uint8_t>(record.aspect);
    out.source = static_cast<uint8_t>(record.source);
    out.hops = record.hops;
    out.flags = static_cast<uint8_t>((record.path_response ? 1U : 0U) |
                                     (record.local_destination ? 2U : 0U) |
                                     (record.delivery ? 4U : 0U) |
                                     (record.propagation ? 8U : 0U));
    out.first_seen_s =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    out.last_seen_s =
        record.last_seen_s != 0 ? record.last_seen_s : out.first_seen_s;
    copy_text(out.display_name, sizeof(out.display_name), record.display_name);
    return out;
}

AnnounceRecord make_announce_record(const PersistedAnnounce& record)
{
    AnnounceRecord out{};
    out.valid = true;
    std::memcpy(out.destination_hash,
                record.destination_hash,
                sizeof(out.destination_hash));
    std::memcpy(out.identity_hash,
                record.identity_hash,
                sizeof(out.identity_hash));
    out.aspect = static_cast<AnnounceAspect>(record.aspect);
    out.source = static_cast<EntrySource>(record.source);
    out.hops = record.hops;
    out.path_response = (record.flags & 1U) != 0;
    out.local_destination = (record.flags & 2U) != 0;
    out.delivery = (record.flags & 4U) != 0;
    out.propagation = (record.flags & 8U) != 0;
    out.first_seen_s = record.first_seen_s;
    out.last_seen_s = record.last_seen_s;
    copy_text(out.display_name, sizeof(out.display_name), record.display_name);
    return out;
}

void upsert_persisted_announce(std::vector<PersistedAnnounce>& records,
                               PersistedAnnounce persisted)
{
    const auto existing = std::find_if(
        records.begin(),
        records.end(),
        [&persisted](const PersistedAnnounce& candidate)
        {
            return hash_equal(candidate.destination_hash,
                              persisted.destination_hash,
                              kReticulumHashSize);
        });
    if (existing != records.end())
    {
        if (existing->first_seen_s != 0 &&
            (persisted.first_seen_s == 0 ||
             existing->first_seen_s < persisted.first_seen_s))
        {
            persisted.first_seen_s = existing->first_seen_s;
        }
        records.erase(existing);
    }
    if (records.size() >= kMaxAnnounceRecords)
    {
        records.erase(records.begin());
    }
    records.push_back(persisted);
}

void announce_worker_entry(void*)
{
    PersistedAnnounce first{};
    for (;;)
    {
        if (xQueueReceive(s_announce_queue, &first, portMAX_DELAY) != pdTRUE)
        {
            continue;
        }

        vTaskDelay(pdMS_TO_TICKS(kAnnounceBatchDelayMs));
        std::vector<PersistedAnnounce> batch;
        batch.reserve(kAnnounceQueueDepth);
        batch.push_back(first);
        PersistedAnnounce pending{};
        while (batch.size() < kAnnounceQueueDepth &&
               xQueueReceive(s_announce_queue, &pending, 0) == pdTRUE)
        {
            batch.push_back(pending);
        }

        std::vector<PersistedAnnounce> records;
        const AnnounceLoadResult loaded = load_persisted_announces(records);
        if (loaded == AnnounceLoadResult::Unavailable ||
            loaded == AnnounceLoadResult::IoError)
        {
            std::printf("[Reticulum][Directory] announce load failed status=%u\n",
                        static_cast<unsigned>(loaded));
            continue;
        }
        for (const PersistedAnnounce& record : batch)
        {
            upsert_persisted_announce(records, record);
        }
        if (!save_persisted_announces(records))
        {
            std::printf("[Reticulum][Directory] announce save failed count=%u\n",
                        static_cast<unsigned>(records.size()));
        }
    }
}

bool ensure_announce_worker()
{
    if (!s_announce_queue)
    {
        s_announce_queue =
            xQueueCreate(kAnnounceQueueDepth, sizeof(PersistedAnnounce));
    }
    if (!s_announce_queue)
    {
        return false;
    }
    if (s_announce_worker)
    {
        return true;
    }

    const BaseType_t created = xTaskCreate(announce_worker_entry,
                                           "rt_announce_io",
                                           kAnnounceWorkerStackBytes,
                                           nullptr,
                                           tskIDLE_PRIORITY + 1,
                                           &s_announce_worker);
    if (created != pdPASS)
    {
        s_announce_worker = nullptr;
        return false;
    }
    return true;
}

chat::MeshPeerSource mesh_source_from_entry_source(EntrySource source)
{
    switch (source)
    {
    case EntrySource::RuntimeRx:
        return chat::MeshPeerSource::RuntimeRx;
    case EntrySource::PathResponse:
        return chat::MeshPeerSource::DiscoveryResponse;
    case EntrySource::Manual:
        return chat::MeshPeerSource::Manual;
    case EntrySource::Import:
        return chat::MeshPeerSource::Import;
    case EntrySource::Unknown:
    default:
        return chat::MeshPeerSource::Unknown;
    }
}

EntrySource entry_source_from_mesh_source(chat::MeshPeerSource source)
{
    switch (source)
    {
    case chat::MeshPeerSource::RuntimeRx:
        return EntrySource::RuntimeRx;
    case chat::MeshPeerSource::DiscoveryResponse:
        return EntrySource::PathResponse;
    case chat::MeshPeerSource::Manual:
        return EntrySource::Manual;
    case chat::MeshPeerSource::Import:
        return EntrySource::Import;
    case chat::MeshPeerSource::Unknown:
    default:
        return EntrySource::Unknown;
    }
}

void init_lxmf_status(Status& out)
{
    out.supported = true;
    out.sd_present = sd_ready();
    out.file_present = s_mesh_peer_directory != nullptr;
}

chat::IMeshPeerDirectory* require_mesh_peer_directory(Status& out)
{
    init_lxmf_status(out);
    if (!s_mesh_peer_directory)
    {
        set_status(out, "Mesh peer directory unavailable", kLxmfAddressesPath);
        return nullptr;
    }
    return s_mesh_peer_directory;
}

void set_mesh_peer_failure(Status& out,
                           chat::MeshPeerDirectoryStatusCode code,
                           const char* not_found_message,
                           const char* invalid_message)
{
    switch (code)
    {
    case chat::MeshPeerDirectoryStatusCode::NotFound:
        set_status(out, not_found_message, kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::InvalidArgument:
        set_status(out, invalid_message, kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::StorageUnavailable:
        set_status(out,
                   "Mesh peer directory storage unavailable",
                   kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::IoError:
        set_status(out, "Cannot access mesh peer directory", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::CapacityExceeded:
        set_status(out, "Mesh peer directory full", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Unsupported:
        set_status(out, "Mesh peer directory unsupported", kLxmfAddressesPath);
        break;
    case chat::MeshPeerDirectoryStatusCode::Ok:
    default:
        set_status(out, "Mesh peer directory failed", kLxmfAddressesPath);
        break;
    }
}

bool lxmf_to_mesh_peer(const LxmfAddressRecord& record,
                       chat::MeshPeerRecord& out)
{
    out = chat::MeshPeerRecord{};
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize) ||
        zero_hash(record.enc_pub, kReticulumPublicKeySize) ||
        zero_hash(record.sig_pub, kReticulumPublicKeySize))
    {
        return false;
    }

    const chat::ReticulumPeerIdentity identity =
        chat::makeReticulumPeerIdentity(record.destination_hash,
                                        record.identity_hash);
    out.valid = true;
    out.identity = chat::makeMeshPeerReticulumIdentity(identity);
    out.source = mesh_source_from_entry_source(record.source);
    out.first_seen_s =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    out.last_seen_s =
        record.last_seen_s != 0 ? record.last_seen_s : out.first_seen_s;
    chat::copyMeshPeerText(out.display_name,
                           sizeof(out.display_name),
                           record.display_name);
    out.flags.favorite = record.favorite;
    out.flags.ignored = record.ignored;
    out.flags.trusted = record.trusted;
    out.reticulum.identity = identity;
    out.reticulum.has_public_keys = true;
    std::memcpy(out.reticulum.enc_pub,
                record.enc_pub,
                sizeof(out.reticulum.enc_pub));
    std::memcpy(out.reticulum.sig_pub,
                record.sig_pub,
                sizeof(out.reticulum.sig_pub));
    out.reticulum.delivery = true;
    return chat::meshPeerRecordIsValid(out);
}

bool mesh_peer_to_lxmf(const chat::MeshPeerRecord& record,
                       LxmfAddressRecord& out)
{
    out = LxmfAddressRecord{};
    if (!chat::meshPeerRecordIsValid(record) ||
        !chat::meshPeerIsReticulumProtocol(record.identity.protocol))
    {
        return false;
    }

    const chat::ReticulumPeerIdentity identity =
        record.reticulum.identity.valid ? record.reticulum.identity
                                        : record.identity.reticulum;
    if (!identity.valid ||
        zero_hash(identity.destination_hash, kReticulumHashSize))
    {
        return false;
    }

    out.valid = true;
    std::memcpy(out.destination_hash,
                identity.destination_hash,
                sizeof(out.destination_hash));
    std::memcpy(out.identity_hash,
                identity.identity_hash,
                sizeof(out.identity_hash));
    if (record.reticulum.has_public_keys)
    {
        std::memcpy(out.enc_pub,
                    record.reticulum.enc_pub,
                    sizeof(out.enc_pub));
        std::memcpy(out.sig_pub,
                    record.reticulum.sig_pub,
                    sizeof(out.sig_pub));
    }
    copy_text(out.display_name, sizeof(out.display_name), record.display_name);
    out.favorite = record.flags.favorite;
    out.ignored = record.flags.ignored;
    out.trusted = record.flags.trusted;
    out.source = entry_source_from_mesh_source(record.source);
    out.first_seen_s = record.first_seen_s;
    out.last_seen_s = record.last_seen_s;
    return true;
}

Status record_lxmf_address_impl(const LxmfAddressRecord& record)
{
    Status out{};
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }

    chat::MeshPeerRecord mesh_record{};
    if (!lxmf_to_mesh_peer(record, mesh_record))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    chat::MeshPeerUserFlags merged_flags = mesh_record.flags;
    chat::MeshPeerRecord existing{};
    if (directory->find(mesh_record.identity, existing).succeeded())
    {
        merged_flags.favorite = existing.flags.favorite || record.favorite;
        merged_flags.ignored = existing.flags.ignored || record.ignored;
        merged_flags.trusted = existing.flags.trusted || record.trusted;
    }

    const chat::MeshPeerDirectoryStatus status = directory->record(mesh_record);
    if (!status.succeeded())
    {
        set_mesh_peer_failure(out,
                              status.code,
                              "LXMF address not found",
                              "Invalid LXMF address");
        return out;
    }
    if (record.favorite || record.ignored || record.trusted)
    {
        const chat::MeshPeerDirectoryStatus flag_status =
            directory->setUserFlags(mesh_record.identity, merged_flags);
        if (!flag_status.succeeded())
        {
            set_mesh_peer_failure(out,
                                  flag_status.code,
                                  "LXMF address not found",
                                  "Invalid LXMF address");
            return out;
        }
    }

    out.saved = true;
    out.loaded = true;
    set_status(out, "LXMF address saved", kLxmfAddressesPath);
    return out;
}

Status load_lxmf_records(const char* query,
                         LxmfAddressRecord* out_records,
                         std::size_t max_records,
                         std::size_t* out_count)
{
    Status out{};
    if (out_count)
    {
        *out_count = 0;
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out,
                   "LXMF address storage unavailable",
                   kLxmfAddressesPath);
        return out;
    }

    std::vector<chat::MeshPeerRecord> records(max_records);
    std::size_t loaded_count = 0;
    const bool searching = query && query[0] != '\0';
    const chat::MeshPeerDirectoryStatus status =
        searching
            ? directory->search(chat::MeshProtocol::Reticulum,
                                query,
                                records.data(),
                                records.size(),
                                &loaded_count)
            : directory->loadRecent(chat::MeshProtocol::Reticulum,
                                    records.data(),
                                    records.size(),
                                    &loaded_count);
    if (!status.succeeded())
    {
        set_mesh_peer_failure(out,
                              status.code,
                              searching ? "No LXMF address matches"
                                        : "No LXMF addresses",
                              "LXMF address storage unavailable");
        return out;
    }

    std::size_t count = 0;
    for (std::size_t index = 0;
         index < loaded_count && count < max_records;
         ++index)
    {
        LxmfAddressRecord converted{};
        if (mesh_peer_to_lxmf(records[index], converted))
        {
            out_records[count++] = converted;
        }
    }
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0
                   ? (searching ? "No LXMF address matches"
                                : "No LXMF addresses")
                   : (searching ? "LXMF address matches loaded"
                                : "LXMF addresses loaded"),
               kLxmfAddressesPath);
    return out;
}

} // namespace

const char* announces_path()
{
    return kAnnouncesPath;
}

const char* lxmf_addresses_path()
{
    return kLxmfAddressesPath;
}

void bind_mesh_peer_directory(chat::IMeshPeerDirectory* directory)
{
    s_mesh_peer_directory = directory;
}

Status record_announce(const AnnounceRecord& record)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_ready();
    out.file_present = is_regular_file(kAnnouncesPath);
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid Reticulum announce", kAnnouncesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kAnnouncesPath);
        return out;
    }

    if (!ensure_announce_worker())
    {
        set_status(out,
                   "Reticulum announce queue unavailable",
                   kAnnouncesPath);
        return out;
    }

    const PersistedAnnounce persisted = make_persisted_announce(record);
    if (xQueueSend(s_announce_queue, &persisted, 0) != pdTRUE)
    {
        set_status(out, "Reticulum announce queue full", kAnnouncesPath);
        return out;
    }

    out.saved = true;
    set_status(out, "Reticulum announce queued", kAnnouncesPath);
    return out;
}

Status record_lxmf_address(const LxmfAddressRecord& record)
{
    return record_lxmf_address_impl(record);
}

Status record_lxmf_address_now(const LxmfAddressRecord& record)
{
    return record_lxmf_address_impl(record);
}

Status set_lxmf_address_favorite_now(
    const uint8_t destination_hash[kReticulumHashSize],
    bool favorite)
{
    Status out{};
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!destination_hash || zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    const chat::ReticulumPeerIdentity reticulum_identity =
        chat::makeReticulumDestinationIdentity(destination_hash);
    const chat::MeshPeerIdentity identity =
        chat::makeMeshPeerReticulumIdentity(reticulum_identity);
    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus find_status =
        directory->find(identity, record);
    if (!find_status.succeeded())
    {
        set_mesh_peer_failure(out,
                              find_status.code,
                              "LXMF address not found",
                              "Invalid LXMF address");
        return out;
    }

    chat::MeshPeerUserFlags flags = record.flags;
    flags.favorite = favorite;
    const chat::MeshPeerDirectoryStatus flag_status =
        directory->setUserFlags(record.identity, flags);
    if (!flag_status.succeeded())
    {
        set_mesh_peer_failure(out,
                              flag_status.code,
                              "LXMF address not found",
                              "Invalid LXMF address");
        return out;
    }
    out.saved = true;
    out.loaded = true;
    set_status(out, "LXMF address saved", kLxmfAddressesPath);
    return out;
}

Status load_announces(AnnounceRecord* out_records,
                      std::size_t max_records,
                      std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = sd_ready();
    out.file_present = is_regular_file(kAnnouncesPath);
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out,
                   "Reticulum announce storage unavailable",
                   kAnnouncesPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kAnnouncesPath);
        return out;
    }

    std::vector<PersistedAnnounce> records;
    const AnnounceLoadResult loaded = load_persisted_announces(records);
    if (loaded == AnnounceLoadResult::IoError ||
        loaded == AnnounceLoadResult::Unavailable)
    {
        set_status(out, "Cannot read Reticulum announces", kAnnouncesPath);
        return out;
    }

    const std::size_t count = std::min(max_records, records.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        out_records[index] =
            make_announce_record(records[records.size() - 1U - index]);
    }
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No Reticulum announces"
                          : "Reticulum announces loaded",
               kAnnouncesPath);
    return out;
}

Status load_lxmf_addresses(LxmfAddressRecord* out_records,
                           std::size_t max_records,
                           std::size_t* out_count)
{
    return load_lxmf_records(nullptr, out_records, max_records, out_count);
}

Status load_lxmf_addresses_matching(const char* query,
                                    LxmfAddressRecord* out_records,
                                    std::size_t max_records,
                                    std::size_t* out_count)
{
    return load_lxmf_records(query, out_records, max_records, out_count);
}

Status find_lxmf_address_by_destination(
    const uint8_t destination_hash[kReticulumHashSize],
    LxmfAddressRecord* out_record)
{
    Status out{};
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (!destination_hash || !out_record ||
        zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }

    const chat::ReticulumPeerIdentity reticulum_identity =
        chat::makeReticulumDestinationIdentity(destination_hash);
    const chat::MeshPeerIdentity identity =
        chat::makeMeshPeerReticulumIdentity(reticulum_identity);
    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus status =
        directory->find(identity, record);
    if (!status.succeeded())
    {
        set_mesh_peer_failure(out,
                              status.code,
                              "LXMF address not found",
                              "Invalid LXMF address");
        return out;
    }
    if (!mesh_peer_to_lxmf(record, *out_record))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }
    out.loaded = true;
    set_status(out, "LXMF address loaded", kLxmfAddressesPath);
    return out;
}

Status find_lxmf_address_by_node_id(uint32_t node_id,
                                    LxmfAddressRecord* out_record)
{
    Status out{};
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    chat::IMeshPeerDirectory* directory = require_mesh_peer_directory(out);
    if (!directory)
    {
        return out;
    }
    if (node_id == 0 || !out_record)
    {
        set_status(out, "Invalid LXMF node", kLxmfAddressesPath);
        return out;
    }

    chat::MeshPeerRecord record{};
    const chat::MeshPeerDirectoryStatus status =
        directory->findByNodeId(chat::MeshProtocol::Reticulum,
                                node_id,
                                record);
    if (!status.succeeded())
    {
        set_mesh_peer_failure(out,
                              status.code,
                              "LXMF address not found",
                              "Invalid LXMF node");
        return out;
    }
    if (!mesh_peer_to_lxmf(record, *out_record))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesPath);
        return out;
    }
    out.loaded = true;
    set_status(out, "LXMF address loaded", kLxmfAddressesPath);
    return out;
}

} // namespace platform::ui::reticulum_directory
