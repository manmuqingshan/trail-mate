#pragma once

#include "ui_presentation/common/fixed_text.h"
#include "ui_presentation/common/snapshot_header.h"
#include "ui_presentation/map/map_annotation_kind.h"

#include <cstddef>
#include <cstdint>

namespace ui::map
{
struct MapPoiItem
{
    ui::FixedText<48> id;
    ui::FixedText<80> label;
    ui::FixedText<16> category;
    int16_t x = 0;
    int16_t y = 0;
    uint8_t priority = 0;
    AnnotationKind kind = AnnotationKind::Poi;
    int16_t text_x = 0, text_y = 0, text_width = 0, text_height = 0;
    uint16_t cluster_count = 1;
    bool marker = true;
};

// Caller-owned snapshot: never returned by value on a small ESP task stack.
struct MapPoiSnapshot
{
    static constexpr std::size_t kMaxItems = 48;
    ui::SnapshotHeader header;
    bool enabled = false;
    bool labels = true;
    bool truncated = false;
    bool loading = false;
    bool layout_ready = false;
    std::size_t candidate_count = 0;
    std::size_t item_count = 0;
    std::size_t capacity = 0;
    MapPoiItem* items = nullptr; // Caller-owned, on-demand PSRAM storage on ESP.
    uint64_t view_key = 0;
    uint64_t compatibility_key = 0;
    int16_t width = 0, height = 0;
};
static_assert(sizeof(MapPoiSnapshot) <= 80, "POI snapshots must not embed large buffers");
} // namespace ui::map
