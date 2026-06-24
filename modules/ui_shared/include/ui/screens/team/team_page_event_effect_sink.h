#pragma once

#include "ui/screens/team/team_page_event_reducer.h"
#include "ui/screens/team/team_page_key_event_log.h"
#include "ui/screens/team/team_page_runtime_port.h"

#include <cstdint>

namespace team
{
namespace ui
{

class ITeamPageEventDeferred
{
  public:
    virtual ~ITeamPageEventDeferred() = default;
    virtual void confirmKeyDist(uint32_t node_id, uint32_t key_id) = 0;
    virtual void scheduleStatusBroadcast(uint8_t repeats,
                                         uint32_t delay_s) = 0;
};

class ITeamPageEventNotifier
{
  public:
    virtual ~ITeamPageEventNotifier() = default;
    virtual void showMessage(const char* message) = 0;
    virtual void notifySendFailed(const char* action, bool needs_keys) = 0;
};

struct TeamPageEventEffectResult
{
    bool confirmed_keydist = false;
    bool appended_epoch_rotated = false;
    bool appended_member_accepted = false;
    bool saved_keys = false;
    bool applied_keys = false;
    bool sent_status = false;
    bool sent_status_plain = false;
    bool scheduled_status_broadcast = false;
    bool request_status_in_team_page = false;
    bool request_status_not_in_team_page = false;
    bool request_kicked_out_page = false;
    bool clear_nav_stack = false;
};

class TeamPageEventEffectSink
{
  public:
    TeamPageEventEffectResult applyEffects(
        const TeamPageEventState& state,
        TeamPageKeyEventState& key_event_state,
        const TeamPageEventEffects& effects,
        const TeamPageEventReducer& reducer,
        const TeamPageRuntimePort& runtime,
        const TeamPageKeyEventLog& key_log,
        ITeamPageEventDeferred& deferred,
        ITeamPageEventNotifier& notifier) const;
};

} // namespace ui
} // namespace team
