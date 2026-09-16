#pragma once

#include "ui_map_runtime/map_poi/annotation_layout.h"
#include "ui_presentation/map/map_poi_snapshot.h"

namespace ui::map_poi
{
// Ownership and publication of a prepared frame. The platform supplies a strict
// PSRAM allocator; host tests inject allocation failures at this same boundary.
class AnnotationFrame
{
  public:
    using Allocate = void* (*)(std::size_t, void*);
    using Free = void (*)(void*, void*);
    AnnotationFrame(Allocate allocate, Free release, void* context = nullptr);
    ~AnnotationFrame();
    AnnotationFrame(const AnnotationFrame&) = delete;
    AnnotationFrame& operator=(const AnnotationFrame&) = delete;
    bool begin(std::size_t needed, const ui::map::MapPoiSnapshot& metadata);
    void add(const AnnotationCandidate& candidate);
    bool finish(const AnnotationLayoutOptions& options, AnnotationMeasure measure, void* context);
    void clear();
    const ui::map::MapPoiSnapshot& snapshot() const { return snapshot_; }
    const AnnotationLayoutResult& result() const { return result_; }
    AnnotationCandidate* candidates() { return candidates_; }
    std::size_t candidate_count() const { return count_; }
    bool same_view(const ui::map::MapPoiSnapshot& metadata) const;

  private:
    bool reserve(std::size_t candidates);
    bool better(const AnnotationCandidate& a, const AnnotationCandidate& b) const;
    int cell(const AnnotationCandidate& c) const;
    Allocate allocate_;
    Free release_;
    void* context_;
    AnnotationCandidate* candidates_ = nullptr;
    AnnotationPlacement* placements_ = nullptr;
    AnnotationPlacement* previous_ = nullptr;
    ui::map::MapPoiItem* front_ = nullptr;
    ui::map::MapPoiItem* back_ = nullptr;
    std::size_t capacity_ = 0, count_ = 0, previous_count_ = 0;
    ui::map::MapPoiSnapshot snapshot_{};
    ui::map::MapPoiSnapshot next_{};
    AnnotationLayoutResult result_{};
};
} // namespace ui::map_poi
