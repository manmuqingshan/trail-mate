#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <Arduino.h>
#include <SPI.h>
#ifndef DISABLE_FS_H_WARNING
#define DISABLE_FS_H_WARNING 1
#endif
#include <SdFat.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

namespace platform::esp::arduino_common::storage
{
namespace
{

constexpr uint8_t kRuntimeCardNone = 0;
constexpr uint8_t kRuntimeCardSd = 2;
constexpr uint8_t kRuntimeCardSdhc = 3;
constexpr uint8_t kRuntimeCardUnknown = 4;
constexpr uint32_t kSdSectorSize = 512;
constexpr uint32_t kDefaultSharedSpiSdHz = 4000000U;
constexpr uint32_t kMaxSharedSpiSdHz = 10000000U;
constexpr uint32_t kSdInitHz = 400000U;
constexpr uint8_t kSdR1IdleState = 0x01U;

#ifndef TRAIL_MATE_SD_IO_LOG_ENABLE
#define TRAIL_MATE_SD_IO_LOG_ENABLE 1
#endif

#ifndef TRAIL_MATE_SD_IO_TRACE_LOG
#define TRAIL_MATE_SD_IO_TRACE_LOG 0
#endif

#ifndef TRAIL_MATE_SD_IO_SLOW_MS
#define TRAIL_MATE_SD_IO_SLOW_MS 20
#endif

#ifndef TRAIL_MATE_SD_IO_LOG_INTERVAL_MS
#define TRAIL_MATE_SD_IO_LOG_INTERVAL_MS 1000
#endif

SdFs s_sdfat;
SdCardInfo s_info{};
bool s_sdfat_mounted = false;
uint32_t s_last_sd_io_log_ms = 0;
uint32_t s_suppressed_sd_io_logs = 0;

const char* backend_name_from_info()
{
    switch (s_info.backend)
    {
    case SdCardBackend::SdFat:
        return "sdfat";
    case SdCardBackend::None:
    default:
        return "none";
    }
}

const char* safe_path(const char* path)
{
    return path ? path : "";
}

void copy_path(char* out, std::size_t out_size, const char* path)
{
    if (!out || out_size == 0)
    {
        return;
    }
    std::snprintf(out, out_size, "%s", safe_path(path));
}

uint32_t sd_io_begin(const char* op, const char* path, std::size_t bytes = 0)
{
    const uint32_t start_ms = millis();
#if TRAIL_MATE_SD_IO_LOG_ENABLE && TRAIL_MATE_SD_IO_TRACE_LOG
    Serial.printf("[SD][IO] begin op=%s backend=%s path=%s bytes=%u t=%lu\n",
                  op,
                  backend_name_from_info(),
                  safe_path(path),
                  static_cast<unsigned>(bytes),
                  static_cast<unsigned long>(start_ms));
#else
    (void)op;
    (void)path;
    (void)bytes;
#endif
    return start_ms;
}

void sd_io_end(const char* op,
               const char* path,
               uint32_t start_ms,
               bool ok,
               std::size_t bytes = 0,
               int32_t result = 0)
{
    const uint32_t end_ms = millis();
    const uint32_t elapsed_ms = end_ms - start_ms;
#if TRAIL_MATE_SD_IO_LOG_ENABLE
    if (TRAIL_MATE_SD_IO_TRACE_LOG || !ok || elapsed_ms >= TRAIL_MATE_SD_IO_SLOW_MS)
    {
        ++s_suppressed_sd_io_logs;
        if (TRAIL_MATE_SD_IO_TRACE_LOG || s_last_sd_io_log_ms == 0 ||
            end_ms - s_last_sd_io_log_ms >= TRAIL_MATE_SD_IO_LOG_INTERVAL_MS)
        {
            Serial.printf("[SD][IO] end op=%s backend=%s path=%s ok=%d bytes=%u result=%ld elapsed_ms=%lu suppressed=%lu\n",
                          op,
                          backend_name_from_info(),
                          safe_path(path),
                          ok ? 1 : 0,
                          static_cast<unsigned>(bytes),
                          static_cast<long>(result),
                          static_cast<unsigned long>(elapsed_ms),
                          static_cast<unsigned long>(s_suppressed_sd_io_logs - 1));
            s_suppressed_sd_io_logs = 0;
            s_last_sd_io_log_ms = end_ms;
        }
    }
#else
    (void)op;
    (void)path;
    (void)ok;
    (void)bytes;
    (void)result;
    (void)elapsed_ms;
#endif
}

uint32_t sanitize_sd_spi_hz(uint32_t requested_hz)
{
    if (requested_hz == 0 || requested_hz > kMaxSharedSpiSdHz)
    {
        return kDefaultSharedSpiSdHz;
    }
    return requested_hz;
}

uint8_t card_type_from_sdfat(SdFs& fs)
{
    SdCard* card = fs.card();
    if (card == nullptr)
    {
        return kRuntimeCardNone;
    }

    const uint8_t type = card->type();
    if (type == SD_CARD_TYPE_SD1)
    {
        return kRuntimeCardSd;
    }
    if (type == SD_CARD_TYPE_SD2)
    {
        return kRuntimeCardSd;
    }
    if (type == SD_CARD_TYPE_SDHC)
    {
        return kRuntimeCardSdhc;
    }
    return kRuntimeCardUnknown;
}

bool path_empty(const char* path)
{
    return path == nullptr || path[0] == '\0';
}

const char* normalize_sd_path(const char* path)
{
    if (path_empty(path))
    {
        return "/";
    }

    if ((path[0] == 'A' || path[0] == 'a') && path[1] == ':')
    {
        path += 2;
    }

    if (path[0] == '\0')
    {
        return "/";
    }
    return path;
}

oflag_t sdfat_open_flags(const char* mode)
{
    if (mode == nullptr || std::strcmp(mode, "r") == 0 || std::strcmp(mode, "rb") == 0)
    {
        return O_RDONLY;
    }
    if (std::strcmp(mode, "w") == 0 || std::strcmp(mode, "wb") == 0)
    {
        return O_WRONLY | O_CREAT | O_TRUNC;
    }
    if (std::strcmp(mode, "a") == 0 || std::strcmp(mode, "ab") == 0)
    {
        return O_WRONLY | O_CREAT | O_APPEND;
    }
    if (std::strcmp(mode, "r+") == 0 || std::strcmp(mode, "rb+") == 0 ||
        std::strcmp(mode, "r+b") == 0)
    {
        return O_RDWR;
    }
    if (std::strcmp(mode, "w+") == 0 || std::strcmp(mode, "wb+") == 0 ||
        std::strcmp(mode, "w+b") == 0)
    {
        return O_RDWR | O_CREAT | O_TRUNC;
    }
    if (std::strcmp(mode, "a+") == 0 || std::strcmp(mode, "ab+") == 0 ||
        std::strcmp(mode, "a+b") == 0)
    {
        return O_RDWR | O_CREAT | O_APPEND;
    }
    return O_RDONLY;
}

void clear_sdfat()
{
    if (s_sdfat_mounted)
    {
        s_sdfat.end();
        s_sdfat_mounted = false;
    }
}

void reset_info()
{
    s_info = SdCardInfo{};
}

void sd_clock_bytes(SPIClass& spi, uint8_t count)
{
    for (uint8_t i = 0; i < count; ++i)
    {
        spi.transfer(0xFF);
    }
}

bool sd_wait_not_busy(SPIClass& spi, uint32_t timeout_ms)
{
    const uint32_t start_ms = millis();
    do
    {
        if (spi.transfer(0xFF) != 0x00)
        {
            return true;
        }
        delay(1);
    } while (static_cast<uint32_t>(millis() - start_ms) < timeout_ms);
    return false;
}

uint8_t sd_send_cmd0(SPIClass& spi)
{
    static constexpr uint8_t kCmd0Packet[] = {0x40, 0, 0, 0, 0, 0x95};
    for (uint8_t byte : kCmd0Packet)
    {
        spi.transfer(byte);
    }

    for (uint8_t i = 0; i < 16; ++i)
    {
        const uint8_t token = spi.transfer(0xFF);
        if ((token & 0x80U) == 0)
        {
            return token;
        }
    }
    return 0xFF;
}

bool sd_preflight_go_idle(int sd_cs, SPIClass& spi)
{
    uint8_t last_token = 0xFF;
    bool ok = false;
    uint8_t attempt = 0;
    uint8_t attempts_used = 0;

    spi.beginTransaction(SPISettings(kSdInitHz, MSBFIRST, SPI_MODE0));
    digitalWrite(sd_cs, HIGH);
    sd_clock_bytes(spi, 20);

    for (attempt = 1; attempt <= 4; ++attempt)
    {
        attempts_used = attempt;
        digitalWrite(sd_cs, LOW);
        const bool ready = sd_wait_not_busy(spi, 500);
        last_token = sd_send_cmd0(spi);
        digitalWrite(sd_cs, HIGH);
        sd_clock_bytes(spi, 2);

        if (last_token == kSdR1IdleState)
        {
            ok = true;
            break;
        }

        Serial.printf("[SD] SdFat preflight CMD0 retry=%u ready=%d token=0x%02X\n",
                      static_cast<unsigned>(attempt),
                      ready ? 1 : 0,
                      static_cast<unsigned>(last_token));
        delay(25);
    }

    spi.endTransaction();
    Serial.printf("[SD] SdFat preflight CMD0 -> %d token=0x%02X attempts=%u\n",
                  ok ? 1 : 0,
                  static_cast<unsigned>(last_token),
                  static_cast<unsigned>(attempts_used));
    delay(2);
    return ok;
}

void record_sdfat_info()
{
    s_info = SdCardInfo{};
    s_info.backend = SdCardBackend::SdFat;
    s_info.card_type = card_type_from_sdfat(s_sdfat);
    s_info.fat_type = s_sdfat.fatType();
    s_info.sector_size = kSdSectorSize;
    SdCard* card = s_sdfat.card();
    if (card != nullptr)
    {
        s_info.sector_count = static_cast<uint32_t>(card->sectorCount());
        s_info.card_size_bytes = static_cast<uint64_t>(s_info.sector_count) * kSdSectorSize;
    }
    const uint64_t cluster_count = s_sdfat.clusterCount();
    const uint64_t bytes_per_cluster = s_sdfat.bytesPerCluster();
    if (cluster_count > 0 && bytes_per_cluster > 0)
    {
        s_info.total_bytes = cluster_count * bytes_per_cluster;
    }
    const int32_t free_clusters = s_sdfat.freeClusterCount();
    if (free_clusters >= 0 && bytes_per_cluster > 0)
    {
        const uint64_t free_bytes = static_cast<uint64_t>(free_clusters) * bytes_per_cluster;
        s_info.used_bytes = s_info.total_bytes > free_bytes ? s_info.total_bytes - free_bytes : 0;
    }
}

} // namespace

bool mount_sd_card(int sd_cs,
                   SPIClass& spi,
                   uint32_t spi_hz,
                   const char* mount_point,
                   uint8_t max_files)
{
    (void)mount_point;
    (void)max_files;
    clear_sdfat();
    reset_info();

    const uint32_t effective_hz = sanitize_sd_spi_hz(spi_hz);
    if (effective_hz != spi_hz)
    {
        Serial.printf("[SD] clamp shared SPI hz requested=%lu effective=%lu\n",
                      static_cast<unsigned long>(spi_hz),
                      static_cast<unsigned long>(effective_hz));
    }

    const bool preflight_ok = sd_preflight_go_idle(sd_cs, spi);
    if (!preflight_ok)
    {
        Serial.println("[SD] SdFat preflight failed; continuing to SdFat.begin for detailed error");
    }

    const bool sdfat_ok = s_sdfat.begin(
        SdSpiConfig(sd_cs, SHARED_SPI | USER_SPI_BEGIN, effective_hz, &spi));
    Serial.printf("[SD] SdFat.begin hz=%lu -> %d\n",
                  static_cast<unsigned long>(effective_hz),
                  sdfat_ok ? 1 : 0);
    if (!sdfat_ok || s_sdfat.fatType() == 0)
    {
        s_sdfat.initErrorPrint(&Serial);
        clear_sdfat();
        reset_info();
        return false;
    }

    s_sdfat_mounted = true;
    record_sdfat_info();
    Serial.printf("[SD] backend=sdfat fs=%s card=%llu MB total=%llu MB sectors=%lu sector_size=%lu\n",
                  sd_card_filesystem_name(),
                  static_cast<unsigned long long>(s_info.card_size_bytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long long>(s_info.total_bytes / (1024ULL * 1024ULL)),
                  static_cast<unsigned long>(s_info.sector_count),
                  static_cast<unsigned long>(s_info.sector_size));
    return true;
}

void unmount_sd_card()
{
    clear_sdfat();
    reset_info();
}

bool sd_card_ready()
{
    return s_info.backend != SdCardBackend::None && s_info.sector_size != 0 &&
           s_info.card_type != kRuntimeCardNone;
}

bool sd_card_uses_sdfat()
{
    return s_info.backend == SdCardBackend::SdFat;
}

bool sd_card_is_exfat()
{
    return s_info.backend == SdCardBackend::SdFat && s_info.fat_type == FAT_TYPE_EXFAT;
}

SdCardBackend sd_card_backend()
{
    return s_info.backend;
}

SdCardInfo sd_card_info()
{
    return s_info;
}

const char* sd_card_backend_name()
{
    switch (s_info.backend)
    {
    case SdCardBackend::SdFat:
        return "sdfat";
    case SdCardBackend::None:
    default:
        return "none";
    }
}

const char* sd_card_filesystem_name()
{
    if (s_info.backend == SdCardBackend::SdFat)
    {
        switch (s_info.fat_type)
        {
        case FAT_TYPE_EXFAT:
            return "exfat";
        case FAT_TYPE_FAT32:
            return "fat32";
        case FAT_TYPE_FAT16:
            return "fat16";
        case FAT_TYPE_FAT12:
            return "fat12";
        default:
            return "fat";
        }
    }
    return "none";
}

bool sd_exists(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("exists", normalized);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.exists(normalized);
        sd_io_end("exists", normalized, start_ms, true, 0, result ? 1 : 0);
        return result;
    }
    sd_io_end("exists", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_is_directory(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("is_dir", normalized);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat)
    {
        FsFile dir = s_sdfat.open(normalized, O_RDONLY);
        result = dir && dir.isDir();
        dir.close();
        sd_io_end("is_dir", normalized, start_ms, true, 0, result ? 1 : 0);
        return result;
    }
    sd_io_end("is_dir", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_mkdir(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("mkdir", normalized);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.mkdir(normalized, true);
        sd_io_end("mkdir", normalized, start_ms, result);
        return result;
    }
    sd_io_end("mkdir", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_rmdir(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("rmdir", normalized);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.rmdir(normalized);
        sd_io_end("rmdir", normalized, start_ms, result);
        return result;
    }
    sd_io_end("rmdir", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_remove(const char* path)
{
    const char* normalized = normalize_sd_path(path);
    const uint32_t start_ms = sd_io_begin("remove", normalized);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.remove(normalized);
        sd_io_end("remove", normalized, start_ms, result);
        return result;
    }
    sd_io_end("remove", normalized, start_ms, false, 0, -1);
    return false;
}

bool sd_rename(const char* old_path, const char* new_path)
{
    const char* normalized_old = normalize_sd_path(old_path);
    const char* normalized_new = normalize_sd_path(new_path);
    const uint32_t start_ms = sd_io_begin("rename", normalized_old);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat)
    {
        result = s_sdfat.rename(normalized_old, normalized_new);
        sd_io_end("rename", normalized_old, start_ms, result, 0, result ? 0 : -1);
        return result;
    }
    sd_io_end("rename", normalized_old, start_ms, false, 0, -1);
    return false;
}

class SdRuntimeFile::Impl
{
  public:
    FsFile sdfat_file;
    SdCardBackend backend = SdCardBackend::None;
    char path[128]{};
    char mode[8]{};
};

SdRuntimeFile::SdRuntimeFile()
    : impl_(new (std::nothrow) Impl())
{
}

SdRuntimeFile::~SdRuntimeFile()
{
    close();
    delete impl_;
}

bool SdRuntimeFile::open(const char* path, const char* mode)
{
    close();
    if (impl_ == nullptr || path_empty(path))
    {
        return false;
    }

    const char* normalized = normalize_sd_path(path);
    copy_path(impl_->path, sizeof(impl_->path), normalized);
    copy_path(impl_->mode, sizeof(impl_->mode), mode ? mode : "r");
    const uint32_t start_ms = sd_io_begin("file_open", impl_->path);
    if (s_info.backend == SdCardBackend::SdFat)
    {
        impl_->sdfat_file = s_sdfat.open(normalized, sdfat_open_flags(mode));
        impl_->backend = impl_->sdfat_file ? SdCardBackend::SdFat : SdCardBackend::None;
        sd_io_end("file_open", impl_->path, start_ms, impl_->backend == SdCardBackend::SdFat);
        return impl_->backend == SdCardBackend::SdFat;
    }

    sd_io_end("file_open", impl_->path, start_ms, false, 0, -1);
    return false;
}

void SdRuntimeFile::close()
{
    if (impl_ == nullptr)
    {
        return;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_close", impl_->path);
        impl_->sdfat_file.close();
        sd_io_end("file_close", impl_->path, start_ms, true);
    }
    impl_->backend = SdCardBackend::None;
    impl_->path[0] = '\0';
    impl_->mode[0] = '\0';
}

bool SdRuntimeFile::is_open() const
{
    return impl_ != nullptr && impl_->backend != SdCardBackend::None;
}

int SdRuntimeFile::available() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.available();
    }
    return 0;
}

int SdRuntimeFile::read(void* buffer, std::size_t bytes_to_read)
{
    if (!is_open() || buffer == nullptr || bytes_to_read == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_read", impl_->path, bytes_to_read);
        const int result = impl_->sdfat_file.read(buffer, bytes_to_read);
        sd_io_end("file_read", impl_->path, start_ms, result >= 0, bytes_to_read, result);
        return result;
    }
    return -1;
}

int SdRuntimeFile::read_byte()
{
    if (!is_open())
    {
        return -1;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.read();
    }
    return -1;
}

std::size_t SdRuntimeFile::read_bytes(char* buffer, std::size_t bytes_to_read)
{
    if (!is_open() || buffer == nullptr || bytes_to_read == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_read_bytes", impl_->path, bytes_to_read);
        int result = impl_->sdfat_file.read(buffer, bytes_to_read);
        sd_io_end("file_read_bytes", impl_->path, start_ms, result >= 0, bytes_to_read, result);
        return result > 0 ? static_cast<std::size_t>(result) : 0;
    }
    return 0;
}

std::size_t SdRuntimeFile::write(const void* buffer, std::size_t bytes_to_write)
{
    if (!is_open() || buffer == nullptr || bytes_to_write == 0)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_write", impl_->path, bytes_to_write);
        const std::size_t result = impl_->sdfat_file.write(buffer, bytes_to_write);
        sd_io_end("file_write", impl_->path, start_ms, result == bytes_to_write, bytes_to_write, result);
        return result;
    }
    return 0;
}

std::size_t SdRuntimeFile::write_byte(uint8_t value)
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.write(value);
    }
    return 0;
}

std::size_t SdRuntimeFile::print(const char* text)
{
    if (!is_open() || text == nullptr)
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.print(text);
    }
    return 0;
}

std::size_t SdRuntimeFile::print(double value, int digits)
{
    if (!is_open())
    {
        return 0;
    }
    const uint8_t precision = digits < 0 ? 0 : static_cast<uint8_t>(digits);
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.print(value, precision);
    }
    return 0;
}

std::size_t SdRuntimeFile::printf(const char* format, ...)
{
    if (!is_open() || format == nullptr)
    {
        return 0;
    }

    va_list args;
    va_start(args, format);
    va_list args_copy;
    va_copy(args_copy, args);
    int len = std::vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);
    if (len <= 0)
    {
        va_end(args);
        return 0;
    }

    std::vector<char> buffer(static_cast<std::size_t>(len) + 1U);
    std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    return write(buffer.data(), static_cast<std::size_t>(len));
}

bool SdRuntimeFile::seek(uint64_t offset)
{
    if (!is_open())
    {
        return false;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.seekSet(offset);
    }
    return false;
}

uint64_t SdRuntimeFile::position() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.curPosition();
    }
    return 0;
}

uint64_t SdRuntimeFile::size() const
{
    if (!is_open())
    {
        return 0;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        return impl_->sdfat_file.fileSize();
    }
    return 0;
}

bool SdRuntimeFile::flush()
{
    if (!is_open())
    {
        return false;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("file_flush", impl_->path);
        const bool result = impl_->sdfat_file.sync();
        sd_io_end("file_flush", impl_->path, start_ms, result);
        return result;
    }
    return false;
}

class SdRuntimeDir::Impl
{
  public:
    FsFile sdfat_dir;
    SdCardBackend backend = SdCardBackend::None;
    char path[128]{};
};

SdRuntimeDir::SdRuntimeDir()
    : impl_(new (std::nothrow) Impl())
{
}

SdRuntimeDir::~SdRuntimeDir()
{
    close();
    delete impl_;
}

bool SdRuntimeDir::open(const char* path)
{
    close();
    if (impl_ == nullptr)
    {
        return false;
    }
    const char* normalized = normalize_sd_path(path);
    copy_path(impl_->path, sizeof(impl_->path), normalized);
    const uint32_t start_ms = sd_io_begin("dir_open", impl_->path);
    if (s_info.backend == SdCardBackend::SdFat)
    {
        impl_->sdfat_dir = s_sdfat.open(normalized, O_RDONLY);
        impl_->backend =
            (impl_->sdfat_dir && impl_->sdfat_dir.isDir()) ? SdCardBackend::SdFat
                                                           : SdCardBackend::None;
        sd_io_end("dir_open", impl_->path, start_ms, impl_->backend == SdCardBackend::SdFat);
        return impl_->backend == SdCardBackend::SdFat;
    }
    sd_io_end("dir_open", impl_->path, start_ms, false, 0, -1);
    return false;
}

void SdRuntimeDir::close()
{
    if (impl_ == nullptr)
    {
        return;
    }
    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("dir_close", impl_->path);
        impl_->sdfat_dir.close();
        sd_io_end("dir_close", impl_->path, start_ms, true);
    }
    impl_->backend = SdCardBackend::None;
    impl_->path[0] = '\0';
}

bool SdRuntimeDir::is_open() const
{
    return impl_ != nullptr && impl_->backend != SdCardBackend::None;
}

bool SdRuntimeDir::read_next(char* name, std::size_t name_size, bool* is_dir)
{
    if (!is_open() || name == nullptr || name_size == 0)
    {
        return false;
    }
    name[0] = '\0';
    if (is_dir != nullptr)
    {
        *is_dir = false;
    }

    if (impl_->backend == SdCardBackend::SdFat)
    {
        const uint32_t start_ms = sd_io_begin("dir_read", impl_->path);
        FsFile entry = impl_->sdfat_dir.openNextFile(O_RDONLY);
        if (!entry)
        {
            sd_io_end("dir_read", impl_->path, start_ms, true, 0, 0);
            return false;
        }
        entry.getName(name, name_size);
        if (is_dir != nullptr)
        {
            *is_dir = entry.isDir();
        }
        entry.close();
        sd_io_end("dir_read", impl_->path, start_ms, true, 0, name[0] != '\0' ? 1 : 0);
        return name[0] != '\0';
    }

    return false;
}

bool sd_read_raw(uint32_t lba, uint8_t* buffer)
{
    char path[32];
    std::snprintf(path, sizeof(path), "raw:%lu", static_cast<unsigned long>(lba));
    const uint32_t start_ms = sd_io_begin("raw_read", path, kSdSectorSize);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat && s_sdfat.card() != nullptr)
    {
        result = s_sdfat.card()->readSector(lba, buffer);
        sd_io_end("raw_read", path, start_ms, result, kSdSectorSize);
        return result;
    }
    sd_io_end("raw_read", path, start_ms, false, kSdSectorSize, -1);
    return false;
}

bool sd_write_raw(uint32_t lba, const uint8_t* buffer)
{
    char path[32];
    std::snprintf(path, sizeof(path), "raw:%lu", static_cast<unsigned long>(lba));
    const uint32_t start_ms = sd_io_begin("raw_write", path, kSdSectorSize);
    bool result = false;
    if (s_info.backend == SdCardBackend::SdFat && s_sdfat.card() != nullptr)
    {
        result = s_sdfat.card()->writeSector(lba, buffer);
        sd_io_end("raw_write", path, start_ms, result, kSdSectorSize);
        return result;
    }
    sd_io_end("raw_write", path, start_ms, false, kSdSectorSize, -1);
    return false;
}

} // namespace platform::esp::arduino_common::storage
