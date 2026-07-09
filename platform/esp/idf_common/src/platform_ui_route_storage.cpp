#include "platform/ui/route_storage.h"

#include "platform/esp/idf_common/bsp_runtime.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>

namespace platform::ui::route_storage
{
namespace
{

constexpr const char* kRouteDir = "/routes";
constexpr const char* kRouteAssetRoot = "/routes/.trailmate";

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

bool is_regular_file(const std::string& path)
{
    struct stat st
    {
    };
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool is_directory(const std::string& path)
{
    struct stat st
    {
    };
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string mount_path_for(const std::string& logical_path)
{
    return std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + logical_path;
}

bool ensure_dir(const std::string& logical_path)
{
    const std::string path = mount_path_for(logical_path);
    return is_directory(path) || ::mkdir(path.c_str(), 0775) == 0 || is_directory(path);
}

} // namespace

bool is_supported()
{
    return platform::esp::idf_common::bsp_runtime::sdcard_capable();
}

bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count)
{
    out_routes.clear();
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready())
    {
        return false;
    }

    const std::string base = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + kRouteDir;
    DIR* dir = ::opendir(base.c_str());
    if (dir == nullptr)
    {
        return false;
    }

    while (out_routes.size() < max_count)
    {
        dirent* entry = ::readdir(dir);
        if (entry == nullptr)
        {
            break;
        }
        const char* name_c = entry->d_name;
        if (name_c == nullptr || ::strcmp(name_c, ".") == 0 || ::strcmp(name_c, "..") == 0)
        {
            continue;
        }
        std::string name = name_c;
        if (!has_kml_extension(name))
        {
            continue;
        }
        const std::string full_path = base + "/" + name;
        if (is_regular_file(full_path))
        {
            out_routes.push_back(name);
        }
    }
    ::closedir(dir);
    std::sort(out_routes.begin(), out_routes.end());
    return true;
}

bool remove_route(const std::string& path)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() || path.empty())
    {
        return false;
    }
    const std::string mount_prefixed = std::string(platform::esp::idf_common::bsp_runtime::sdcard_mount_point()) + path;
    return std::remove(mount_prefixed.c_str()) == 0;
}

const char* route_dir()
{
    return kRouteDir;
}

bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir)
{
    out_dir.clear();
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() ||
        !is_safe_asset_id(asset_id))
    {
        return false;
    }
    if (!ensure_dir(kRouteDir) || !ensure_dir(kRouteAssetRoot))
    {
        return false;
    }
    out_dir = std::string(kRouteAssetRoot) + "/" + asset_id;
    if (!ensure_dir(out_dir) ||
        !ensure_dir(out_dir + "/images") ||
        !ensure_dir(out_dir + "/thumbs") ||
        !ensure_dir(out_dir + "/views"))
    {
        out_dir.clear();
        return false;
    }
    return true;
}

bool route_asset_file_exists(const std::string& path)
{
    if (!platform::esp::idf_common::bsp_runtime::ensure_sdcard_ready() || path.empty())
    {
        return false;
    }
    return is_regular_file(mount_path_for(path));
}

RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes)
{
    (void)url;
    (void)output_path;
    (void)max_bytes;
    RouteImageDownloadResult result{};
    result.error = "Route image download unsupported on this target";
    return result;
}

bool start_route_image_download(const std::string& asset_id,
                                const std::vector<RouteImageDownloadItem>& items,
                                std::string& out_error)
{
    (void)asset_id;
    (void)items;
    out_error = "Route image download unsupported on this target";
    return false;
}

bool start_route_image_cache_build(const std::string& asset_id,
                                   const std::vector<RouteImageCacheItem>& items,
                                   std::string& out_error)
{
    (void)asset_id;
    (void)items;
    out_error = "Route image cache unsupported on this target";
    return false;
}

RouteImageDownloadStatus route_image_download_status()
{
    RouteImageDownloadStatus status{};
    return status;
}

} // namespace platform::ui::route_storage
