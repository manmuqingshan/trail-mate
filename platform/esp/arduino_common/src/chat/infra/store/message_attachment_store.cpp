/**
 * @file message_attachment_store.cpp
 * @brief Atomic local attachment snapshots; VMP voice is the first adapter.
 */

#include "platform/esp/arduino_common/chat/infra/store/message_attachment_store.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <cstring>

namespace platform::esp::arduino_common::chat_attachment
{
namespace
{

namespace vmp = ::chat::voice::vmp;
namespace storage = ::platform::esp::arduino_common::storage;

constexpr const char* kAttachmentRoot = "/data/v2/attachments";
constexpr const char* kVoiceRoot = "/data/v2/attachments/voice";
constexpr const char* kVoiceSnapshotPath =
    "/data/v2/attachments/voice/inbox.v1";
constexpr const char* kVoiceSnapshotTempPath =
    "/data/v2/attachments/voice/inbox.v1.tmp";
constexpr const char* kVoiceSnapshotBackupPath =
    "/data/v2/attachments/voice/inbox.v1.bak";
constexpr uint32_t kSnapshotMagic = 0x54414D56UL;       // "VMAT", little-endian.
constexpr uint32_t kSnapshotFooterMagic = 0x454E4456UL; // "VDNE".
constexpr uint16_t kSnapshotSchemaVersion = 1U;
constexpr uint8_t kVoiceCompleteFlag = 0x01U;
constexpr uint8_t kVoiceUnverifiedFlag = 0x02U;

struct SnapshotHeader
{
    uint32_t magic = kSnapshotMagic;
    uint16_t schema = kSnapshotSchemaVersion;
    uint8_t kind = static_cast<uint8_t>(AttachmentKind::Voice);
    uint8_t record_count = 0U;
};

struct VoiceRecordHeader
{
    uint64_t local_id = 0U;
    uint64_t session_id = 0U;
    uint32_t sender_id = 0U;
    uint32_t target_id = 0U;
    uint32_t object_fingerprint = 0U;
    uint32_t received_at_seconds = 0U;
    uint16_t encoded_media_len = 0U;
    uint8_t codec = 0U;
    uint8_t mode = 0U;
    uint8_t flags = 0U;
    uint8_t reserved[3] = {};
    uint32_t media_crc32 = 0U;
};

struct SnapshotFooter
{
    uint32_t magic = kSnapshotFooterMagic;
    uint32_t payload_crc32 = 0U;
};

static_assert(sizeof(SnapshotHeader) == 8U,
              "VMP attachment snapshot header must stay compact");
static_assert(sizeof(VoiceRecordHeader) == 48U,
              "VMP attachment record ABI is part of the local schema");
static_assert(sizeof(SnapshotFooter) == 8U,
              "VMP attachment snapshot footer must stay compact");

uint32_t crc32Update(uint32_t crc, const uint8_t* data, std::size_t size)
{
    while (data && size-- != 0U)
    {
        crc ^= *data++;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    return crc;
}

uint32_t crc32(const uint8_t* data, std::size_t size)
{
    return crc32Update(0xFFFFFFFFUL, data, size) ^ 0xFFFFFFFFUL;
}

bool writeExact(storage::SdRuntimeFile* file,
                const void* data,
                std::size_t size)
{
    return file && data && size != 0U && file->write(data, size) == size;
}

bool readExact(storage::SdRuntimeFile* file, void* data, std::size_t size)
{
    return file && data && size != 0U &&
           file->read(data, size) == static_cast<int>(size);
}

bool ensureVoiceLayout()
{
    return storage::sd_card_ready() &&
           (storage::sd_exists(kAttachmentRoot) ||
            storage::sd_mkdir(kAttachmentRoot)) &&
           (storage::sd_exists(kVoiceRoot) || storage::sd_mkdir(kVoiceRoot));
}

VoiceRecordHeader makeRecordHeader(const vmp::VoiceMessageMetadata& metadata,
                                   const uint8_t* media)
{
    VoiceRecordHeader record{};
    record.local_id = metadata.local_id;
    record.session_id = metadata.session_id;
    record.sender_id = metadata.sender_id;
    record.target_id = metadata.target_id;
    record.object_fingerprint = metadata.object_fingerprint;
    record.received_at_seconds = metadata.received_at_seconds;
    record.encoded_media_len = metadata.encoded_media_len;
    record.codec = static_cast<uint8_t>(metadata.codec);
    record.mode = static_cast<uint8_t>(metadata.mode);
    record.flags = (metadata.complete ? kVoiceCompleteFlag : 0U) |
                   (metadata.source_unverified ? kVoiceUnverifiedFlag : 0U);
    record.media_crc32 = crc32(media, metadata.encoded_media_len);
    return record;
}

bool decodeRecordMetadata(const VoiceRecordHeader& record,
                          vmp::VoiceMessageMetadata* metadata)
{
    if (!metadata || record.local_id == 0U ||
        record.encoded_media_len == 0U ||
        record.encoded_media_len > vmp::kMaxEncodedMediaSize ||
        (record.flags & kVoiceCompleteFlag) == 0U ||
        record.codec != static_cast<uint8_t>(vmp::Codec::Codec2_1300) ||
        (record.mode != static_cast<uint8_t>(vmp::DeliveryMode::Private) &&
         record.mode != static_cast<uint8_t>(vmp::DeliveryMode::Broadcast)))
    {
        return false;
    }

    metadata->local_id = record.local_id;
    metadata->session_id = record.session_id;
    metadata->sender_id = record.sender_id;
    metadata->target_id = record.target_id;
    metadata->object_fingerprint = record.object_fingerprint;
    metadata->received_at_seconds = record.received_at_seconds;
    metadata->encoded_media_len = record.encoded_media_len;
    metadata->codec = static_cast<vmp::Codec>(record.codec);
    metadata->mode = static_cast<vmp::DeliveryMode>(record.mode);
    metadata->source_unverified = (record.flags & kVoiceUnverifiedFlag) != 0U;
    metadata->complete = true;
    return true;
}

bool replaceCommittedSnapshot()
{
    if (storage::sd_exists(kVoiceSnapshotBackupPath) &&
        !storage::sd_remove(kVoiceSnapshotBackupPath))
    {
        return false;
    }

    bool moved_current = false;
    if (storage::sd_exists(kVoiceSnapshotPath))
    {
        if (!storage::sd_rename(kVoiceSnapshotPath, kVoiceSnapshotBackupPath))
        {
            return false;
        }
        moved_current = true;
    }

    if (storage::sd_rename(kVoiceSnapshotTempPath, kVoiceSnapshotPath))
    {
        // Keep the last known-good generation after a successful commit.
        // Besides covering power loss between the two renames, this lets boot
        // recovery reject a corrupt primary snapshot without discarding all
        // locally retained voice objects.
        return true;
    }

    if (moved_current && !storage::sd_exists(kVoiceSnapshotPath))
    {
        (void)storage::sd_rename(kVoiceSnapshotBackupPath, kVoiceSnapshotPath);
    }
    (void)storage::sd_remove(kVoiceSnapshotTempPath);
    return false;
}

VoiceInboxLoadResult restoreVoiceInboxSnapshot(
    const char* path,
    vmp::VoiceMessageInbox* inbox,
    uint8_t* media_scratch,
    std::size_t media_scratch_size)
{
    if (!path || !inbox || !media_scratch ||
        media_scratch_size < vmp::kMaxEncodedMediaSize)
    {
        return VoiceInboxLoadResult::IoError;
    }

    storage::SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return VoiceInboxLoadResult::IoError;
    }

    SnapshotHeader header{};
    if (!readExact(&file, &header, sizeof(header)) ||
        header.magic != kSnapshotMagic ||
        header.schema != kSnapshotSchemaVersion ||
        header.kind != static_cast<uint8_t>(AttachmentKind::Voice) ||
        header.record_count > vmp::kVoiceInboxCapacity)
    {
        file.close();
        return VoiceInboxLoadResult::Corrupt;
    }

    inbox->clear();
    uint32_t payload_crc = 0xFFFFFFFFUL;
    for (uint8_t index = 0U; index < header.record_count; ++index)
    {
        VoiceRecordHeader record{};
        vmp::VoiceMessageMetadata metadata{};
        if (!readExact(&file, &record, sizeof(record)) ||
            !decodeRecordMetadata(record, &metadata) ||
            !readExact(&file, media_scratch, metadata.encoded_media_len) ||
            crc32(media_scratch, metadata.encoded_media_len) != record.media_crc32 ||
            !inbox->restore(metadata, media_scratch, metadata.encoded_media_len))
        {
            file.close();
            inbox->clear();
            return VoiceInboxLoadResult::Corrupt;
        }
        payload_crc = crc32Update(payload_crc,
                                  reinterpret_cast<const uint8_t*>(&record),
                                  sizeof(record));
        payload_crc = crc32Update(payload_crc,
                                  media_scratch,
                                  metadata.encoded_media_len);
    }

    SnapshotFooter footer{};
    const bool valid_footer = readExact(&file, &footer, sizeof(footer)) &&
                              footer.magic == kSnapshotFooterMagic &&
                              footer.payload_crc32 ==
                                  (payload_crc ^ 0xFFFFFFFFUL) &&
                              file.available() == 0;
    file.close();
    if (!valid_footer)
    {
        inbox->clear();
        return VoiceInboxLoadResult::Corrupt;
    }
    return header.record_count == 0U ? VoiceInboxLoadResult::Empty
                                     : VoiceInboxLoadResult::Restored;
}

} // namespace

bool persistVoiceInbox(const vmp::VoiceMessageInbox& inbox,
                       vmp::VoiceMessageMetadata* metadata_scratch,
                       std::size_t metadata_capacity)
{
    if (!metadata_scratch || metadata_capacity < vmp::kVoiceInboxCapacity ||
        !ensureVoiceLayout())
    {
        return false;
    }

    const std::size_t count = inbox.listMetadata(metadata_scratch,
                                                 vmp::kVoiceInboxCapacity);
    if (count > vmp::kVoiceInboxCapacity)
    {
        return false;
    }

    if (storage::sd_exists(kVoiceSnapshotTempPath))
    {
        (void)storage::sd_remove(kVoiceSnapshotTempPath);
    }

    storage::SdRuntimeFile file;
    if (!file.open(kVoiceSnapshotTempPath, "w"))
    {
        return false;
    }

    SnapshotHeader header{};
    header.record_count = static_cast<uint8_t>(count);
    bool wrote = writeExact(&file, &header, sizeof(header));
    uint32_t payload_crc = 0xFFFFFFFFUL;
    // `listMetadata` is newest-first; serializing oldest-first lets inbox
    // restore rebuild the original presentation order without a second RAM
    // array or storing its private insertion sequence on disk.
    for (std::size_t remaining = count; wrote && remaining > 0U; --remaining)
    {
        const vmp::VoiceMessageMetadata& metadata =
            metadata_scratch[remaining - 1U];
        vmp::VoiceMessageView view{};
        if (!inbox.get(metadata.local_id, &view) ||
            !view.encoded_media ||
            view.metadata.encoded_media_len != metadata.encoded_media_len)
        {
            wrote = false;
            break;
        }

        const VoiceRecordHeader record = makeRecordHeader(view.metadata,
                                                          view.encoded_media);
        wrote = writeExact(&file, &record, sizeof(record)) &&
                writeExact(&file,
                           view.encoded_media,
                           view.metadata.encoded_media_len);
        payload_crc = crc32Update(payload_crc,
                                  reinterpret_cast<const uint8_t*>(&record),
                                  sizeof(record));
        payload_crc = crc32Update(payload_crc,
                                  view.encoded_media,
                                  view.metadata.encoded_media_len);
    }

    SnapshotFooter footer{};
    footer.payload_crc32 = payload_crc ^ 0xFFFFFFFFUL;
    wrote = wrote && writeExact(&file, &footer, sizeof(footer)) && file.flush();
    file.close();
    if (!wrote)
    {
        (void)storage::sd_remove(kVoiceSnapshotTempPath);
        return false;
    }
    return replaceCommittedSnapshot();
}

VoiceInboxLoadResult restoreVoiceInbox(vmp::VoiceMessageInbox* inbox,
                                       uint8_t* media_scratch,
                                       std::size_t media_scratch_size)
{
    if (!inbox || !media_scratch ||
        media_scratch_size < vmp::kMaxEncodedMediaSize)
    {
        return VoiceInboxLoadResult::IoError;
    }
    if (!storage::sd_card_ready())
    {
        return VoiceInboxLoadResult::Unavailable;
    }
    const bool has_primary = storage::sd_exists(kVoiceSnapshotPath);
    const bool has_backup = storage::sd_exists(kVoiceSnapshotBackupPath);
    if (!has_primary && !has_backup)
    {
        return VoiceInboxLoadResult::Empty;
    }

    const VoiceInboxLoadResult primary_result =
        has_primary
            ? restoreVoiceInboxSnapshot(kVoiceSnapshotPath,
                                        inbox,
                                        media_scratch,
                                        media_scratch_size)
            : VoiceInboxLoadResult::Corrupt;
    if (primary_result == VoiceInboxLoadResult::Restored ||
        primary_result == VoiceInboxLoadResult::Empty)
    {
        return primary_result;
    }

    if (!has_backup)
    {
        return primary_result;
    }

    const VoiceInboxLoadResult backup_result = restoreVoiceInboxSnapshot(
        kVoiceSnapshotBackupPath, inbox, media_scratch, media_scratch_size);
    if (backup_result == VoiceInboxLoadResult::Restored ||
        backup_result == VoiceInboxLoadResult::Empty)
    {
        return backup_result;
    }
    return has_primary ? primary_result : backup_result;
}

} // namespace platform::esp::arduino_common::chat_attachment
