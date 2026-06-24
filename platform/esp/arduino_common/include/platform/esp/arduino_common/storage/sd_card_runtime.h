#pragma once

#include <cstddef>
#include <cstdint>

class SPIClass;

namespace platform::esp::arduino_common::storage
{

enum class SdCardBackend : uint8_t
{
    None = 0,
    SdFat,
};

struct SdCardInfo
{
    SdCardBackend backend = SdCardBackend::None;
    uint8_t card_type = 0;
    uint8_t fat_type = 0;
    uint32_t sector_size = 0;
    uint32_t sector_count = 0;
    uint64_t card_size_bytes = 0;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
};

bool mount_sd_card(int sd_cs,
                   SPIClass& spi,
                   uint32_t spi_hz,
                   const char* mount_point,
                   uint8_t max_files);
void unmount_sd_card();

bool sd_card_ready();
bool sd_card_uses_sdfat();
bool sd_card_is_exfat();
SdCardBackend sd_card_backend();
SdCardInfo sd_card_info();
const char* sd_card_backend_name();
const char* sd_card_filesystem_name();

bool sd_exists(const char* path);
bool sd_is_directory(const char* path);
bool sd_mkdir(const char* path);
bool sd_rmdir(const char* path);
bool sd_remove(const char* path);
bool sd_rename(const char* old_path, const char* new_path);

class SdRuntimeFile
{
  public:
    SdRuntimeFile();
    ~SdRuntimeFile();

    SdRuntimeFile(const SdRuntimeFile&) = delete;
    SdRuntimeFile& operator=(const SdRuntimeFile&) = delete;

    bool open(const char* path, const char* mode);
    void close();
    bool is_open() const;
    int available() const;
    int read(void* buffer, std::size_t bytes_to_read);
    int read_byte();
    std::size_t read_bytes(char* buffer, std::size_t bytes_to_read);
    std::size_t write(const void* buffer, std::size_t bytes_to_write);
    std::size_t write_byte(uint8_t value);
    std::size_t print(const char* text);
    std::size_t print(double value, int digits = 2);
    std::size_t printf(const char* format, ...);
    bool seek(uint64_t offset);
    uint64_t position() const;
    uint64_t size() const;
    bool flush();

  private:
    class Impl;
    Impl* impl_;
};

class SdRuntimeDir
{
  public:
    SdRuntimeDir();
    ~SdRuntimeDir();

    SdRuntimeDir(const SdRuntimeDir&) = delete;
    SdRuntimeDir& operator=(const SdRuntimeDir&) = delete;

    bool open(const char* path);
    void close();
    bool is_open() const;
    bool read_next(char* name, std::size_t name_size, bool* is_dir);

  private:
    class Impl;
    Impl* impl_;
};

bool sd_read_raw(uint32_t lba, uint8_t* buffer);
bool sd_write_raw(uint32_t lba, const uint8_t* buffer);

} // namespace platform::esp::arduino_common::storage
