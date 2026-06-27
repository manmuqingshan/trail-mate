#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::nrf52::arduino_common::settings_file
{

enum class StoreStatus : uint8_t
{
    Ok = 0,
    NotFound,
    FsInitFailed,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    FlushFailed,
    HeaderInvalid,
    VersionMismatch,
    PayloadSizeMismatch,
    CrcMismatch,
    RenameFailed,
    BackupFailed,
};

struct SettingsFileHeader
{
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t reserved = 0;
    uint32_t payload_size = 0;
    uint32_t crc32 = 0;
} __attribute__((packed));

struct VerifyRequest
{
    const char* path = nullptr;
    const char* log_prefix = nullptr;
    uint32_t magic = 0;
    uint16_t version = 0;
    std::size_t payload_size = 0;
    SettingsFileHeader* header_scratch = nullptr;
    void* payload_scratch = nullptr;
};

struct VerifyResult
{
    StoreStatus status = StoreStatus::NotFound;
    uint32_t actual_size = 0;
    uint32_t expected_size = 0;
    uint32_t actual_crc = 0;
    uint32_t expected_crc = 0;
};

struct ReplaceRequest
{
    const char* path = nullptr;
    const char* temp_path = nullptr;
    const char* fs_log_tag = nullptr;
    const char* log_prefix = nullptr;
    uint32_t magic = 0;
    uint16_t version = 0;
    const void* payload = nullptr;
    std::size_t payload_size = 0;
    SettingsFileHeader* header_scratch = nullptr;
    void* verify_payload_scratch = nullptr;
    bool allow_format_recovery = false;
};

struct ReplaceResult
{
    StoreStatus status = StoreStatus::NotFound;
    bool had_old_size = false;
    bool used_destructive_rewrite = false;
    uint32_t old_size = 0;
    std::size_t header_written = 0;
    std::size_t payload_written = 0;
    uint32_t final_size = 0;
    uint32_t crc32 = 0;
};

const char* statusText(StoreStatus status);
uint32_t crc32(const uint8_t* data, std::size_t len);
StoreStatus verifySettingsFile(const VerifyRequest& request, VerifyResult* result);
StoreStatus replaceSettingsFile(const ReplaceRequest& request, ReplaceResult* result);

} // namespace platform::nrf52::arduino_common::settings_file
