#include "platform/esp/arduino_common/src/chat/internal/blob_store_io.h"

#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/ui/settings_store.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace chat::infra
{
namespace
{

constexpr std::size_t kRawBlobReadChunkBytes = 256;

bool valid_key(const char* value)
{
    return value && value[0] != '\0';
}

std::string absolute_sd_path(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return {};
    }

    const char* mount = ::platform::esp::idf_common::bsp_runtime::sdcard_mount_point();
    std::string absolute = mount ? mount : "";
    if (absolute.empty())
    {
        return {};
    }
    if (absolute.back() == '/' && path[0] == '/')
    {
        absolute.pop_back();
    }
    else if (absolute.back() != '/' && path[0] != '/')
    {
        absolute.push_back('/');
    }
    absolute += path;
    return absolute;
}

} // namespace

bool loadRawBlobFromSd(const char* path, std::vector<uint8_t>& out, std::size_t max_len)
{
    out.clear();
    if (!valid_key(path) ||
        !::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    const std::string absolute = absolute_sd_path(path);
    FILE* file = std::fopen(absolute.c_str(), "rb");
    if (!file)
    {
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0)
    {
        std::fclose(file);
        return false;
    }
    const long file_size = std::ftell(file);
    if (file_size <= 0 || static_cast<std::size_t>(file_size) > max_len ||
        std::fseek(file, 0, SEEK_SET) != 0)
    {
        std::fclose(file);
        return false;
    }

    out.reserve(static_cast<std::size_t>(file_size));
    uint8_t buffer[kRawBlobReadChunkBytes] = {};
    std::size_t total = 0;
    while (total < static_cast<std::size_t>(file_size))
    {
        const std::size_t chunk =
            std::min(sizeof(buffer), static_cast<std::size_t>(file_size) - total);
        const std::size_t read = std::fread(buffer, 1, chunk, file);
        if (read != chunk)
        {
            std::fclose(file);
            out.clear();
            return false;
        }
        out.insert(out.end(), buffer, buffer + read);
        total += read;
    }
    std::fclose(file);
    return true;
}

bool saveRawBlobToSd(const char* path, const uint8_t* data, std::size_t len)
{
    if (!valid_key(path) || (!data && len != 0) ||
        !::platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    const std::string absolute = absolute_sd_path(path);
    const std::string temporary = absolute + ".tmp";
    std::remove(temporary.c_str());

    FILE* file = std::fopen(temporary.c_str(), "wb");
    if (!file)
    {
        return false;
    }
    const std::size_t written = len == 0 ? 0 : std::fwrite(data, 1, len, file);
    const int close_status = std::fclose(file);
    if (written != len || close_status != 0)
    {
        std::remove(temporary.c_str());
        return false;
    }

    std::remove(absolute.c_str());
    if (std::rename(temporary.c_str(), absolute.c_str()) != 0)
    {
        std::remove(temporary.c_str());
        return false;
    }
    return true;
}

bool loadRawBlobFromPreferences(const char* ns,
                                const char* key,
                                std::vector<uint8_t>& out)
{
    out.clear();
    return valid_key(ns) && valid_key(key) &&
           ::platform::ui::settings_store::get_blob(ns, key, out) &&
           !out.empty();
}

bool saveRawBlobToPreferences(const char* ns,
                              const char* key,
                              const uint8_t* data,
                              std::size_t len)
{
    return valid_key(ns) && valid_key(key) &&
           ::platform::ui::settings_store::put_blob(ns, key, data, len);
}

void clearRawBlobFromPreferences(const char* ns, const char* key)
{
    clearPreferencesKeys(ns, key);
}

bool loadRawBlobFromPreferencesWithMetadata(const char* ns,
                                            const char* key,
                                            const char* version_key,
                                            const char* crc_key,
                                            std::vector<uint8_t>& out,
                                            PreferencesBlobMetadata* meta)
{
    out.clear();
    if (meta)
    {
        *meta = PreferencesBlobMetadata{};
    }
    if (!valid_key(ns) || !valid_key(key))
    {
        return false;
    }

    (void)::platform::ui::settings_store::get_blob(ns, key, out);
    PreferencesBlobMetadata loaded{};
    loaded.len = out.size();
    if (valid_key(version_key))
    {
        const uint32_t version =
            ::platform::ui::settings_store::get_uint(ns, version_key, UINT32_MAX);
        if (version != UINT32_MAX)
        {
            loaded.has_version = true;
            loaded.version = static_cast<uint8_t>(version);
        }
    }
    if (valid_key(crc_key))
    {
        const uint32_t crc =
            ::platform::ui::settings_store::get_uint(ns, crc_key, UINT32_MAX);
        if (crc != UINT32_MAX)
        {
            loaded.has_crc = true;
            loaded.crc = crc;
        }
    }
    if (meta)
    {
        *meta = loaded;
    }
    return true;
}

bool saveRawBlobToPreferencesWithMetadata(const char* ns,
                                          const char* key,
                                          const char* version_key,
                                          const char* crc_key,
                                          const uint8_t* data,
                                          std::size_t len,
                                          const PreferencesBlobMetadata* meta,
                                          bool retry_after_clear)
{
    if (!valid_key(ns) || !valid_key(key) || (!data && len != 0))
    {
        return false;
    }

    bool ok = ::platform::ui::settings_store::put_blob(ns, key, data, len);
    if (!ok && retry_after_clear)
    {
        clearPreferencesKeys(ns, key);
        ok = ::platform::ui::settings_store::put_blob(ns, key, data, len);
    }
    if (!ok)
    {
        return false;
    }

    if (valid_key(version_key))
    {
        if (meta && meta->has_version)
        {
            ::platform::ui::settings_store::put_uint(ns, version_key, meta->version);
        }
        else
        {
            clearPreferencesKeys(ns, version_key);
        }
    }
    if (valid_key(crc_key))
    {
        if (meta && meta->has_crc)
        {
            ::platform::ui::settings_store::put_uint(ns, crc_key, meta->crc);
        }
        else
        {
            clearPreferencesKeys(ns, crc_key);
        }
    }
    return true;
}

void clearPreferencesKeys(const char* ns,
                          const char* key_a,
                          const char* key_b,
                          const char* key_c)
{
    if (!valid_key(ns))
    {
        return;
    }

    const char* keys[3] = {};
    std::size_t count = 0;
    if (valid_key(key_a))
    {
        keys[count++] = key_a;
    }
    if (valid_key(key_b))
    {
        keys[count++] = key_b;
    }
    if (valid_key(key_c))
    {
        keys[count++] = key_c;
    }
    ::platform::ui::settings_store::remove_keys(ns, keys, count);
}

} // namespace chat::infra

#endif
