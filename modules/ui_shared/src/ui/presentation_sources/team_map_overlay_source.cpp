#include "ui/presentation_sources/team_map_overlay_source.h"

#include "sys/clock.h"
#include "ui/team_presentation/team_member_label.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ui::presentation_sources
{
namespace
{

constexpr uint32_t kPositionCacheTtlMs = 1000;

bool sampleHasCoordinate(const ::team::ui::TeamPosSample& sample)
{
    return sample.lat_e7 != 0 || sample.lon_e7 != 0;
}

} // namespace

TeamMapOverlaySource::TeamMapOverlaySource(
    ::team::ui::ITeamUiSnapshotStore& snapshot_store)
    : snapshot_store_(snapshot_store)
{
}

bool TeamMapOverlaySource::loadSnapshot(
    ::team::ui::TeamUiSnapshot& out) const
{
    return snapshot_store_.load(out) && out.in_team && out.has_team_id;
}

bool TeamMapOverlaySource::loadCachedPositions(
    ::team::ui::TeamUiSnapshot& snapshot,
    const std::vector<::team::ui::TeamPosSample>*& samples) const
{
    samples = nullptr;
    if (!loadSnapshot(snapshot))
    {
        cached_positions_valid_ = false;
        return false;
    }

    const uint32_t now_ms = sys::millis_now();
    if (cached_positions_valid_ &&
        cached_team_id_ == snapshot.team_id &&
        static_cast<uint32_t>(now_ms - cached_positions_ms_) < kPositionCacheTtlMs)
    {
        snapshot = cached_snapshot_;
        samples = &cached_samples_;
        return true;
    }

    cached_samples_.clear();
    if (!::team::ui::team_ui_posring_load_latest(snapshot.team_id, cached_samples_))
    {
        cached_snapshot_ = snapshot;
        cached_team_id_ = snapshot.team_id;
        cached_positions_ms_ = now_ms;
        cached_positions_valid_ = true;
        samples = &cached_samples_;
        return true;
    }

    cached_snapshot_ = snapshot;
    cached_team_id_ = snapshot.team_id;
    cached_positions_ms_ = now_ms;
    cached_positions_valid_ = true;
    samples = &cached_samples_;
    return true;
}

std::size_t TeamMapOverlaySource::latestTeamPoints(
    TeamPoint* out,
    std::size_t capacity) const
{
    if (out == nullptr || capacity == 0)
    {
        return 0;
    }

    ::team::ui::TeamUiSnapshot snapshot;
    const std::vector<::team::ui::TeamPosSample>* samples = nullptr;
    if (!loadCachedPositions(snapshot, samples) || samples == nullptr)
    {
        return 0;
    }

    std::size_t written = 0;
    for (const auto& sample : *samples)
    {
        if (written >= capacity)
        {
            break;
        }

        out[written].node_id = sample.member_id;
        out[written].label = labelForMember(snapshot, sample.member_id, written);
        out[written].lat = static_cast<double>(sample.lat_e7) / 10000000.0;
        out[written].lon = static_cast<double>(sample.lon_e7) / 10000000.0;
        out[written].valid = sampleHasCoordinate(sample);
        ++written;
    }
    return samples->size();
}

std::size_t TeamMapOverlaySource::latestTeamLocations(
    TeamMapLocation* out,
    std::size_t capacity) const
{
    if (out == nullptr || capacity == 0)
    {
        return 0;
    }

    ::team::ui::TeamUiSnapshot snapshot;
    const std::vector<::team::ui::TeamPosSample>* samples = nullptr;
    if (!loadCachedPositions(snapshot, samples) || samples == nullptr)
    {
        return 0;
    }

    std::size_t count = 0;
    for (const auto& sample : *samples)
    {
        if (count >= capacity)
        {
            break;
        }
        out[count] = locationFromSample(snapshot, sample);
        ++count;
    }
    return count;
}

bool TeamMapOverlaySource::loadMemberLocation(
    uint32_t member_id,
    TeamMapLocation& out) const
{
    out = TeamMapLocation{};
    if (member_id == 0)
    {
        return false;
    }

    ::team::ui::TeamUiSnapshot snapshot;
    const std::vector<::team::ui::TeamPosSample>* samples = nullptr;
    if (!loadCachedPositions(snapshot, samples) || samples == nullptr)
    {
        return false;
    }

    for (const auto& sample : *samples)
    {
        if (sample.member_id != member_id)
        {
            continue;
        }

        out = locationFromSample(snapshot, sample);
        return out.valid;
    }

    return false;
}

TeamMapLocation TeamMapOverlaySource::locationFromSample(
    const ::team::ui::TeamUiSnapshot& snapshot,
    const ::team::ui::TeamPosSample& sample)
{
    TeamMapLocation out;
    out.member_id = sample.member_id;
    out.lat_e7 = sample.lat_e7;
    out.lon_e7 = sample.lon_e7;
    out.alt_m = sample.alt_m;
    out.ts = sample.ts;
    out.color = colorForMember(snapshot, sample.member_id);
    out.valid = sampleHasCoordinate(sample);
    return out;
}

uint32_t TeamMapOverlaySource::colorForMember(
    const ::team::ui::TeamUiSnapshot& snapshot,
    uint32_t member_id)
{
    for (const auto& member : snapshot.members)
    {
        if (member.node_id == member_id)
        {
            if (member.color_index < ::team::ui::kTeamMaxMembers)
            {
                return ::team::ui::team_color_from_index(member.color_index);
            }
            break;
        }
    }

    return ::team::ui::team_color_from_index(
        ::team::ui::team_color_index_from_node_id(member_id));
}

const char* TeamMapOverlaySource::labelForMember(
    const ::team::ui::TeamUiSnapshot& snapshot,
    uint32_t member_id,
    std::size_t index)
{
    static char labels[::ui::map::MapOverlaySnapshot::kMaxItems][32]{};
    if (index >= ::ui::map::MapOverlaySnapshot::kMaxItems)
    {
        return nullptr;
    }

    for (const auto& member : snapshot.members)
    {
        if (member.node_id == member_id)
        {
            const std::string label =
                ::ui::team_presentation::shortTeamMemberLabel(member_id);
            std::snprintf(labels[index],
                          sizeof(labels[index]),
                          "%s",
                          label.c_str());
            return labels[index];
        }
    }
    return nullptr;
}

} // namespace ui::presentation_sources
