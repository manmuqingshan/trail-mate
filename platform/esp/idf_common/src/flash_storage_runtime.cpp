#include "platform/esp/idf_common/flash_storage_runtime.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "wear_levelling.h"

namespace platform::esp::idf_common::flash_storage_runtime
{
namespace
{

constexpr const char* kTag = "flash_storage";
constexpr const char* kMountPoint = "/fs";
constexpr const char* kPartitionLabel = "ffat";
constexpr std::size_t kMaxOpenFiles = 12;
constexpr std::size_t kAllocationUnitBytes = 16 * 1024;

SemaphoreHandle_t s_mutex = nullptr;
wl_handle_t s_wear_levelling = WL_INVALID_HANDLE;
bool s_ready = false;

bool ensure_mutex()
{
    if (s_mutex)
    {
        return true;
    }
    s_mutex = xSemaphoreCreateMutex();
    return s_mutex != nullptr;
}

class RuntimeLock final
{
  public:
    RuntimeLock()
    {
        if (ensure_mutex())
        {
            locked_ = xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE;
        }
    }

    ~RuntimeLock()
    {
        if (locked_)
        {
            xSemaphoreGive(s_mutex);
        }
    }

    bool locked() const
    {
        return locked_;
    }

    RuntimeLock(const RuntimeLock&) = delete;
    RuntimeLock& operator=(const RuntimeLock&) = delete;

  private:
    bool locked_ = false;
};

} // namespace

bool ensure_ready(bool format_if_mount_failed)
{
    RuntimeLock lock;
    if (!lock.locked())
    {
        ESP_LOGE(kTag, "flash storage mutex unavailable");
        return false;
    }
    if (s_ready)
    {
        return true;
    }

    esp_vfs_fat_mount_config_t config = {};
    config.format_if_mount_failed = format_if_mount_failed;
    config.max_files = kMaxOpenFiles;
    config.allocation_unit_size = kAllocationUnitBytes;
    config.disk_status_check_enable = false;
    config.use_one_fat = false;

    const esp_err_t error = esp_vfs_fat_spiflash_mount_rw_wl(
        kMountPoint,
        kPartitionLabel,
        &config,
        &s_wear_levelling);
    if (error != ESP_OK)
    {
        s_wear_levelling = WL_INVALID_HANDLE;
        ESP_LOGE(kTag,
                 "mount failed partition=%s format=%u err=%s",
                 kPartitionLabel,
                 format_if_mount_failed ? 1U : 0U,
                 esp_err_to_name(error));
        return false;
    }

    s_ready = true;
    ESP_LOGI(kTag,
             "mounted partition=%s path=%s handle=%ld",
             kPartitionLabel,
             kMountPoint,
             static_cast<long>(s_wear_levelling));
    return true;
}

bool ready()
{
    RuntimeLock lock;
    return lock.locked() && s_ready;
}

const char* mount_point()
{
    return kMountPoint;
}

} // namespace platform::esp::idf_common::flash_storage_runtime
