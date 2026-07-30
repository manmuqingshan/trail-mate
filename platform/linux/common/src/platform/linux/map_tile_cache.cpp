#include "platform/linux/map_tile_cache.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <curl/curl.h>
#include <sqlite3.h>

#include "platform/linux/env_config.h"
#include "platform/linux/map_diagnostics.h"
#include "platform/linux/runtime_paths.h"
#include "ui_map_runtime/map_tiles/map_tile_geometry.h"

namespace platform::linux_runtime
{
namespace
{

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxMercatorLat = 85.05112878;
constexpr int kMaxZoom = 19;
constexpr long kConnectTimeoutSeconds = 5;
constexpr long kTransferTimeoutSeconds = 12;
constexpr const char* kUserAgent = "TrailMate-uConsole/0.1";

std::filesystem::path default_root()
{
    return resolve_paths().sd_root / "maps" / "base";
}

std::filesystem::path default_contour_root()
{
    return resolve_paths().sd_root / "maps" / "contour";
}

std::filesystem::path legacy_center_contour_root()
{
    return resolve_paths().sd_root / "contours" / "tiles";
}

sqlite3* open_database()
{
    const std::filesystem::path db_path = sqlite_database_path();
    if (!ensure_directory(db_path.parent_path()))
    {
        return nullptr;
    }

    sqlite3* db = nullptr;
    const int rc = sqlite3_open_v2(db_path.string().c_str(),
                                   &db,
                                   SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                       SQLITE_OPEN_FULLMUTEX,
                                   nullptr);
    if (rc != SQLITE_OK)
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
        return nullptr;
    }

    sqlite3_busy_timeout(db, 5000);
    return db;
}

bool exec_sql(sqlite3* db, const char* sql)
{
    char* error = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (error != nullptr)
    {
        sqlite3_free(error);
    }
    return rc == SQLITE_OK;
}

bool ensure_schema(sqlite3* db)
{
    return db != nullptr && exec_sql(db, "PRAGMA busy_timeout=5000;") &&
           exec_sql(db, "PRAGMA journal_mode=WAL;") &&
           exec_sql(db,
                    "CREATE TABLE IF NOT EXISTS map_tile_cache ("
                    "source TEXT NOT NULL,"
                    "z INTEGER NOT NULL,"
                    "x INTEGER NOT NULL,"
                    "y INTEGER NOT NULL,"
                    "path TEXT NOT NULL,"
                    "content_type TEXT NOT NULL,"
                    "status TEXT NOT NULL,"
                    "http_status INTEGER NOT NULL DEFAULT 0,"
                    "bytes INTEGER NOT NULL DEFAULT 0,"
                    "fetched_at INTEGER NOT NULL DEFAULT "
                    "(CAST(strftime('%s','now') AS INTEGER)),"
                    "last_error TEXT NOT NULL DEFAULT '',"
                    "PRIMARY KEY(source, z, x, y)"
                    ");");
}

struct DatabaseHandle
{
    sqlite3* db = nullptr;

    DatabaseHandle()
    {
        db = open_database();
        if (db != nullptr && !ensure_schema(db))
        {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    ~DatabaseHandle()
    {
        if (db != nullptr)
        {
            sqlite3_close(db);
        }
    }

    DatabaseHandle(const DatabaseHandle&) = delete;
    DatabaseHandle& operator=(const DatabaseHandle&) = delete;
};

bool bind_text(sqlite3_stmt* stmt, int index, std::string_view text)
{
    return sqlite3_bind_text(stmt,
                             index,
                             text.data(),
                             static_cast<int>(text.size()),
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string replace_all(std::string text,
                        std::string_view needle,
                        std::string_view value)
{
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos)
    {
        text.replace(pos, needle.size(), value);
        pos += value.size();
    }
    return text;
}

std::string build_url_from_template(std::string url_template,
                                    const MapTileId& tile)
{
    url_template = replace_all(url_template, "{z}", std::to_string(tile.z));
    url_template = replace_all(url_template, "{x}", std::to_string(tile.x));
    url_template = replace_all(url_template, "{y}", std::to_string(tile.y));
    return url_template;
}

std::string default_url_template(MapBaseSource source)
{
    switch (source)
    {
    case MapBaseSource::Terrain:
        return "https://tile.opentopomap.org/{z}/{x}/{y}.png";
    case MapBaseSource::Satellite:
        return "https://services.arcgisonline.com/ArcGIS/rest/services/"
               "World_Imagery/MapServer/tile/{z}/{y}/{x}";
    case MapBaseSource::Osm:
    default:
        return "https://tile.openstreetmap.org/{z}/{x}/{y}.png";
    }
}

std::string url_template_for_source(MapBaseSource source)
{
    const char* env_name = nullptr;
    switch (source)
    {
    case MapBaseSource::Terrain:
        env_name = "TRAIL_MATE_TERRAIN_TILE_URL";
        break;
    case MapBaseSource::Satellite:
        env_name = "TRAIL_MATE_SATELLITE_TILE_URL";
        break;
    case MapBaseSource::Osm:
    default:
        env_name = "TRAIL_MATE_OSM_TILE_URL";
        break;
    }

    if (const char* value = std::getenv(env_name))
    {
        if (value[0] != '\0')
        {
            return value;
        }
    }
    return default_url_template(source);
}

const char* content_type_for_source(MapBaseSource source)
{
    (void)source;
    return "image/png";
}

MapContourProfile major_contour(int interval_m) noexcept
{
    return MapContourProfile{MapContourKind::Major, interval_m};
}

MapContourProfile minor_contour(int interval_m) noexcept
{
    return MapContourProfile{MapContourKind::Minor, interval_m};
}

bool valid_tile(const MapTileId& tile)
{
    if (tile.z < 0 || tile.z > kMaxZoom)
    {
        return false;
    }

    const int tiles = 1 << tile.z;
    return tile.x >= 0 && tile.x < tiles && tile.y >= 0 && tile.y < tiles;
}

std::uintmax_t file_size_or_zero(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0U : size;
}

bool file_has_png_signature(const std::filesystem::path& path)
{
    constexpr unsigned char kPngSignature[] = {0x89, 'P', 'N', 'G',
                                               0x0D, 0x0A, 0x1A, 0x0A};
    unsigned char header[sizeof(kPngSignature)]{};

    std::ifstream input(path, std::ios::binary);
    if (!input.is_open())
    {
        return false;
    }

    input.read(reinterpret_cast<char*>(header),
               static_cast<std::streamsize>(sizeof(header)));
    return input.gcount() == static_cast<std::streamsize>(sizeof(header)) &&
           std::memcmp(header, kPngSignature, sizeof(header)) == 0;
}

struct DownloadContext
{
    std::ofstream stream;
};

std::size_t write_file_callback(char* ptr,
                                std::size_t size,
                                std::size_t nmemb,
                                void* userdata)
{
    auto* ctx = static_cast<DownloadContext*>(userdata);
    const std::size_t bytes = size * nmemb;
    if (ctx == nullptr || !ctx->stream.is_open())
    {
        return 0;
    }

    ctx->stream.write(ptr, static_cast<std::streamsize>(bytes));
    return ctx->stream.good() ? bytes : 0;
}

MapTileResult record_tile_status(const MapTileId& tile,
                                 const std::filesystem::path& relative_path,
                                 MapTileStatus status,
                                 long http_status,
                                 std::uintmax_t bytes,
                                 std::string_view error)
{
    DatabaseHandle handle;
    if (handle.db != nullptr)
    {
        sqlite3_stmt* stmt = nullptr;
        constexpr const char* kSql =
            "INSERT INTO map_tile_cache("
            "source, z, x, y, path, content_type, status, http_status, bytes, "
            "fetched_at, last_error) "
            "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, "
            "CAST(strftime('%s','now') AS INTEGER), ?10) "
            "ON CONFLICT(source, z, x, y) DO UPDATE SET "
            "path=excluded.path, "
            "content_type=excluded.content_type, "
            "status=excluded.status, "
            "http_status=excluded.http_status, "
            "bytes=excluded.bytes, "
            "fetched_at=excluded.fetched_at, "
            "last_error=excluded.last_error;";
        if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) ==
            SQLITE_OK)
        {
            const std::string relative = relative_path.generic_string();
            const std::string status_text =
                (status == MapTileStatus::Failed) ? "failed" : "cached";
            const bool ok =
                bind_text(stmt, 1, map_base_source_key(tile.source)) &&
                sqlite3_bind_int(stmt, 2, tile.z) == SQLITE_OK &&
                sqlite3_bind_int(stmt, 3, tile.x) == SQLITE_OK &&
                sqlite3_bind_int(stmt, 4, tile.y) == SQLITE_OK &&
                bind_text(stmt, 5, relative) &&
                bind_text(stmt, 6, content_type_for_source(tile.source)) &&
                bind_text(stmt, 7, status_text) &&
                sqlite3_bind_int64(stmt, 8, http_status) == SQLITE_OK &&
                sqlite3_bind_int64(stmt, 9,
                                   static_cast<sqlite3_int64>(bytes)) ==
                    SQLITE_OK &&
                bind_text(stmt, 10, error);
            if (ok)
            {
                (void)sqlite3_step(stmt);
            }
        }
        sqlite3_finalize(stmt);
    }

    MapTileResult result{};
    result.status = status;
    result.tile = tile;
    result.path = resolve_paths().sd_root / relative_path;
    result.message = std::string(error);
    result.http_status = http_status;
    result.bytes = bytes;
    return result;
}

MapTileResult fail_result(const MapTileId& tile,
                          const std::filesystem::path& relative_path,
                          std::string_view message,
                          long http_status = 0)
{
    return record_tile_status(tile,
                              relative_path,
                              MapTileStatus::Failed,
                              http_status,
                              0,
                              message);
}

} // namespace

MapBaseSource sanitize_map_base_source(std::uint8_t source) noexcept
{
    switch (source)
    {
    case 1:
        return MapBaseSource::Terrain;
    case 2:
        return MapBaseSource::Satellite;
    case 0:
    default:
        return MapBaseSource::Osm;
    }
}

const char* map_base_source_key(MapBaseSource source) noexcept
{
    switch (source)
    {
    case MapBaseSource::Terrain:
        return "terrain";
    case MapBaseSource::Satellite:
        return "satellite";
    case MapBaseSource::Osm:
    default:
        return "osm";
    }
}

const char* map_base_source_label(MapBaseSource source) noexcept
{
    switch (source)
    {
    case MapBaseSource::Terrain:
        return "Terrain";
    case MapBaseSource::Satellite:
        return "Satellite";
    case MapBaseSource::Osm:
    default:
        return "OSM";
    }
}

const char* map_base_source_extension(MapBaseSource source) noexcept
{
    (void)source;
    return "png";
}

const char* map_contour_kind_key(MapContourKind kind) noexcept
{
    switch (kind)
    {
    case MapContourKind::Minor:
        return "minor";
    case MapContourKind::Major:
    default:
        return "major";
    }
}

std::string map_contour_profile_key(const MapContourProfile& profile)
{
    return std::string(map_contour_kind_key(profile.kind)) + "-" +
           std::to_string(std::max(1, profile.interval_m));
}

MapTileCache::MapTileCache() : root_(default_root())
{
}

MapTileCache::MapTileCache(std::filesystem::path root) : root_(std::move(root))
{
}

const std::filesystem::path& MapTileCache::root() const noexcept
{
    return root_;
}

std::filesystem::path MapTileCache::relative_tile_path(
    const MapTileId& tile) const
{
    MapTileId normalized = tile;
    normalize_map_tile(normalized);
    return std::filesystem::path("maps") / "base" /
           map_base_source_key(normalized.source) /
           std::to_string(normalized.z) / std::to_string(normalized.x) /
           (std::to_string(normalized.y) + "." +
            map_base_source_extension(normalized.source));
}

std::filesystem::path MapTileCache::tile_path(const MapTileId& tile) const
{
    MapTileId normalized = tile;
    normalize_map_tile(normalized);
    return root_ / map_base_source_key(normalized.source) /
           std::to_string(normalized.z) / std::to_string(normalized.x) /
           (std::to_string(normalized.y) + "." +
            map_base_source_extension(normalized.source));
}

bool MapTileCache::tile_available(const MapTileId& tile) const
{
    return std::filesystem::exists(tile_path(tile));
}

MapTileCacheStats MapTileCache::stats() const
{
    MapTileCacheStats out{};
    out.root = root_;
    out.database = sqlite_database_path();

    DatabaseHandle handle;
    if (handle.db == nullptr)
    {
        return out;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT "
        "SUM(CASE WHEN status='cached' THEN 1 ELSE 0 END),"
        "SUM(CASE WHEN status='failed' THEN 1 ELSE 0 END),"
        "COALESCE(SUM(CASE WHEN status='cached' THEN bytes ELSE 0 END), 0) "
        "FROM map_tile_cache;";
    if (sqlite3_prepare_v2(handle.db, kSql, -1, &stmt, nullptr) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
    {
        out.cached_tiles = static_cast<std::uint64_t>(
            std::max<sqlite3_int64>(0, sqlite3_column_int64(stmt, 0)));
        out.failed_tiles = static_cast<std::uint64_t>(
            std::max<sqlite3_int64>(0, sqlite3_column_int64(stmt, 1)));
        out.total_bytes = static_cast<std::uint64_t>(
            std::max<sqlite3_int64>(0, sqlite3_column_int64(stmt, 2)));
    }
    sqlite3_finalize(stmt);
    return out;
}

MapTileResult MapTileCache::ensure_tile(const MapTileId& requested) const
{
    MapTileId tile = requested;
    normalize_map_tile(tile);
    const std::filesystem::path relative_path = relative_tile_path(tile);
    const std::filesystem::path path = tile_path(tile);

    if (!valid_tile(tile))
    {
        return fail_result(tile, relative_path, "Invalid tile coordinate.");
    }

    if (std::filesystem::exists(path) && file_size_or_zero(path) > 0U)
    {
        return record_tile_status(tile,
                                  relative_path,
                                  MapTileStatus::Cached,
                                  0,
                                  file_size_or_zero(path),
                                  "");
    }

    if (!ensure_directory(path.parent_path()))
    {
        return fail_result(tile, relative_path, "Cannot create tile directory.");
    }

    const std::string url =
        build_url_from_template(url_template_for_source(tile.source), tile);
    const std::filesystem::path temp_path = path.string() + ".tmp";

    DownloadContext ctx{};
    ctx.stream.open(temp_path, std::ios::binary | std::ios::trunc);
    if (!ctx.stream.is_open())
    {
        return fail_result(tile, relative_path, "Cannot create temp tile file.");
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr)
    {
        ctx.stream.close();
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        append_map_diagnostic("tile",
                              std::string("libcurl init failed for ") + url);
        return fail_result(tile, relative_path, "Cannot initialize libcurl.");
    }

    char error_buffer[CURL_ERROR_SIZE] = {};
    apply_map_curl_resolver(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, kUserAgent);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, kTransferTimeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);

    const CURLcode rc = curl_easy_perform(curl);
    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    curl_easy_cleanup(curl);
    ctx.stream.close();

    if (rc != CURLE_OK)
    {
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        std::ostringstream message;
        message << "Download failed: "
                << curl_error_message(rc, error_buffer) << " / url " << url;
        if (http_status != 0)
        {
            message << " / HTTP " << http_status;
        }
        const std::string doh = map_curl_doh_url();
        if (!doh.empty())
        {
            message << " / DoH " << doh;
        }
        append_map_diagnostic(
            "tile",
            std::string(map_base_source_key(tile.source)) + " z" +
                std::to_string(tile.z) + "/" + std::to_string(tile.x) +
                "/" + std::to_string(tile.y) + " failed: " +
                message.str());
        return fail_result(tile, relative_path, message.str(), http_status);
    }

    const std::uintmax_t bytes = file_size_or_zero(temp_path);
    if (bytes == 0U)
    {
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        append_map_diagnostic(
            "tile",
            std::string(map_base_source_key(tile.source)) + " z" +
                std::to_string(tile.z) + "/" + std::to_string(tile.x) +
                "/" + std::to_string(tile.y) + " empty response from " + url);
        return fail_result(tile, relative_path, "Downloaded tile is empty.",
                           http_status);
    }

    if (!file_has_png_signature(temp_path))
    {
        std::error_code remove_ec;
        std::filesystem::remove(temp_path, remove_ec);
        append_map_diagnostic(
            "tile",
            std::string(map_base_source_key(tile.source)) + " z" +
                std::to_string(tile.z) + "/" + std::to_string(tile.x) +
                "/" + std::to_string(tile.y) + " rejected non-PNG response from " +
                url);
        return fail_result(tile,
                           relative_path,
                           "Downloaded tile is not PNG.",
                           http_status);
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(temp_path, path, ec);
    if (ec)
    {
        std::filesystem::remove(temp_path, ec);
        append_map_diagnostic(
            "tile",
            std::string(map_base_source_key(tile.source)) + " z" +
                std::to_string(tile.z) + "/" + std::to_string(tile.x) +
                "/" + std::to_string(tile.y) + " commit failed for " +
                path.string());
        return fail_result(tile, relative_path, "Cannot commit tile file.",
                           http_status);
    }

    append_map_diagnostic(
        "tile",
        std::string(map_base_source_key(tile.source)) + " z" +
            std::to_string(tile.z) + "/" + std::to_string(tile.x) + "/" +
            std::to_string(tile.y) + " cached HTTP " +
            std::to_string(http_status) + " " + std::to_string(bytes) +
            " bytes from " + url);
    return record_tile_status(tile,
                              relative_path,
                              MapTileStatus::Downloaded,
                              http_status,
                              bytes,
                              "");
}

MapContourTileStore::MapContourTileStore() : root_(default_contour_root())
{
}

MapContourTileStore::MapContourTileStore(std::filesystem::path root)
    : root_(std::move(root))
{
}

const std::filesystem::path& MapContourTileStore::root() const noexcept
{
    return root_;
}

std::filesystem::path MapContourTileStore::relative_tile_path(
    const MapContourTileId& tile) const
{
    MapContourTileId normalized = tile;
    normalize_map_contour_tile(normalized);
    return std::filesystem::path("maps") / "contour" /
           map_contour_profile_key(normalized.profile) /
           std::to_string(normalized.z) / std::to_string(normalized.x) /
           (std::to_string(normalized.y) + ".png");
}

std::filesystem::path MapContourTileStore::tile_path(
    const MapContourTileId& tile) const
{
    MapContourTileId normalized = tile;
    normalize_map_contour_tile(normalized);
    return root_ / map_contour_profile_key(normalized.profile) /
           std::to_string(normalized.z) / std::to_string(normalized.x) /
           (std::to_string(normalized.y) + ".png");
}

std::filesystem::path MapContourTileStore::existing_tile_path(
    const MapContourTileId& tile) const
{
    const auto primary = tile_path(tile);
    if (std::filesystem::exists(primary))
    {
        return primary;
    }

    MapContourTileId normalized = tile;
    normalize_map_contour_tile(normalized);
    const auto center_layout =
        legacy_center_contour_root() /
        map_contour_profile_key(normalized.profile) /
        std::to_string(normalized.z) / std::to_string(normalized.x) /
        (std::to_string(normalized.y) + ".png");
    return std::filesystem::exists(center_layout) ? center_layout : primary;
}

bool MapContourTileStore::tile_available(
    const MapContourTileId& tile) const
{
    return std::filesystem::exists(existing_tile_path(tile));
}

void normalize_map_tile(MapTileId& tile) noexcept
{
    tile.source = sanitize_map_base_source(static_cast<std::uint8_t>(tile.source));
    tile.z = std::clamp(tile.z, 0, kMaxZoom);
    ::ui::map_tiles::normalizeTile(tile.z, tile.x, tile.y);
}

void normalize_map_contour_tile(MapContourTileId& tile) noexcept
{
    tile.profile.interval_m = std::clamp(tile.profile.interval_m, 1, 10000);
    tile.z = std::clamp(tile.z, 0, kMaxZoom);
    const int tiles = 1 << tile.z;
    if (tiles <= 0)
    {
        tile.x = 0;
        tile.y = 0;
        return;
    }

    tile.x %= tiles;
    if (tile.x < 0)
    {
        tile.x += tiles;
    }
    tile.y = std::clamp(tile.y, 0, tiles - 1);
}

std::vector<MapTileId> map_tiles_around(double lat,
                                        double lon,
                                        int zoom,
                                        MapBaseSource source,
                                        int radius_x,
                                        int radius_y)
{
    zoom = std::clamp(zoom, 0, kMaxZoom);
    const auto coordinates =
        ::ui::map_tiles::tilesAround(lat, lon, zoom, radius_x, radius_y);
    std::vector<MapTileId> out;
    out.reserve(coordinates.size());
    for (const auto& coordinate : coordinates)
    {
        MapTileId tile{source, zoom, coordinate.x, coordinate.y};
        normalize_map_tile(tile);
        out.push_back(tile);
    }
    return out;
}

std::vector<MapContourProfile> contour_profiles_for_zoom(
    int zoom,
    bool allow_ultra_fine)
{
    zoom = std::clamp(zoom, 0, kMaxZoom);
    std::vector<MapContourProfile> out;

    if (zoom <= 7)
    {
        return out;
    }
    if (zoom == 8)
    {
        out.push_back(major_contour(500));
        return out;
    }
    if (zoom == 9)
    {
        out.push_back(major_contour(200));
        return out;
    }
    if (zoom == 10)
    {
        out.push_back(major_contour(500));
        out.push_back(minor_contour(100));
        return out;
    }
    if (zoom == 11)
    {
        out.push_back(major_contour(200));
        out.push_back(minor_contour(50));
        return out;
    }
    if (zoom == 12)
    {
        out.push_back(major_contour(100));
        out.push_back(minor_contour(50));
        return out;
    }
    if (zoom == 13 || zoom == 14)
    {
        out.push_back(major_contour(100));
        out.push_back(minor_contour(20));
        return out;
    }
    if (zoom == 15 || zoom == 16)
    {
        out.push_back(major_contour(50));
        out.push_back(minor_contour(10));
        return out;
    }

    out.push_back(major_contour(25));
    if (allow_ultra_fine)
    {
        out.push_back(minor_contour(5));
    }
    return out;
}

} // namespace platform::linux_runtime
