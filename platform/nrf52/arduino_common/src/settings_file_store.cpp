#include "platform/nrf52/arduino_common/settings_file_store.h"

#include "platform/nrf52/arduino_common/internal_fs_utils.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include <cstring>

namespace platform::nrf52::arduino_common::settings_file
{
namespace
{

using Adafruit_LittleFS_Namespace::FILE_O_READ;
using Adafruit_LittleFS_Namespace::FILE_O_WRITE;

const char* logPrefix(const char* prefix)
{
    return prefix ? prefix : "[nrf52][settings]";
}

void reset(VerifyResult* result)
{
    if (result)
    {
        *result = VerifyResult{};
    }
}

void reset(ReplaceResult* result)
{
    if (result)
    {
        *result = ReplaceResult{};
    }
}

void setStatus(VerifyResult* result, StoreStatus status)
{
    if (result)
    {
        result->status = status;
    }
}

void setStatus(ReplaceResult* result, StoreStatus status)
{
    if (result)
    {
        result->status = status;
    }
}

StoreStatus writeWholeFile(const ReplaceRequest& request,
                           const SettingsFileHeader& header,
                           const char* path,
                           const char* label,
                           bool truncate_first,
                           ReplaceResult* result)
{
    const char* prefix = logPrefix(request.log_prefix);
    Adafruit_LittleFS_Namespace::File file(InternalFS);
    if (truncate_first)
    {
        // nRF52 LittleFS can assert when an existing file is truncated and
        // rewritten in place. Remove first so fallback writes use fresh blocks.
        internal_fs::removeIfExists(path);
        if (InternalFS.exists(path))
        {
            Serial.printf("%s %s remove before rewrite failed path=%s\n", prefix, label, path);
            return StoreStatus::WriteFailed;
        }
    }

    file = InternalFS.open(path, FILE_O_WRITE);
    if (!file)
    {
        Serial.printf("%s %s open failed path=%s\n", prefix, label, path);
        return StoreStatus::OpenFailed;
    }

    if (!file.seek(0))
    {
        file.close();
        Serial.printf("%s %s seek failed path=%s\n", prefix, label, path);
        return StoreStatus::WriteFailed;
    }

    const std::size_t header_written =
        file.write(reinterpret_cast<const uint8_t*>(&header), sizeof(SettingsFileHeader));
    const std::size_t payload_written =
        (header_written == sizeof(SettingsFileHeader))
            ? file.write(reinterpret_cast<const uint8_t*>(request.payload), request.payload_size)
            : 0U;

    if (result)
    {
        result->header_written = header_written;
        result->payload_written = payload_written;
    }

    file.flush();
    file.close();

    if (header_written != sizeof(SettingsFileHeader) || payload_written != request.payload_size)
    {
        Serial.printf("%s %s write failed header=%lu payload=%lu expected_header=%lu expected_payload=%lu crc=0x%08lX exists=%u\n",
                      prefix,
                      label,
                      static_cast<unsigned long>(header_written),
                      static_cast<unsigned long>(payload_written),
                      static_cast<unsigned long>(sizeof(SettingsFileHeader)),
                      static_cast<unsigned long>(request.payload_size),
                      static_cast<unsigned long>(header.crc32),
                      InternalFS.exists(path) ? 1U : 0U);
        return StoreStatus::WriteFailed;
    }

    return StoreStatus::Ok;
}

} // namespace

const char* statusText(StoreStatus status)
{
    switch (status)
    {
    case StoreStatus::Ok:
        return "ok";
    case StoreStatus::NotFound:
        return "not_found";
    case StoreStatus::FsInitFailed:
        return "fs_init_failed";
    case StoreStatus::OpenFailed:
        return "open_failed";
    case StoreStatus::ReadFailed:
        return "read_failed";
    case StoreStatus::WriteFailed:
        return "write_failed";
    case StoreStatus::FlushFailed:
        return "flush_failed";
    case StoreStatus::HeaderInvalid:
        return "header_invalid";
    case StoreStatus::VersionMismatch:
        return "version_mismatch";
    case StoreStatus::PayloadSizeMismatch:
        return "payload_size_mismatch";
    case StoreStatus::CrcMismatch:
        return "crc_mismatch";
    case StoreStatus::RenameFailed:
        return "rename_failed";
    case StoreStatus::BackupFailed:
        return "backup_failed";
    default:
        return "unknown";
    }
}

uint32_t crc32(const uint8_t* data, std::size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
}

StoreStatus verifySettingsFile(const VerifyRequest& request, VerifyResult* result)
{
    reset(result);
    const char* prefix = logPrefix(request.log_prefix);
    if (!request.path || !request.header_scratch || !request.payload_scratch || request.payload_size == 0)
    {
        setStatus(result, StoreStatus::OpenFailed);
        return StoreStatus::OpenFailed;
    }

    auto file = InternalFS.open(request.path, FILE_O_READ);
    if (!file)
    {
        Serial.printf("%s verify open failed path=%s\n", prefix, request.path);
        setStatus(result, StoreStatus::OpenFailed);
        return StoreStatus::OpenFailed;
    }

    const uint32_t expected_size = static_cast<uint32_t>(sizeof(SettingsFileHeader) + request.payload_size);
    const uint32_t actual_size = file.size();
    if (result)
    {
        result->actual_size = actual_size;
        result->expected_size = expected_size;
    }

    if (actual_size != expected_size)
    {
        file.close();
        Serial.printf("%s verify size mismatch actual=%lu expected=%lu\n",
                      prefix,
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(expected_size));
        setStatus(result, StoreStatus::PayloadSizeMismatch);
        return StoreStatus::PayloadSizeMismatch;
    }

    auto& header = *request.header_scratch;
    std::memset(&header, 0, sizeof(header));
    if (file.read(&header, sizeof(header)) != sizeof(header))
    {
        file.close();
        Serial.printf("%s verify header read failed\n", prefix);
        setStatus(result, StoreStatus::ReadFailed);
        return StoreStatus::ReadFailed;
    }

    if (header.magic != request.magic)
    {
        file.close();
        Serial.printf("%s verify magic mismatch got=0x%08lX expected=0x%08lX\n",
                      prefix,
                      static_cast<unsigned long>(header.magic),
                      static_cast<unsigned long>(request.magic));
        setStatus(result, StoreStatus::HeaderInvalid);
        return StoreStatus::HeaderInvalid;
    }

    if (header.version != request.version)
    {
        file.close();
        Serial.printf("%s verify version mismatch got=%u expected=%u\n",
                      prefix,
                      static_cast<unsigned>(header.version),
                      static_cast<unsigned>(request.version));
        setStatus(result, StoreStatus::VersionMismatch);
        return StoreStatus::VersionMismatch;
    }

    if (header.payload_size != request.payload_size)
    {
        file.close();
        Serial.printf("%s verify payload size mismatch got=%lu expected=%lu\n",
                      prefix,
                      static_cast<unsigned long>(header.payload_size),
                      static_cast<unsigned long>(request.payload_size));
        setStatus(result, StoreStatus::PayloadSizeMismatch);
        return StoreStatus::PayloadSizeMismatch;
    }

    std::memset(request.payload_scratch, 0, request.payload_size);
    const int payload_read = file.read(request.payload_scratch, static_cast<uint16_t>(request.payload_size));
    if (payload_read != static_cast<int>(request.payload_size))
    {
        file.close();
        Serial.printf("%s verify payload read failed\n", prefix);
        setStatus(result, StoreStatus::ReadFailed);
        return StoreStatus::ReadFailed;
    }

    file.close();

    const uint32_t actual_crc =
        crc32(reinterpret_cast<const uint8_t*>(request.payload_scratch), request.payload_size);
    if (result)
    {
        result->actual_crc = actual_crc;
        result->expected_crc = header.crc32;
    }

    if (actual_crc != header.crc32)
    {
        Serial.printf("%s verify crc mismatch got=0x%08lX expected=0x%08lX\n",
                      prefix,
                      static_cast<unsigned long>(actual_crc),
                      static_cast<unsigned long>(header.crc32));
        setStatus(result, StoreStatus::CrcMismatch);
        return StoreStatus::CrcMismatch;
    }

    setStatus(result, StoreStatus::Ok);
    return StoreStatus::Ok;
}

StoreStatus replaceSettingsFile(const ReplaceRequest& request, ReplaceResult* result)
{
    reset(result);
    const char* prefix = logPrefix(request.log_prefix);
    if (!request.path || !request.temp_path || !request.payload || !request.header_scratch ||
        !request.verify_payload_scratch || request.payload_size == 0)
    {
        setStatus(result, StoreStatus::OpenFailed);
        return StoreStatus::OpenFailed;
    }

    if (!internal_fs::ensureMounted(request.allow_format_recovery, request.fs_log_tag))
    {
        Serial.printf("%s ensureMounted failed\n", prefix);
        setStatus(result, StoreStatus::FsInitFailed);
        return StoreStatus::FsInitFailed;
    }

    if (InternalFS.exists(request.path))
    {
        Serial.printf("%s existing file before replace path=%s\n", prefix, request.path);
    }

    internal_fs::removeIfExists(request.temp_path);
    if (InternalFS.exists(request.temp_path))
    {
        Serial.printf("%s temp remove failed path=%s\n", prefix, request.temp_path);
        setStatus(result, StoreStatus::WriteFailed);
        return StoreStatus::WriteFailed;
    }

    auto& header = *request.header_scratch;
    std::memset(&header, 0, sizeof(header));
    header.magic = request.magic;
    header.version = request.version;
    header.reserved = 0;
    header.payload_size = request.payload_size;
    header.crc32 = crc32(reinterpret_cast<const uint8_t*>(request.payload), request.payload_size);

    if (result)
    {
        result->crc32 = header.crc32;
        result->final_size = static_cast<uint32_t>(sizeof(SettingsFileHeader) + request.payload_size);
    }

    StoreStatus status = writeWholeFile(request, header, request.temp_path, "temp", false, result);
    if (status != StoreStatus::Ok)
    {
        internal_fs::removeIfExists(request.temp_path);
        Serial.printf("%s direct rewrite start reason=%s path=%s\n",
                      prefix,
                      statusText(status),
                      request.path);
        if (result)
        {
            result->used_destructive_rewrite = true;
        }

        status = writeWholeFile(request, header, request.path, "direct", true, result);
        if (status != StoreStatus::Ok)
        {
            Serial.printf("%s fresh rewrite start reason=%s path=%s\n",
                          prefix,
                          statusText(status),
                          request.path);
            internal_fs::removeIfExists(request.path);
            if (InternalFS.exists(request.path))
            {
                Serial.printf("%s fresh remove failed path=%s\n", prefix, request.path);
                setStatus(result, StoreStatus::WriteFailed);
                return StoreStatus::WriteFailed;
            }

            status = writeWholeFile(request, header, request.path, "fresh", false, result);
            if (status != StoreStatus::Ok)
            {
                setStatus(result, status);
                return status;
            }
        }

        VerifyRequest verify_direct{};
        verify_direct.path = request.path;
        verify_direct.log_prefix = prefix;
        verify_direct.magic = request.magic;
        verify_direct.version = request.version;
        verify_direct.payload_size = request.payload_size;
        verify_direct.header_scratch = request.header_scratch;
        verify_direct.payload_scratch = request.verify_payload_scratch;
        VerifyResult verify_result{};
        status = verifySettingsFile(verify_direct, &verify_result);
        if (status != StoreStatus::Ok)
        {
            Serial.printf("%s direct verify failed status=%s\n", prefix, statusText(status));
            setStatus(result, status);
            return status;
        }

        setStatus(result, StoreStatus::Ok);
        return StoreStatus::Ok;
    }

    VerifyRequest verify_temp{};
    verify_temp.path = request.temp_path;
    verify_temp.log_prefix = prefix;
    verify_temp.magic = request.magic;
    verify_temp.version = request.version;
    verify_temp.payload_size = request.payload_size;
    verify_temp.header_scratch = request.header_scratch;
    verify_temp.payload_scratch = request.verify_payload_scratch;
    VerifyResult verify_result{};
    status = verifySettingsFile(verify_temp, &verify_result);
    if (status != StoreStatus::Ok)
    {
        Serial.printf("%s temp verify failed status=%s\n", prefix, statusText(status));
        internal_fs::removeIfExists(request.temp_path);
        setStatus(result, status);
        return status;
    }

    if (!InternalFS.rename(request.temp_path, request.path))
    {
        Serial.printf("%s rename failed temp=%s path=%s\n", prefix, request.temp_path, request.path);
        internal_fs::removeIfExists(request.temp_path);
        setStatus(result, StoreStatus::RenameFailed);
        return StoreStatus::RenameFailed;
    }

    VerifyRequest verify_final = verify_temp;
    verify_final.path = request.path;
    status = verifySettingsFile(verify_final, &verify_result);
    if (status != StoreStatus::Ok)
    {
        Serial.printf("%s final verify failed status=%s\n", prefix, statusText(status));
        setStatus(result, status);
        return status;
    }

    setStatus(result, StoreStatus::Ok);
    return StoreStatus::Ok;
}

} // namespace platform::nrf52::arduino_common::settings_file
