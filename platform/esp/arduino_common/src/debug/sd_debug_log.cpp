#include "platform/esp/arduino_common/debug/sd_debug_log.h"

#include <Arduino.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "esp_core_dump.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/settings_store.h"
#include "sdkconfig.h"

namespace platform::esp::arduino_common::debug
{
namespace
{

using ::platform::esp::arduino_common::storage::SdRuntimeFile;

constexpr const char* kSettingsNs = "settings";
constexpr const char* kDebugLogKey = "adv_debug";
constexpr const char* kTrailMateDir = "/trailmate";
constexpr const char* kDebugDir = "/trailmate/debug";
constexpr const char* kCoredumpDir = "/trailmate/coredumps";
constexpr const char* kDebugLogPath = "/trailmate/debug/debug.log";
constexpr const char* kPreviousDebugLogPath = "/trailmate/debug/debug.prev.log";
constexpr std::uint64_t kMaxDebugLogBytes = 256ULL * 1024ULL;
constexpr std::size_t kFormatBufferBytes = 256U;
constexpr std::size_t kCoredumpChunkBytes = 1024U;

SdDebugLogStatus s_status{};
SdRuntimeFile s_log_file{};
bool s_log_started = false;

struct CoredumpSummarySnapshot
{
    bool available = false;
    char exception_task[17] = {};
    std::uint32_t exception_pc = 0;
    std::uint32_t exception_tcb = 0;
    std::uint32_t core_dump_version = 0;
    char app_elf_sha256[65] = {};
};

void copy_path(char* out, std::size_t out_size, const char* value)
{
    if (out == nullptr || out_size == 0)
    {
        return;
    }

    out[0] = '\0';
    if (value == nullptr)
    {
        return;
    }

    std::snprintf(out, out_size, "%s", value);
}

bool ensure_dir(const char* path)
{
    using namespace ::platform::esp::arduino_common::storage;

    if (sd_is_directory(path))
    {
        return true;
    }
    return sd_mkdir(path) || sd_is_directory(path);
}

bool ensure_debug_dirs()
{
    return ensure_dir(kTrailMateDir) && ensure_dir(kDebugDir) &&
           ensure_dir(kCoredumpDir);
}

std::uint64_t file_size(const char* path)
{
    SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return 0;
    }
    const std::uint64_t size = file.size();
    file.close();
    return size;
}

void rotate_debug_log_if_needed()
{
    using namespace ::platform::esp::arduino_common::storage;

    const std::uint64_t size = file_size(kDebugLogPath);
    if (size < kMaxDebugLogBytes)
    {
        return;
    }

    if (sd_exists(kPreviousDebugLogPath))
    {
        sd_remove(kPreviousDebugLogPath);
    }
    sd_rename(kDebugLogPath, kPreviousDebugLogPath);
}

void write_log_line(const char* line)
{
    if (!s_log_file.is_open() || line == nullptr)
    {
        return;
    }

    s_log_file.print(line);
    s_log_file.print("\n");
}

void open_debug_log_if_needed()
{
    if (s_log_file.is_open())
    {
        return;
    }

    if (!s_log_started)
    {
        return;
    }

    if (!::platform::esp::arduino_common::storage::sd_card_ready())
    {
        s_status.sd_ready = false;
        s_status.log_open = false;
        return;
    }

    if (!ensure_debug_dirs())
    {
        s_status.log_open = false;
        return;
    }

    rotate_debug_log_if_needed();

    s_status.log_open = s_log_file.open(kDebugLogPath, "a");
    if (s_status.log_open)
    {
        copy_path(s_status.debug_log_path, sizeof(s_status.debug_log_path), kDebugLogPath);
    }
}

[[nodiscard]] const char* reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return "poweron";
    case ESP_RST_EXT:
        return "external";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
        return "interrupt-watchdog";
    case ESP_RST_TASK_WDT:
        return "task-watchdog";
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_DEEPSLEEP:
        return "deepsleep";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_SDIO:
        return "sdio";
    case ESP_RST_UNKNOWN:
    default:
        return "unknown";
    }
}

void format_coredump_path(char* out, std::size_t out_size)
{
    const std::uint32_t seq =
        ::platform::ui::settings_store::get_uint("debug", "core_seq", 0U) + 1U;
    ::platform::ui::settings_store::put_uint("debug", "core_seq", seq);

    std::snprintf(out,
                  out_size,
                  "%s/core-%08lX-%08lX.elf",
                  kCoredumpDir,
                  static_cast<unsigned long>(seq),
                  static_cast<unsigned long>(esp_random()));
}

void capture_coredump_summary(CoredumpSummarySnapshot& out)
{
    out = CoredumpSummarySnapshot{};
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
    esp_core_dump_summary_t summary{};
    if (esp_core_dump_get_summary(&summary) != ESP_OK)
    {
        return;
    }

    out.available = true;
    std::snprintf(out.exception_task,
                  sizeof(out.exception_task),
                  "%s",
                  summary.exc_task);
    out.exception_pc = summary.exc_pc;
    out.exception_tcb = summary.exc_tcb;
    out.core_dump_version = summary.core_dump_version;
    std::snprintf(out.app_elf_sha256,
                  sizeof(out.app_elf_sha256),
                  "%s",
                  reinterpret_cast<const char*>(summary.app_elf_sha256));
#endif
}

bool write_coredump_metadata(const char* coredump_path,
                             std::size_t coredump_size,
                             std::size_t flash_addr,
                             esp_err_t check_result,
                             esp_err_t erase_result,
                             const CoredumpSummarySnapshot& summary)
{
    char metadata_path[144] = {};
    std::snprintf(metadata_path, sizeof(metadata_path), "%s.txt", coredump_path);

    SdRuntimeFile metadata;
    if (!metadata.open(metadata_path, "w"))
    {
        return false;
    }

    metadata.printf("path=%s\n", coredump_path);
    metadata.printf("size=%lu\n", static_cast<unsigned long>(coredump_size));
    metadata.printf("flash_addr=0x%08lX\n", static_cast<unsigned long>(flash_addr));
    metadata.printf("check_result=0x%08lX\n", static_cast<unsigned long>(check_result));
    metadata.printf("erase_result=0x%08lX\n", static_cast<unsigned long>(erase_result));
    metadata.printf("reset_reason=%s\n", reset_reason_name(esp_reset_reason()));

    if (summary.available)
    {
        metadata.printf("exception_task=%s\n", summary.exception_task);
        metadata.printf("exception_pc=0x%08lX\n",
                        static_cast<unsigned long>(summary.exception_pc));
        metadata.printf("exception_tcb=0x%08lX\n",
                        static_cast<unsigned long>(summary.exception_tcb));
        metadata.printf("core_dump_version=0x%08lX\n",
                        static_cast<unsigned long>(summary.core_dump_version));
        metadata.printf("app_elf_sha256=%s\n", summary.app_elf_sha256);
    }

    const bool ok = metadata.flush();
    metadata.close();
    return ok;
}

bool export_coredump_payload(const esp_partition_t* partition,
                             const char* path,
                             std::size_t partition_offset,
                             std::size_t size)
{
    if (partition == nullptr || path == nullptr || size == 0)
    {
        return false;
    }

    SdRuntimeFile out;
    if (!out.open(path, "w"))
    {
        return false;
    }

    std::uint8_t buffer[kCoredumpChunkBytes] = {};
    std::size_t offset = 0;
    while (offset < size)
    {
        const std::size_t to_read = std::min(kCoredumpChunkBytes, size - offset);
        if (esp_partition_read(partition, partition_offset + offset, buffer, to_read) != ESP_OK)
        {
            out.close();
            ::platform::esp::arduino_common::storage::sd_remove(path);
            return false;
        }
        if (out.write(buffer, to_read) != to_read)
        {
            out.close();
            ::platform::esp::arduino_common::storage::sd_remove(path);
            return false;
        }
        offset += to_read;
    }

    const bool ok = out.flush();
    out.close();
    if (!ok)
    {
        ::platform::esp::arduino_common::storage::sd_remove(path);
    }
    return ok;
}

void note(const char* format, ...)
{
    char buffer[kFormatBufferBytes] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    Serial.println(buffer);
    append_line(buffer);
}

} // namespace

bool debug_logs_enabled()
{
    return ::platform::ui::settings_store::get_bool(kSettingsNs, kDebugLogKey, false);
}

bool begin_sd_debug_log()
{
    s_status.debug_logs_enabled = debug_logs_enabled();
    s_status.sd_ready = ::platform::esp::arduino_common::storage::sd_card_ready();
    copy_path(s_status.debug_log_path, sizeof(s_status.debug_log_path), kDebugLogPath);

    if (!s_status.debug_logs_enabled || !s_status.sd_ready)
    {
        s_status.log_open = false;
        return false;
    }

    s_log_started = true;
    open_debug_log_if_needed();
    if (s_log_file.is_open())
    {
        s_log_file.printf("\n[TrailMate] boot millis=%lu heap=%lu psram=%lu\n",
                          static_cast<unsigned long>(millis()),
                          static_cast<unsigned long>(ESP.getFreeHeap()),
                          static_cast<unsigned long>(ESP.getFreePsram()));
        s_log_file.flush();
    }

    return s_log_file.is_open();
}

void append_line(const char* line)
{
    if (line == nullptr)
    {
        return;
    }

    open_debug_log_if_needed();
    write_log_line(line);
}

void printf(const char* format, ...)
{
    if (format == nullptr)
    {
        return;
    }

    char buffer[kFormatBufferBytes] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    append_line(buffer);
}

void flush()
{
    if (s_log_file.is_open())
    {
        s_log_file.flush();
    }
}

SdDebugLogStatus status()
{
    s_status.sd_ready = ::platform::esp::arduino_common::storage::sd_card_ready();
    s_status.log_open = s_log_file.is_open();
    return s_status;
}

bool export_previous_coredump_to_sd()
{
    s_status.coredump_found = false;
    s_status.coredump_exported = false;
    s_status.coredump_erased = false;
    s_status.coredump_size = 0;
    s_status.coredump_supported =
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
        true;
#else
        false;
#endif

#if !CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    note("[TrailMate][Coredump] flash coredump unsupported in this SDK config");
    return false;
#else
    std::size_t flash_addr = 0;
    std::size_t size = 0;
    const esp_err_t get_result = esp_core_dump_image_get(&flash_addr, &size);
    if (get_result == ESP_ERR_NOT_FOUND)
    {
        append_line("[TrailMate][Coredump] no flash coredump present");
        return false;
    }
    if (get_result != ESP_OK || size == 0)
    {
        note("[TrailMate][Coredump] image_get failed err=0x%08lX size=%lu",
             static_cast<unsigned long>(get_result),
             static_cast<unsigned long>(size));
        return false;
    }

    s_status.coredump_found = true;
    s_status.coredump_size = size;

    const esp_err_t check_result = esp_core_dump_image_check();
    if (check_result != ESP_OK)
    {
        note("[TrailMate][Coredump] image_check failed err=0x%08lX size=%lu",
             static_cast<unsigned long>(check_result),
             static_cast<unsigned long>(size));
        return false;
    }

    s_status.sd_ready = ::platform::esp::arduino_common::storage::sd_card_ready();
    if (!s_status.sd_ready)
    {
        note("[TrailMate][Coredump] found size=%lu but SD is not ready; keeping flash copy",
             static_cast<unsigned long>(size));
        return false;
    }

    if (!ensure_debug_dirs())
    {
        note("[TrailMate][Coredump] failed to create SD coredump directories");
        return false;
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
        nullptr);
    if (partition == nullptr)
    {
        note("[TrailMate][Coredump] coredump partition not found");
        return false;
    }
    if (flash_addr < partition->address)
    {
        note("[TrailMate][Coredump] flash_addr=0x%08lX is before partition=0x%08lX",
             static_cast<unsigned long>(flash_addr),
             static_cast<unsigned long>(partition->address));
        return false;
    }
    const std::size_t partition_offset = flash_addr - partition->address;
    if (partition_offset > partition->size || size > (partition->size - partition_offset))
    {
        note("[TrailMate][Coredump] image size=%lu exceeds partition=%lu",
             static_cast<unsigned long>(size),
             static_cast<unsigned long>(partition->size));
        return false;
    }

    char path[128] = {};
    format_coredump_path(path, sizeof(path));
    copy_path(s_status.coredump_path, sizeof(s_status.coredump_path), path);

    note("[TrailMate][Coredump] exporting flash coredump size=%lu path=%s",
         static_cast<unsigned long>(size),
         path);

    if (!export_coredump_payload(partition, path, partition_offset, size))
    {
        note("[TrailMate][Coredump] export failed path=%s", path);
        return false;
    }

    CoredumpSummarySnapshot summary{};
    capture_coredump_summary(summary);

    const esp_err_t erase_result = esp_core_dump_image_erase();
    s_status.coredump_erased = erase_result == ESP_OK;
    s_status.coredump_exported = true;
    write_coredump_metadata(path, size, flash_addr, check_result, erase_result, summary);

    note("[TrailMate][Coredump] exported path=%s size=%lu erased=%d",
         path,
         static_cast<unsigned long>(size),
         s_status.coredump_erased ? 1 : 0);
    return true;
#endif
}

} // namespace platform::esp::arduino_common::debug
