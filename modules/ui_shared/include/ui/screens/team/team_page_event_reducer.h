#pragma once

#include "platform/ui/team_ui_types.h"
#include "team/domain/team_events.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace team
{
namespace ui
{

struct TeamPageEventState
{
    bool in_team = false;
    bool pending_join = false;
    uint32_t pending_join_started_s = 0;
    bool kicked_out = false;
    bool self_is_leader = false;
    uint32_t last_event_seq = 0;
    TeamPairingRole pairing_role = TeamPairingRole::None;
    TeamPairingState pairing_state = TeamPairingState::Idle;
    uint32_t pairing_peer_id = 0;
    std::string pairing_team_name;
    TeamId team_id{};
    bool has_team_id = false;
    std::string team_name;
    uint32_t security_round = 0;
    uint32_t last_update_s = 0;
    std::array<uint8_t, team::proto::kTeamChannelPskSize> team_psk{};
    bool has_team_psk = false;
    bool waiting_new_keys = false;
    std::vector<TeamMemberUi> members;
};

struct TeamPageEventContext
{
    uint32_t now_s = 0;
    uint32_t self_node_id = 0;
};

class ITeamPageMemberNameResolver
{
  public:
    virtual ~ITeamPageMemberNameResolver() = default;
    virtual std::string resolveMemberName(uint32_t node_id) const = 0;
};

struct TeamPageEventEffects
{
    bool accepted = false;
    bool changed = false;
    bool show_key_mismatch = false;
    bool clear_keys = false;
    bool stop_pairing = false;
    bool clear_keydist_pending = false;
    bool clear_status_pending = false;
    bool save_keys = false;
    bool apply_keys = false;
    bool request_status_in_team_page = false;
    bool request_status_not_in_team_page = false;
    bool request_kicked_out_page = false;
    bool clear_nav_stack = false;
    bool epoch_rotated = false;
    uint32_t epoch_key_id = 0;
    bool keydist_confirmed = false;
    uint32_t keydist_member_id = 0;
    uint32_t keydist_key_id = 0;
    bool member_kicked_key_event = false;
    uint32_t member_kicked_id = 0;
    bool member_accepted_key_event = false;
    uint32_t member_accepted_id = 0;
    bool status_should_send = false;
    bool show_pairing_peer = false;
    uint32_t pairing_peer_id = 0;
    bool show_pairing_success = false;
    bool show_pairing_failed = false;
};

struct TeamPagePairingUpdate
{
    TeamPairingRole role = TeamPairingRole::None;
    TeamPairingState state = TeamPairingState::Idle;
    TeamId team_id{};
    bool has_team_id = false;
    uint32_t key_id = 0;
    uint32_t peer_id = 0;
    std::string team_name;
    bool has_team_name = false;
};

class TeamPageEventReducer
{
  public:
    TeamPageEventReducer(TeamPageEventContext context,
                         const ITeamPageMemberNameResolver& names);

    int findMemberIndex(const TeamPageEventState& state, uint32_t node_id) const;
    void touchMember(TeamPageEventState& state,
                     uint32_t node_id,
                     uint32_t last_seen_s) const;
    void fillStatusMembers(const TeamPageEventState& state,
                           team::proto::TeamStatus& status) const;
    void applyStatusRoster(TeamPageEventState& state,
                           const team::proto::TeamStatus& status) const;
    std::string memberDisplayLabel(uint32_t node_id) const;

    TeamPageEventEffects reduceError(TeamPageEventState& state,
                                     const team::TeamErrorEvent& event) const;
    TeamPageEventEffects reduceStatus(TeamPageEventState& state,
                                      const team::TeamStatusEvent& event) const;
    TeamPageEventEffects reduceActivity(TeamPageEventState& state,
                                        const team::TeamEventContext& event,
                                        uint32_t member_id) const;
    TeamPageEventEffects reduceKick(TeamPageEventState& state,
                                    const team::TeamKickEvent& event) const;
    TeamPageEventEffects reduceTransferLeader(
        TeamPageEventState& state,
        const team::TeamTransferLeaderEvent& event) const;
    TeamPageEventEffects reduceKeyDist(
        TeamPageEventState& state,
        const team::TeamKeyDistEvent& event) const;
    TeamPageEventEffects reducePairing(
        TeamPageEventState& state,
        const TeamPagePairingUpdate& update) const;
    TeamPageEventEffects reducePairingStatus(
        TeamPageEventState& state,
        const TeamPagePairingUpdate& update) const;

    static std::string formatTeamNameFromId(const TeamId& id);

  private:
    bool acceptsTeam(const TeamPageEventState& state,
                     const TeamEventContext& event) const;
    void ensureTeamIdentity(TeamPageEventState& state,
                            const TeamEventContext& event) const;
    void ensureSelfMember(TeamPageEventState& state,
                          bool leader) const;
    void resetTeamMembership(TeamPageEventState& state,
                             bool kicked_out) const;
    void applyPairingIdentity(TeamPageEventState& state,
                              const TeamPagePairingUpdate& update) const;
    bool isPairingActive(TeamPairingState state) const;
    void assignMemberColor(TeamMemberUi& member) const;

    TeamPageEventContext context_;
    const ITeamPageMemberNameResolver* names_ = nullptr;
};

} // namespace ui
} // namespace team
