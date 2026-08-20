#include "platform/esp/arduino_common/app_config_sd_tms_runtime.h"

#include "app/tms_config_codec.h"
#include "platform/esp/arduino_common/app_config_tms_settings_extension.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/settings_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstdio>
#include <cstring>

namespace app::sd_tms
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kConfigDir = "/trailmate";
constexpr const char* kConfigPath = "/trailmate/config.tms";
constexpr const char* kConfigTempPath = "/trailmate/config.tms.tmp";
constexpr const char* kCommitPath = "/trailmate/config.tms.commit";
constexpr const char* kCommitTempPath = "/trailmate/config.tms.commit.tmp";
constexpr const char* kReplicaNamespace = "cfgmirror";
constexpr const char* kReplicaKey = "meta";
constexpr uint32_t kReplicaMagic = 0x544D4352UL; // TMCR
constexpr uint32_t kCommitMagic = 0x544D434DUL;  // TMCM

enum class ReplicaState : uint8_t
{
    NvsCommitted = 1U,
    Synced = 2U,
    // Factory Reset may occur while the SD card is absent.  Keep a tiny NVS
    // tombstone until the next SD sync so an old card cannot repopulate a
    // freshly reset device before its working document is replaced.
    FactoryResetPending = 3U,
};

struct ReplicaMeta
{
    uint32_t magic = kReplicaMagic;
    uint32_t revision = 0U;
    uint32_t last_sd_crc = 0U;
    uint8_t state = static_cast<uint8_t>(ReplicaState::NvsCommitted);
    uint8_t reserved[3]{};
    uint32_t crc = 0U;
};

static_assert(sizeof(ReplicaMeta) <= 24U,
              "NVS SD replica metadata must remain a tiny scalar record");

struct CommitRecord
{
    uint32_t magic = kCommitMagic;
    uint16_t schema = tms::kSchemaVersion;
    uint16_t records = 0U;
    uint32_t file_bytes = 0U;
    uint32_t file_crc = 0U;
    uint32_t crc = 0U;
};

static_assert(sizeof(CommitRecord) <= 24U,
              "SD commit sidecar must remain a tiny scalar record");

struct FileDigest
{
    uint32_t bytes = 0U;
    uint32_t crc = 0U;
    uint16_t records = 0U;
};

// This is deliberately static storage, not a task-stack object and not a
// transient heap allocation.  The SD runtime itself places its file handles in
// PSRAM; keeping this sub-384 byte parser buffer in BSS also keeps boot usable
// on ESP variants without PSRAM and avoids a hidden allocator fallback.
tms::LineScratch s_line_scratch{};
const AppConfig* s_working_config = nullptr;
bool s_sync_pending = false;
bool s_sync_suppressed = false;
uint32_t s_next_sync_attempt_ms = 0U;

class ScopedSyncSuppression
{
  public:
    ScopedSyncSuppression()
        : previous_(s_sync_suppressed)
    {
        s_sync_suppressed = true;
    }

    ~ScopedSyncSuppression() { s_sync_suppressed = previous_; }

  private:
    bool previous_ = false;
};

bool tracksWorkingSettings(const char* ns)
{
    // A batched notification intentionally has no key.  The current ESP
    // multi-key owners are Wi-Fi and cellular and are both working-config
    // fields, so it must be retained rather than dropped.
    return ns == nullptr || std::strcmp(ns, "settings") == 0 ||
           std::strcmp(ns, "power") == 0 || std::strcmp(ns, "a7682e") == 0;
}

void onSettingsStoreChanged(void*, const char* ns, const char*)
{
    if (s_sync_suppressed || !tracksWorkingSettings(ns))
    {
        return;
    }
    // This small NVS marker is the write-ahead record.  If power is lost
    // before the loop can flush SD, boot detects the stale SD replica and
    // retains the newer NVS settings instead of reversing the user's change.
    (void)::app::sd_tms::markNvsCommitted();
    s_sync_pending = true;
    s_next_sync_attempt_ms = 0U;
}

uint32_t crc32Update(uint32_t crc, const uint8_t* data, std::size_t length)
{
    for (std::size_t index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320UL : crc >> 1U;
        }
    }
    return crc;
}

uint32_t crc32(const void* data, std::size_t length)
{
    return crc32Update(0xFFFFFFFFUL,
                       static_cast<const uint8_t*>(data),
                       length) ^
           0xFFFFFFFFUL;
}

bool metaIsValid(const ReplicaMeta& meta)
{
    return meta.magic == kReplicaMagic &&
           (meta.state == static_cast<uint8_t>(ReplicaState::NvsCommitted) ||
            meta.state == static_cast<uint8_t>(ReplicaState::Synced) ||
            meta.state == static_cast<uint8_t>(ReplicaState::FactoryResetPending)) &&
           meta.crc == crc32(&meta, sizeof(meta) - sizeof(meta.crc));
}

bool readMeta(ReplicaMeta* out)
{
    if (!out)
    {
        return false;
    }
    *out = ReplicaMeta{};
    Preferences preferences;
    if (!preferences.begin(kReplicaNamespace, true))
    {
        return false;
    }
    const std::size_t read = preferences.getBytes(kReplicaKey, out, sizeof(*out));
    preferences.end();
    return read == sizeof(*out) && metaIsValid(*out);
}

bool writeMeta(ReplicaMeta meta)
{
    meta.magic = kReplicaMagic;
    meta.crc = crc32(&meta, sizeof(meta) - sizeof(meta.crc));
    Preferences preferences;
    if (!preferences.begin(kReplicaNamespace, false))
    {
        return false;
    }
    const bool written = preferences.putBytes(kReplicaKey, &meta, sizeof(meta)) == sizeof(meta);
    preferences.end();
    return written;
}

bool sdAvailable()
{
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

bool ensureConfigDirectory()
{
    if (::platform::esp::arduino_common::storage::sd_exists(kConfigDir))
    {
        return ::platform::esp::arduino_common::storage::sd_is_directory(kConfigDir);
    }
    return ::platform::esp::arduino_common::storage::sd_mkdir(kConfigDir);
}

bool atomicReplace(const char* temporary_path, const char* destination_path)
{
    if (::platform::esp::arduino_common::storage::sd_exists(destination_path) &&
        !::platform::esp::arduino_common::storage::sd_remove(destination_path))
    {
        return false;
    }
    if (::platform::esp::arduino_common::storage::sd_rename(temporary_path, destination_path))
    {
        return true;
    }
    (void)::platform::esp::arduino_common::storage::sd_remove(temporary_path);
    return false;
}

bool consumePhysicalLine(tms::Decoder& decoder, std::size_t length)
{
    if (length >= sizeof(s_line_scratch.bytes))
    {
        return false;
    }
    if (length > 0U && s_line_scratch.bytes[length - 1U] == '\r')
    {
        --length;
    }
    s_line_scratch.bytes[length] = '\0';
    return decoder.consumeLine(s_line_scratch.bytes);
}

bool readDocument(AppConfig* target, FileDigest* digest, tms::DocumentInfo* info)
{
    if (!digest || !sdAvailable() ||
        !::platform::esp::arduino_common::storage::sd_exists(kConfigPath))
    {
        return false;
    }
    SdRuntimeFile file;
    if (!file.open(kConfigPath, "r"))
    {
        return false;
    }
    const uint64_t file_size = file.size();
    if (file_size == 0U || file_size > tms::kMaxDocumentBytes)
    {
        file.close();
        return false;
    }

    ScopedSyncSuppression suppress_sync;
    settings_extension::beginRead(target != nullptr);
    tms::Decoder decoder(target,
                         tms::DocumentKind::Working,
                         settings_extension::consumeRecord,
                         nullptr,
                         settings_extension::finishDocument);
    uint32_t running_crc = 0xFFFFFFFFUL;
    std::size_t line_length = 0U;
    bool ok = true;
    for (uint64_t offset = 0U; offset < file_size; ++offset)
    {
        const int raw = file.read_byte();
        if (raw < 0)
        {
            ok = false;
            break;
        }
        const uint8_t byte = static_cast<uint8_t>(raw);
        running_crc = crc32Update(running_crc, &byte, 1U);
        if (byte == '\n')
        {
            if (!consumePhysicalLine(decoder, line_length))
            {
                ok = false;
                break;
            }
            line_length = 0U;
            continue;
        }
        if (line_length + 1U >= sizeof(s_line_scratch.bytes))
        {
            ok = false;
            break;
        }
        s_line_scratch.bytes[line_length++] = static_cast<char>(byte);
    }
    if (ok && line_length > 0U)
    {
        ok = consumePhysicalLine(decoder, line_length);
    }
    file.close();
    const bool finished = ok && decoder.finish();
    settings_extension::endRead();
    if (!finished)
    {
        if (info)
        {
            *info = decoder.info();
        }
        return false;
    }
    digest->bytes = static_cast<uint32_t>(file_size);
    digest->crc = running_crc ^ 0xFFFFFFFFUL;
    digest->records = decoder.info().records;
    if (info)
    {
        *info = decoder.info();
    }
    return true;
}

bool commitIsValid(const CommitRecord& record)
{
    return record.magic == kCommitMagic && record.schema == tms::kSchemaVersion &&
           record.crc == crc32(&record, sizeof(record) - sizeof(record.crc));
}

bool readCommit(CommitRecord* out)
{
    if (!out || !sdAvailable() ||
        !::platform::esp::arduino_common::storage::sd_exists(kCommitPath))
    {
        return false;
    }
    SdRuntimeFile file;
    if (!file.open(kCommitPath, "r") || file.size() != sizeof(*out))
    {
        file.close();
        return false;
    }
    const bool read = file.read(out, sizeof(*out)) == static_cast<int>(sizeof(*out));
    file.close();
    return read && commitIsValid(*out);
}

struct FileOutput
{
    SdRuntimeFile* file = nullptr;
    uint32_t running_crc = 0xFFFFFFFFUL;
    uint32_t bytes = 0U;
};

bool writeOutput(void* context, const char* data, std::size_t length)
{
    auto* output = static_cast<FileOutput*>(context);
    if (!output || !output->file || !data || length == 0U ||
        output->file->write(data, length) != length)
    {
        return false;
    }
    output->running_crc = crc32Update(output->running_crc,
                                      reinterpret_cast<const uint8_t*>(data),
                                      length);
    output->bytes += static_cast<uint32_t>(length);
    return true;
}

bool writeCommitTemporary(const CommitRecord& record)
{
    (void)::platform::esp::arduino_common::storage::sd_remove(kCommitTempPath);
    SdRuntimeFile file;
    if (!file.open(kCommitTempPath, "w"))
    {
        return false;
    }
    const bool wrote = file.write(&record, sizeof(record)) == sizeof(record);
    const bool flushed = file.flush();
    file.close();
    if (!wrote || !flushed)
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(kCommitTempPath);
        return false;
    }
    return true;
}

} // namespace

LoadResult loadWorkingConfig(AppConfig& config)
{
    if (!sdAvailable())
    {
        return LoadResult::Unavailable;
    }
    if (!::platform::esp::arduino_common::storage::sd_exists(kConfigPath))
    {
        return LoadResult::Missing;
    }

    FileDigest digest{};
    tms::DocumentInfo info{};
    if (!readDocument(nullptr, &digest, &info))
    {
        std::printf("[AppCfg][SD] config.tms invalid error=%s\n",
                    tms::decodeErrorName(info.error));
        return LoadResult::Invalid;
    }

    ReplicaMeta meta{};
    const bool has_meta = readMeta(&meta);
    if (has_meta && meta.state == static_cast<uint8_t>(ReplicaState::FactoryResetPending))
    {
        std::printf("[AppCfg][SD] deferred config.tms after factory reset\n");
        return LoadResult::DeferredToNvs;
    }
    if (has_meta && meta.state == static_cast<uint8_t>(ReplicaState::NvsCommitted) &&
        digest.crc == meta.last_sd_crc)
    {
        std::printf("[AppCfg][SD] deferred stale replica crc=%08lx\n",
                    static_cast<unsigned long>(digest.crc));
        return LoadResult::DeferredToNvs;
    }

    CommitRecord commit{};
    const bool commit_matches = readCommit(&commit) && commit.file_crc == digest.crc &&
                                commit.file_bytes == digest.bytes &&
                                commit.records == digest.records;
    if (!commit_matches)
    {
        std::printf("[AppCfg][SD] config.tms has no matching commit; treating as external edit\n");
    }

    if (!readDocument(&config, &digest, &info))
    {
        // The validation pass already succeeded.  This guards card removal or
        // an I/O error between passes without treating a partially applied
        // document as authoritative on the next boot.
        std::printf("[AppCfg][SD] config.tms changed while applying\n");
        return LoadResult::Invalid;
    }
    std::printf("[AppCfg][SD] applied config.tms crc=%08lx records=%u source=%s\n",
                static_cast<unsigned long>(digest.crc),
                static_cast<unsigned>(digest.records),
                commit_matches ? "committed" : "external");
    return LoadResult::Applied;
}

void bindWorkingConfig(const AppConfig& config)
{
    s_working_config = &config;
    ::platform::ui::settings_store::set_change_observer(onSettingsStoreChanged, nullptr);
}

void serviceWorkingConfig()
{
    if (!s_sync_pending || !s_working_config)
    {
        return;
    }
    const uint32_t now_ms = millis();
    if (s_next_sync_attempt_ms != 0U &&
        static_cast<int32_t>(now_ms - s_next_sync_attempt_ms) < 0)
    {
        return;
    }
    if (syncWorkingConfig(*s_working_config))
    {
        s_sync_pending = false;
        s_next_sync_attempt_ms = 0U;
        return;
    }
    // Card insertion/removal and SD contention are expected states.  Retry at
    // a bounded cadence from the foreground loop rather than from a timer or
    // a settings-store write call.
    s_next_sync_attempt_ms = now_ms + 1000U;
}

bool markNvsCommitted()
{
    ReplicaMeta meta{};
    const bool has_meta = readMeta(&meta);
    if (has_meta && meta.state == static_cast<uint8_t>(ReplicaState::FactoryResetPending))
    {
        // A user may change NVS-backed settings before an absent card returns.
        // Do not let that ordinary write-ahead transition erase the Factory
        // Reset tombstone; only a successful SD replacement may do so.
        return true;
    }
    meta.magic = kReplicaMagic;
    meta.state = static_cast<uint8_t>(ReplicaState::NvsCommitted);
    return writeMeta(meta);
}

bool syncWorkingConfig(const AppConfig& config)
{
    ScopedSyncSuppression suppress_sync;
    if (!sdAvailable())
    {
        return false;
    }
    if (!ensureConfigDirectory())
    {
        return false;
    }
    (void)::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
    SdRuntimeFile file;
    if (!file.open(kConfigTempPath, "w"))
    {
        return false;
    }
    FileOutput output{&file};
    tms::DocumentInfo info{};
    const bool wrote = tms::writeDocument(config,
                                          tms::DocumentKind::Working,
                                          {&output, writeOutput},
                                          s_line_scratch,
                                          &info,
                                          settings_extension::writeRecords,
                                          nullptr);
    const bool flushed = file.flush();
    file.close();
    if (!wrote || !flushed)
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(kConfigTempPath);
        return false;
    }

    CommitRecord commit{};
    // Encoder accounting includes the magic and END physical lines; the
    // decoder's record count intentionally counts only key/value records.
    commit.records = info.records >= 2U ? static_cast<uint16_t>(info.records - 2U) : 0U;
    commit.file_bytes = output.bytes;
    commit.file_crc = output.running_crc ^ 0xFFFFFFFFUL;
    commit.crc = crc32(&commit, sizeof(commit) - sizeof(commit.crc));
    if (!writeCommitTemporary(commit) || !atomicReplace(kConfigTempPath, kConfigPath) ||
        !atomicReplace(kCommitTempPath, kCommitPath))
    {
        return false;
    }

    ReplicaMeta meta{};
    (void)readMeta(&meta);
    meta.magic = kReplicaMagic;
    ++meta.revision;
    meta.last_sd_crc = commit.file_crc;
    meta.state = static_cast<uint8_t>(ReplicaState::Synced);
    if (!writeMeta(meta))
    {
        return false;
    }
    std::printf("[AppCfg][SD] synchronized config.tms revision=%lu crc=%08lx records=%u\n",
                static_cast<unsigned long>(meta.revision),
                static_cast<unsigned long>(meta.last_sd_crc),
                static_cast<unsigned>(commit.records));
    s_sync_pending = false;
    s_next_sync_attempt_ms = 0U;
    return true;
}

void beginWorkingConfigReset()
{
    s_sync_suppressed = true;
}

void endWorkingConfigReset()
{
    s_sync_suppressed = false;
    s_sync_pending = false;
    s_next_sync_attempt_ms = 0U;
}

bool resetWorkingConfig()
{
    s_sync_pending = false;
    s_next_sync_attempt_ms = 0U;
    if (!sdAvailable())
    {
        ReplicaMeta meta{};
        (void)readMeta(&meta);
        meta.magic = kReplicaMagic;
        ++meta.revision;
        meta.last_sd_crc = 0U;
        meta.state = static_cast<uint8_t>(ReplicaState::FactoryResetPending);
        return writeMeta(meta);
    }
    bool ok = true;
    const char* paths[] = {kConfigTempPath, kCommitTempPath, kConfigPath, kCommitPath};
    for (const char* path : paths)
    {
        if (::platform::esp::arduino_common::storage::sd_exists(path))
        {
            ok = ::platform::esp::arduino_common::storage::sd_remove(path) && ok;
        }
    }
    if (!ok)
    {
        return false;
    }
    Preferences preferences;
    if (preferences.begin(kReplicaNamespace, false))
    {
        (void)preferences.remove(kReplicaKey);
        preferences.end();
    }
    return true;
}

const char* loadResultName(LoadResult result)
{
    switch (result)
    {
    case LoadResult::Unavailable:
        return "unavailable";
    case LoadResult::Missing:
        return "missing";
    case LoadResult::Invalid:
        return "invalid";
    case LoadResult::DeferredToNvs:
        return "deferred_to_nvs";
    case LoadResult::Applied:
        return "applied";
    }
    return "unknown";
}

} // namespace app::sd_tms
