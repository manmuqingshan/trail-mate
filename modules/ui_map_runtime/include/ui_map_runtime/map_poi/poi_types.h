#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ui::map_poi
{
struct Policy
{
    uint32_t enabled_levels = 0;
    bool labels = true;
    bool enabled(int zoom) const { return zoom >= 0 && zoom <= 18 && (enabled_levels & (1UL << zoom)) != 0; }
};

struct Record
{
    char id[48]{};
    char name[80]{};
    char category[16]{};
    double lat = 0;
    double lon = 0;
    uint8_t priority = 0;
};

// Small header followed by exactly `count` Records in an owned PSRAM buffer.
// There is deliberately no fixed array or large data object in this type.
struct alignas(8) TileHeader
{
    static constexpr uint32_t kMagic = 0x504F4932U;
    static constexpr std::size_t kMaxRecords = 200;
    uint32_t magic = kMagic;
    Policy policy{};
    uint16_t count = 0;
    uint16_t invalid_rows = 0;
    bool truncated = false;
    bool manifest_valid = false;
};

static_assert(sizeof(TileHeader) <= 32, "POI metadata must stay small");
static_assert(sizeof(Record) <= 168, "POI records must remain compact");
static_assert(std::is_trivially_copyable<Record>::value, "POI payload records have no hidden allocations");

inline const Record* payloadRecords(const uint8_t* data)
{
    return reinterpret_cast<const Record*>(data + sizeof(TileHeader));
}
inline bool validPayload(const uint8_t* data, std::size_t size)
{
    if (!data || size < sizeof(TileHeader)) return false;
    TileHeader header{};
    std::memcpy(&header, data, sizeof(header));
    return header.magic == TileHeader::kMagic && header.count <= TileHeader::kMaxRecords &&
           size == sizeof(TileHeader) + header.count * sizeof(Record);
}

class Parser
{
  public:
    virtual ~Parser() = default;
    virtual bool manifest(const char* json, std::size_t size, Policy& out) const = 0;
    virtual bool record(const char* json, std::size_t size, Record& out) const = 0;
};
} // namespace ui::map_poi
