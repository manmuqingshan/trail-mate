#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include "platform/esp/idf_common/flash_storage_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <new>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace platform::esp::arduino_common::storage
{
namespace
{

SemaphoreHandle_t s_storage_mutex = nullptr;
bool s_external_block_owner_active = false;

bool ensure_storage_mutex()
{
    if (s_storage_mutex)
    {
        return true;
    }
    s_storage_mutex = xSemaphoreCreateRecursiveMutex();
    return s_storage_mutex != nullptr;
}

class StorageLock final
{
  public:
    StorageLock()
    {
        if (ensure_storage_mutex())
        {
            locked_ = xSemaphoreTakeRecursive(s_storage_mutex, portMAX_DELAY) == pdTRUE;
        }
    }

    ~StorageLock()
    {
        if (locked_)
        {
            xSemaphoreGiveRecursive(s_storage_mutex);
        }
    }

    bool locked() const { return locked_; }

    StorageLock(const StorageLock&) = delete;
    StorageLock& operator=(const StorageLock&) = delete;

  private:
    bool locked_ = false;
};

bool path_is_safe(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }
    return std::strcmp(path, "..") != 0 &&
           std::strncmp(path, "../", 3) != 0 &&
           std::strstr(path, "/../") == nullptr &&
           !(std::strlen(path) >= 3 &&
             std::strcmp(path + std::strlen(path) - 3, "/..") == 0);
}

bool physical_path(const char* logical_path, std::string& out)
{
    out.clear();
    if (!logical_path)
    {
        return false;
    }

    const char* path = logical_path;
    if ((path[0] == 'A' || path[0] == 'a') && path[1] == ':')
    {
        path += 2;
    }
    if (path[0] == '\0')
    {
        path = "/";
    }
    if (!path_is_safe(path))
    {
        return false;
    }

    out = ::platform::esp::idf_common::flash_storage_runtime::mount_point();
    if (path[0] != '/')
    {
        out.push_back('/');
    }
    out += path;
    return true;
}

bool ensure_ffat_ready()
{
    return ::platform::esp::idf_common::flash_storage_runtime::ensure_ready(true);
}

bool mode_mutates(const char* mode)
{
    return mode && (std::strchr(mode, 'w') ||
                    std::strchr(mode, 'a') ||
                    std::strchr(mode, '+'));
}

} // namespace

bool mount_sd_card(int, SPIClass&, uint32_t, const char*, uint8_t)
{
    return false;
}

void unmount_sd_card()
{
    // This adapter borrows the process-wide FFat mount. Its lifecycle belongs
    // to flash_storage_runtime and must not be coupled to an SdStore instance.
}

bool sd_card_ready()
{
    return ensure_ffat_ready();
}

bool sd_card_uses_sdfat()
{
    return false;
}

bool sd_card_is_exfat()
{
    return false;
}

SdCardBackend sd_card_backend()
{
    return SdCardBackend::None;
}

SdCardInfo sd_card_info()
{
    SdCardInfo info{};
    info.backend = SdCardBackend::None;
    return info;
}

const char* sd_card_backend_name()
{
    return ensure_ffat_ready() ? "ffat" : "none";
}

const char* sd_card_filesystem_name()
{
    return ensure_ffat_ready() ? "FAT" : "none";
}

bool sd_external_block_owner_active()
{
    StorageLock lock;
    return lock.locked() && s_external_block_owner_active;
}

void sd_set_external_block_owner_active(bool active)
{
    StorageLock lock;
    if (lock.locked())
    {
        s_external_block_owner_active = active;
    }
}

bool sd_exists(const char* path)
{
    StorageLock lock;
    std::string physical;
    struct stat st
    {
    };
    return lock.locked() && ensure_ffat_ready() && physical_path(path, physical) &&
           ::stat(physical.c_str(), &st) == 0;
}

bool sd_is_directory(const char* path)
{
    StorageLock lock;
    std::string physical;
    struct stat st
    {
    };
    return lock.locked() && ensure_ffat_ready() && physical_path(path, physical) &&
           ::stat(physical.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool sd_mkdir(const char* path)
{
    StorageLock lock;
    std::string physical;
    if (!lock.locked() || s_external_block_owner_active || !ensure_ffat_ready() ||
        !physical_path(path, physical))
    {
        return false;
    }
    return ::mkdir(physical.c_str(), 0775) == 0 || errno == EEXIST;
}

bool sd_rmdir(const char* path)
{
    StorageLock lock;
    std::string physical;
    return lock.locked() && !s_external_block_owner_active && ensure_ffat_ready() &&
           physical_path(path, physical) && ::rmdir(physical.c_str()) == 0;
}

bool sd_remove(const char* path)
{
    StorageLock lock;
    std::string physical;
    return lock.locked() && !s_external_block_owner_active && ensure_ffat_ready() &&
           physical_path(path, physical) && std::remove(physical.c_str()) == 0;
}

bool sd_rename(const char* old_path, const char* new_path)
{
    StorageLock lock;
    std::string old_physical;
    std::string new_physical;
    return lock.locked() && !s_external_block_owner_active && ensure_ffat_ready() &&
           physical_path(old_path, old_physical) &&
           physical_path(new_path, new_physical) &&
           std::rename(old_physical.c_str(), new_physical.c_str()) == 0;
}

class SdRuntimeFile::Impl
{
  public:
    std::FILE* file = nullptr;
};

SdRuntimeFile::SdRuntimeFile()
    : impl_(new (std::nothrow) Impl())
{
}

SdRuntimeFile::~SdRuntimeFile()
{
    close();
    delete impl_;
    impl_ = nullptr;
}

bool SdRuntimeFile::open(const char* path, const char* mode)
{
    StorageLock lock;
    std::string physical;
    if (!lock.locked() || !impl_ || !ensure_ffat_ready() ||
        !physical_path(path, physical) || !mode ||
        (s_external_block_owner_active && mode_mutates(mode)))
    {
        return false;
    }
    if (impl_->file)
    {
        std::fclose(impl_->file);
        impl_->file = nullptr;
    }
    impl_->file = std::fopen(physical.c_str(), mode);
    return impl_->file != nullptr;
}

void SdRuntimeFile::close()
{
    StorageLock lock;
    if (lock.locked() && impl_ && impl_->file)
    {
        std::fclose(impl_->file);
        impl_->file = nullptr;
    }
}

bool SdRuntimeFile::is_open() const
{
    StorageLock lock;
    return lock.locked() && impl_ && impl_->file;
}

int SdRuntimeFile::available() const
{
    StorageLock lock;
    if (!lock.locked() || !impl_ || !impl_->file)
    {
        return 0;
    }
    const long current = std::ftell(impl_->file);
    if (current < 0 || std::fseek(impl_->file, 0, SEEK_END) != 0)
    {
        return 0;
    }
    const long end = std::ftell(impl_->file);
    (void)std::fseek(impl_->file, current, SEEK_SET);
    if (end <= current)
    {
        return 0;
    }
    const long remaining = end - current;
    return remaining > std::numeric_limits<int>::max()
               ? std::numeric_limits<int>::max()
               : static_cast<int>(remaining);
}

int SdRuntimeFile::read(void* buffer, std::size_t bytes_to_read)
{
    StorageLock lock;
    if (!lock.locked() || !impl_ || !impl_->file || (!buffer && bytes_to_read > 0))
    {
        return -1;
    }
    const std::size_t count = std::fread(buffer, 1, bytes_to_read, impl_->file);
    return count > static_cast<std::size_t>(std::numeric_limits<int>::max())
               ? std::numeric_limits<int>::max()
               : static_cast<int>(count);
}

int SdRuntimeFile::read_byte()
{
    uint8_t value = 0;
    return read(&value, 1) == 1 ? value : -1;
}

std::size_t SdRuntimeFile::read_bytes(char* buffer, std::size_t bytes_to_read)
{
    const int count = read(buffer, bytes_to_read);
    return count > 0 ? static_cast<std::size_t>(count) : 0;
}

std::size_t SdRuntimeFile::write(const void* buffer, std::size_t bytes_to_write)
{
    StorageLock lock;
    if (!lock.locked() || !impl_ || !impl_->file || s_external_block_owner_active ||
        (!buffer && bytes_to_write > 0))
    {
        return 0;
    }
    return std::fwrite(buffer, 1, bytes_to_write, impl_->file);
}

std::size_t SdRuntimeFile::write_byte(uint8_t value)
{
    return write(&value, 1);
}

std::size_t SdRuntimeFile::print(const char* text)
{
    return text ? write(text, std::strlen(text)) : 0;
}

std::size_t SdRuntimeFile::print(double value, int digits)
{
    char text[48] = {};
    const int count = std::snprintf(text, sizeof(text), "%.*f", digits, value);
    return count > 0 ? write(text, static_cast<std::size_t>(count)) : 0;
}

std::size_t SdRuntimeFile::printf(const char* format, ...)
{
    if (!format)
    {
        return 0;
    }
    char text[256] = {};
    va_list args;
    va_start(args, format);
    const int count = std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if (count <= 0)
    {
        return 0;
    }
    return write(text, std::min<std::size_t>(static_cast<std::size_t>(count), sizeof(text) - 1));
}

bool SdRuntimeFile::seek(uint64_t offset)
{
    StorageLock lock;
    return lock.locked() && impl_ && impl_->file &&
           offset <= static_cast<uint64_t>(std::numeric_limits<long>::max()) &&
           std::fseek(impl_->file, static_cast<long>(offset), SEEK_SET) == 0;
}

uint64_t SdRuntimeFile::position() const
{
    StorageLock lock;
    if (!lock.locked() || !impl_ || !impl_->file)
    {
        return 0;
    }
    const long position = std::ftell(impl_->file);
    return position >= 0 ? static_cast<uint64_t>(position) : 0;
}

uint64_t SdRuntimeFile::size() const
{
    StorageLock lock;
    if (!lock.locked() || !impl_ || !impl_->file)
    {
        return 0;
    }
    const long current = std::ftell(impl_->file);
    if (current < 0 || std::fseek(impl_->file, 0, SEEK_END) != 0)
    {
        return 0;
    }
    const long end = std::ftell(impl_->file);
    (void)std::fseek(impl_->file, current, SEEK_SET);
    return end >= 0 ? static_cast<uint64_t>(end) : 0;
}

bool SdRuntimeFile::flush()
{
    StorageLock lock;
    return lock.locked() && impl_ && impl_->file && std::fflush(impl_->file) == 0;
}

class SdRuntimeDir::Impl
{
  public:
    DIR* dir = nullptr;
    std::string physical_path{};
};

SdRuntimeDir::SdRuntimeDir()
    : impl_(new (std::nothrow) Impl())
{
}

SdRuntimeDir::~SdRuntimeDir()
{
    close();
    delete impl_;
    impl_ = nullptr;
}

bool SdRuntimeDir::open(const char* path)
{
    StorageLock lock;
    std::string physical;
    if (!lock.locked() || !impl_ || !ensure_ffat_ready() || !physical_path(path, physical))
    {
        return false;
    }
    if (impl_->dir)
    {
        ::closedir(impl_->dir);
        impl_->dir = nullptr;
    }
    impl_->dir = ::opendir(physical.c_str());
    if (!impl_->dir)
    {
        impl_->physical_path.clear();
        return false;
    }
    impl_->physical_path = std::move(physical);
    return true;
}

void SdRuntimeDir::close()
{
    StorageLock lock;
    if (lock.locked() && impl_ && impl_->dir)
    {
        ::closedir(impl_->dir);
        impl_->dir = nullptr;
        impl_->physical_path.clear();
    }
}

bool SdRuntimeDir::is_open() const
{
    StorageLock lock;
    return lock.locked() && impl_ && impl_->dir;
}

bool SdRuntimeDir::read_next(char* name, std::size_t name_size, bool* is_dir)
{
    StorageLock lock;
    if (!lock.locked() || !impl_ || !impl_->dir || !name || name_size == 0)
    {
        return false;
    }
    while (dirent* entry = ::readdir(impl_->dir))
    {
        if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }
        std::snprintf(name, name_size, "%s", entry->d_name);
        if (is_dir)
        {
            const std::string path = impl_->physical_path + "/" + entry->d_name;
            struct stat st
            {
            };
            *is_dir = ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        }
        return true;
    }
    return false;
}

bool sd_read_raw(uint32_t, uint8_t*)
{
    return false;
}

bool sd_write_raw(uint32_t, const uint8_t*)
{
    return false;
}

} // namespace platform::esp::arduino_common::storage
