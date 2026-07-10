#include "platform/ui/route_storage.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/http_client_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_rom_tjpgd.h"
#include <esp_heap_caps.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <utility>
#include <vector>

namespace platform::ui::route_storage
{
namespace
{

constexpr const char* kRouteDir = "/routes";
constexpr const char* kRouteAssetRoot = "/routes/.trailmate";
constexpr const char* kRouteAssetImageSubdir = "images";
constexpr const char* kRouteAssetThumbSubdir = "thumbs";
constexpr const char* kRouteAssetViewSubdir = "views";
constexpr std::size_t kRouteDownloadBufferSize = 512;
constexpr int kRouteDownloadTxBufferSize = 512;
constexpr uint32_t kRouteImageWorkerStackBytes = 6 * 1024;
constexpr UBaseType_t kRouteImageWorkerPriority = 2;
constexpr uint16_t kRouteThumbWidth = 200;
constexpr uint16_t kRouteThumbHeight = 120;
constexpr uint16_t kRouteViewWidth = 320;
constexpr uint16_t kRouteViewHeight = 180;
constexpr std::size_t kRouteCacheInternalReserveBytes = 96 * 1024;
constexpr std::size_t kRouteCacheInternalSlackBytes = 8 * 1024;
constexpr std::size_t kRouteJpegDecoderWorkBytes = 3100;
constexpr std::size_t kRouteHttpInternalFreeTargetBytes = 48 * 1024;
constexpr std::size_t kRouteHttpInternalLargestTargetBytes = 12 * 1024;
constexpr std::size_t kRouteImageDownloadAttempts = 1;
constexpr int kRouteImageMemoryWaitAttempts = 16;
constexpr TickType_t kRouteImageRetryDelayTicks = pdMS_TO_TICKS(1600);
constexpr TickType_t kRouteImageMemoryWaitTicks = pdMS_TO_TICKS(250);

enum class RouteImageBatchKind : uint8_t
{
    Download = 0,
    Cache,
};

struct RouteImageBatchContext
{
#if defined(ESP_PLATFORM)
    static void* allocate_storage(std::size_t size) noexcept
    {
        void* ptr = heap_caps_malloc_prefer(size,
                                            2,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return ptr;
    }

    void* operator new(std::size_t size)
    {
        void* ptr = allocate_storage(size);
        return ptr != nullptr ? ptr : ::operator new(size);
    }

    void* operator new(std::size_t size, const std::nothrow_t&) noexcept
    {
        return allocate_storage(size);
    }

    void operator delete(void* ptr) noexcept
    {
        heap_caps_free(ptr);
    }

    void operator delete(void* ptr, std::size_t) noexcept
    {
        operator delete(ptr);
    }

    void operator delete(void* ptr, const std::nothrow_t&) noexcept
    {
        operator delete(ptr);
    }
#endif

    RouteImageBatchKind kind = RouteImageBatchKind::Download;
    std::string asset_id{};
    std::vector<RouteImageDownloadItem> download_items{};
    std::vector<RouteImageCacheItem> cache_items{};
};

struct RouteImageBatchRuntime
{
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t worker_task = nullptr;
    bool launch_pending = false;
    RouteImageDownloadStatus status{};
};

RouteImageBatchRuntime s_image_batch{};

bool has_kml_extension(const std::string& name)
{
    if (name.size() < 4)
    {
        return false;
    }
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch)
                   { return static_cast<char>(std::tolower(ch)); });
    return lower.compare(lower.size() - 4, 4, ".kml") == 0;
}

bool is_safe_asset_id(const std::string& asset_id)
{
    if (asset_id.empty() || asset_id.size() > 48)
    {
        return false;
    }
    for (unsigned char ch : asset_id)
    {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
        {
            continue;
        }
        return false;
    }
    return true;
}

bool ensure_dir(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return false;
    }
    return ::platform::esp::arduino_common::storage::sd_is_directory(path) ||
           ::platform::esp::arduino_common::storage::sd_mkdir(path) ||
           ::platform::esp::arduino_common::storage::sd_is_directory(path);
}

bool ensure_parent_dir(const std::string& path)
{
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash == 0)
    {
        return false;
    }
    return ensure_dir(path.substr(0, slash).c_str());
}

bool ensure_batch_mutex()
{
    if (s_image_batch.mutex != nullptr)
    {
        return true;
    }
    s_image_batch.mutex = xSemaphoreCreateMutex();
    return s_image_batch.mutex != nullptr;
}

class BatchStateLock
{
  public:
    explicit BatchStateLock(SemaphoreHandle_t mutex) : mutex_(mutex)
    {
        if (mutex_)
        {
            xSemaphoreTake(mutex_, portMAX_DELAY);
        }
    }

    ~BatchStateLock()
    {
        if (mutex_)
        {
            xSemaphoreGive(mutex_);
        }
    }

    BatchStateLock(const BatchStateLock&) = delete;
    BatchStateLock& operator=(const BatchStateLock&) = delete;

  private:
    SemaphoreHandle_t mutex_ = nullptr;
};

void put_le16(uint8_t* out, uint16_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
}

void put_le32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 16) & 0xFFU);
    out[3] = static_cast<uint8_t>((value >> 24) & 0xFFU);
}

class HeapCapsBuffer
{
  public:
    HeapCapsBuffer() = default;
    ~HeapCapsBuffer()
    {
        reset();
    }

    HeapCapsBuffer(const HeapCapsBuffer&) = delete;
    HeapCapsBuffer& operator=(const HeapCapsBuffer&) = delete;

    bool allocate(std::size_t bytes, bool zero_fill = true)
    {
        reset();
        if (bytes == 0)
        {
            return false;
        }

        data_ = static_cast<uint8_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!data_)
        {
            const std::size_t internal_free =
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            const std::size_t internal_largest =
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (internal_free < bytes + kRouteCacheInternalReserveBytes ||
                internal_largest < bytes + kRouteCacheInternalSlackBytes)
            {
                return false;
            }
            data_ = static_cast<uint8_t*>(
                heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        }
        if (!data_)
        {
            return false;
        }
        size_ = bytes;
        if (zero_fill)
        {
            std::memset(data_, 0, size_);
        }
        return true;
    }

    void reset()
    {
        if (data_)
        {
            heap_caps_free(data_);
            data_ = nullptr;
        }
        size_ = 0;
    }

    uint8_t* data()
    {
        return data_;
    }

    const uint8_t* data() const
    {
        return data_;
    }

    std::size_t size() const
    {
        return size_;
    }

  private:
    uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
};

void log_cache_memory_skip(const char* label, std::size_t bytes)
{
    std::printf("[RouteImage][cache] skip %s bytes=%u internal_free=%u "
                "internal_largest=%u psram_largest=%u\n",
                label ? label : "-",
                static_cast<unsigned>(bytes),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void log_route_http_memory(const char* stage)
{
    std::printf("[RouteImage][http] %s internal_free=%u internal_largest=%u "
                "psram_free=%u psram_largest=%u\n",
                stage ? stage : "-",
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void log_route_task_create_failed(const char* task_name,
                                  std::size_t item_count,
                                  uint32_t stack_bytes)
{
    std::printf("[RouteImage][task] create_failed name=%s items=%u stack=%u "
                "internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
                task_name ? task_name : "-",
                static_cast<unsigned>(item_count),
                static_cast<unsigned>(stack_bytes),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                static_cast<unsigned>(
                    heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

bool route_http_memory_ready()
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
               kRouteHttpInternalFreeTargetBytes &&
           heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >=
               kRouteHttpInternalLargestTargetBytes;
}

bool wait_for_route_http_memory()
{
    if (route_http_memory_ready())
    {
        return true;
    }

    log_route_http_memory("wait_begin");
    for (int attempt = 0; attempt < kRouteImageMemoryWaitAttempts; ++attempt)
    {
        vTaskDelay(kRouteImageMemoryWaitTicks);
        if (route_http_memory_ready())
        {
            log_route_http_memory("wait_ready");
            return true;
        }
    }
    log_route_http_memory("wait_continue");
    return false;
}

bool route_image_http_error_retryable(const std::string& error)
{
    return error == "Open HTTP request failed" ||
           error == "Fetch HTTP headers failed" ||
           error == "Read HTTP response failed" ||
           error == "Low HTTP memory" ||
           error == "Create HTTP client failed";
}

void log_route_worker_stack(const char* stage)
{
#if defined(ESP_PLATFORM)
    const UBaseType_t free_words = uxTaskGetStackHighWaterMark(nullptr);
    std::printf("[RouteImage][task] %s stack_free_words=%u stack_free_bytes=%u\n",
                stage ? stage : "-",
                static_cast<unsigned>(free_words),
                static_cast<unsigned>(free_words * sizeof(StackType_t)));
#else
    (void)stage;
#endif
}

struct RouteJpegCacheContext
{
    ::platform::esp::arduino_common::storage::SdRuntimeFile input{};
    uint8_t* pixels = nullptr;
    std::size_t pixel_bytes = 0;
    double crop_x = 0.0;
    double crop_y = 0.0;
    double crop_w = 0.0;
    double crop_h = 0.0;
    uint16_t source_w = 0;
    uint16_t source_h = 0;
    uint16_t target_w = 0;
    uint16_t target_h = 0;
};

uint32_t jpeg_input_cb(esp_rom_tjpgd_dec_t* dec, uint8_t* buffer, uint32_t ndata)
{
    auto* ctx = dec ? static_cast<RouteJpegCacheContext*>(dec->device) : nullptr;
    if (!ctx)
    {
        return 0;
    }
    if (buffer)
    {
        const int read = ctx->input.read(buffer, ndata);
        return read > 0 ? static_cast<uint32_t>(read) : 0;
    }

    const uint64_t next = ctx->input.position() + static_cast<uint64_t>(ndata);
    return ctx->input.seek(next) ? ndata : 0;
}

uint32_t jpeg_output_cb(esp_rom_tjpgd_dec_t* dec,
                        void* bitmap,
                        esp_rom_tjpgd_rect_t* rect)
{
    auto* ctx = dec ? static_cast<RouteJpegCacheContext*>(dec->device) : nullptr;
    auto* rgb = static_cast<const uint8_t*>(bitmap);
    if (!ctx || !rgb || !rect || !ctx->pixels ||
        ctx->crop_w <= 0.0 || ctx->crop_h <= 0.0 ||
        ctx->target_w == 0 || ctx->target_h == 0)
    {
        return 0;
    }

    const int rect_w = static_cast<int>(rect->right - rect->left + 1);
    if (rect_w <= 0)
    {
        return 1;
    }

    const int dy0 = std::max<int>(0,
                                  static_cast<int>(std::floor(
                                      ((static_cast<double>(rect->top) - ctx->crop_y) *
                                       ctx->target_h) /
                                      ctx->crop_h)) -
                                      1);
    const int dy1 = std::min<int>(
        static_cast<int>(ctx->target_h) - 1,
        static_cast<int>(std::ceil(
            ((static_cast<double>(rect->bottom + 1U) - ctx->crop_y) * ctx->target_h) /
            ctx->crop_h)) +
            1);
    const int dx0 = std::max<int>(0,
                                  static_cast<int>(std::floor(
                                      ((static_cast<double>(rect->left) - ctx->crop_x) *
                                       ctx->target_w) /
                                      ctx->crop_w)) -
                                      1);
    const int dx1 = std::min<int>(
        static_cast<int>(ctx->target_w) - 1,
        static_cast<int>(std::ceil(
            ((static_cast<double>(rect->right + 1U) - ctx->crop_x) * ctx->target_w) /
            ctx->crop_w)) +
            1);

    for (int dy = dy0; dy <= dy1; ++dy)
    {
        const double syf = ctx->crop_y +
                           ((static_cast<double>(dy) + 0.5) * ctx->crop_h) /
                               ctx->target_h;
        const int sy = std::max<int>(
            0,
            std::min<int>(static_cast<int>(ctx->source_h) - 1,
                          static_cast<int>(syf)));
        if (sy < rect->top || sy > rect->bottom)
        {
            continue;
        }

        for (int dx = dx0; dx <= dx1; ++dx)
        {
            const double sxf = ctx->crop_x +
                               ((static_cast<double>(dx) + 0.5) * ctx->crop_w) /
                                   ctx->target_w;
            const int sx = std::max<int>(
                0,
                std::min<int>(static_cast<int>(ctx->source_w) - 1,
                              static_cast<int>(sxf)));
            if (sx < rect->left || sx > rect->right)
            {
                continue;
            }

            const std::size_t src_offset =
                (static_cast<std::size_t>(sy - rect->top) *
                     static_cast<std::size_t>(rect_w) +
                 static_cast<std::size_t>(sx - rect->left)) *
                3U;
            const std::size_t dst_offset =
                (static_cast<std::size_t>(dy) * ctx->target_w +
                 static_cast<std::size_t>(dx)) *
                3U;
            if (dst_offset + 2U >= ctx->pixel_bytes)
            {
                continue;
            }
            ctx->pixels[dst_offset + 0] = rgb[src_offset + 2];
            ctx->pixels[dst_offset + 1] = rgb[src_offset + 1];
            ctx->pixels[dst_offset + 2] = rgb[src_offset + 0];
        }
    }
    return 1;
}

void configure_cover_crop(RouteJpegCacheContext& ctx)
{
    const double source_aspect =
        static_cast<double>(ctx.source_w) / static_cast<double>(ctx.source_h);
    const double target_aspect =
        static_cast<double>(ctx.target_w) / static_cast<double>(ctx.target_h);
    if (source_aspect > target_aspect)
    {
        ctx.crop_h = ctx.source_h;
        ctx.crop_w = ctx.crop_h * target_aspect;
        ctx.crop_x = (static_cast<double>(ctx.source_w) - ctx.crop_w) / 2.0;
        ctx.crop_y = 0.0;
    }
    else
    {
        ctx.crop_w = ctx.source_w;
        ctx.crop_h = ctx.crop_w / target_aspect;
        ctx.crop_x = 0.0;
        ctx.crop_y = (static_cast<double>(ctx.source_h) - ctx.crop_h) / 2.0;
    }
}

bool write_bmp_24(const std::string& path,
                  const uint8_t* pixels,
                  std::size_t pixel_size,
                  uint16_t width,
                  uint16_t height)
{
    if (path.empty() || width == 0 || height == 0 ||
        !pixels ||
        pixel_size < static_cast<std::size_t>(width) * height * 3U)
    {
        return false;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeFile out;
    if (!out.open(path.c_str(), "w"))
    {
        return false;
    }

    const uint32_t row_bytes = (static_cast<uint32_t>(width) * 3U + 3U) & ~3U;
    const uint32_t pixel_bytes = row_bytes * height;
    const uint32_t data_offset = 14U + 40U;
    const uint32_t file_size = data_offset + pixel_bytes;

    uint8_t file_hdr[14] = {'B', 'M'};
    put_le32(file_hdr + 2, file_size);
    put_le32(file_hdr + 10, data_offset);
    if (out.write(file_hdr, sizeof(file_hdr)) != sizeof(file_hdr))
    {
        out.close();
        return false;
    }

    uint8_t info_hdr[40]{};
    put_le32(info_hdr + 0, 40U);
    put_le32(info_hdr + 4, width);
    put_le32(info_hdr + 8, height);
    put_le16(info_hdr + 12, 1U);
    put_le16(info_hdr + 14, 24U);
    put_le32(info_hdr + 20, pixel_bytes);
    if (out.write(info_hdr, sizeof(info_hdr)) != sizeof(info_hdr))
    {
        out.close();
        return false;
    }

    std::vector<uint8_t> row(row_bytes, 0);
    const std::size_t src_row_bytes = static_cast<std::size_t>(width) * 3U;
    for (uint16_t row_index = 0; row_index < height; ++row_index)
    {
        const uint16_t source_y = static_cast<uint16_t>(height - 1U - row_index);
        const uint8_t* src = pixels +
                             static_cast<std::size_t>(source_y) * src_row_bytes;
        std::memcpy(row.data(), src, src_row_bytes);
        if (out.write(row.data(), row.size()) != row.size())
        {
            out.close();
            return false;
        }
    }

    const bool flushed = out.flush();
    out.close();
    return flushed;
}

bool generate_route_image_bmp_cache(const std::string& source_path,
                                    const std::string& output_path,
                                    uint16_t width,
                                    uint16_t height)
{
    if (source_path.empty() || output_path.empty() || width == 0 || height == 0)
    {
        return false;
    }
    if (route_asset_file_exists(output_path))
    {
        return true;
    }
    if (!ensure_parent_dir(output_path))
    {
        return false;
    }

    RouteJpegCacheContext ctx{};
    ctx.target_w = width;
    ctx.target_h = height;
    HeapCapsBuffer pixels;
    const std::size_t pixel_bytes = static_cast<std::size_t>(width) * height * 3U;
    if (!pixels.allocate(pixel_bytes))
    {
        log_cache_memory_skip("pixel_alloc", pixel_bytes);
        return false;
    }
    ctx.pixels = pixels.data();
    ctx.pixel_bytes = pixels.size();
    if (!ctx.input.open(source_path.c_str(), "r"))
    {
        return false;
    }

    constexpr uint8_t kDecodeScale = 1;
    HeapCapsBuffer decoder_work;
    if (!decoder_work.allocate(kRouteJpegDecoderWorkBytes))
    {
        ctx.input.close();
        log_cache_memory_skip("decoder_work", kRouteJpegDecoderWorkBytes);
        return false;
    }
    esp_rom_tjpgd_dec_t decoder{};
    esp_rom_tjpgd_result_t result =
        esp_rom_tjpgd_prepare(&decoder,
                              jpeg_input_cb,
                              decoder_work.data(),
                              static_cast<uint32_t>(decoder_work.size()),
                              &ctx);
    if (result != JDR_OK || decoder.width == 0 || decoder.height == 0)
    {
        ctx.input.close();
        return false;
    }
    ctx.source_w = static_cast<uint16_t>(
        std::max<uint32_t>(1U, decoder.width >> kDecodeScale));
    ctx.source_h = static_cast<uint16_t>(
        std::max<uint32_t>(1U, decoder.height >> kDecodeScale));
    configure_cover_crop(ctx);

    result = esp_rom_tjpgd_decomp(&decoder, jpeg_output_cb, kDecodeScale);
    ctx.input.close();
    if (result != JDR_OK)
    {
        return false;
    }

    const std::string temp_path = output_path + ".tmp";
    (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
    if (!write_bmp_24(temp_path, pixels.data(), pixels.size(), width, height))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }
    if (::platform::esp::arduino_common::storage::sd_exists(output_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(output_path.c_str());
    }
    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(), output_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return false;
    }
    return true;
}

bool route_image_cache_ready(const RouteImageCacheItem& item)
{
    return !item.source_path.empty() &&
           route_asset_file_exists(item.source_path) &&
           (item.preview_path.empty() || route_asset_file_exists(item.preview_path));
}

bool ensure_route_image_cache(const RouteImageCacheItem& item)
{
    if (route_image_cache_ready(item))
    {
        return true;
    }
    if (item.source_path.empty() || !route_asset_file_exists(item.source_path))
    {
        return false;
    }

    bool ok = true;
    if (!item.preview_path.empty() && !route_asset_file_exists(item.preview_path))
    {
        ok = generate_route_image_bmp_cache(
                 item.source_path,
                 item.preview_path,
                 kRouteThumbWidth,
                 kRouteThumbHeight) &&
             ok;
    }
    if (!ok)
    {
        return false;
    }
    if (!item.view_path.empty() && !route_asset_file_exists(item.view_path))
    {
        (void)generate_route_image_bmp_cache(item.source_path,
                                             item.view_path,
                                             kRouteViewWidth,
                                             kRouteViewHeight);
    }
    return route_image_cache_ready(item);
}

void set_batch_status_locked(RouteImageDownloadPhase phase,
                             bool busy,
                             const std::string& asset_id,
                             std::size_t total,
                             std::size_t processed,
                             std::size_t saved,
                             std::size_t failed,
                             std::size_t current_index,
                             std::uint32_t bytes,
                             const char* message,
                             const char* error)
{
    s_image_batch.status.phase = phase;
    s_image_batch.status.busy = busy;
    s_image_batch.status.asset_id = asset_id;
    s_image_batch.status.total = total;
    s_image_batch.status.processed = processed;
    s_image_batch.status.saved = saved;
    s_image_batch.status.failed = failed;
    s_image_batch.status.current_index = current_index;
    s_image_batch.status.bytes = bytes;
    s_image_batch.status.message = message ? message : "";
    s_image_batch.status.error = error ? error : "";
}

void update_batch_status(RouteImageDownloadPhase phase,
                         bool busy,
                         const std::string& asset_id,
                         std::size_t total,
                         std::size_t processed,
                         std::size_t saved,
                         std::size_t failed,
                         std::size_t current_index,
                         std::uint32_t bytes,
                         const char* message,
                         const char* error = nullptr)
{
    if (!ensure_batch_mutex())
    {
        return;
    }
    BatchStateLock lock(s_image_batch.mutex);
    set_batch_status_locked(phase,
                            busy,
                            asset_id,
                            total,
                            processed,
                            saved,
                            failed,
                            current_index,
                            bytes,
                            message,
                            error);
}

void route_image_worker_task(void* param)
{
    auto* ctx = static_cast<RouteImageBatchContext*>(param);
    std::string asset_id;
    RouteImageBatchKind kind = RouteImageBatchKind::Download;
    std::vector<RouteImageDownloadItem> download_items;
    std::vector<RouteImageCacheItem> cache_items;
    if (ctx != nullptr)
    {
        kind = ctx->kind;
        asset_id = std::move(ctx->asset_id);
        download_items = std::move(ctx->download_items);
        cache_items = std::move(ctx->cache_items);
        delete ctx;
    }

    const bool cache_only = kind == RouteImageBatchKind::Cache;
    const std::size_t total = cache_only ? cache_items.size() : download_items.size();
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::uint32_t bytes = 0;
    std::size_t consecutive_connection_failures = 0;
    bool stopped_for_connection_failure = false;
    char detail[96]{};

    log_route_worker_stack("start");
    std::snprintf(detail,
                  sizeof(detail),
                  cache_only ? "Caching 0/%u" : "Downloading 0/%u",
                  static_cast<unsigned>(total));
    update_batch_status(cache_only ? RouteImageDownloadPhase::Caching
                                   : RouteImageDownloadPhase::Downloading,
                        true,
                        asset_id,
                        total,
                        processed,
                        saved,
                        failed,
                        0,
                        bytes,
                        detail);

    std::string asset_dir;
    if (!ensure_route_asset_dir(asset_id, asset_dir))
    {
        update_batch_status(RouteImageDownloadPhase::Failed,
                            false,
                            asset_id,
                            total,
                            processed,
                            saved,
                            total,
                            0,
                            bytes,
                            "Create image dir failed",
                            "Create image dir failed");
    }
    else if (cache_only)
    {
        for (std::size_t index = 0; index < total; ++index)
        {
            const RouteImageCacheItem& item = cache_items[index];
            std::snprintf(detail,
                          sizeof(detail),
                          "Caching %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            update_batch_status(RouteImageDownloadPhase::Caching,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);

            ++processed;
            if (ensure_route_image_cache(item))
            {
                ++saved;
                std::snprintf(detail,
                              sizeof(detail),
                              "Cached %u/%u",
                              static_cast<unsigned>(saved),
                              static_cast<unsigned>(total));
            }
            else
            {
                ++failed;
                std::snprintf(detail,
                              sizeof(detail),
                              "Cache %u failed",
                              static_cast<unsigned>(index + 1));
            }

            update_batch_status(RouteImageDownloadPhase::Caching,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);
        }

        const bool ok = failed == 0 && saved >= total;
        if (ok)
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "Cached all %u images",
                          static_cast<unsigned>(total));
        }
        else
        {
            std::snprintf(detail,
                          sizeof(detail),
                          "Cached %u/%u  failed %u",
                          static_cast<unsigned>(saved),
                          static_cast<unsigned>(total),
                          static_cast<unsigned>(failed));
        }
        update_batch_status(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed,
                            false,
                            asset_id,
                            total,
                            processed,
                            saved,
                            failed,
                            processed < total ? processed : (total == 0 ? 0 : total - 1),
                            bytes,
                            detail,
                            ok ? nullptr : detail);
    }
    else
    {
        for (std::size_t index = 0; index < total; ++index)
        {
            const RouteImageDownloadItem& item = download_items[index];
            bool original_available = false;
            std::snprintf(detail,
                          sizeof(detail),
                          "Image %u/%u",
                          static_cast<unsigned>(index + 1),
                          static_cast<unsigned>(total));
            update_batch_status(RouteImageDownloadPhase::Downloading,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);

            if (item.output_path.empty() || item.url.empty())
            {
                ++failed;
                consecutive_connection_failures = 0;
                std::snprintf(detail,
                              sizeof(detail),
                              "Image %u missing URL",
                              static_cast<unsigned>(index + 1));
            }
            else if (route_asset_file_exists(item.output_path))
            {
                original_available = true;
                consecutive_connection_failures = 0;
                std::snprintf(detail,
                              sizeof(detail),
                              "Image %u/%u already saved",
                              static_cast<unsigned>(index + 1),
                              static_cast<unsigned>(total));
            }
            else
            {
                RouteImageDownloadResult result{};
                for (std::size_t attempt = 0; attempt < kRouteImageDownloadAttempts; ++attempt)
                {
                    if (wait_for_route_http_memory())
                    {
                        result = download_route_image(item.url, item.output_path);
                    }
                    else
                    {
                        result = {};
                        result.error = "Low HTTP memory";
                    }
                    if (result.ok ||
                        !route_image_http_error_retryable(result.error) ||
                        attempt + 1U >= kRouteImageDownloadAttempts)
                    {
                        break;
                    }

                    std::snprintf(detail,
                                  sizeof(detail),
                                  "Retry %u/%u image %u",
                                  static_cast<unsigned>(attempt + 2U),
                                  static_cast<unsigned>(kRouteImageDownloadAttempts),
                                  static_cast<unsigned>(index + 1));
                    update_batch_status(RouteImageDownloadPhase::Downloading,
                                        true,
                                        asset_id,
                                        total,
                                        processed,
                                        saved,
                                        failed,
                                        index,
                                        bytes,
                                        detail);
                    log_route_http_memory("retry_wait");
                    vTaskDelay(kRouteImageRetryDelayTicks);
                }
                if (result.ok)
                {
                    original_available = true;
                    bytes += result.bytes;
                    consecutive_connection_failures = 0;
                }
                else
                {
                    ++failed;
                    if (route_image_http_error_retryable(result.error))
                    {
                        ++consecutive_connection_failures;
                    }
                    else
                    {
                        consecutive_connection_failures = 0;
                    }
                    std::snprintf(detail,
                                  sizeof(detail),
                                  "Image %u failed: %.28s",
                                  static_cast<unsigned>(index + 1),
                                  result.error.empty() ? "Download failed" : result.error.c_str());
                }
            }

            if (original_available)
            {
                ++saved;
                std::snprintf(detail,
                              sizeof(detail),
                              "Saved %u/%u  %u KB",
                              static_cast<unsigned>(saved),
                              static_cast<unsigned>(total),
                              static_cast<unsigned>((bytes + 1023U) / 1024U));
            }
            ++processed;

            update_batch_status(RouteImageDownloadPhase::Downloading,
                                true,
                                asset_id,
                                total,
                                processed,
                                saved,
                                failed,
                                index,
                                bytes,
                                detail);

            if (consecutive_connection_failures >= 3)
            {
                stopped_for_connection_failure = true;
                std::snprintf(detail,
                              sizeof(detail),
                              "Connection failed; stopped at %u/%u",
                              static_cast<unsigned>(index + 1),
                              static_cast<unsigned>(total));
                break;
            }
        }

        const bool ok = !stopped_for_connection_failure && failed == 0 && saved >= total;
        if (!stopped_for_connection_failure)
        {
            if (ok)
            {
                std::snprintf(detail,
                              sizeof(detail),
                              "Saved all %u images",
                              static_cast<unsigned>(total));
            }
            else
            {
                std::snprintf(detail,
                              sizeof(detail),
                              "Saved %u/%u  failed %u",
                              static_cast<unsigned>(saved),
                              static_cast<unsigned>(total),
                              static_cast<unsigned>(failed));
            }
        }
        update_batch_status(ok ? RouteImageDownloadPhase::Done : RouteImageDownloadPhase::Failed,
                            false,
                            asset_id,
                            total,
                            processed,
                            saved,
                            failed,
                            processed < total ? processed : (total == 0 ? 0 : total - 1),
                            bytes,
                            detail,
                            ok ? nullptr : detail);
    }

    if (ensure_batch_mutex())
    {
        BatchStateLock lock(s_image_batch.mutex);
        s_image_batch.worker_task = nullptr;
        s_image_batch.launch_pending = false;
    }
    std::vector<RouteImageDownloadItem>().swap(download_items);
    std::vector<RouteImageCacheItem>().swap(cache_items);
    std::string().swap(asset_id);
    log_route_worker_stack("finish");
    vTaskDelete(nullptr);
}

} // namespace

bool is_supported()
{
    return true;
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!platform::ui::device::sd_ready())
    {
        return false;
    }

    ::platform::esp::arduino_common::storage::SdRuntimeDir dir;
    if (!dir.open(kRouteDir))
    {
        return false;
    }

    char name_buf[128];
    bool is_dir = false;
    while (out_routes.size() < max_count && dir.read_next(name_buf, sizeof(name_buf), &is_dir))
    {
        if (!is_dir)
        {
            std::string name = name_buf;
            if (has_kml_extension(name))
            {
                out_routes.push_back(name);
            }
        }
    }
    dir.close();

    std::sort(out_routes.begin(), out_routes.end());
    return true;
}

bool remove_route(const std::string& path)
{
    if (!platform::ui::device::sd_ready() || path.empty())
    {
        return false;
    }
    return ::platform::esp::arduino_common::storage::sd_remove(path.c_str());
}

const char* route_dir()
{
    return kRouteDir;
}

bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir)
{
    out_dir.clear();
    if (!platform::ui::device::sd_ready() || !is_safe_asset_id(asset_id))
    {
        return false;
    }
    if (!ensure_dir(kRouteDir) || !ensure_dir(kRouteAssetRoot))
    {
        return false;
    }
    out_dir = std::string(kRouteAssetRoot) + "/" + asset_id;
    if (!ensure_dir(out_dir.c_str()))
    {
        out_dir.clear();
        return false;
    }
    const std::string image_dir = out_dir + "/" + kRouteAssetImageSubdir;
    const std::string thumb_dir = out_dir + "/" + kRouteAssetThumbSubdir;
    const std::string view_dir = out_dir + "/" + kRouteAssetViewSubdir;
    if (!ensure_dir(image_dir.c_str()) ||
        !ensure_dir(thumb_dir.c_str()) ||
        !ensure_dir(view_dir.c_str()))
    {
        out_dir.clear();
        return false;
    }
    return true;
}

bool route_asset_file_exists(const std::string& path)
{
    return platform::ui::device::sd_ready() && !path.empty() &&
           ::platform::esp::arduino_common::storage::sd_exists(path.c_str()) &&
           !::platform::esp::arduino_common::storage::sd_is_directory(path.c_str());
}

RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes)
{
    RouteImageDownloadResult result{};
    if (url.empty() || output_path.empty())
    {
        result.error = "Missing URL";
        return result;
    }
    if (!platform::ui::device::sd_ready())
    {
        result.error = "No SD card";
        return result;
    }
    if (!ensure_parent_dir(output_path))
    {
        result.error = "Create image dir failed";
        return result;
    }

    const std::string temp_path = output_path + ".tmp";
    (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());

    ::platform::esp::arduino_common::storage::SdRuntimeFile out;
    if (!out.open(temp_path.c_str(), "w"))
    {
        result.error = "Open image file failed";
        return result;
    }

    platform::ui::http_client::Request request{};
    request.url = url.c_str();
    request.client = platform::ui::wifi_access::Client::RouteStorage;
    request.access_kind = platform::ui::wifi_access::AccessKind::HttpDownload;
    request.priority = platform::ui::wifi_access::Priority::UserForeground;
    request.reason = "route_image";
    request.buffer_size = static_cast<int>(kRouteDownloadBufferSize);
    request.tx_buffer_size = kRouteDownloadTxBufferSize;
    request.max_bytes = max_bytes;

    platform::ui::http_client::TransferStats stats{};
    const bool ok = platform::ui::http_client::download(
        request,
        [](const std::uint8_t* data, std::size_t len, void* context)
        {
            auto* file = static_cast<::platform::esp::arduino_common::storage::SdRuntimeFile*>(context);
            return file && file->write(data, len) == len;
        },
        &out,
        result.error,
        &stats);
    result.http_status = stats.http_status;
    result.bytes = stats.bytes;

    (void)out.flush();
    out.close();

    if (!ok)
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        return result;
    }

    if (::platform::esp::arduino_common::storage::sd_exists(output_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(output_path.c_str());
    }
    if (!::platform::esp::arduino_common::storage::sd_rename(temp_path.c_str(), output_path.c_str()))
    {
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        result.ok = false;
        result.error = "Finalize image failed";
        return result;
    }

    result.ok = true;
    result.error.clear();
    return result;
}

bool start_route_image_download(const std::string& asset_id,
                                std::vector<RouteImageDownloadItem> items,
                                std::string& out_error)
{
    out_error.clear();
    const std::size_t total = items.size();
    if (!ensure_batch_mutex())
    {
        out_error = "Create image download mutex failed";
        return false;
    }
    if (!is_safe_asset_id(asset_id))
    {
        out_error = "Invalid route asset id";
        return false;
    }
    if (total == 0)
    {
        out_error = "No route images";
        return false;
    }

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (s_image_batch.status.busy ||
            s_image_batch.worker_task != nullptr ||
            s_image_batch.launch_pending)
        {
            if (s_image_batch.status.asset_id == asset_id)
            {
                return true;
            }
            out_error = "Another route image download is running";
            return false;
        }
    }

    auto* ctx = new (std::nothrow) RouteImageBatchContext{};
    if (ctx == nullptr)
    {
        log_route_task_create_failed("route_img_dl_ctx",
                                     total,
                                     kRouteImageWorkerStackBytes);
        out_error = "Allocate image download task failed";
        return false;
    }
    ctx->kind = RouteImageBatchKind::Download;
    ctx->asset_id = asset_id;
    ctx->download_items = std::move(items);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (s_image_batch.status.busy ||
            s_image_batch.worker_task != nullptr ||
            s_image_batch.launch_pending)
        {
            if (s_image_batch.status.asset_id == asset_id)
            {
                delete ctx;
                return true;
            }
            out_error = "Another route image download is running";
            delete ctx;
            return false;
        }

        s_image_batch.launch_pending = true;
        set_batch_status_locked(RouteImageDownloadPhase::Downloading,
                                true,
                                asset_id,
                                total,
                                0,
                                0,
                                0,
                                0,
                                0,
                                "Queued image download",
                                nullptr);
    }

    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ok = xTaskCreate(route_image_worker_task,
                                           "route_img_dl",
                                           kRouteImageWorkerStackBytes,
                                           ctx,
                                           kRouteImageWorkerPriority,
                                           &task_handle);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (task_ok != pdPASS || task_handle == nullptr)
        {
            log_route_task_create_failed("route_img_dl",
                                         total,
                                         kRouteImageWorkerStackBytes);
            s_image_batch.worker_task = nullptr;
            s_image_batch.launch_pending = false;
            set_batch_status_locked(RouteImageDownloadPhase::Failed,
                                    false,
                                    asset_id,
                                    total,
                                    0,
                                    0,
                                    total,
                                    0,
                                    0,
                                    "Create image download task failed",
                                    "Create image download task failed");
            out_error = s_image_batch.status.message;
            delete ctx;
            return false;
        }
        if (s_image_batch.launch_pending)
        {
            s_image_batch.worker_task = task_handle;
            s_image_batch.launch_pending = false;
        }
    }
    return true;
}

bool start_route_image_cache_build(const std::string& asset_id,
                                   const std::vector<RouteImageCacheItem>& items,
                                   std::string& out_error)
{
    out_error.clear();
    if (!ensure_batch_mutex())
    {
        out_error = "Create image cache mutex failed";
        return false;
    }
    if (!is_safe_asset_id(asset_id))
    {
        out_error = "Invalid route asset id";
        return false;
    }
    if (items.empty())
    {
        out_error = "No route image cache work";
        return false;
    }

    auto* ctx = new (std::nothrow) RouteImageBatchContext{};
    if (ctx == nullptr)
    {
        out_error = "Allocate image cache task failed";
        return false;
    }
    ctx->kind = RouteImageBatchKind::Cache;
    ctx->asset_id = asset_id;
    ctx->cache_items = items;

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (s_image_batch.status.busy ||
            s_image_batch.worker_task != nullptr ||
            s_image_batch.launch_pending)
        {
            if (s_image_batch.status.asset_id == asset_id)
            {
                delete ctx;
                return true;
            }
            out_error = "Another route image task is running";
            delete ctx;
            return false;
        }

        s_image_batch.launch_pending = true;
        set_batch_status_locked(RouteImageDownloadPhase::Caching,
                                true,
                                asset_id,
                                items.size(),
                                0,
                                0,
                                0,
                                0,
                                0,
                                "Queued image cache",
                                nullptr);
    }

    TaskHandle_t task_handle = nullptr;
    const BaseType_t task_ok = xTaskCreate(route_image_worker_task,
                                           "route_img_cache",
                                           kRouteImageWorkerStackBytes,
                                           ctx,
                                           kRouteImageWorkerPriority,
                                           &task_handle);

    {
        BatchStateLock lock(s_image_batch.mutex);
        if (task_ok != pdPASS || task_handle == nullptr)
        {
            s_image_batch.worker_task = nullptr;
            s_image_batch.launch_pending = false;
            set_batch_status_locked(RouteImageDownloadPhase::Failed,
                                    false,
                                    asset_id,
                                    items.size(),
                                    0,
                                    0,
                                    items.size(),
                                    0,
                                    0,
                                    "Create image cache task failed",
                                    "Create image cache task failed");
            out_error = s_image_batch.status.message;
            delete ctx;
            return false;
        }
        if (s_image_batch.launch_pending)
        {
            s_image_batch.worker_task = task_handle;
            s_image_batch.launch_pending = false;
        }
    }
    return true;
}

RouteImageDownloadStatus route_image_download_status()
{
    RouteImageDownloadStatus status{};
    if (!ensure_batch_mutex())
    {
        status.phase = RouteImageDownloadPhase::Failed;
        status.message = "Create image download mutex failed";
        status.error = status.message;
        return status;
    }
    BatchStateLock lock(s_image_batch.mutex);
    return s_image_batch.status;
}

} // namespace platform::ui::route_storage
