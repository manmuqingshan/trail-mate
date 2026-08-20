/**
 * @file tms_config_codec.h
 * @brief Bounded streaming codec for Trail Mate settings documents.
 *
 * The codec deliberately does not own a file, an AppConfig snapshot, or a
 * dynamically sized string.  A platform adapter provides one reusable line
 * scratch buffer and streams it to or from durable storage.  This keeps both
 * normal configuration projection and portable backup/restore out of ESP task
 * stacks and avoids a whole-document JSON representation.
 */

#pragma once

#include "app/app_config.h"

#include <cstddef>
#include <cstdint>

namespace app::tms
{

constexpr std::size_t kMaxLineBytes = 384U;
constexpr std::size_t kMaxDocumentBytes = 16U * 1024U;
constexpr uint16_t kSchemaVersion = 2U;

struct LineScratch
{
    char bytes[kMaxLineBytes];
};

static_assert(sizeof(LineScratch) == kMaxLineBytes,
              "TMS storage must use exactly one bounded line scratch buffer");

enum class DocumentKind : uint8_t
{
    Working,
    Backup,
};

enum class DecodeError : uint8_t
{
    None,
    MissingMagic,
    UnsupportedSchema,
    InvalidDocumentKind,
    LineAfterEnd,
    MissingEnd,
    MalformedRecord,
    InvalidKnownValue,
    TooManyRecords,
};

struct DocumentInfo
{
    uint16_t records = 0U;
    uint16_t unknown_records = 0U;
    DecodeError error = DecodeError::None;
};

struct Output
{
    void* context = nullptr;
    bool (*write)(void* context, const char* data, std::size_t length) = nullptr;
};

// A non-owning record emitter used by platform-specific backup extensions.
// It writes through the same bounded scratch as the core document writer.
class RecordWriter
{
  public:
    RecordWriter(Output output, LineScratch& scratch, DocumentInfo* info);

    bool boolean(const char* key, bool value);
    bool u8(const char* key, uint8_t value);
    bool i8(const char* key, int8_t value);
    bool i32(const char* key, int32_t value);
    bool u16(const char* key, uint16_t value);
    bool u32(const char* key, uint32_t value);
    bool f32(const char* key, float value);
    bool enumeration(const char* key, const char* value);
    bool text(const char* key, const char* value);
    bool blob(const char* key, const uint8_t* value, std::size_t length);

  private:
    Output output_{};
    LineScratch& scratch_;
    DocumentInfo* info_ = nullptr;
};

using RecordExtension = bool (*)(void* context, RecordWriter& writer);

/**
 * Emits a complete canonical TMSET2 document.  `scratch` is caller-owned and
 * must remain valid only for the call; it is never retained.  Values that can
 * contain arbitrary bytes are base64 encoded, while text values use percent
 * escapes, so every emitted record is one bounded physical line.
 */
bool writeDocument(const AppConfig& config,
                   DocumentKind kind,
                   Output output,
                   LineScratch& scratch,
                   DocumentInfo* info = nullptr,
                   RecordExtension extension = nullptr,
                   void* extension_context = nullptr);

/**
 * Incremental parser for a document already split into physical lines.  A
 * decoder with a null target is validation-only; with a target it performs the
 * second, application pass.  Callers must use the validation pass before the
 * application pass so a malformed SD card never partially mutates AppConfig.
 */
class Decoder
{
  public:
    explicit Decoder(AppConfig* target, DocumentKind expected_kind);

    // `line` must be NUL-terminated, contain no trailing newline, and is
    // mutable because the parser splits key/type/value in place.
    bool consumeLine(char* line);
    bool finish();

    const DocumentInfo& info() const { return info_; }

  private:
    bool consumeRecord(char* key, char* type, char* value);

    AppConfig* target_ = nullptr;
    DocumentKind expected_kind_ = DocumentKind::Working;
    DocumentInfo info_{};
    bool saw_magic_ = false;
    bool saw_schema_ = false;
    bool saw_kind_ = false;
    bool saw_end_ = false;
};

const char* decodeErrorName(DecodeError error);

} // namespace app::tms
