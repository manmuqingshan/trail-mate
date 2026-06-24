#pragma once

#include "platform/ui/team_ui_store_runtime.h"
#include "ui_map_runtime/map_overlay_projection_adapter.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ui::presentation_sources
{

struct TeamMapLocation
{
    uint32_t member_id = 0;
    int32_t lat_e7 = 0;
    int32_t lon_e7 = 0;
    int16_t alt_m = 0;
    uint32_t ts = 0;
    uint32_t color = 0;
    bool valid = false;
};

class TeamMapOverlaySource final : public ::ui::map_overlay::IMapOverlayTeamSource
{
  public:
    explicit TeamMapOverlaySource(
        ::team::ui::ITeamUiSnapshotStore& snapshot_store);

    std::size_t latestTeamPoints(TeamPoint* out,
                                 std::size_t capacity) const override;

    std::size_t latestTeamLocations(TeamMapLocation* out,
                                    std::size_t capacity) const;

    bool loadMemberLocation(uint32_t member_id,
                            TeamMapLocation& out) const;

  private:
    ::team::ui::ITeamUiSnapshotStore& snapshot_store_;
    mutable bool cached_positions_valid_ = false;
    mutable uint32_t cached_positions_ms_ = 0;
    mutable ::team::TeamId cached_team_id_{};
    mutable ::team::ui::TeamUiSnapshot cached_snapshot_{};
    mutable std::vector<::team::ui::TeamPosSample> cached_samples_;

    bool loadSnapshot(::team::ui::TeamUiSnapshot& out) const;
    bool loadCachedPositions(::team::ui::TeamUiSnapshot& snapshot,
                             const std::vector<::team::ui::TeamPosSample>*& samples) const;
    static TeamMapLocation locationFromSample(
        const ::team::ui::TeamUiSnapshot& snapshot,
        const ::team::ui::TeamPosSample& sample);
    static uint32_t colorForMember(const ::team::ui::TeamUiSnapshot& snapshot,
                                   uint32_t member_id);
    static const char* labelForMember(
        const ::team::ui::TeamUiSnapshot& snapshot,
        uint32_t member_id,
        std::size_t index);
};

} // namespace ui::presentation_sources
