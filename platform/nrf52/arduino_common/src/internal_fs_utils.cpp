#include "platform/nrf52/arduino_common/internal_fs_utils.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

namespace platform::nrf52::arduino_common::internal_fs
{
namespace
{

constexpr const char* kDefaultLogTag = "[nrf52][fs]";

const char* resolveLogTag(const char* log_tag)
{
    return log_tag ? log_tag : kDefaultLogTag;
}

void logLine(const char* log_tag, const char* message)
{
    if (!log_tag || !message)
    {
        return;
    }
    Serial.printf("%s %s\n", resolveLogTag(log_tag), message);
}

void logPath(const char* log_tag, const char* message, const char* path)
{
    if (!log_tag || !message)
    {
        return;
    }
    Serial.printf("%s %s path=%s\n",
                  resolveLogTag(log_tag),
                  message,
                  path ? path : "?");
}

void logPathPair(const char* log_tag, const char* message, const char* temp_path, const char* path)
{
    if (!log_tag || !message)
    {
        return;
    }
    Serial.printf("%s %s temp=%s path=%s\n",
                  resolveLogTag(log_tag),
                  message,
                  temp_path ? temp_path : "?",
                  path ? path : "?");
}

bool recoverByFormat(const char* log_tag)
{
    if (log_tag)
    {
        logLine(log_tag, "fs format recovery start");
    }
    if (!InternalFS.format())
    {
        logLine(log_tag, "fs format failed");
        return false;
    }
    if (!InternalFS.begin())
    {
        logLine(log_tag, "fs remount after format failed");
        return false;
    }
    if (log_tag)
    {
        logLine(log_tag, "fs recovered by format");
    }
    return true;
}

bool openForWrite(const char* path,
                  File* out,
                  bool allow_format_recovery,
                  const char* log_tag,
                  const char* failure_message)
{
    if (!out || !path)
    {
        return false;
    }

    *out = File(InternalFS);
    *out = InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_WRITE);
    if (*out)
    {
        return true;
    }

    logPath(log_tag, failure_message, path);
    if (!allow_format_recovery || !recoverByFormat(log_tag))
    {
        return false;
    }

    *out = InternalFS.open(path, Adafruit_LittleFS_Namespace::FILE_O_WRITE);
    if (!*out)
    {
        logPath(log_tag, "open for write failed after recovery", path);
        return false;
    }
    return true;
}

} // namespace

bool ensureMounted(bool allow_format_recovery, const char* log_tag)
{
    if (InternalFS.begin())
    {
        return true;
    }

    logLine(log_tag, "fs begin failed");
    return allow_format_recovery ? recoverByFormat(log_tag) : false;
}

void removeIfExists(const char* path)
{
    if (path && InternalFS.exists(path))
    {
        InternalFS.remove(path);
    }
}

bool removeVolatileArtifactsPreserveSettings(const char* log_tag)
{
    if (!ensureMounted(false, log_tag))
    {
        return false;
    }

    static constexpr const char* kPaths[] = {
        "/chat_nodes.bin",
        "/chat_nodes.bin.tmp",
        "/chat_contacts.bin",
        "/chat_contacts.bin.tmp",
        "/chat_messages.bin",
        "/chat_messages.bin.tmp",
        "/t_echo_lite_settings.bin.tmp",
        "/gat562_settings.bin.tmp",
        "/ui_settings.bin.tmp",
    };

    bool ok = true;
    for (const char* path : kPaths)
    {
        if (!path || !InternalFS.exists(path))
        {
            continue;
        }

        InternalFS.remove(path);
        if (InternalFS.exists(path))
        {
            ok = false;
            logPath(log_tag, "volatile remove failed", path);
        }
        else
        {
            logPath(log_tag, "volatile removed", path);
        }
    }

    return ok;
}

bool openForOverwrite(const char* path,
                      File* out,
                      bool allow_format_recovery,
                      const char* log_tag)
{
    if (!out || !path)
    {
        return false;
    }

    *out = File(InternalFS);
    if (!ensureMounted(allow_format_recovery, log_tag))
    {
        return false;
    }

    // nRF52 LittleFS can report a valid FILE_O_WRITE handle for an existing
    // file and still reject the following writes. Keep this compatibility
    // helper on fresh-file semantics; crash-safe stores should use a temp-file
    // replace transaction instead.
    removeIfExists(path);
    if (InternalFS.exists(path))
    {
        logPath(log_tag, "remove before overwrite failed", path);
        return false;
    }

    return openForWrite(path, out, allow_format_recovery, log_tag, "open for overwrite failed");
}

bool openTempForReplace(const char* temp_path,
                        File* out,
                        bool allow_format_recovery,
                        const char* log_tag)
{
    if (!out || !temp_path)
    {
        return false;
    }

    *out = File(InternalFS);
    if (!ensureMounted(allow_format_recovery, log_tag))
    {
        return false;
    }

    removeIfExists(temp_path);
    if (InternalFS.exists(temp_path))
    {
        logPath(log_tag, "temp remove before replace failed", temp_path);
        return false;
    }

    return openForWrite(temp_path, out, allow_format_recovery, log_tag, "temp open for replace failed");
}

bool commitTempReplace(const char* path,
                       const char* temp_path,
                       bool allow_format_recovery,
                       const char* log_tag)
{
    if (!path || !temp_path)
    {
        return false;
    }

    if (!ensureMounted(allow_format_recovery, log_tag))
    {
        return false;
    }

    if (!InternalFS.exists(temp_path))
    {
        logPathPair(log_tag, "temp missing before replace", temp_path, path);
        return false;
    }

    if (InternalFS.rename(temp_path, path))
    {
        return true;
    }

    const bool target_exists = InternalFS.exists(path);
    if (!target_exists)
    {
        logPathPair(log_tag, "rename replace failed", temp_path, path);
        removeIfExists(temp_path);
        return false;
    }

    removeIfExists(path);
    if (InternalFS.exists(path))
    {
        logPath(log_tag, "remove before replace failed", path);
        removeIfExists(temp_path);
        return false;
    }

    if (!InternalFS.rename(temp_path, path))
    {
        logPathPair(log_tag, "rename replace failed after target remove", temp_path, path);
        removeIfExists(temp_path);
        return false;
    }
    return true;
}

bool rewindForOverwrite(File& file)
{
    return file && file.seek(0);
}

bool truncateAfterWrite(File& file, uint32_t final_size)
{
    return file && file.truncate(final_size);
}

uint32_t accumulateBytes(File dir)
{
    uint32_t total = 0;
    if (!dir)
    {
        return total;
    }

    dir.rewindDirectory();
    while (true)
    {
        File entry = dir.openNextFile();
        if (!entry)
        {
            break;
        }

        if (entry.isDirectory())
        {
            total += accumulateBytes(entry);
        }
        else
        {
            total += entry.size();
        }
        entry.close();
    }

    return total;
}

} // namespace platform::nrf52::arduino_common::internal_fs
