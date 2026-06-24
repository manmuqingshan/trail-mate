#include "ui/screens/team/team_page_event_effect_sink.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace
{

class TestMemberNameResolver final : public team::ui::ITeamPageMemberNameResolver
{
  public:
    std::string resolveMemberName(uint32_t node_id) const override
    {
        char label[5]{};
        std::snprintf(label,
                      sizeof(label),
                      "%04lX",
                      static_cast<unsigned long>(node_id & 0xFFFFU));
        return std::string(label);
    }
};

const TestMemberNameResolver& testNames()
{
    static TestMemberNameResolver names;
    return names;
}

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0xAB;
    id[1] = 0xCD;
    return id;
}

team::ui::TeamPageEventReducer makeReducer()
{
    team::ui::TeamPageEventContext context;
    context.now_s = 1000;
    context.self_node_id = 0x11111111;
    return team::ui::TeamPageEventReducer(context, testNames());
}

team::ui::TeamPageEventState makeState()
{
    team::ui::TeamPageEventState state;
    state.in_team = true;
    state.self_is_leader = true;
    state.has_team_id = true;
    state.team_id = testTeamId();
    state.security_round = 7;
    state.has_team_psk = true;
    state.team_psk[0] = 0xA5;
    state.members.push_back({0, "You", false, true, 1000, 0});
    state.members.push_back({0x22222222, "Ada", false, false, 1000, 0});
    return state;
}

team::ui::TeamPageKeyEventState makeKeyEventState()
{
    team::ui::TeamPageKeyEventState state;
    state.has_team_id = true;
    state.team_id = testTeamId();
    state.security_round = 7;
    state.last_event_seq = 3;
    return state;
}

class FakeDeferred final : public team::ui::ITeamPageEventDeferred
{
  public:
    void confirmKeyDist(uint32_t node_id, uint32_t key_id) override
    {
        confirmed.push_back(std::make_pair(node_id, key_id));
    }

    void scheduleStatusBroadcast(uint8_t repeats,
                                 uint32_t delay_s) override
    {
        scheduled.push_back(std::make_pair(repeats, delay_s));
    }

    std::vector<std::pair<uint32_t, uint32_t>> confirmed;
    std::vector<std::pair<uint8_t, uint32_t>> scheduled;
};

class FakeNotifier final : public team::ui::ITeamPageEventNotifier
{
  public:
    void showMessage(const char* message) override
    {
        messages.push_back(message ? message : "");
    }

    void notifySendFailed(const char* action, bool needs_keys) override
    {
        failures.push_back(std::make_pair(std::string(action ? action : ""),
                                          needs_keys));
    }

    std::vector<std::string> messages;
    std::vector<std::pair<std::string, bool>> failures;
};

class FakeKeyEventWriter final : public team::ui::ITeamPageKeyEventWriter
{
  public:
    bool appendKeyEvent(const team::TeamId&,
                        team::ui::TeamKeyEventType type,
                        uint32_t event_seq,
                        uint32_t,
                        const uint8_t*,
                        size_t) override
    {
        append_count += 1;
        last_type = type;
        last_event_seq = event_seq;
        return append_ok;
    }

    bool append_ok = true;
    int append_count = 0;
    team::ui::TeamKeyEventType last_type =
        team::ui::TeamKeyEventType::TeamCreated;
    uint32_t last_event_seq = 0;
};

class FakeController final : public team::ui::ITeamPageControllerPort
{
  public:
    void clearKeys() override {}
    void resetUiState() override {}

    bool setKeysFromPsk(const team::TeamId& team_id,
                        uint32_t key_id,
                        const uint8_t* psk,
                        size_t psk_len) override
    {
        set_keys_count += 1;
        last_set_keys_team_id = team_id;
        last_set_keys_key_id = key_id;
        last_set_keys_psk0 = psk ? psk[0] : 0;
        last_set_keys_len = psk_len;
        return set_keys_ok;
    }

    bool sendKick(const team::proto::TeamKick&,
                  chat::ChannelId,
                  chat::NodeId) override
    {
        return true;
    }

    bool sendTransferLeader(const team::proto::TeamTransferLeader&,
                            chat::ChannelId,
                            chat::NodeId) override
    {
        return true;
    }

    bool sendKeyDist(const team::proto::TeamKeyDist&,
                     chat::ChannelId,
                     chat::NodeId) override
    {
        return true;
    }

    bool sendKeyDistPlain(const team::proto::TeamKeyDist&,
                          chat::ChannelId,
                          chat::NodeId) override
    {
        return true;
    }

    bool sendKeyRequest(const team::proto::TeamKeyRequest&,
                        chat::ChannelId,
                        chat::NodeId) override
    {
        return true;
    }

    bool sendStatus(const team::proto::TeamStatus& status,
                    chat::ChannelId,
                    chat::NodeId) override
    {
        status_count += 1;
        last_status = status;
        return status_ok;
    }

    bool sendStatusPlain(const team::proto::TeamStatus& status,
                         chat::ChannelId,
                         chat::NodeId) override
    {
        status_plain_count += 1;
        last_status_plain = status;
        return status_plain_ok;
    }

    team::TeamService::SendError lastSendError() const override
    {
        return team::TeamService::SendError::None;
    }

    bool set_keys_ok = true;
    bool status_ok = true;
    bool status_plain_ok = true;
    int set_keys_count = 0;
    int status_count = 0;
    int status_plain_count = 0;
    team::TeamId last_set_keys_team_id{};
    uint32_t last_set_keys_key_id = 0;
    uint8_t last_set_keys_psk0 = 0;
    size_t last_set_keys_len = 0;
    team::proto::TeamStatus last_status;
    team::proto::TeamStatus last_status_plain;
};

class FakeKeyStore final : public team::ui::ITeamPageKeyStorePort
{
  public:
    bool saveKeysNow(
        const team::TeamId& team_id,
        uint32_t key_id,
        const std::array<uint8_t, team::proto::kTeamChannelPskSize>& psk) override
    {
        save_count += 1;
        last_team_id = team_id;
        last_key_id = key_id;
        last_psk0 = psk[0];
        return save_ok;
    }

    bool save_ok = true;
    int save_count = 0;
    team::TeamId last_team_id{};
    uint32_t last_key_id = 0;
    uint8_t last_psk0 = 0;
};

bool hasMember(const team::proto::TeamStatus& status, uint32_t node_id)
{
    for (const uint32_t member : status.members)
    {
        if (member == node_id)
        {
            return true;
        }
    }
    return false;
}

void testStatusEffectsConfirmKeyDistAndAppendEpoch()
{
    const auto state = makeState();
    auto key_state = makeKeyEventState();
    team::ui::TeamPageEventEffects effects;
    effects.keydist_confirmed = true;
    effects.keydist_member_id = 0x22222222;
    effects.keydist_key_id = 7;
    effects.epoch_rotated = true;
    effects.epoch_key_id = 8;
    FakeDeferred deferred;
    FakeNotifier notifier;
    FakeKeyEventWriter writer;
    team::ui::TeamPageKeyEventLog log(writer, 900);
    team::ui::TeamPageRuntimePort runtime(nullptr, nullptr, nullptr);

    const auto result = team::ui::TeamPageEventEffectSink().applyEffects(
        state,
        key_state,
        effects,
        makeReducer(),
        runtime,
        log,
        deferred,
        notifier);

    assert(result.confirmed_keydist);
    assert(result.appended_epoch_rotated);
    assert(deferred.confirmed.size() == 1);
    assert(deferred.confirmed[0].first == 0x22222222);
    assert(deferred.confirmed[0].second == 7);
    assert(writer.append_count == 1);
    assert(writer.last_type == team::ui::TeamKeyEventType::EpochRotated);
    assert(writer.last_event_seq == 4);
    assert(key_state.last_event_seq == 4);
}

void testKeyDistEffectsSaveApplyKeysAndForwardNavigationRequests()
{
    const auto state = makeState();
    auto key_state = makeKeyEventState();
    team::ui::TeamPageEventEffects effects;
    effects.save_keys = true;
    effects.apply_keys = true;
    effects.request_status_in_team_page = true;
    effects.clear_nav_stack = true;
    FakeDeferred deferred;
    FakeNotifier notifier;
    FakeKeyEventWriter writer;
    team::ui::TeamPageKeyEventLog log(writer, 900);
    FakeController controller;
    FakeKeyStore key_store;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, &key_store);

    const auto result = team::ui::TeamPageEventEffectSink().applyEffects(
        state,
        key_state,
        effects,
        makeReducer(),
        runtime,
        log,
        deferred,
        notifier);

    assert(result.saved_keys);
    assert(result.applied_keys);
    assert(result.request_status_in_team_page);
    assert(result.clear_nav_stack);
    assert(key_store.save_count == 1);
    assert(key_store.last_team_id == testTeamId());
    assert(key_store.last_key_id == 7);
    assert(key_store.last_psk0 == 0xA5);
    assert(controller.set_keys_count == 1);
    assert(controller.last_set_keys_team_id == testTeamId());
    assert(controller.last_set_keys_key_id == 7);
    assert(controller.last_set_keys_psk0 == 0xA5);
    assert(controller.last_set_keys_len == team::proto::kTeamChannelPskSize);
}

void testPairingEffectsAppendAcceptedMemberSendStatusAndNotify()
{
    const auto state = makeState();
    auto key_state = makeKeyEventState();
    team::ui::TeamPageEventEffects effects;
    effects.member_accepted_key_event = true;
    effects.member_accepted_id = 0x22222222;
    effects.status_should_send = true;
    effects.show_pairing_peer = true;
    effects.pairing_peer_id = 0x22222222;
    effects.show_pairing_success = true;
    effects.request_status_in_team_page = true;
    effects.clear_nav_stack = true;
    FakeDeferred deferred;
    FakeNotifier notifier;
    FakeKeyEventWriter writer;
    team::ui::TeamPageKeyEventLog log(writer, 900);
    FakeController controller;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    const auto result = team::ui::TeamPageEventEffectSink().applyEffects(
        state,
        key_state,
        effects,
        makeReducer(),
        runtime,
        log,
        deferred,
        notifier);

    assert(result.appended_member_accepted);
    assert(result.sent_status);
    assert(result.sent_status_plain);
    assert(result.scheduled_status_broadcast);
    assert(result.request_status_in_team_page);
    assert(result.clear_nav_stack);
    assert(writer.append_count == 1);
    assert(writer.last_type == team::ui::TeamKeyEventType::MemberAccepted);
    assert(controller.status_count == 1);
    assert(controller.status_plain_count == 1);
    assert(controller.last_status.key_id == 7);
    assert(controller.last_status.leader_id == 0x11111111);
    assert(hasMember(controller.last_status, 0x11111111));
    assert(hasMember(controller.last_status, 0x22222222));
    assert(deferred.scheduled.size() == 1);
    assert(deferred.scheduled[0].first == 1);
    assert(deferred.scheduled[0].second == 2);
    assert(notifier.messages.size() == 2);
    assert(notifier.messages[0] == "Paired: 2222");
    assert(notifier.messages[1] == "Paired successfully");
}

void testStatusSendFailuresAreTranslatedToNotifier()
{
    const auto state = makeState();
    auto key_state = makeKeyEventState();
    team::ui::TeamPageEventEffects effects;
    effects.status_should_send = true;
    FakeDeferred deferred;
    FakeNotifier notifier;
    FakeKeyEventWriter writer;
    team::ui::TeamPageKeyEventLog log(writer, 900);
    FakeController controller;
    controller.status_ok = false;
    controller.status_plain_ok = false;
    team::ui::TeamPageRuntimePort runtime(&controller, nullptr, nullptr);

    const auto result = team::ui::TeamPageEventEffectSink().applyEffects(
        state,
        key_state,
        effects,
        makeReducer(),
        runtime,
        log,
        deferred,
        notifier);

    assert(!result.sent_status);
    assert(!result.sent_status_plain);
    assert(result.scheduled_status_broadcast);
    assert(notifier.failures.size() == 2);
    assert(notifier.failures[0].first == "Status");
    assert(notifier.failures[0].second);
    assert(notifier.failures[1].first == "Status");
    assert(!notifier.failures[1].second);
}

} // namespace

int main()
{
    testStatusEffectsConfirmKeyDistAndAppendEpoch();
    testKeyDistEffectsSaveApplyKeysAndForwardNavigationRequests();
    testPairingEffectsAppendAcceptedMemberSendStatusAndNotify();
    testStatusSendFailuresAreTranslatedToNotifier();
    return 0;
}
