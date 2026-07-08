#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace platform::ui::route_storage
{

struct RouteImageDownloadResult
{
    bool ok = false;
    std::uint32_t bytes = 0;
    int http_status = 0;
    std::string error{};
};

struct RouteImageDownloadItem
{
    std::string url{};
    std::string output_path{};
};

enum class RouteImageDownloadPhase : uint8_t
{
    Idle = 0,
    Downloading,
    Done,
    Failed,
};

struct RouteImageDownloadStatus
{
    RouteImageDownloadPhase phase = RouteImageDownloadPhase::Idle;
    bool busy = false;
    std::string asset_id{};
    std::string message{};
    std::string error{};
    std::size_t total = 0;
    std::size_t processed = 0;
    std::size_t saved = 0;
    std::size_t failed = 0;
    std::size_t current_index = 0;
    std::uint32_t bytes = 0;
};

bool is_supported();
bool list_routes(std::vector<std::string>& out_routes, std::size_t max_count = 64);
bool remove_route(const std::string& path);
const char* route_dir();
bool ensure_route_asset_dir(const std::string& asset_id, std::string& out_dir);
bool route_asset_file_exists(const std::string& path);
RouteImageDownloadResult download_route_image(const std::string& url,
                                              const std::string& output_path,
                                              std::uint32_t max_bytes = 4U * 1024U * 1024U);
bool start_route_image_download(const std::string& asset_id,
                                const std::vector<RouteImageDownloadItem>& items,
                                std::string& out_error);
RouteImageDownloadStatus route_image_download_status();

} // namespace platform::ui::route_storage
