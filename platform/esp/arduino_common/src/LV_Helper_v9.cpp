/**
 * @file LV_Helper_v9.cpp
 * @brief LVGL v9.x helper functions for T-LoRa-Pager
 *
 * This file provides LVGL initialization and integration functions,
 * including display driver setup, input device registration, and
 * custom memory management for LVGL v9.x.
 */

#include "display/DisplayInterface.h"
#include "input/morse_engine.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/esp/common/shared_spi_coordinator.h"
#include "platform/ui/screen_runtime.h"
#include "screen_sleep.h"
#include "sys/clock.h"
#include "ui/LV_Helper.h"
#include "ui/app_runtime.h"
#include "ui/menu/menu_runtime.h"
#include "ui/support/lvgl_fs_utils.h"
#include "walkie/walkie_service.h"
#include <Arduino.h>
#include <atomic>
#include <cstring>
#include <dirent.h>
#include <errno.h>
#include <esp_heap_caps.h>
#include <fcntl.h>
#include <freertos/task.h>
#include <sys/types.h>
#include <unistd.h>

#if LVGL_VERSION_MAJOR == 9

// Test toggles (set to 0 to disable)
#define LV_TEST_FORCE_DMA_BUF 0
#define LV_TEST_FORCE_DMA_FULL_SIZE 0
#define LV_TEST_FLUSH_LOG 0
#define LV_TEST_FLUSH_SAMPLE 0

static lv_display_t* disp_drv;
static lv_draw_buf_t draw_buf;
static lv_indev_t* indev_touch;
static lv_indev_t* indev_encoder;
static lv_indev_t* indev_keyboard;

static lv_color16_t* buf = nullptr;
static lv_color16_t* buf1 = nullptr;

namespace
{
constexpr char kLvglFlashFsLetter = 'F';
constexpr const char* kLvglFlashFsMountPoint = "/fs";
#if defined(ARDUINO_T_DECK)
#ifndef TRAIL_MATE_TDECK_DMA_DRAW_BUFFER_LINES
#define TRAIL_MATE_TDECK_DMA_DRAW_BUFFER_LINES 40
#endif
constexpr size_t kTDeckDmaDrawBufferLines = TRAIL_MATE_TDECK_DMA_DRAW_BUFFER_LINES;
static_assert(kTDeckDmaDrawBufferLines >= 10, "TDeck draw buffers below 10 lines make refresh visibly unstable");
#endif
// LVGL's SD drive callback can still be reached by resource-pack and image
// paths. Keep every callback acquisition extremely short: a busy shared
// bus must surface as LV_FS_RES_BUSY/failed open, not as a blocked UI frame.
constexpr TickType_t kLvglSdFsWait = pdMS_TO_TICKS(2);
constexpr TickType_t kLvglSdFsCloseWait = pdMS_TO_TICKS(10);
constexpr uint32_t kLvglExternalFontBusAcquireMs = 6000;
constexpr uint32_t kLvglExternalFontSlowHoldMs = 750;
constexpr uint32_t kLvglExternalFontLogIntervalMs = 5000;
constexpr uint32_t kLvglExternalFontBusResource = 0x4C56464EU; // "LVFN"
constexpr uint32_t kLvglExternalFontCommandId = 0x4C564653U;   // "LVFS"
constexpr const char* kLvglExternalFontOwner = "lvgl_font_sd";

lv_fs_drv_t s_flash_fs_drv;
lv_fs_drv_t s_sd_fs_drv;
bool s_flash_fs_ready = false;
bool s_sd_fs_ready = false;
std::atomic<unsigned> s_external_font_load_fs_depth{0};
sys::runtime::BusAccessToken s_external_font_bus_token{};
TaskHandle_t s_external_font_owner_task = nullptr;
uint32_t s_external_font_acquired_ms = 0;
uint32_t s_external_font_last_busy_log_ms = 0;

bool external_font_load_fs_scope_active()
{
    return s_external_font_load_fs_depth.load(std::memory_order_relaxed) > 0 &&
           s_external_font_bus_token.valid;
}

TickType_t current_sd_fs_wait(TickType_t normal_wait)
{
    // A font load scope already owns the shared bus. Same-task callback reentry
    // succeeds immediately; cross-task access should fail fast instead of
    // waiting behind the UI-visible font transaction.
    return external_font_load_fs_scope_active() ? 0 : normal_wait;
}

const char* current_sd_fs_owner()
{
    return external_font_load_fs_scope_active() ? kLvglExternalFontOwner : nullptr;
}

inline int flash_fs_fd_from_ptr(void* file_p)
{
    return static_cast<int>(reinterpret_cast<uintptr_t>(file_p) - 1U);
}

inline void* flash_fs_ptr_from_fd(int fd)
{
    return reinterpret_cast<void*>(static_cast<uintptr_t>(fd + 1));
}

lv_fs_res_t flash_fs_errno_to_res(int errno_val)
{
    switch (errno_val)
    {
    case 0:
        return LV_FS_RES_OK;
    case EIO:
        return LV_FS_RES_HW_ERR;
    case EFAULT:
        return LV_FS_RES_FS_ERR;
    case ENOENT:
        return LV_FS_RES_NOT_EX;
    case ENOSPC:
        return LV_FS_RES_FULL;
    case EALREADY:
        return LV_FS_RES_LOCKED;
    case EACCES:
        return LV_FS_RES_DENIED;
    case EBUSY:
        return LV_FS_RES_BUSY;
    case ETIMEDOUT:
        return LV_FS_RES_TOUT;
    case ENOSYS:
        return LV_FS_RES_NOT_IMP;
    case ENOMEM:
        return LV_FS_RES_OUT_OF_MEM;
    case EINVAL:
        return LV_FS_RES_INV_PARAM;
    default:
        return LV_FS_RES_UNKNOWN;
    }
}

lv_fs_res_t sd_fs_error_to_res()
{
    return LV_FS_RES_FS_ERR;
}

class LvglSdBusGuard final
{
  public:
    LvglSdBusGuard(TickType_t wait_ticks, const char* owner)
    {
        sys::runtime::BusAcquireRequest request{};
        request.resource =
            ::platform::esp::common::SharedSpiCoordinator::kSharedBusResource;
        request.policy = wait_ticks == 0
                             ? sys::runtime::BusAccessPolicy::UiNeverBlock
                             : sys::runtime::BusAccessPolicy::InteractiveWorkerBounded;
        request.command_id = 0x4C564653U;
        request.origin = request.command_id;
        request.deadline_ms =
            sys::millis_now() + static_cast<uint32_t>(wait_ticks) * portTICK_PERIOD_MS;
        request.owner_label = owner ? owner : "lvgl_sd_fs";
        result_ = ::platform::esp::common::shared_spi_coordinator().acquire(request);
        token_ = result_.token;
        locked_ = result_.status == sys::runtime::BusAcquireStatus::Acquired &&
                  token_.valid;
    }

    ~LvglSdBusGuard()
    {
        if (locked_)
        {
            ::platform::esp::common::shared_spi_coordinator().release(token_);
        }
    }

    bool locked() const { return locked_; }

  private:
    sys::runtime::BusAcquireResult result_{};
    sys::runtime::BusAccessToken token_{};
    bool locked_ = false;
};

bool sd_fs_ready_cb(lv_fs_drv_t* drv)
{
    LV_UNUSED(drv);
    return ::platform::esp::arduino_common::storage::sd_card_ready();
}

void* sd_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    // This is a platform adapter boundary. Product/UI code must not use LVGL
    // FS as a synchronous SD probe; callers should submit runtime work and let
    // worker-domain code handle retry/backpressure.
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return nullptr;
    }

    const char* open_mode = "r";
    if (mode == LV_FS_MODE_WR)
    {
        open_mode = "w";
    }
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
    {
        open_mode = "r+";
    }

    auto* file = new (std::nothrow)::platform::esp::arduino_common::storage::SdRuntimeFile();
    if (file == nullptr)
    {
        return nullptr;
    }
    if (!file->open(path, open_mode))
    {
        delete file;
        return nullptr;
    }
    return file;
}

lv_fs_res_t sd_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsCloseWait), current_sd_fs_owner());
    file->close();
    delete file;
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr || buf == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return LV_FS_RES_BUSY;
    }

    const int result = file->read(buf, btr);
    if (result < 0)
    {
        return sd_fs_error_to_res();
    }
    if (br != nullptr)
    {
        *br = static_cast<uint32_t>(result);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_write(lv_fs_drv_t* drv,
                        void* file_p,
                        const void* buf,
                        uint32_t btw,
                        uint32_t* bw)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr || buf == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return LV_FS_RES_BUSY;
    }

    const std::size_t result = file->write(buf, btw);
    if (bw != nullptr)
    {
        *bw = static_cast<uint32_t>(result);
    }
    return result == btw ? LV_FS_RES_OK : sd_fs_error_to_res();
}

lv_fs_res_t sd_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return LV_FS_RES_BUSY;
    }

    uint64_t target = pos;
    switch (whence)
    {
    case LV_FS_SEEK_CUR:
        target = file->position() + pos;
        break;
    case LV_FS_SEEK_END:
        target = file->size() + pos;
        break;
    case LV_FS_SEEK_SET:
    default:
        target = pos;
        break;
    }

    return file->seek(target) ? LV_FS_RES_OK : sd_fs_error_to_res();
}

lv_fs_res_t sd_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p)
{
    LV_UNUSED(drv);
    auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(file_p);
    if (file == nullptr || pos_p == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return LV_FS_RES_BUSY;
    }
    *pos_p = static_cast<uint32_t>(file->position());
    return LV_FS_RES_OK;
}

void* sd_fs_dir_open(lv_fs_drv_t* drv, const char* path)
{
    LV_UNUSED(drv);
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return nullptr;
    }

    auto* dir = new (std::nothrow)::platform::esp::arduino_common::storage::SdRuntimeDir();
    if (dir == nullptr)
    {
        return nullptr;
    }
    if (!dir->open(path))
    {
        delete dir;
        return nullptr;
    }
    return dir;
}

lv_fs_res_t sd_fs_dir_read(lv_fs_drv_t* drv, void* dir_p, char* fn, uint32_t fn_len)
{
    LV_UNUSED(drv);
    auto* dir = static_cast<::platform::esp::arduino_common::storage::SdRuntimeDir*>(dir_p);
    if (dir == nullptr || fn == nullptr || fn_len == 0)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsWait), current_sd_fs_owner());
    if (!spi_guard.locked())
    {
        return LV_FS_RES_BUSY;
    }

    bool is_dir = false;
    if (!dir->read_next(fn, fn_len, &is_dir))
    {
        lv_strlcpy(fn, "", fn_len);
        return LV_FS_RES_OK;
    }
    if (is_dir && fn[0] != '/')
    {
        char tmp[LV_FS_MAX_PATH_LEN];
        lv_snprintf(tmp, sizeof(tmp), "/%s", fn);
        lv_strlcpy(fn, tmp, fn_len);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t sd_fs_dir_close(lv_fs_drv_t* drv, void* dir_p)
{
    LV_UNUSED(drv);
    auto* dir = static_cast<::platform::esp::arduino_common::storage::SdRuntimeDir*>(dir_p);
    if (dir == nullptr)
    {
        return LV_FS_RES_INV_PARAM;
    }
    LvglSdBusGuard spi_guard(
        current_sd_fs_wait(kLvglSdFsCloseWait), current_sd_fs_owner());
    dir->close();
    delete dir;
    return LV_FS_RES_OK;
}

bool flash_fs_ready_for_io()
{
    return ::ui::fs::ensure_flash_storage_ready(false);
}

bool flash_fs_ready_cb(lv_fs_drv_t* drv)
{
    LV_UNUSED(drv);
    return flash_fs_ready_for_io();
}

void flash_fs_make_full_path(const char* path, char* out, size_t out_size)
{
    if (path == nullptr || path[0] == '\0')
    {
        lv_snprintf(out, out_size, "%s", kLvglFlashFsMountPoint);
        return;
    }

    if (path[0] == '/')
    {
        lv_snprintf(out, out_size, "%s%s", kLvglFlashFsMountPoint, path);
        return;
    }

    lv_snprintf(out, out_size, "%s/%s", kLvglFlashFsMountPoint, path);
}

void* flash_fs_open(lv_fs_drv_t* drv, const char* path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);
    static uint8_t s_open_fail_log_count = 0;

    if (!flash_fs_ready_for_io())
    {
        if (s_open_fail_log_count < 8)
        {
            Serial.printf("[LVGL][FS] F open blocked: flash not ready path=%s\n",
                          path ? path : "<null>");
            ++s_open_fail_log_count;
        }
        return nullptr;
    }

    int flags = 0;
    if (mode == LV_FS_MODE_WR)
    {
        flags = O_WRONLY | O_CREAT;
    }
    else if (mode == LV_FS_MODE_RD)
    {
        flags = O_RDONLY;
    }
    else if (mode == (LV_FS_MODE_WR | LV_FS_MODE_RD))
    {
        flags = O_RDWR | O_CREAT;
    }

    char full_path[LV_FS_MAX_PATH_LEN];
    flash_fs_make_full_path(path, full_path, sizeof(full_path));

    const int fd = ::open(full_path, flags, 0666);
    if (fd < 0)
    {
        if (s_open_fail_log_count < 8)
        {
            Serial.printf("[LVGL][FS] F open failed path=%s full=%s errno=%d mode=%u\n",
                          path ? path : "<null>",
                          full_path,
                          errno,
                          static_cast<unsigned>(mode));
            ++s_open_fail_log_count;
        }
        return nullptr;
    }

    return flash_fs_ptr_from_fd(fd);
}

lv_fs_res_t flash_fs_close(lv_fs_drv_t* drv, void* file_p)
{
    LV_UNUSED(drv);
    return ::close(flash_fs_fd_from_ptr(file_p)) < 0 ? flash_fs_errno_to_res(errno)
                                                     : LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_read(lv_fs_drv_t* drv, void* file_p, void* buf, uint32_t btr, uint32_t* br)
{
    LV_UNUSED(drv);

    const ssize_t result = ::read(flash_fs_fd_from_ptr(file_p), buf, btr);
    if (result < 0)
    {
        return flash_fs_errno_to_res(errno);
    }

    if (br != nullptr)
    {
        *br = static_cast<uint32_t>(result);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_write(lv_fs_drv_t* drv,
                           void* file_p,
                           const void* buf,
                           uint32_t btw,
                           uint32_t* bw)
{
    LV_UNUSED(drv);

    const ssize_t result = ::write(flash_fs_fd_from_ptr(file_p), buf, btw);
    if (result < 0)
    {
        return flash_fs_errno_to_res(errno);
    }

    if (bw != nullptr)
    {
        *bw = static_cast<uint32_t>(result);
    }
    return LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_seek(lv_fs_drv_t* drv, void* file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);

    int origin = SEEK_SET;
    switch (whence)
    {
    case LV_FS_SEEK_CUR:
        origin = SEEK_CUR;
        break;
    case LV_FS_SEEK_END:
        origin = SEEK_END;
        break;
    case LV_FS_SEEK_SET:
    default:
        origin = SEEK_SET;
        break;
    }

    return ::lseek(flash_fs_fd_from_ptr(file_p), static_cast<off_t>(pos), origin) < 0
               ? flash_fs_errno_to_res(errno)
               : LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_tell(lv_fs_drv_t* drv, void* file_p, uint32_t* pos_p)
{
    LV_UNUSED(drv);

    const off_t offset = ::lseek(flash_fs_fd_from_ptr(file_p), 0, SEEK_CUR);
    if (offset < 0)
    {
        return flash_fs_errno_to_res(errno);
    }

    if (pos_p != nullptr)
    {
        *pos_p = static_cast<uint32_t>(offset);
    }
    return LV_FS_RES_OK;
}

void* flash_fs_dir_open(lv_fs_drv_t* drv, const char* path)
{
    LV_UNUSED(drv);
    static uint8_t s_dir_fail_log_count = 0;

    if (!flash_fs_ready_for_io())
    {
        if (s_dir_fail_log_count < 8)
        {
            Serial.printf("[LVGL][FS] F dir_open blocked: flash not ready path=%s\n",
                          path ? path : "<null>");
            ++s_dir_fail_log_count;
        }
        return nullptr;
    }

    char full_path[LV_FS_MAX_PATH_LEN];
    flash_fs_make_full_path(path, full_path, sizeof(full_path));
    DIR* dir = ::opendir(full_path);
    if (!dir && s_dir_fail_log_count < 8)
    {
        Serial.printf("[LVGL][FS] F dir_open failed path=%s full=%s errno=%d\n",
                      path ? path : "<null>",
                      full_path,
                      errno);
        ++s_dir_fail_log_count;
    }
    return dir;
}

lv_fs_res_t flash_fs_dir_read(lv_fs_drv_t* drv, void* dir_p, char* fn, uint32_t fn_len)
{
    LV_UNUSED(drv);

    if (fn == nullptr || fn_len == 0)
    {
        return LV_FS_RES_INV_PARAM;
    }

    struct dirent* entry = nullptr;
    do
    {
        entry = ::readdir(static_cast<DIR*>(dir_p));
        if (entry == nullptr)
        {
            lv_strlcpy(fn, "", fn_len);
            return LV_FS_RES_OK;
        }

        if (entry->d_type == DT_DIR)
        {
            lv_snprintf(fn, fn_len, "/%s", entry->d_name);
        }
        else
        {
            lv_strlcpy(fn, entry->d_name, fn_len);
        }
    } while (lv_strcmp(fn, "/.") == 0 || lv_strcmp(fn, "/..") == 0);

    return LV_FS_RES_OK;
}

lv_fs_res_t flash_fs_dir_close(lv_fs_drv_t* drv, void* dir_p)
{
    LV_UNUSED(drv);
    return ::closedir(static_cast<DIR*>(dir_p)) < 0 ? flash_fs_errno_to_res(errno)
                                                    : LV_FS_RES_OK;
}

void init_flash_fs_driver()
{
    if (s_flash_fs_ready)
    {
        return;
    }

    lv_fs_drv_init(&s_flash_fs_drv);
    s_flash_fs_drv.letter = kLvglFlashFsLetter;
    s_flash_fs_drv.ready_cb = flash_fs_ready_cb;
    s_flash_fs_drv.open_cb = flash_fs_open;
    s_flash_fs_drv.close_cb = flash_fs_close;
    s_flash_fs_drv.read_cb = flash_fs_read;
    s_flash_fs_drv.write_cb = flash_fs_write;
    s_flash_fs_drv.seek_cb = flash_fs_seek;
    s_flash_fs_drv.tell_cb = flash_fs_tell;
    s_flash_fs_drv.dir_open_cb = flash_fs_dir_open;
    s_flash_fs_drv.dir_read_cb = flash_fs_dir_read;
    s_flash_fs_drv.dir_close_cb = flash_fs_dir_close;
    lv_fs_drv_register(&s_flash_fs_drv);
    s_flash_fs_ready = true;

    char letters[16];
    letters[0] = '\0';
    lv_fs_get_letters(letters);
    Serial.printf("[LVGL][FS] flash driver registered letter=%c ready=%d letters=%s\n",
                  kLvglFlashFsLetter,
                  lv_fs_is_ready(kLvglFlashFsLetter) ? 1 : 0,
                  letters);
}

void init_sd_fs_driver()
{
    if (s_sd_fs_ready)
    {
        return;
    }

    lv_fs_drv_init(&s_sd_fs_drv);
    s_sd_fs_drv.letter = TRAIL_MATE_LVGL_SD_FS_LETTER;
    s_sd_fs_drv.ready_cb = sd_fs_ready_cb;
    s_sd_fs_drv.open_cb = sd_fs_open;
    s_sd_fs_drv.close_cb = sd_fs_close;
    s_sd_fs_drv.read_cb = sd_fs_read;
    s_sd_fs_drv.write_cb = sd_fs_write;
    s_sd_fs_drv.seek_cb = sd_fs_seek;
    s_sd_fs_drv.tell_cb = sd_fs_tell;
    s_sd_fs_drv.dir_open_cb = sd_fs_dir_open;
    s_sd_fs_drv.dir_read_cb = sd_fs_dir_read;
    s_sd_fs_drv.dir_close_cb = sd_fs_dir_close;
    lv_fs_drv_register(&s_sd_fs_drv);
    s_sd_fs_ready = true;

    char letters[16];
    letters[0] = '\0';
    lv_fs_get_letters(letters);
    Serial.printf("[LVGL][FS] SD driver registered letter=%c ready=%d letters=%s\n",
                  TRAIL_MATE_LVGL_SD_FS_LETTER,
                  lv_fs_is_ready(TRAIL_MATE_LVGL_SD_FS_LETTER) ? 1 : 0,
                  letters);
}
} // namespace

static void disp_flush(lv_display_t* disp_drv, const lv_area_t* area, uint8_t* color_p)
{
    static uint8_t s_tdeck_pro_flush_log_count = 0;
#if LV_TEST_FLUSH_LOG
    static uint32_t s_flush_count = 0;
    static uint32_t s_flush_last_ms = 0;
    static lv_area_t s_last_area = {0, 0, 0, 0};
#if LV_TEST_FLUSH_SAMPLE
    static uint32_t s_zero_streak = 0;
#endif
#endif
    size_t len = lv_area_get_size(area);
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    auto* plane = (LilyGo_Display*)lv_display_get_user_data(disp_drv);

#if defined(ARDUINO_T_DECK_PRO)
    if (s_tdeck_pro_flush_log_count < 8)
    {
        uint32_t blackish = 0;
        if (color_p != nullptr)
        {
            const uint16_t* pix = reinterpret_cast<const uint16_t*>(color_p);
            for (size_t i = 0; i < len; ++i)
            {
                if (pix[i] == 0x0000)
                {
                    blackish++;
                }
            }
        }
        Serial.printf("[LVGL][TDeckPro] flush #%u area=(%d,%d)-(%d,%d) len=%lu rgb565_black=%lu\n",
                      static_cast<unsigned>(s_tdeck_pro_flush_log_count + 1),
                      static_cast<int>(area->x1),
                      static_cast<int>(area->y1),
                      static_cast<int>(area->x2),
                      static_cast<int>(area->y2),
                      static_cast<unsigned long>(len),
                      static_cast<unsigned long>(blackish));
        s_tdeck_pro_flush_log_count++;
    }
#endif

    const bool transferred =
        plane->pushColorsResult(area->x1, area->y1, w, h, (uint16_t*)color_p);
    if (!transferred)
    {
        auto& coordinator = platform::esp::common::shared_spi_coordinator();
        coordinator.noteDisplayFrameFailed();
        Serial.printf("[SPI][DISPLAY] flush_recovery area=(%d,%d)-(%d,%d) "
                      "requests=%lu completed=%lu busy=%lu failed=%lu\n",
                      static_cast<int>(area->x1),
                      static_cast<int>(area->y1),
                      static_cast<int>(area->x2),
                      static_cast<int>(area->y2),
                      static_cast<unsigned long>(coordinator.displayFrameRequests()),
                      static_cast<unsigned long>(coordinator.displayFrameCompletions()),
                      static_cast<unsigned long>(coordinator.displayFrameBusyRetries()),
                      static_cast<unsigned long>(coordinator.displayFrameFailures()));
        // LVGL cannot be left waiting forever for a transfer that was never
        // started. Complete this flush through the explicit recovery path and
        // invalidate both the active screen and the top layer so the next
        // timer pass retries the pixels instead of accepting a silent drop.
        lv_display_flush_ready(disp_drv);
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
        return;
    }

    platform::esp::common::shared_spi_coordinator().noteDisplayFrameCompleted();
    lv_display_flush_ready(disp_drv);

#if LV_TEST_FLUSH_LOG
#if LV_TEST_FLUSH_SAMPLE
    bool all_zero_sample = false;
    if (color_p && len > 0)
    {
        const uint16_t* pix = reinterpret_cast<const uint16_t*>(color_p);
        size_t step = len / 8;
        if (step == 0) step = 1;
        all_zero_sample = true;
        for (size_t i = 0, idx = 0; i < 8 && idx < len; ++i, idx += step)
        {
            if (pix[idx] != 0)
            {
                all_zero_sample = false;
                break;
            }
        }
    }
    if (all_zero_sample)
    {
        s_zero_streak++;
    }
    else
    {
        s_zero_streak = 0;
    }
#endif
    s_flush_count++;
    s_last_area = *area;
    uint32_t now_ms = millis();
    if (now_ms - s_flush_last_ms >= 1000)
    {
        lv_obj_t* scr = lv_screen_active();
        unsigned child_cnt = scr ? (unsigned)lv_obj_get_child_cnt(scr) : 0;
        Serial.printf("[LVGL] flush/s=%lu last_area=%d,%d-%d,%d children=%u",
                      (unsigned long)s_flush_count,
                      (int)s_last_area.x1, (int)s_last_area.y1,
                      (int)s_last_area.x2, (int)s_last_area.y2,
                      child_cnt);
#if LV_TEST_FLUSH_SAMPLE
        Serial.printf(" zero_streak=%lu", (unsigned long)s_zero_streak);
#endif
        Serial.printf("\n");
        s_flush_count = 0;
        s_flush_last_ms = now_ms;
    }
#endif
}

#ifdef USING_INPUT_DEV_TOUCHPAD
static void touchpad_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    static int16_t x, y;
    static bool was_touched = false;
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    uint8_t touched = plane->getPoint(&x, &y, 1);
    if (touched)
    {
        was_touched = true;
        input::MorseEngine::notifyTouch();
        if (::platform::ui::screen::is_sleeping() ||
            ::platform::ui::screen::is_saver_active())
        {
            ::platform::ui::screen::handle_input();
            data->state = LV_INDEV_STATE_REL;
            return;
        }
        ::platform::ui::screen::record_activity();
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }
    if (was_touched)
    {
        was_touched = false;
        ::platform::ui::screen::handle_input_release();
    }
    data->state = LV_INDEV_STATE_REL;
}
#endif

#ifdef USING_INPUT_DEV_ROTARY
// Forward declaration from GPS UI runtime

extern bool isGPSLoadingTiles();

static void lv_encoder_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    RotaryMsg_t msg = plane->getRotary();
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;
#endif

    // Screen power transitions consume the input event before normal UI
    // navigation. The state machine decides whether this is a wake preview
    // or confirmation into the main menu.
    if (::platform::ui::screen::is_sleeping() ||
        ::platform::ui::screen::is_saver_active())
    {
        if (msg.dir != ROTARY_DIR_NONE || msg.centerBtnPressed)
        {
            ::platform::ui::screen::handle_input();
        }
#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
        // T-Deck path already reset above.
#else
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED; // Don't pass input to UI
#endif
        return;
    }

    // If GPS is loading tiles, ignore input
    if (isGPSLoadingTiles())
    {
#if !defined(ARDUINO_T_DECK) && !defined(ARDUINO_T_DECK_PRO)
        data->enc_diff = 0;
        data->state = LV_INDEV_STATE_RELEASED;
#endif
        return;
    }

    // Screen is awake, process input normally
    if (msg.dir != ROTARY_DIR_NONE || msg.centerBtnPressed)
    {
        ::platform::ui::screen::record_activity();
    }

    switch (msg.dir)
    {
    case ROTARY_DIR_UP:
        data->enc_diff = 1;
        break;
    case ROTARY_DIR_DOWN:
        data->enc_diff = -1;
        break;
    default:
#if !defined(ARDUINO_T_DECK) && !defined(ARDUINO_T_DECK_PRO)
        data->state = LV_INDEV_STATE_RELEASED;
#endif
        break;
    }
    if (msg.centerBtnPressed)
    {
        data->state = LV_INDEV_STATE_PRESSED;
    }
    plane->feedback((void*)drv);
}
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
static void keypad_read(lv_indev_t* drv, lv_indev_data_t* data)
{
    char c = '\0';
    uint32_t key = 0;
    bool from_nav = false;
    auto* plane = (LilyGo_Display*)lv_indev_get_user_data(drv);
    int state = plane->getNavKey(&key);
    if (state >= 0)
    {
        from_nav = true;
        switch (key)
        {
        case INPUT_NAV_KEY_LEFT:
            key = LV_KEY_LEFT;
            break;
        case INPUT_NAV_KEY_RIGHT:
            key = LV_KEY_RIGHT;
            break;
        case INPUT_NAV_KEY_UP:
            key = LV_KEY_UP;
            break;
        case INPUT_NAV_KEY_DOWN:
            key = LV_KEY_DOWN;
            break;
        case INPUT_NAV_KEY_ENTER:
            key = LV_KEY_ENTER;
            break;
        default:
            state = -1;
            break;
        }
    }
    else
    {
        state = plane->getKeyChar(&c);
        if (state >= 0)
        {
            key = static_cast<uint8_t>(c);
        }
    }

    // Screen power transitions consume the input event before normal keyboard
    // dispatch. The state machine applies the same two-step policy to every
    // physical input device.
    if (::platform::ui::screen::is_sleeping() ||
        ::platform::ui::screen::is_saver_active())
    {
        if (state == KEYBOARD_PRESSED)
        {
            ::platform::ui::screen::handle_input();
        }
        else if (state == KEYBOARD_RELEASED)
        {
            ::platform::ui::screen::handle_input_release();
        }
        data->state = LV_INDEV_STATE_REL; // Don't pass key to UI
        return;
    }

    // Screen is awake, process input normally
    if (!from_nav && (state == KEYBOARD_PRESSED || state == KEYBOARD_RELEASED))
    {
        AppScreen* active_app = ui_get_active_app();
        const bool on_menu = active_app == nullptr;
        const bool on_walkie_page =
            active_app != nullptr && active_app->stable_id() != nullptr &&
            std::strcmp(active_app->stable_id(), "walkie_talkie") == 0;

        if (on_menu && ui::menu_runtime::handleWalkieKey(c, state))
        {
            ::platform::ui::screen::record_activity();
            plane->feedback((void*)drv);
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        if (on_menu && ui::menu_runtime::handleShortcutKey(c, state))
        {
            ::platform::ui::screen::record_activity();
            plane->feedback((void*)drv);
            data->state = LV_INDEV_STATE_REL;
            return;
        }

        if (on_walkie_page)
        {
            walkie::on_key_event(c, state);
        }
    }

#if defined(ARDUINO_T_DECK) || defined(ARDUINO_T_DECK_PRO)
    if (!from_nav && state == KEYBOARD_PRESSED && c == ' ' && ui_get_active_app() == nullptr)
    {
        ::platform::ui::screen::record_activity();
        menu_show();
        plane->feedback((void*)drv);
        data->state = LV_INDEV_STATE_REL;
        return;
    }
#endif

    if (state == KEYBOARD_PRESSED)
    {
        ::platform::ui::screen::record_activity();
        data->key = key;
        data->state = LV_INDEV_STATE_PR;
        plane->feedback((void*)drv);
        return;
    }
    data->state = LV_INDEV_STATE_REL;
}
#endif

static uint32_t lv_tick_get_callback(void)
{
    return millis();
}

static void lv_rounder_cb(lv_event_t* e)
{
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    if (!(area->x2 & 1))
        area->x2++;
    if (area->y1 & 1)
        area->y1--;
    if (!(area->y2 & 1))
        area->y2++;
}

static void lv_res_changed_cb(lv_event_t* e)
{
    auto* plane = (LilyGo_Display*)lv_event_get_user_data(e);
    plane->setRotation(lv_display_get_rotation(NULL));
}

void beginLvglHelper(LilyGo_Display& board, bool debug)
{
    Serial.println("[LVGL] init");
    lv_init();
    Serial.println("[LVGL] init done");

    init_sd_fs_driver();
    init_flash_fs_driver();

#if LV_USE_LOG
    if (debug)
    {
        lv_log_register_print_cb(lv_log_print_g_cb);
    }
#endif

    // Allocate display buffers
    // Use DMA-capable memory if board supports DMA, otherwise use PSRAM
    bool use_dma_draw_buffers = board.useDMA();
#if LV_TEST_FORCE_DMA_BUF
    use_dma_draw_buffers = true;
#endif
    Serial.printf("[LVGL] buffer alloc start (use_dma_draw_buffers=%d)\n", use_dma_draw_buffers ? 1 : 0);
    Serial.printf("[LVGL] free heap internal=%u dma=%u psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
    size_t lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);
    size_t full_screen_size = lv_buffer_size;

    if (use_dma_draw_buffers)
    {
        // For DMA, keep internal RAM pressure low to avoid starving other subsystems.
#if LV_TEST_FORCE_DMA_FULL_SIZE
        // keep full size for testing if memory allows
#else
#if defined(ARDUINO_T_DECK)
        lv_buffer_size = board.width() * kTDeckDmaDrawBufferLines * sizeof(lv_color16_t);
#else
        lv_buffer_size = (board.width() * board.height() / 6) * sizeof(lv_color16_t);
#endif
#endif
        buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
        log_d("Using DMA buffers, size: %d bytes each", lv_buffer_size);

        if (!buf || !buf1)
        {
            Serial.println("[LVGL] DMA buffer alloc failed, fallback to PSRAM");
            if (buf)
            {
                free(buf);
            }
            if (buf1)
            {
                free(buf1);
            }
            buf = nullptr;
            buf1 = nullptr;
            use_dma_draw_buffers = false;
        }
    }
    if (!use_dma_draw_buffers)
    {
        lv_buffer_size = board.width() * board.height() * sizeof(lv_color16_t);
#if HAS_PSRAM
        // For non-DMA, use full screen buffer in PSRAM
        buf = (lv_color16_t*)ps_malloc(lv_buffer_size);
        buf1 = (lv_color16_t*)ps_malloc(lv_buffer_size);
        log_d("Using PSRAM buffers, size: %d bytes each", lv_buffer_size);
        if (!buf || !buf1)
        {
            if (buf)
            {
                free(buf);
            }
            if (buf1)
            {
                free(buf1);
            }
            buf = nullptr;
            buf1 = nullptr;

            // Avoid grabbing huge internal buffers if PSRAM is unavailable.
            lv_buffer_size = (board.width() * board.height() / 8) * sizeof(lv_color16_t);
            if (lv_buffer_size < (board.width() * 20U * sizeof(lv_color16_t)))
            {
                lv_buffer_size = board.width() * 20U * sizeof(lv_color16_t);
            }
            buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
            buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DMA);
            if (!buf || !buf1)
            {
                if (buf)
                {
                    free(buf);
                }
                if (buf1)
                {
                    free(buf1);
                }
                buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
                buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
            }
            Serial.printf("[LVGL] PSRAM alloc failed, fallback to small internal buffers (size=%u)\n",
                          (unsigned)lv_buffer_size);
        }
#else
        // For boards without PSRAM, use heap
        buf = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
        buf1 = (lv_color16_t*)heap_caps_malloc(lv_buffer_size, MALLOC_CAP_DEFAULT);
        Serial.println("[LVGL] PSRAM disabled, using heap buffers");
#endif
    }

    if (!buf || !buf1)
    {
        Serial.println("[LVGL] Failed to allocate display buffers");
        log_e("Failed to allocate LVGL display buffers!");
        return;
    }

    Serial.printf("[LVGL] buffers ready (size=%u)\n", static_cast<unsigned>(lv_buffer_size));
    disp_drv = lv_display_create(board.width(), board.height());

    if (board.needFullRefresh())
    {
        lv_display_render_mode_t mode = LV_DISPLAY_RENDER_MODE_FULL;
        if (lv_buffer_size < full_screen_size)
        {
            mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
            Serial.println("[LVGL] full-refresh downgraded to partial due buffer size");
        }
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, mode);
    }
    else
    {
        lv_display_set_buffers(disp_drv, buf, buf1, lv_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
        lv_display_add_event_cb(disp_drv, lv_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    }
    Serial.println("[LVGL] display buffers set");
    Serial.printf("[LVGL] free heap after alloc internal=%u dma=%u psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                  (unsigned)ESP.getFreePsram());
    lv_display_set_color_format(disp_drv, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(disp_drv, disp_flush);
    lv_display_set_user_data(disp_drv, &board);

    lv_display_set_resolution(disp_drv, board.width(), board.height());
    lv_display_set_rotation(disp_drv, (lv_display_rotation_t)board.getRotation());
    lv_display_add_event_cb(disp_drv, lv_res_changed_cb, LV_EVENT_RESOLUTION_CHANGED, &board);

    lv_group_t* default_group = lv_group_get_default();
    if (default_group == nullptr)
    {
        default_group = lv_group_create();
        lv_group_set_default(default_group);
    }

#ifdef USING_INPUT_DEV_TOUCHPAD

    if (board.hasTouch())
    {
        indev_touch = lv_indev_create();
        lv_indev_set_type(indev_touch, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev_touch, touchpad_read);
        lv_indev_set_user_data(indev_touch, &board);
        lv_indev_enable(indev_touch, true);
        lv_indev_set_display(indev_touch, disp_drv);
        lv_indev_set_group(indev_touch, default_group);
    }
#endif

#ifdef USING_INPUT_DEV_ROTARY
    if (board.hasEncoder())
    {
        indev_encoder = lv_indev_create();
        lv_indev_set_type(indev_encoder, LV_INDEV_TYPE_ENCODER);
        lv_indev_set_read_cb(indev_encoder, lv_encoder_read);
        lv_indev_set_user_data(indev_encoder, &board);
        lv_indev_enable(indev_encoder, true);
        lv_indev_set_display(indev_encoder, disp_drv);
        lv_indev_set_group(indev_encoder, default_group);
    }
#endif

#ifdef USING_INPUT_DEV_KEYBOARD
    if (board.hasKeyboard() || board.hasNavKeys())
    {
        indev_keyboard = lv_indev_create();
        lv_indev_set_type(indev_keyboard, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(indev_keyboard, keypad_read);
        lv_indev_set_user_data(indev_keyboard, &board);
        lv_indev_enable(indev_keyboard, true);
        lv_indev_set_display(indev_keyboard, disp_drv);
        lv_indev_set_group(indev_keyboard, default_group);
    }
#endif

    lv_tick_set_cb(lv_tick_get_callback);
    lv_set_default_group(default_group);
}

void lv_set_default_group(lv_group_t* group)
{
    lv_indev_t* cur_drv = NULL;
    for (;;)
    {
        cur_drv = lv_indev_get_next(cur_drv);
        if (!cur_drv)
        {
            break;
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_KEYPAD)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_ENCODER)
        {
            lv_indev_set_group(cur_drv, group);
        }
        if (lv_indev_get_type(cur_drv) == LV_INDEV_TYPE_POINTER)
        {
            lv_indev_set_group(cur_drv, group);
        }
    }
    lv_group_set_default(group);
}

lv_indev_t* lv_get_touch_indev()
{
    return indev_touch;
}

lv_indev_t* lv_get_keyboard_indev()
{
    return indev_keyboard;
}

lv_indev_t* lv_get_encoder_indev()
{
    return indev_encoder;
}

const char* bus_acquire_status_name(sys::runtime::BusAcquireStatus status)
{
    switch (status)
    {
    case sys::runtime::BusAcquireStatus::Acquired:
        return "acquired";
    case sys::runtime::BusAcquireStatus::Busy:
        return "busy";
    case sys::runtime::BusAcquireStatus::TimedOut:
        return "timeout";
    case sys::runtime::BusAcquireStatus::Unavailable:
        return "unavailable";
    default:
        return "unknown";
    }
}

bool lv_begin_external_font_load_fs_scope()
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    unsigned depth = s_external_font_load_fs_depth.load(std::memory_order_acquire);
    if (depth > 0)
    {
        if (s_external_font_owner_task == current_task && s_external_font_bus_token.valid)
        {
            s_external_font_load_fs_depth.fetch_add(1, std::memory_order_acq_rel);
            return true;
        }

        const uint32_t now_ms = sys::millis_now();
        if (s_external_font_last_busy_log_ms == 0 ||
            static_cast<uint32_t>(now_ms - s_external_font_last_busy_log_ms) >=
                kLvglExternalFontLogIntervalMs)
        {
            Serial.printf("[SPI][LVGL_FONT] acquire failed reason=owned_by_other depth=%lu owner_task=%s current_task=%s\n",
                          static_cast<unsigned long>(depth),
                          s_external_font_owner_task ? pcTaskGetName(s_external_font_owner_task)
                                                     : "-",
                          current_task ? pcTaskGetName(current_task) : "-");
            s_external_font_last_busy_log_ms = now_ms;
        }
        return false;
    }

    sys::runtime::BusAcquireRequest request{};
    const uint32_t start_ms = sys::millis_now();
    request.resource = kLvglExternalFontBusResource;
    request.policy = sys::runtime::BusAccessPolicy::RecoveryExclusive;
    request.command_id = kLvglExternalFontCommandId;
    request.deadline_ms = start_ms + kLvglExternalFontBusAcquireMs;
    request.origin = kLvglExternalFontCommandId;
    request.owner_label = kLvglExternalFontOwner;

    sys::runtime::BusAcquireResult result =
        ::platform::esp::common::shared_spi_coordinator().acquire(request);
    if (result.status != sys::runtime::BusAcquireStatus::Acquired || !result.token.valid)
    {
        const uint32_t now_ms = sys::millis_now();
        if (s_external_font_last_busy_log_ms == 0 ||
            static_cast<uint32_t>(now_ms - s_external_font_last_busy_log_ms) >=
                kLvglExternalFontLogIntervalMs)
        {
            Serial.printf("[SPI][LVGL_FONT] acquire failed status=%s wait_ms=%lu owner=%s task=%s\n",
                          bus_acquire_status_name(result.status),
                          static_cast<unsigned long>(result.diagnostics.wait_ms),
                          kLvglExternalFontOwner,
                          current_task ? pcTaskGetName(current_task) : "-");
            s_external_font_last_busy_log_ms = now_ms;
        }
        return false;
    }

    s_external_font_bus_token = result.token;
    s_external_font_owner_task = current_task;
    s_external_font_acquired_ms = result.token.acquired_ms;
    s_external_font_load_fs_depth.store(1, std::memory_order_release);
    s_external_font_last_busy_log_ms = 0;

    Serial.printf("[SPI][LVGL_FONT] acquire ok wait_ms=%lu owner=%s task=%s\n",
                  static_cast<unsigned long>(result.diagnostics.wait_ms),
                  kLvglExternalFontOwner,
                  current_task ? pcTaskGetName(current_task) : "-");
    return true;
}

void lv_end_external_font_load_fs_scope()
{
    unsigned depth = s_external_font_load_fs_depth.load(std::memory_order_acquire);
    while (depth > 0)
    {
        const unsigned next_depth = depth - 1U;
        if (!s_external_font_load_fs_depth.compare_exchange_weak(
                depth, next_depth, std::memory_order_acq_rel))
        {
            continue;
        }
        if (next_depth > 0)
        {
            return;
        }

        sys::runtime::BusAccessToken token = s_external_font_bus_token;
        s_external_font_bus_token = {};
        s_external_font_owner_task = nullptr;
        const uint32_t now_ms = sys::millis_now();
        const uint32_t hold_ms = s_external_font_acquired_ms == 0
                                     ? 0
                                     : static_cast<uint32_t>(now_ms - s_external_font_acquired_ms);
        s_external_font_acquired_ms = 0;

        if (token.valid)
        {
            ::platform::esp::common::shared_spi_coordinator().release(token);
            Serial.printf("[SPI][LVGL_FONT] release hold_ms=%lu slow=%d\n",
                          static_cast<unsigned long>(hold_ms),
                          hold_ms >= kLvglExternalFontSlowHoldMs ? 1 : 0);
        }
        return;
    }

    Serial.printf("[SPI][LVGL_FONT] release skipped reason=no_active_scope\n");
}

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
namespace
{
constexpr size_t kLvglLargeAllocThresholdBytes = 2048;

bool lvgl_has_psram()
{
#if HAS_PSRAM
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
    return false;
#endif
}

uint32_t lvgl_primary_caps(size_t size)
{
    if (lvgl_has_psram() && size >= kLvglLargeAllocThresholdBytes)
    {
        return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    }
    return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
}

uint32_t lvgl_secondary_caps(size_t size)
{
    if (lvgl_has_psram() && size >= kLvglLargeAllocThresholdBytes)
    {
        return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    }
    return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
}
} // namespace

// LVGL 9.x custom memory management functions
extern "C" void lv_mem_init(void)
{
    return; /*Nothing to init*/
}

extern "C" void lv_mem_deinit(void)
{
    return; /*Nothing to deinit*/
}

extern "C" lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes)
{
    /*Not supported*/
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

extern "C" void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    /*Not supported*/
    LV_UNUSED(pool);
    return;
}

extern "C" void* lv_malloc_core(size_t size)
{
#if HAS_PSRAM
    if (size == 0)
    {
        return nullptr;
    }
    return heap_caps_malloc_prefer(size, 2, lvgl_primary_caps(size), lvgl_secondary_caps(size));
#else
    return malloc(size);
#endif
}

extern "C" void* lv_realloc_core(void* p, size_t new_size)
{
#if HAS_PSRAM
    if (new_size == 0)
    {
        heap_caps_free(p);
        return nullptr;
    }
    return heap_caps_realloc_prefer(
        p, new_size, 2, lvgl_primary_caps(new_size), lvgl_secondary_caps(new_size));
#else
    return realloc(p, new_size);
#endif
}

extern "C" void lv_free_core(void* p)
{
#if HAS_PSRAM
    heap_caps_free(p);
#else
    free(p);
#endif
}

extern "C" void lv_mem_monitor_core(lv_mem_monitor_t* mon_p)
{
    /*Not supported*/
    LV_UNUSED(mon_p);
    return;
}

lv_result_t lv_mem_test_core(void)
{
    /*Not supported*/
    return LV_RESULT_OK;
}
#endif

#endif
