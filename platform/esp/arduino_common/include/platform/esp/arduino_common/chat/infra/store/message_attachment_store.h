/**
 * @file message_attachment_store.h
 * @brief Durable local sidecar storage for non-text chat message payloads.
 *
 * Text stays in SdStore's protocol-partitioned message journal. Large or
 * structured message bodies live here and are linked by a stable local
 * attachment identifier instead of being duplicated into the text journal.
 * The storage layer owns no radio, MQTT, LXMF, or relay operation.
 */

#pragma once

#include "chat/infra/voice/vmp_voice_inbox.h"

#include <cstddef>
#include <cstdint>

namespace platform::esp::arduino_common::chat_attachment
{

/**
 * Stable attachment family identifiers. Voice is the first implemented
 * adapter; Image and Location reserve the common storage contract so they do
 * not grow a protocol-specific persistence path later.
 */
enum class AttachmentKind : uint8_t
{
    Voice = 1U,
    Image = 2U,
    Location = 3U,
};

enum class VoiceInboxLoadResult : uint8_t
{
    Restored = 1U,
    Empty = 2U,
    Unavailable = 3U,
    Corrupt = 4U,
    IoError = 5U,
};

/**
 * Writes an atomic snapshot of the local voice attachment index and payloads.
 * The caller supplies PSRAM-backed metadata and byte scratch storage so the
 * persistence path cannot reserve a second internal-RAM inbox.
 */
bool persistVoiceInbox(
    const ::chat::voice::vmp::VoiceMessageInbox& inbox,
    ::chat::voice::vmp::VoiceMessageMetadata* metadata_scratch,
    std::size_t metadata_capacity);

/**
 * Restores the voice attachment adapter from its committed snapshot. The
 * caller supplies a PSRAM-backed media scratch buffer of at least
 * kMaxEncodedMediaSize bytes. No received record is put back on any bearer.
 */
VoiceInboxLoadResult restoreVoiceInbox(
    ::chat::voice::vmp::VoiceMessageInbox* inbox,
    uint8_t* media_scratch,
    std::size_t media_scratch_size);

} // namespace platform::esp::arduino_common::chat_attachment
