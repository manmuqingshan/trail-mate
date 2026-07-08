#include "platform/ui/route_storage.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/http_client_runtime.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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
constexpr std::size_t kRouteDownloadBufferSize = 1024;
constexpr int kRouteDownloadTxBufferSize = 512;
constexpr uint32_t kRouteImageWorkerStackBytes = 12 * 1024;
constexpr UBaseType_t kRouteImageWorkerPriority = 2;

struct RouteImageBatchContext
{
    std::string asset_id{};
    std::vector<RouteImageDownloadItem> items{};
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
    std::vector<RouteImageDownloadItem> items;
    if (ctx != nullptr)
    {
        asset_id = std::move(ctx->asset_id);
        items = std::move(ctx->items);
        delete ctx;
    }

    const std::size_t total = items.size();
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::uint32_t bytes = 0;
    std::size_t consecutive_connection_failures = 0;
    bool stopped_for_connection_failure = false;
    char detail[96]{};

    std::snprintf(detail, sizeof(detail), "Downloading 0/%u", static_cast<unsigned>(total));
    update_batch_status(RouteImageDownloadPhase::Downloading,
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
    else
    {
        for (std::size_t index = 0; index < total; ++index)
        {
            const RouteImageDownloadItem& item = items[index];
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
                ++processed;
                ++failed;
                consecutive_connection_failures = 0;
                std::snprintf(detail,
                              sizeof(detail),
                              "Image %u missing URL",
                              static_cast<unsigned>(index + 1));
            }
            else if (route_asset_file_exists(item.output_path))
            {
                ++processed;
                ++saved;
                consecutive_connection_failures = 0;
                std::snprintf(detail,
                              sizeof(detail),
                              "Image %u/%u already saved",
                              static_cast<unsigned>(index + 1),
                              static_cast<unsigned>(total));
            }
            else
            {
                const auto result = download_route_image(item.url, item.output_path);
                ++processed;
                if (result.ok)
                {
                    ++saved;
                    bytes += result.bytes;
                    consecutive_connection_failures = 0;
                    std::snprintf(detail,
                                  sizeof(detail),
                                  "Saved %u/%u  %u KB",
                                  static_cast<unsigned>(saved),
                                  static_cast<unsigned>(total),
                                  static_cast<unsigned>((bytes + 1023U) / 1024U));
                }
                else
                {
                    ++failed;
                    if (result.error == "Open HTTP request failed")
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
    if (!ensure_dir(image_dir.c_str()))
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
                                const std::vector<RouteImageDownloadItem>& items,
                                std::string& out_error)
{
    out_error.clear();
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
    if (items.empty())
    {
        out_error = "No route images";
        return false;
    }

    auto* ctx = new (std::nothrow) RouteImageBatchContext{};
    if (ctx == nullptr)
    {
        out_error = "Allocate image download task failed";
        return false;
    }
    ctx->asset_id = asset_id;
    ctx->items = items;

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
                                items.size(),
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
