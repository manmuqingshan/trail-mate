#pragma once

#include "lvgl.h"

#include <string>

#if (defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO)) && __has_include(<FFat.h>)
#include <FFat.h>
#include <cstdio>
#include <sys/stat.h>
#define UI_FS_HAS_ARDUINO_FLASH_PACK_STORAGE 1
#else
#define UI_FS_HAS_ARDUINO_FLASH_PACK_STORAGE 0
#endif

#if UI_FS_HAS_ARDUINO_FLASH_PACK_STORAGE
#define UI_FS_HAS_FLASH_PACK_STORAGE 1
#else
#define UI_FS_HAS_FLASH_PACK_STORAGE 0
#endif

namespace ui::fs
{

inline bool ensure_flash_storage_ready(bool allow_format_on_fail = false)
{
#if UI_FS_HAS_ARDUINO_FLASH_PACK_STORAGE
    static bool mounted = false;
    static bool mount_attempted = false;
    static bool format_mount_attempted = false;

    struct stat st
    {
    };
    if (::stat("/fs", &st) == 0 && S_ISDIR(st.st_mode))
    {
        mounted = true;
        return true;
    }

    if (mounted)
    {
        return true;
    }

    if (!mount_attempted)
    {
        mount_attempted = true;
        mounted = FFat.begin(false, "/fs", 10, "ffat");
        std::printf("[I18N][FS] flash mount readonly=%d\n", mounted ? 1 : 0);
        if (mounted)
        {
            return true;
        }
    }

    if (allow_format_on_fail && !format_mount_attempted)
    {
        format_mount_attempted = true;
        mounted = FFat.begin(true, "/fs", 10, "ffat");
        std::printf("[I18N][FS] flash mount format_retry=%d\n", mounted ? 1 : 0);
    }

    return mounted;
#else
    (void)allow_format_on_fail;
    return false;
#endif
}

inline std::string normalize_path(const char* path)
{
    if (path == nullptr || path[0] == '\0')
    {
        return {};
    }

    if (path[0] != '\0' && path[1] == ':')
    {
#if UI_FS_HAS_FLASH_PACK_STORAGE
        if ((path[0] == 'F' || path[0] == 'f') && !ensure_flash_storage_ready(false))
        {
            return {};
        }
#endif
        return std::string(path);
    }

    if ((path[0] == '/' && path[1] == 'f' && path[2] == 's' &&
         (path[3] == '\0' || path[3] == '/')))
    {
#if UI_FS_HAS_FLASH_PACK_STORAGE
        if (!ensure_flash_storage_ready(false))
        {
            return {};
        }
#endif

        const char* relative = path + 3;
        if (*relative == '\0')
        {
            relative = "/";
        }
        return std::string("F:") + relative;
    }

    if (path[0] == '/')
    {
        return std::string("A:") + path;
    }

    return std::string("A:/") + path;
}

inline bool file_exists(const char* path)
{
    const std::string normalized = normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }
    lv_fs_close(&file);
    return true;
}

inline bool dir_exists(const char* path)
{
    const std::string normalized = normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_dir_t dir;
    if (lv_fs_dir_open(&dir, normalized.c_str()) != LV_FS_RES_OK)
    {
        return false;
    }
    lv_fs_dir_close(&dir);
    return true;
}

template <typename LineHandler>
inline bool read_text_file_lines(const char* path,
                                 LineHandler on_line,
                                 std::size_t max_line_bytes = 2048)
{
    const std::string normalized = normalize_path(path);
    if (normalized.empty())
    {
        return false;
    }

    lv_fs_file_t file;
    if (lv_fs_open(&file, normalized.c_str(), LV_FS_MODE_RD) != LV_FS_RES_OK)
    {
        return false;
    }

    char buffer[256];
    std::string line;
    bool ok = true;
    while (true)
    {
        uint32_t bytes_read = 0;
        if (lv_fs_read(&file, buffer, sizeof(buffer), &bytes_read) != LV_FS_RES_OK)
        {
            ok = false;
            break;
        }
        if (bytes_read == 0)
        {
            break;
        }
        for (uint32_t index = 0; index < bytes_read; ++index)
        {
            const char ch = buffer[index];
            if (ch == '\n')
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (!on_line(line))
                {
                    ok = false;
                    break;
                }
                line.clear();
                continue;
            }

            if (line.size() >= max_line_bytes)
            {
                ok = false;
                break;
            }
            line.push_back(ch);
        }
        if (!ok)
        {
            break;
        }
    }

    if (ok && !line.empty())
    {
        if (line.back() == '\r')
        {
            line.pop_back();
        }
        ok = on_line(line);
    }

    lv_fs_close(&file);
    return ok;
}

} // namespace ui::fs
