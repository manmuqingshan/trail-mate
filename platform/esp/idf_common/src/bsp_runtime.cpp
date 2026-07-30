#include "platform/esp/idf_common/bsp_runtime.h"

#include <cstddef>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "boards/tab5/tab5_board.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "platform/esp/idf_common/sd_card_runtime_sdfat_adapter.h"
#include "platform/esp/idf_common/sdmmc_host_runtime.h"

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
extern "C"
{
    esp_err_t bsp_display_brightness_set(int brightness_percent);
    bool trail_mate_tab5_display_runtime_is_ready(void);
}
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
extern "C"
{
#include "bsp/trail_mate_t_display_p4_runtime.h"
}
#endif

namespace platform::esp::idf_common::bsp_runtime
{
namespace
{

constexpr const char* kTag = "idf-bsp-runtime";
constexpr int kTab5SdLdoChan = 4;
char kSdMountPoint[] = "/sdcard";
bool s_nvs_ready = false;
bool s_sdcard_ready = false;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
bool s_sdcard_attempted = false;
#elif defined(TRAIL_MATE_ESP_BOARD_TAB5)
sd_pwr_ctrl_handle_t s_tab5_sd_pwr_ctrl_handle = nullptr;
#endif

} // namespace

bool ensure_nvs_ready()
{
    if (s_nvs_ready)
    {
        return true;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(kTag, "NVS init returned %s, erasing partition", esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK)
        {
            err = nvs_flash_init();
        }
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }

    s_nvs_ready = true;
    return true;
}

bool ensure_sdcard_ready()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (s_sdcard_ready)
    {
        return true;
    }
    if (!::boards::tab5::Tab5Board::hasSdCard())
    {
        return false;
    }

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    if (!s_tab5_sd_pwr_ctrl_handle)
    {
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = kTab5SdLdoChan,
        };
        const esp_err_t ldo_err =
            sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &s_tab5_sd_pwr_ctrl_handle);
        if (ldo_err != ESP_OK)
        {
            ESP_LOGW(kTag, "Tab5 SD LDO init failed: %s", esp_err_to_name(ldo_err));
            return false;
        }
    }
    host.pwr_ctrl_handle = s_tab5_sd_pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    const auto& pins = ::boards::tab5::Tab5Board::sdmmcPins();
    slot_config.clk = static_cast<gpio_num_t>(pins.clk);
    slot_config.cmd = static_cast<gpio_num_t>(pins.cmd);
    slot_config.d0 = static_cast<gpio_num_t>(pins.d0);
    slot_config.d1 = static_cast<gpio_num_t>(pins.d1);
    slot_config.d2 = static_cast<gpio_num_t>(pins.d2);
    slot_config.d3 = static_cast<gpio_num_t>(pins.d3);

    if (::platform::esp::idf_common::sd_card_runtime::mount_sdmmc(
            ::platform::esp::idf_common::sdmmc_host_runtime::SlotOwner::SdCard,
            host,
            slot_config,
            kSdMountPoint,
            8))
    {
        s_sdcard_ready = true;
        return true;
    }

    ESP_LOGW(kTag, "Tab5 SD unavailable via SdFat SDMMC backend");
    return false;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (s_sdcard_ready)
    {
        return true;
    }
    if (s_sdcard_attempted)
    {
        return false;
    }
    s_sdcard_attempted = true;
    if (!::boards::t_display_p4::TDisplayP4Board::hasSdCard())
    {
        return false;
    }

    if (!::boards::t_display_p4::TDisplayP4Board::instance().mountSdCard(kSdMountPoint, 8))
    {
        ESP_LOGW(kTag,
                 "T-Display-P4 SD unavailable for this boot; continuing without storage");
        return false;
    }

    s_sdcard_ready = true;
    return true;
#else
    return false;
#endif
}

bool sdcard_ready()
{
    return s_sdcard_ready;
}

void mark_sdcard_unmounted()
{
    s_sdcard_ready = false;
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    s_sdcard_attempted = false;
#endif
}

const char* sdcard_mount_point()
{
    return kSdMountPoint;
}

bool display_ready()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return trail_mate_tab5_display_runtime_is_ready();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return trail_mate_t_display_p4_display_runtime_is_ready();
#else
    return false;
#endif
}

bool set_display_brightness(int brightness_percent)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (display_ready() == false)
    {
        return false;
    }
    const esp_err_t err = bsp_display_brightness_set(brightness_percent);
    if (err == ESP_OK)
    {
        return true;
    }
    ESP_LOGW(kTag,
             "bsp_display_brightness_set(%d) failed: %s",
             brightness_percent,
             esp_err_to_name(err));
    return false;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (!display_ready())
    {
        return false;
    }
    const esp_err_t err = trail_mate_t_display_p4_display_set_brightness_percent(brightness_percent);
    if (err == ESP_OK)
    {
        return true;
    }
    ESP_LOGW(kTag,
             "trail_mate_t_display_p4_display_set_brightness_percent(%d) failed: %s",
             brightness_percent,
             esp_err_to_name(err));
    return false;
#else
    (void)brightness_percent;
    return false;
#endif
}

bool wake_display()
{
    return set_display_brightness(default_awake_brightness_percent());
}

bool sleep_display()
{
    return set_display_brightness(0);
}

int default_awake_brightness_percent()
{
    return 100;
}

bool sdcard_capable()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasSdCard();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::hasSdCard();
#else
    return false;
#endif
}

} // namespace platform::esp::idf_common::bsp_runtime
