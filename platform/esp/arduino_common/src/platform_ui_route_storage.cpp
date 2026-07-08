#include "platform/ui/route_storage.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"
#include "platform/ui/http_client_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
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

} // namespace platform::ui::route_storage
