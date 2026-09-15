#include "platform/esp/arduino_common/map_poi/cjson_poi_parser.h"
#include "cJSON.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>

namespace platform::esp::arduino_common::map_poi
{
namespace
{
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;

Json parse(const char* text, std::size_t size)
{
    // Bound recursion before invoking cJSON on an ESP task stack.
    bool quoted = false, escaped = false;
    int depth = 0;
    for (std::size_t i = 0; i < size; ++i)
    {
        const char c = text[i];
        if (quoted)
        {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        }
        else if (c == '"') quoted = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > 4) return {nullptr, cJSON_Delete};
        }
        else if (c == '}' || c == ']')
        {
            if (--depth < 0) return {nullptr, cJSON_Delete};
        }
    }
    if (quoted || depth != 0) return {nullptr, cJSON_Delete};
    const char* end = nullptr;
    Json root(cJSON_ParseWithLengthOpts(text, size, &end, false), cJSON_Delete);
    if (!root || !end) return {nullptr, cJSON_Delete};
    while (end < text + size && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (end != text + size || !cJSON_IsObject(root.get())) return {nullptr, cJSON_Delete};
    return root;
}

const char* string(cJSON* object, const char* key, const char* alternate = nullptr)
{
    auto* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (!item && alternate) item = cJSON_GetObjectItemCaseSensitive(object, alternate);
    return cJSON_IsString(item) ? item->valuestring : nullptr;
}

bool integer(cJSON* item, int& value)
{
    if (!cJSON_IsNumber(item) || !std::isfinite(item->valuedouble) || item->valuedouble < -100000 || item->valuedouble > 100000 ||
        std::floor(item->valuedouble) != item->valuedouble) return false;
    value = static_cast<int>(item->valuedouble);
    return true;
}

void copy_utf8(char* destination, std::size_t capacity, const char* source)
{
    if (!source)
    {
        destination[0] = '\0';
        return;
    }
    std::size_t size = std::min(capacity - 1, std::strlen(source));
    while (size > 0 && (static_cast<unsigned char>(source[size]) & 0xC0U) == 0x80U) --size;
    std::memcpy(destination, source, size);
    destination[size] = '\0';
}

uint64_t legacy_key(const char* value)
{
    uint64_t key = UINT64_C(14695981039346656037);
    while (*value)
    {
        key ^= static_cast<uint8_t>(*value++);
        key *= UINT64_C(1099511628211);
    }
    return key;
}

bool hex_key(const char* text, uint64_t& key)
{
    if (!text || std::strlen(text) != 16) return false;
    key = 0;
    for (std::size_t i = 0; i < 16; ++i)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return false;
        key = (key << 4) | digit;
    }
    return true;
}
} // namespace

bool CJsonPoiParser::manifest(const char* json, std::size_t size, ::ui::map_poi::Policy& out) const
{
    out = {};
    if (!json || size == 0 || size > 16384) return false;
    const auto root = parse(json, size);
    if (!root) return false;
    int version = 0;
    if (!integer(cJSON_GetObjectItemCaseSensitive(root.get(), "version"), version) || (version != 1 && version != 2 && version != 3)) return false;
    out.schema_version = static_cast<uint8_t>(version);
    auto* index = cJSON_GetObjectItemCaseSensitive(root.get(), "index");
    if (!cJSON_IsObject(index)) return false;
    const char* scheme = string(index, "scheme");
    const char* format = string(index, "format");
    if (!scheme || std::strcmp(scheme, "web-mercator-xyz") != 0 || !format || std::strcmp(format, "jsonl") != 0) return false;
    if (version == 3)
    {
        const char* geometry = string(index, "geometry");
        int points = 0, margin = 0;
        if (!geometry || std::strcmp(geometry, "tile-local-pixels") != 0 ||
            !integer(cJSON_GetObjectItemCaseSensitive(index, "max_path_points"), points) || points != 8 ||
            !integer(cJSON_GetObjectItemCaseSensitive(index, "tile_margin"), margin) || margin != 32) return false;
    }
    auto* levels = cJSON_GetObjectItemCaseSensitive(index, "enabled_zoom_levels");
    if (levels)
    {
        if (!cJSON_IsArray(levels) || cJSON_GetArraySize(levels) > 19) return false;
        for (auto* item = levels->child; item; item = item->next)
        {
            int zoom = 0;
            if (!integer(item, zoom) || zoom < 0 || zoom > 18) return false;
            out.enabled_levels |= 1UL << zoom;
        }
    }
    else
    {
        if (version != 1) return false;
        int min = 0, max = 0;
        if (!integer(cJSON_GetObjectItemCaseSensitive(index, "min_zoom"), min) ||
            !integer(cJSON_GetObjectItemCaseSensitive(index, "max_zoom"), max) || min < 0 || max < min || max > 24) return false;
        for (int zoom = min; zoom <= std::min(max, 18); ++zoom) out.enabled_levels |= 1UL << zoom;
    }
    auto* labels = cJSON_GetObjectItemCaseSensitive(index, "include_labels");
    out.labels = !cJSON_IsBool(labels) || cJSON_IsTrue(labels);
    return true;
}

bool CJsonPoiParser::record(const char* json, std::size_t size, ::ui::map_poi::Record& out) const
{
    out = {};
    if (!json || size == 0 || size > 2048) return false;
    const auto root = parse(json, size);
    if (!root) return false;
    const char* id = string(root.get(), "id");
    const char* category = string(root.get(), "type", "t");
    if (!id || !*id || std::strlen(id) >= sizeof(out.id) || !category || !*category || std::strlen(category) >= sizeof(out.category)) return false;
    const char* kind = string(root.get(), "kind");
    out.explicit_kind = kind != nullptr;
    if (kind)
    {
        if (std::strcmp(kind, "road") == 0) out.kind = ui::map::AnnotationKind::Road;
        else if (std::strcmp(kind, "place") == 0) out.kind = ui::map::AnnotationKind::Place;
        else if (std::strcmp(kind, "poi") != 0) return false;
        if (!hex_key(id, out.key) || !hex_key(string(root.get(), "feature_id"), out.feature_key)) return false;
    }
    else out.key = out.feature_key = legacy_key(id);
    auto* path = cJSON_GetObjectItemCaseSensitive(root.get(), "path");
    if (out.kind == ui::map::AnnotationKind::Road)
    {
        const int count = cJSON_IsArray(path) ? cJSON_GetArraySize(path) : 0;
        if (count < 4 || count > 16 || (count % 2) != 0) return false;
        std::size_t i = 0;
        for (auto* item = path->child; item; item = item->next)
        {
            int value = 0;
            if (!integer(item, value) || value < -32 || value > 288) return false;
            out.path[i++] = static_cast<int16_t>(value);
        }
        out.path_points = static_cast<uint8_t>(count / 2);
    }
    else if (path && !cJSON_IsNull(path)) return false;
    auto* lat = cJSON_GetObjectItemCaseSensitive(root.get(), "lat");
    auto* lon = cJSON_GetObjectItemCaseSensitive(root.get(), "lon");
    if (!cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || !std::isfinite(lat->valuedouble) || !std::isfinite(lon->valuedouble) ||
        lat->valuedouble < -85.05112878 || lat->valuedouble > 85.05112878 || lon->valuedouble < -180 || lon->valuedouble > 180) return false;
    out.lat = lat->valuedouble;
    out.lon = lon->valuedouble;
    auto* priority = cJSON_GetObjectItemCaseSensitive(root.get(), "priority");
    if (!priority) priority = cJSON_GetObjectItemCaseSensitive(root.get(), "p");
    int rank = 0;
    if (priority && !integer(priority, rank)) return false;
    out.priority = static_cast<uint8_t>(std::clamp(rank, 0, 255));
    std::strcpy(out.id, id);
    std::strcpy(out.category, category);
    copy_utf8(out.name, sizeof(out.name), string(root.get(), "name", "n"));
    if (out.kind != ui::map::AnnotationKind::Poi && out.name[0] == '\0') return false;
    return true;
}
} // namespace platform::esp::arduino_common::map_poi
