/**
 * @file blob_store_io.cpp
 * @brief Shared raw blob I/O helpers for legacy Arduino storage shells
 */

#include "blob_store_io.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <Preferences.h>
#include <algorithm>
#include <string>

namespace chat
{
namespace infra
{
namespace
{

constexpr size_t kRawBlobReadChunkBytes = 256;

std::string makeTempSdPath(const char* path)
{
    return std::string(path ? path : "") + ".tmp";
}

bool isValidPrefKey(const char* key)
{
    return key && key[0] != '\0';
}

void loadPreferencesMetadata(Preferences& prefs,
                             const char* version_key,
                             const char* crc_key,
                             PreferencesBlobMetadata* meta)
{
    if (!meta)
    {
        return;
    }

    if (isValidPrefKey(version_key) && prefs.isKey(version_key))
    {
        meta->has_version = true;
        meta->version = prefs.getUChar(version_key, 0);
    }
    if (isValidPrefKey(crc_key) && prefs.isKey(crc_key))
    {
        meta->has_crc = true;
        meta->crc = prefs.getUInt(crc_key, 0);
    }
}

bool savePreferencesMetadata(Preferences& prefs,
                             const char* version_key,
                             const char* crc_key,
                             const PreferencesBlobMetadata* meta)
{
    bool ok = true;

    if (isValidPrefKey(version_key))
    {
        if (meta && meta->has_version)
        {
            ok = ok && (prefs.putUChar(version_key, meta->version) == sizeof(uint8_t));
        }
        else
        {
            prefs.remove(version_key);
        }
    }

    if (isValidPrefKey(crc_key))
    {
        if (meta && meta->has_crc)
        {
            ok = ok && (prefs.putUInt(crc_key, meta->crc) == sizeof(uint32_t));
        }
        else
        {
            prefs.remove(crc_key);
        }
    }

    return ok;
}

} // namespace

bool loadRawBlobFromSd(const char* path, std::vector<uint8_t>& out, size_t max_len)
{
    out.clear();
    if (!path || path[0] == '\0' ||
        !::platform::esp::arduino_common::storage::sd_card_ready())
    {
        return false;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(path, "r"))
    {
        return false;
    }

    const size_t file_size = file.size();
    if (file_size == 0 || file_size > max_len)
    {
        file.close();
        return false;
    }

    out.reserve(file_size);
    uint8_t buffer[kRawBlobReadChunkBytes];
    size_t total_read = 0;
    while (total_read < file_size)
    {
        const size_t chunk = std::min(kRawBlobReadChunkBytes, file_size - total_read);
        const size_t read_bytes = file.read(buffer, chunk);
        if (read_bytes != chunk)
        {
            file.close();
            out.clear();
            return false;
        }
        out.insert(out.end(), buffer, buffer + read_bytes);
        total_read += read_bytes;
    }
    file.close();
    return true;
}

bool saveRawBlobToSd(const char* path, const uint8_t* data, size_t len)
{
    if (!path || path[0] == '\0' ||
        !::platform::esp::arduino_common::storage::sd_card_ready())
    {
        return false;
    }

    const std::string temp_path = makeTempSdPath(path);
    if (temp_path.empty())
    {
        return false;
    }

    if (::platform::esp::arduino_common::storage::sd_exists(temp_path.c_str()))
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile file;
    if (!file.open(temp_path.c_str(), "w"))
    {
        return false;
    }

    bool ok = true;
    if (data && len > 0)
    {
        ok = (file.write(data, len) == len);
    }
    file.close();
    if (!ok)
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }

    if (::platform::esp::arduino_common::storage::sd_exists(path))
    {
        ::platform::esp::arduino_common::storage::sd_remove(path);
    }

    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(), path))
    {
        ::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }

    return true;
}

bool loadRawBlobFromPreferences(const char* ns, const char* key, std::vector<uint8_t>& out)
{
    out.clear();
    if (!ns || !key || ns[0] == '\0' || key[0] == '\0')
    {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, true))
    {
        return false;
    }

    const size_t len = prefs.getBytesLength(key);
    if (len == 0)
    {
        prefs.end();
        return false;
    }

    out.resize(len);
    const size_t read_bytes = prefs.getBytes(key, out.data(), len);
    prefs.end();
    if (read_bytes != len)
    {
        out.clear();
        return false;
    }
    return true;
}

bool saveRawBlobToPreferences(const char* ns, const char* key, const uint8_t* data, size_t len)
{
    if (!ns || !key || ns[0] == '\0' || key[0] == '\0')
    {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false))
    {
        return false;
    }

    bool ok = true;
    if (data && len > 0)
    {
        ok = (prefs.putBytes(key, data, len) == len);
    }
    else
    {
        prefs.remove(key);
    }
    prefs.end();
    return ok;
}

void clearRawBlobFromPreferences(const char* ns, const char* key)
{
    clearPreferencesKeys(ns, key);
}

bool loadRawBlobFromPreferencesWithMetadata(const char* ns, const char* key,
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
    if (!ns || !key || ns[0] == '\0' || key[0] == '\0')
    {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, true))
    {
        return false;
    }

    PreferencesBlobMetadata local_meta{};
    local_meta.len = prefs.getBytesLength(key);
    loadPreferencesMetadata(prefs, version_key, crc_key, &local_meta);

    if (local_meta.len > 0)
    {
        out.resize(local_meta.len);
        const size_t read_bytes = prefs.getBytes(key, out.data(), local_meta.len);
        prefs.end();
        if (read_bytes != local_meta.len)
        {
            out.clear();
            return false;
        }
    }
    else
    {
        prefs.end();
    }

    if (meta)
    {
        *meta = local_meta;
    }
    return true;
}

bool saveRawBlobToPreferencesWithMetadata(const char* ns, const char* key,
                                          const char* version_key,
                                          const char* crc_key,
                                          const uint8_t* data, size_t len,
                                          const PreferencesBlobMetadata* meta,
                                          bool retry_after_clear)
{
    if (!ns || !key || ns[0] == '\0' || key[0] == '\0')
    {
        return false;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false))
    {
        return false;
    }

    bool ok = true;
    if (data && len > 0)
    {
        size_t written = prefs.putBytes(key, data, len);
        if (retry_after_clear && written != len)
        {
            prefs.remove(key);
            written = prefs.putBytes(key, data, len);
        }

        ok = (written == len) && savePreferencesMetadata(prefs, version_key, crc_key, meta);
    }
    else
    {
        prefs.remove(key);
        savePreferencesMetadata(prefs, version_key, crc_key, nullptr);
    }

    prefs.end();
    return ok;
}

void clearPreferencesKeys(const char* ns,
                          const char* key_a,
                          const char* key_b,
                          const char* key_c)
{
    if (!ns || ns[0] == '\0')
    {
        return;
    }

    Preferences prefs;
    if (!prefs.begin(ns, false))
    {
        return;
    }

    if (isValidPrefKey(key_a))
    {
        prefs.remove(key_a);
    }
    if (isValidPrefKey(key_b))
    {
        prefs.remove(key_b);
    }
    if (isValidPrefKey(key_c))
    {
        prefs.remove(key_c);
    }
    prefs.end();
}

} // namespace infra
} // namespace chat
