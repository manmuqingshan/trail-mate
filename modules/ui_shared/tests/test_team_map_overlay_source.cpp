#include "ui/presentation_sources/team_map_overlay_source.h"

#include <cassert>
#include <cstring>

namespace
{

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0x54;
    id[1] = 0x4D;
    return id;
}

team::ui::TeamUiSnapshot makeSnapshot()
{
    team::ui::TeamUiSnapshot snapshot;
    snapshot.in_team = true;
    snapshot.has_team_id = true;
    snapshot.team_id = testTeamId();

    team::ui::TeamMemberUi member;
    member.node_id = 0xCAFE;
    member.name = "Ada";
    member.color_index = 2;
    snapshot.members.push_back(member);
    return snapshot;
}

void resetStores()
{
    team::ui::team_ui_snapshot_store().clear();
}

} // namespace

int main()
{
    resetStores();

    ui::presentation_sources::TeamMapOverlaySource source(
        team::ui::team_ui_snapshot_store());

    ui::map_overlay::IMapOverlayTeamSource::TeamPoint points[4]{};
    assert(source.latestTeamPoints(points, 4) == 0);

    const team::ui::TeamUiSnapshot snapshot = makeSnapshot();
    team::ui::team_ui_snapshot_store().save(snapshot);
    assert(team::ui::team_ui_posring_append(snapshot.team_id,
                                            0xCAFE,
                                            312345678,
                                            1219876543,
                                            42,
                                            0,
                                            100));
    assert(team::ui::team_ui_posring_append(snapshot.team_id,
                                            0xBEEF,
                                            0,
                                            0,
                                            0,
                                            0,
                                            101));

    const std::size_t count = source.latestTeamPoints(points, 4);
    assert(count == 2);
    assert(points[0].node_id == 0xCAFE);
    assert(std::strcmp(points[0].label, "CAFE") == 0);
    assert(points[0].valid);
    assert(points[1].node_id == 0xBEEF);
    assert(points[1].label == nullptr);
    assert(!points[1].valid);

    ui::presentation_sources::TeamMapLocation locations[4]{};
    const std::size_t location_count = source.latestTeamLocations(locations, 4);
    assert(location_count == 2);
    assert(locations[0].member_id == 0xCAFE);
    assert(locations[0].valid);
    assert(locations[1].member_id == 0xBEEF);
    assert(!locations[1].valid);

    ui::presentation_sources::TeamMapLocation location;
    assert(source.loadMemberLocation(0xCAFE, location));
    assert(location.member_id == 0xCAFE);
    assert(location.lat_e7 == 312345678);
    assert(location.lon_e7 == 1219876543);
    assert(location.alt_m == 42);
    assert(location.ts == 100);
    assert(location.color == team::ui::team_color_from_index(2));

    assert(!source.loadMemberLocation(0xBEEF, location));
    assert(!source.loadMemberLocation(0x1234, location));

    resetStores();
    return 0;
}
