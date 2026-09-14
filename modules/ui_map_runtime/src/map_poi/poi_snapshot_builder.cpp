#include "ui_map_runtime/map_poi/poi_snapshot_builder.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace ui::map_poi
{
SnapshotBuilder::SnapshotBuilder(ui::map::MapPoiSnapshot& out, int width, int height)
    : out_(out), width_(width), height_(height) { out_.item_count = 0; }

bool SnapshotBuilder::better(uint8_t priority, int x, int y, const char* id, const ui::map::MapPoiItem& other) const
{
    if (priority != other.priority) return priority > other.priority;
    const int ax = x - width_ / 2, ay = y - height_ / 2;
    const int bx = other.x - width_ / 2, by = other.y - height_ / 2;
    const int ad = ax * ax + ay * ay, bd = bx * bx + by * by;
    return ad != bd ? ad < bd : std::strcmp(id, other.id.c_str()) < 0;
}

void SnapshotBuilder::add(const Record& record, int x, int y)
{
    if (!out_.items || out_.capacity == 0) return;
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
    for (std::size_t i = 0; i < out_.item_count; ++i)
        if (std::strcmp(record.id, out_.items[i].id.c_str()) == 0) return;
    std::size_t slot = out_.item_count;
    if (slot == std::min(out_.capacity, ui::map::MapPoiSnapshot::kMaxItems))
    {
        out_.truncated = true;
        slot = 0;
        for (std::size_t i = 1; i < out_.item_count; ++i)
            if (better(out_.items[slot].priority, out_.items[slot].x, out_.items[slot].y, out_.items[slot].id.c_str(), out_.items[i])) slot = i;
        if (!better(record.priority, x, y, record.id, out_.items[slot])) return;
    }
    else ++out_.item_count;
    auto& item = out_.items[slot];
    ui::copyText(item.id, record.id);
    ui::copyText(item.label, record.name);
    ui::copyText(item.category, record.category);
    item.x = static_cast<int16_t>(x);
    item.y = static_cast<int16_t>(y);
    item.priority = record.priority;
}

void SnapshotBuilder::finish()
{
    if (!out_.items) return;
    std::sort(out_.items, out_.items + out_.item_count, [this](const auto& a, const auto& b)
              { return better(a.priority, a.x, a.y, a.id.c_str(), b); });
    std::size_t count = 0;
    for (std::size_t i = 0; i < out_.item_count; ++i)
    {
        bool overlaps = false;
        for (std::size_t j = 0; j < count; ++j)
            if (std::abs(out_.items[i].x - out_.items[j].x) < 18 && std::abs(out_.items[i].y - out_.items[j].y) < 18) overlaps = true;
        if (!overlaps) out_.items[count++] = out_.items[i];
    }
    out_.item_count = count;
}
} // namespace ui::map_poi
