#include "platform/ui/route_storage.h"

#include "platform/esp/arduino_common/storage/sd_card_runtime.h"
#include "platform/ui/device_runtime.h"

#include "esp_err.h"
#include "esp_http_client.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <new>
#include <vector>

extern "C" esp_err_t esp_crt_bundle_attach(void* conf);

namespace platform::ui::route_storage
{
namespace
{

constexpr const char* kRouteDir = "/routes";
constexpr const char* kRouteAssetRoot = "/routes/.trailmate";
constexpr const char* kRouteAssetImageSubdir = "images";
constexpr std::size_t kRouteDownloadBufferSize = 2048;

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

void configure_http_client(esp_http_client_config_t& config, const std::string& url)
{
    config = esp_http_client_config_t{};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.timeout_ms = 30000;
    config.disable_auto_redirect = false;
    config.buffer_size = static_cast<int>(kRouteDownloadBufferSize);
    config.buffer_size_tx = 1024;
    config.crt_bundle_attach = esp_crt_bundle_attach;
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

    esp_http_client_config_t config{};
    configure_http_client(config, url);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr)
    {
        out.close();
        (void)::platform::esp::arduino_common::storage::sd_remove(temp_path.c_str());
        result.error = "Create HTTP client failed";
        return result;
    }

    bool ok = false;
    esp_err_t open_err = esp_http_client_open(client, 0);
    if (open_err != ESP_OK)
    {
        result.error = "Open HTTP request failed";
    }
    else if (esp_http_client_fetch_headers(client) < 0)
    {
        result.error = "Fetch HTTP headers failed";
    }
    else
    {
        result.http_status = esp_http_client_get_status_code(client);
        const int64_t content_length = esp_http_client_get_content_length(client);
        if (result.http_status < 200 || result.http_status >= 300)
        {
            char text[48];
            std::snprintf(text, sizeof(text), "HTTP %d", result.http_status);
            result.error = text;
        }
        else if (content_length > 0 && static_cast<std::uint64_t>(content_length) > max_bytes)
        {
            result.error = "Image too large";
        }
        else
        {
            std::vector<std::uint8_t> buffer(kRouteDownloadBufferSize);
            if (buffer.empty())
            {
                result.error = "No download buffer";
            }
            while (result.error.empty())
            {
                const int read = esp_http_client_read(
                    client,
                    reinterpret_cast<char*>(buffer.data()),
                    static_cast<int>(buffer.size()));
                if (read < 0)
                {
                    result.error = "Read image failed";
                    break;
                }
                if (read == 0)
                {
                    ok = true;
                    break;
                }
                if (result.bytes + static_cast<std::uint32_t>(read) > max_bytes)
                {
                    result.error = "Image too large";
                    break;
                }
                const std::size_t written = out.write(buffer.data(), static_cast<std::size_t>(read));
                if (written != static_cast<std::size_t>(read))
                {
                    result.error = "Write image failed";
                    break;
                }
                result.bytes += static_cast<std::uint32_t>(read);
            }
        }
    }

    (void)out.flush();
    out.close();
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

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
