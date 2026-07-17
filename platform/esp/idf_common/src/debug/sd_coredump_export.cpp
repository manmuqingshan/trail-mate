#include "platform/esp/idf_common/debug/sd_coredump_export.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "esp_core_dump.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "sdkconfig.h"

namespace platform::esp::idf_common::debug
{
namespace
{

constexpr const char* kTag = "idf-sd-coredump";
constexpr const char* kTrailMateDir = "trailmate";
constexpr const char* kCoredumpDir = "coredumps";
constexpr std::size_t kCoredumpChunkBytes = 1024U;

SdCoredumpExportStatus s_status{};

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
    if (!out || out_size == 0)
    {
        return;
    }

    out[0] = '\0';
    if (value)
    {
        std::snprintf(out, out_size, "%s", value);
    }
}

bool path_is_dir(const char* path)
{
    return path && path[0] != '\0' &&
           ::platform::esp::arduino_common::storage::sd_is_directory(path);
}

bool ensure_dir(const char* path)
{
    if (path_is_dir(path))
    {
        return true;
    }
    if (::platform::esp::arduino_common::storage::sd_mkdir(path))
    {
        return true;
    }
    return path_is_dir(path);
}

bool build_trailmate_path(char* out,
                          std::size_t out_size,
                          const char* first,
                          const char* second = nullptr)
{
    if (!out || out_size == 0 || !first)
    {
        return false;
    }

    if (second)
    {
        std::snprintf(out, out_size, "/%s/%s", first, second);
    }
    else
    {
        std::snprintf(out, out_size, "/%s", first);
    }
    out[out_size - 1] = '\0';
    return true;
}

bool ensure_coredump_dirs()
{
    char trailmate_path[96] = {};
    char coredump_path[128] = {};
    return build_trailmate_path(trailmate_path,
                                sizeof(trailmate_path),
                                kTrailMateDir) &&
           build_trailmate_path(coredump_path,
                                sizeof(coredump_path),
                                kTrailMateDir,
                                kCoredumpDir) &&
           ensure_dir(trailmate_path) && ensure_dir(coredump_path);
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
    char coredump_dir[128] = {};
    if (!build_trailmate_path(coredump_dir,
                              sizeof(coredump_dir),
                              kTrailMateDir,
                              kCoredumpDir))
    {
        copy_path(out, out_size, "");
        return;
    }

    const std::uint32_t boot_part =
        static_cast<std::uint32_t>(esp_timer_get_time() & 0xFFFFFFFFULL);
    std::snprintf(out,
                  out_size,
                  "%s/core-%08lX-%08lX.elf",
                  coredump_dir,
                  static_cast<unsigned long>(boot_part),
                  static_cast<unsigned long>(esp_random()));
    out[out_size - 1] = '\0';
}

void capture_coredump_summary(CoredumpSummarySnapshot& out)
{
    out = CoredumpSummarySnapshot{};
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && \
    defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF) && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
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
    if (!coredump_path || coredump_path[0] == '\0')
    {
        return false;
    }

    char metadata_path[192] = {};
    std::snprintf(metadata_path, sizeof(metadata_path), "%s.txt", coredump_path);
    metadata_path[sizeof(metadata_path) - 1] = '\0';

    ::platform::esp::arduino_common::storage::SdRuntimeFile metadata;
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
    if (!partition || !path || path[0] == '\0' || size == 0)
    {
        return false;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile out;
    if (!out.open(path, "wb"))
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

void reset_status()
{
    s_status.found = false;
    s_status.exported = false;
    s_status.erased = false;
    s_status.size = 0;
    s_status.path[0] = '\0';
    s_status.sd_ready = bsp_runtime::sdcard_ready();
    s_status.supported =
#if defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) && CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
        true;
#else
        false;
#endif
}

} // namespace

SdCoredumpExportStatus sd_coredump_export_status()
{
    s_status.sd_ready = bsp_runtime::sdcard_ready();
    return s_status;
}

bool export_previous_coredump_to_sd()
{
    reset_status();

#if !defined(CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH) || !CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    ESP_LOGW(kTag, "flash coredump unsupported in this sdkconfig");
    return false;
#else
    std::size_t flash_addr = 0;
    std::size_t size = 0;
    const esp_err_t get_result = esp_core_dump_image_get(&flash_addr, &size);
    if (get_result == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGI(kTag, "no flash coredump present");
        return false;
    }
    if (get_result != ESP_OK || size == 0)
    {
        ESP_LOGW(kTag,
                 "image_get failed err=%s size=%lu",
                 esp_err_to_name(get_result),
                 static_cast<unsigned long>(size));
        return false;
    }

    s_status.found = true;
    s_status.size = size;

    const esp_err_t check_result = esp_core_dump_image_check();
    if (check_result != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "image_check failed err=%s size=%lu",
                 esp_err_to_name(check_result),
                 static_cast<unsigned long>(size));
        return false;
    }

    s_status.sd_ready = bsp_runtime::sdcard_ready();
    if (!s_status.sd_ready)
    {
        ESP_LOGW(kTag,
                 "found coredump size=%lu but SD is not ready; keeping flash copy",
                 static_cast<unsigned long>(size));
        return false;
    }

    if (!ensure_coredump_dirs())
    {
        ESP_LOGW(kTag, "failed to create SD coredump directories");
        return false;
    }

    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
        nullptr);
    if (!partition)
    {
        ESP_LOGW(kTag, "coredump partition not found");
        return false;
    }
    if (flash_addr < partition->address)
    {
        ESP_LOGW(kTag,
                 "flash_addr=0x%08lX is before partition=0x%08lX",
                 static_cast<unsigned long>(flash_addr),
                 static_cast<unsigned long>(partition->address));
        return false;
    }

    const std::size_t partition_offset = flash_addr - partition->address;
    if (partition_offset > partition->size || size > (partition->size - partition_offset))
    {
        ESP_LOGW(kTag,
                 "image size=%lu exceeds partition=%lu",
                 static_cast<unsigned long>(size),
                 static_cast<unsigned long>(partition->size));
        return false;
    }

    char path[160] = {};
    format_coredump_path(path, sizeof(path));
    if (path[0] == '\0')
    {
        ESP_LOGW(kTag, "failed to format SD coredump path");
        return false;
    }
    copy_path(s_status.path, sizeof(s_status.path), path);

    ESP_LOGI(kTag,
             "exporting flash coredump size=%lu path=%s",
             static_cast<unsigned long>(size),
             path);

    if (!export_coredump_payload(partition, path, partition_offset, size))
    {
        ESP_LOGW(kTag, "export failed path=%s", path);
        return false;
    }

    CoredumpSummarySnapshot summary{};
    capture_coredump_summary(summary);

    const esp_err_t erase_result = esp_core_dump_image_erase();
    s_status.erased = erase_result == ESP_OK;
    s_status.exported = true;
    (void)write_coredump_metadata(path, size, flash_addr, check_result, erase_result, summary);

    ESP_LOGI(kTag,
             "exported path=%s size=%lu erased=%d",
             path,
             static_cast<unsigned long>(size),
             s_status.erased ? 1 : 0);
    return true;
#endif
}

} // namespace platform::esp::idf_common::debug
