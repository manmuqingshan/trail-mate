#include "ui/screens/team/team_page_event_effect_sink.h"

#include "chat/domain/chat_types.h"

namespace team
{
namespace ui
{
namespace
{

constexpr uint8_t kKeyRoleMember = 1;

} // namespace

TeamPageEventEffectResult TeamPageEventEffectSink::applyEffects(
    const TeamPageEventState& state,
    TeamPageKeyEventState& key_event_state,
    const TeamPageEventEffects& effects,
    const TeamPageEventReducer& reducer,
    const TeamPageRuntimePort& runtime,
    const TeamPageKeyEventLog& key_log,
    ITeamPageEventDeferred& deferred,
    ITeamPageEventNotifier& notifier) const
{
    TeamPageEventEffectResult result;
    result.request_status_in_team_page =
        effects.request_status_in_team_page;
    result.request_status_not_in_team_page =
        effects.request_status_not_in_team_page;
    result.request_kicked_out_page = effects.request_kicked_out_page;
    result.clear_nav_stack = effects.clear_nav_stack;

    if (effects.keydist_confirmed)
    {
        deferred.confirmKeyDist(effects.keydist_member_id,
                                effects.keydist_key_id);
        result.confirmed_keydist = true;
    }

    if (effects.epoch_rotated)
    {
        result.appended_epoch_rotated =
            key_log.appendEpochRotated(key_event_state,
                                       effects.epoch_key_id);
    }

    if (effects.member_accepted_key_event)
    {
        result.appended_member_accepted =
            key_log.appendMemberAccepted(key_event_state,
                                         effects.member_accepted_id,
                                         kKeyRoleMember);
    }

    if (effects.save_keys)
    {
        result.saved_keys = runtime.saveKeysNow(state.team_id,
                                                state.security_round,
                                                state.team_psk);
    }

    if (effects.apply_keys && state.has_team_psk && state.has_team_id)
    {
        result.applied_keys =
            runtime.setKeysFromPsk(state.team_id,
                                   state.security_round,
                                   state.team_psk.data(),
                                   state.team_psk.size());
    }

    if (effects.status_should_send && runtime.hasController() &&
        state.has_team_id)
    {
        team::proto::TeamStatus status{};
        status.key_id = state.security_round;
        reducer.fillStatusMembers(state, status);

        result.sent_status =
            runtime.sendStatus(status, chat::ChannelId::PRIMARY, 0);
        if (!result.sent_status)
        {
            notifier.notifySendFailed("Status", true);
        }

        result.sent_status_plain =
            runtime.sendStatusPlain(status, chat::ChannelId::PRIMARY, 0);
        if (!result.sent_status_plain)
        {
            notifier.notifySendFailed("Status", false);
        }

        deferred.scheduleStatusBroadcast(1, 2);
        result.scheduled_status_broadcast = true;
    }

    if (effects.show_pairing_peer)
    {
        const std::string name =
            reducer.memberDisplayLabel(effects.pairing_peer_id);
        const std::string message = "Paired: " + name;
        notifier.showMessage(message.c_str());
    }
    if (effects.show_pairing_success)
    {
        notifier.showMessage("Paired successfully");
    }
    if (effects.show_pairing_failed)
    {
        notifier.showMessage("Pairing failed");
    }

    return result;
}

} // namespace ui
} // namespace team
