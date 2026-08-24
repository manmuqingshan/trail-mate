/**
 * @file tms_config_codec.h
 * @brief Bounded streaming codec for Trail Mate settings documents.
 *
 * The codec deliberately does not own a file, an AppConfig snapshot, or a
 * dynamically sized string.  A platform adapter provides one reusable line
 * scratch buffer and streams it to or from durable storage.  This keeps both
 * normal configuration projection and platform-owned extensions out of ESP task
 * stacks and avoids a whole-document JSON representation.
 */

#pragma once

#include "app/app_config.h"

#include <cstddef>
#include <cstdint>

namespace app::tms
{

constexpr std::size_t kMaxLineBytes = 384U;
// The document is streamed a line at a time.  This limit bounds SD-card
// input without reserving document-sized RAM; it also leaves room for all ten
// Wi-Fi profiles and optional cellular credentials alongside AppConfig.
constexpr std::size_t kMaxDocumentBytes = 32U * 1024U;
// TMSET7 is the complete, strict working-document schema. TMSET6 was emitted
// by a pre-release implementation with a different Reticulum key dialect and
// no BLE projection, so it is a migration input only. Earlier versions follow
// the same one-time migration rule and are never emitted as working config.
constexpr uint16_t kSchemaVersion = 7U;

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
    DuplicateRecord,
    MissingRequiredRecord,
    UnknownRecord,
    RecordBeforeHeader,
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

// A non-owning record emitter used by platform-specific TMS extensions.
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

// A non-owning decoded record exposed to platform extensions.  It deliberately
// reuses the core codec's strict scalar, percent-escaped text, and base64
// parsers so an extension cannot accidentally create a second TMS grammar.
class RecordReader
{
  public:
    const char* key() const { return key_; }
    bool boolean(bool* target = nullptr) const;
    bool u8(uint8_t* target = nullptr, uint8_t maximum = UINT8_MAX) const;
    bool u16(uint16_t* target = nullptr, uint16_t maximum = UINT16_MAX) const;
    bool u32(uint32_t* target = nullptr) const;
    bool i32(int32_t* target = nullptr) const;
    bool text(char* target, std::size_t capacity) const;
    bool blob(uint8_t* target,
              std::size_t capacity,
              std::size_t* decoded_length = nullptr) const;

  private:
    friend class Decoder;
    RecordReader(const char* key, const char* type, const char* value)
        : key_(key), type_(type), value_(value)
    {
    }

    const char* key_ = nullptr;
    const char* type_ = nullptr;
    const char* value_ = nullptr;
};

enum class RecordConsumeResult : uint8_t
{
    Unhandled,
    Accepted,
    Invalid,
};

using RecordConsumer = RecordConsumeResult (*)(void* context, const RecordReader& reader);
using DocumentFinalizer = bool (*)(void* context, bool applying, uint16_t schema_version);

/**
 * Emits a complete canonical document for the current TMS schema.  `scratch` is caller-owned and
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
    explicit Decoder(AppConfig* target,
                     DocumentKind expected_kind,
                     RecordConsumer consumer = nullptr,
                     void* extension_context = nullptr,
                     DocumentFinalizer finalizer = nullptr);

    // `line` must be NUL-terminated, contain no trailing newline, and is
    // mutable because the parser splits key/type/value in place.
    bool consumeLine(char* line);
    bool finish();

    const DocumentInfo& info() const { return info_; }
    uint16_t schemaVersion() const { return schema_version_; }

  private:
    bool consumeRecord(char* key, char* type, char* value);

    AppConfig* target_ = nullptr;
    DocumentKind expected_kind_ = DocumentKind::Working;
    RecordConsumer consumer_ = nullptr;
    void* extension_context_ = nullptr;
    DocumentFinalizer finalizer_ = nullptr;
    DocumentInfo info_{};
    uint16_t schema_version_ = 0U;
    uint16_t magic_version_ = 0U;
    uint16_t core_records_ = 0U;
    uint64_t core_seen_[4]{};
    bool saw_magic_ = false;
    bool saw_schema_ = false;
    bool saw_kind_ = false;
    bool saw_user_record_ = false;
    bool saw_end_ = false;
};

const char* decodeErrorName(DecodeError error);

} // namespace app::tms
