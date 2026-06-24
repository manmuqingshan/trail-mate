#pragma once

#include "platform/ui/team_ui_types.h"
#include "team/domain/team_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

struct TeamUiSnapshot;

enum class TeamRelativeTimeKind
{
    Unknown,
    SecondsAgo,
    MinutesAgo,
    HoursAgo,
    DaysAgo
};

struct TeamRelativeTimeView
{
    TeamRelativeTimeKind kind = TeamRelativeTimeKind::Unknown;
    uint32_t value = 0;
};

struct TeamPageReadModelInput
{
    bool in_team = false;
    bool pending_join = false;
    bool kicked_out = false;
    bool self_is_leader = false;
    bool waiting_new_keys = false;

    TeamId team_id{};
    bool has_team_id = false;
    std::string team_name;
    uint32_t security_round = 0;
    uint32_t last_update_s = 0;
    bool has_team_psk = false;

    TeamPairingRole pairing_role = TeamPairingRole::None;
    TeamPairingState pairing_state = TeamPairingState::Idle;
    std::string pairing_team_name;

    std::vector<TeamMemberUi> members;
    int selected_member_index = -1;
};

struct TeamMemberRowView
{
    std::size_t source_index = 0;
    uint32_t node_id = 0;
    std::string name;
    TeamRelativeTimeView last_seen;
    bool self = false;
    bool leader = false;
    uint8_t color_index = kTeamColorUnassigned;
};

struct TeamPageSummaryView
{
    bool in_team = false;
    bool pending_join = false;
    bool kicked_out = false;
    bool self_is_leader = false;
    bool waiting_new_keys = false;

    std::string team_name;
    std::size_t member_count = 0;
    uint32_t security_round = 0;
    bool has_security_round = false;
    TeamRelativeTimeView last_update;
};

struct TeamMemberDetailView
{
    bool valid = false;
    TeamMemberRowView member;
    TeamRelativeTimeView last_seen;
    bool management_actions_enabled = false;
};

struct TeamJoinPendingView
{
    const char* title = "Pairing";
    bool show_leader_members = false;
    std::string target_team_name;
    const char* state_line = "Waiting for handshake...";
};

class TeamPageReadModel
{
  public:
    explicit TeamPageReadModel(uint32_t now_s);

    static TeamPageReadModelInput inputFromSnapshot(const TeamUiSnapshot& snapshot);
    static std::string formatTeamNameFromId(const TeamId& id);

    TeamPageSummaryView buildSummary(const TeamPageReadModelInput& input) const;
    TeamJoinPendingView buildJoinPending(const TeamPageReadModelInput& input) const;
    std::vector<TeamMemberRowView> buildMemberRows(
        const std::vector<TeamMemberUi>& members) const;
    TeamMemberDetailView buildSelectedMember(
        const TeamPageReadModelInput& input) const;
    TeamRelativeTimeView buildLastSeen(uint32_t last_seen_s) const;
    TeamRelativeTimeView buildLastUpdate(uint32_t last_update_s) const;

  private:
    TeamMemberRowView buildMemberRow(const TeamMemberUi& member,
                                     std::size_t source_index) const;

    uint32_t now_s_ = 0;
};

} // namespace ui
} // namespace team
