#include "screen_app_internal.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

struct TeamPageState
{
    ::team::ui::TeamUiSnapshot snapshot{};
    enum class Route : unsigned char
    {
        Status,
        Members,
        MemberDetail,
        LeaveConfirm,
        Chat,
    };

    Route route = Route::Status;
    size_t selected_member_index = 0;
};

TeamPageState s_team_page_state;

} // namespace

void reset_team_page_state()
{
    s_team_page_state = TeamPageState{};
    reset_team_chat_flow();
}

struct TeamActionScratch
{
    ::team::ui::TeamPageCommandState command_state{};
    ::team::ui::TeamPageKeyEventState key_event_state{};
    ::team::ui::TeamPageCreateTeamEffects create_effects{};
    ::team::ui::TeamPagePairingCommandEffects pairing_effects{};
    ::team::ui::TeamPageCommandEffects command_effects{};
};

TeamActionScratch s_team_action_scratch;

struct TeamChatRuntime
{
    ::team::TeamController* controller = nullptr;
    std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource> source;
    std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort> command_port;
    std::unique_ptr<::ui::presentation_sources::TeamChatActionSink> sink;
    std::unique_ptr<::ui::chat::ChatWorkspaceModel> model;

    ::ui::chat::ChatWorkspaceModel* ensure()
    {
        if (!::app::hasAppFacade())
        {
            return nullptr;
        }

        ::team::TeamController* const current_controller =
            ::app::teamFacade().getTeamController();
        if (model && controller == current_controller)
        {
            return model.get();
        }

        controller = current_controller;
        source = std::unique_ptr<::ui::presentation_sources::TeamChatPresentationSource>(
            new ::ui::presentation_sources::TeamChatPresentationSource(
                ::team::ui::team_ui_snapshot_store(),
                ::team::ui::team_ui_chat_log_store()));
        command_port.reset();
        if (controller != nullptr)
        {
            command_port = std::unique_ptr<::ui::presentation_sources::ITeamChatCommandPort>(
                new ::ui::team_actions::TeamControllerChatCommandPort(*controller));
        }
        sink = std::unique_ptr<::ui::presentation_sources::TeamChatActionSink>(
            new ::ui::presentation_sources::TeamChatActionSink(
                ::team::ui::team_ui_snapshot_store(),
                ::team::ui::team_ui_chat_log_store(),
                command_port.get()));
        model = std::unique_ptr<::ui::chat::ChatWorkspaceModel>(
            new ::ui::chat::ChatWorkspaceModel(*source, *sink));
        return model.get();
    }
};

TeamChatRuntime s_team_chat_runtime;

class TeamRandom final : public ::team::ui::ITeamPageCreateTeamRandom
{
  public:
    uint8_t nextByte() override
    {
        if (state_ == 0)
        {
            state_ = ::sys::millis_now() ^ 0xA5A55A5Au;
            if (state_ == 0)
            {
                state_ = 0x13579BDFu;
            }
        }
        state_ ^= state_ << 13;
        state_ ^= state_ >> 17;
        state_ ^= state_ << 5;
        return static_cast<uint8_t>(state_ & 0xFFu);
    }

  private:
    uint32_t state_ = 0;
};

class TeamKeyEventWriter final : public ::team::ui::ITeamPageKeyEventWriter
{
  public:
    bool appendKeyEvent(const ::team::TeamId& team_id,
                        ::team::ui::TeamKeyEventType type,
                        uint32_t event_seq,
                        uint32_t timestamp_s,
                        const uint8_t* payload,
                        size_t payload_size) override
    {
        return ::team::ui::team_ui_append_key_event(
            team_id, type, event_seq, timestamp_s, payload, payload_size);
    }
};

uint32_t
team_now_s()
{
    return ::sys::millis_now() / 1000U;
}

void copy_team_command_state(const ::team::ui::TeamUiSnapshot& snapshot,
                             ::team::ui::TeamPageCommandState& state)
{
    state.in_team = snapshot.in_team;
    state.pending_join = snapshot.pending_join;
    state.pending_join_started_s = snapshot.pending_join_started_s;
    state.kicked_out = snapshot.kicked_out;
    state.self_is_leader = snapshot.self_is_leader;
    state.last_event_seq = snapshot.last_event_seq;
    state.team_id = snapshot.team_id;
    state.has_team_id = snapshot.has_team_id;
    state.team_name = snapshot.team_name;
    state.security_round = snapshot.security_round;
    state.last_update_s = snapshot.last_update_s;
    state.team_psk = snapshot.team_psk;
    state.has_team_psk = snapshot.has_team_psk;
    state.members = snapshot.members;
}

void save_team_command_state(const ::team::ui::TeamPageCommandState& state,
                             uint32_t unread)
{
    ::team::ui::TeamUiSnapshot& snapshot = s_team_page_state.snapshot;
    snapshot.in_team = state.in_team;
    snapshot.pending_join = state.pending_join;
    snapshot.pending_join_started_s = state.pending_join_started_s;
    snapshot.kicked_out = state.kicked_out;
    snapshot.self_is_leader = state.self_is_leader;
    snapshot.last_event_seq = state.last_event_seq;
    snapshot.team_chat_unread = unread;
    snapshot.team_id = state.team_id;
    snapshot.has_team_id = state.has_team_id;
    snapshot.team_name = state.team_name;
    snapshot.security_round = state.security_round;
    snapshot.last_update_s = state.last_update_s;
    snapshot.team_psk = state.team_psk;
    snapshot.has_team_psk = state.has_team_psk;
    snapshot.members = state.members;
    ::team::ui::team_ui_snapshot_store().save(snapshot);
}

::team::ui::TeamPageRuntimePort team_runtime_port()
{
    static ::team::ui::TeamPageKeyStorePortAdapter key_store;
    static ::team::ui::TeamPageControllerPortAdapter controller(nullptr);
    static ::team::ui::TeamPagePairingPortAdapter pairing(nullptr);
    controller = ::team::ui::TeamPageControllerPortAdapter(
        ::app::teamFacade().getTeamController());
    pairing = ::team::ui::TeamPagePairingPortAdapter(
        ::app::teamFacade().getTeamPairing());
    return ::team::ui::TeamPageRuntimePort(&controller, &pairing, &key_store);
}

void apply_team_runtime_effects(const ::team::ui::TeamPageCommandEffects& effects,
                                const ::team::ui::TeamPageRuntimePort& runtime)
{
    if (effects.clear_keys)
    {
        runtime.clearKeys();
    }
    if (effects.reset_controller_ui)
    {
        runtime.resetControllerUi();
    }
    if (effects.stop_pairing)
    {
        runtime.stopPairing();
    }
}

const char* pairing_state_text(::team::TeamPairingState state)
{
    switch (state)
    {
    case ::team::TeamPairingState::LeaderBeacon:
        return "WAITING FOR MEMBER";
    case ::team::TeamPairingState::MemberScanning:
        return "SCANNING FOR TEAM";
    case ::team::TeamPairingState::JoinSent:
        return "JOIN REQUEST SENT";
    case ::team::TeamPairingState::WaitingKey:
        return "WAITING FOR KEYS";
    case ::team::TeamPairingState::Completed:
        return "PAIRING COMPLETE";
    case ::team::TeamPairingState::Failed:
        return "PAIRING FAILED";
    case ::team::TeamPairingState::Idle:
    default:
        return "IDLE";
    }
}

void render_team()
{
    reset_chat_conversation_page();
    if (!::team::ui::team_ui_snapshot_store().load(s_team_page_state.snapshot))
    {
        set_line(0, "TEAM DATA UNAVAILABLE");
        set_line(1, "WAIT FOR TEAM RUNTIME");
        set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "REFRESH TO RETRY");
        clear_lines_from(3);
        return;
    }

    const ::team::ui::TeamUiSnapshot& snapshot = s_team_page_state.snapshot;

    if (s_team_page_state.route == TeamPageState::Route::Chat)
    {
        if (!ensure_team_chat_flow())
        {
            set_text(s_state.title, "TEAM");
            set_text(s_state.subtitle, "MEMORY");
            set_line(0, "PSRAM REQUIRED FOR TEAM CHAT");
            set_line(1, "RETURN TO TEAM STATUS");
            clear_lines_from(2);
            return;
        }
        ChatFlowState& flow = team_chat_flow();
        ::ui::chat::ChatWorkspaceModel* const model = s_team_chat_runtime.ensure();
        if (model == nullptr || !model->buildSnapshot(flow.snapshot))
        {
            set_text(s_state.title, "TEAM");
            set_text(s_state.subtitle, "CHAT");
            set_line(0, "TEAM CHAT UNAVAILABLE");
            set_line(1, "WAIT FOR TEAM RUNTIME");
            set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO TEAM");
            clear_lines_from(3);
            return;
        }
        if (flow.snapshot.conversation_count == 0)
        {
            set_text(s_state.title, "TEAM");
            set_text(s_state.subtitle, "CHAT");
            set_line(0, "NO ACTIVE TEAM CHAT");
            set_line(1, "CREATE OR JOIN A TEAM FIRST");
            set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO TEAM");
            clear_lines_from(3);
            return;
        }
        const ::ui::chat::ConversationId id = flow.snapshot.conversations[0].id;
        if (!flow.snapshot.conversations[0].selected)
        {
            if (!model->selectConversation(id).ok)
            {
                set_line(0, "TEAM CHAT UNAVAILABLE");
                set_line(1, "CONVERSATION NOT READY");
                clear_lines_from(2);
                return;
            }
            (void)model->markRead(id);
            (void)model->buildSnapshot(flow.snapshot);
        }

        if (flow.route == ChatRoute::Compose)
        {
            render_chat_compose_page(flow, "TEAM");
        }
        else
        {
            flow.route = ChatRoute::Conversation;
            render_chat_conversation_page(flow, "TEAM CHAT");
        }
        return;
    }

    if (!snapshot.in_team && !snapshot.pending_join)
    {
        set_line(0, snapshot.kicked_out ? "TEAM MEMBERSHIP ENDED" : "NOT IN A TEAM");
        set_line(1, "CREATE A LOCAL TEAM OR JOIN");
        set_line(2, "PAIRING USES THE EXISTING RADIO FLOW");
        set_line(3, s_state.notice[0] != '\0' ? s_state.notice : "CREATE / JOIN");
        clear_lines_from(4);
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::Members)
    {
        set_text(s_state.title, "TEAM");
        set_text(s_state.subtitle, "MEMBERS");
        if (snapshot.members.empty())
        {
            set_line(0, "NO TEAM MEMBERS YET");
            set_line(1, "PAIRING UPDATES THIS LIST");
            set_line(2, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO TEAM");
            clear_lines_from(3);
            return;
        }
        if (s_team_page_state.selected_member_index >= snapshot.members.size())
        {
            s_team_page_state.selected_member_index = snapshot.members.size() - 1U;
        }
        set_linef(0,
                  "MEMBERS %u  SELECT %u",
                  static_cast<unsigned>(snapshot.members.size()),
                  static_cast<unsigned>(s_team_page_state.selected_member_index + 1U));
        size_t line = 1;
        for (size_t index = 0; index < snapshot.members.size() && line < 9; ++index)
        {
            const auto& member = snapshot.members[index];
            set_linef(line++,
                      "%c %s %s %s",
                      index == s_team_page_state.selected_member_index ? '>' : ' ',
                      member.name.empty() ? "UNKNOWN" : member.name.c_str(),
                      member.leader ? "LEADER" : "MEMBER",
                      member.online ? "ONLINE" : "OFFLINE");
        }
        clear_lines_from(line);
        set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "PREV/NEXT SELECT  OPEN DETAIL");
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::MemberDetail)
    {
        set_text(s_state.title, "TEAM");
        set_text(s_state.subtitle, "MEMBER");
        if (snapshot.members.empty() ||
            s_team_page_state.selected_member_index >= snapshot.members.size())
        {
            set_line(0, "MEMBER UNAVAILABLE");
            set_line(1, "BACK TO MEMBERS");
            clear_lines_from(2);
            return;
        }
        const auto& member = snapshot.members[s_team_page_state.selected_member_index];
        set_linef(0, "NAME %s", member.name.empty() ? "UNKNOWN" : member.name.c_str());
        set_linef(1, "ROLE %s", member.leader ? "LEADER" : "MEMBER");
        set_linef(2, "STATUS %s", member.online ? "ONLINE" : "OFFLINE");
        set_line(3, "MEMBER DETAIL IS READ-ONLY");
        set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "BACK TO MEMBERS");
        clear_lines_from(5);
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::LeaveConfirm)
    {
        set_text(s_state.title, "TEAM");
        set_text(s_state.subtitle, "CONFIRM");
        set_line(0, "LEAVE THIS TEAM?");
        set_line(1, snapshot.team_name.empty() ? "CURRENT TEAM" : snapshot.team_name.c_str());
        set_line(2, "KEYS AND MEMBER STATE WILL RESET");
        set_line(3, "CANCEL TO KEEP MEMBERSHIP");
        set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "CONFIRM LEAVE TO CONTINUE");
        clear_lines_from(5);
        return;
    }

    const char* const title = snapshot.team_name.empty() ? "TEAM" : snapshot.team_name.c_str();
    set_text(s_state.title, "TEAM");
    set_text(s_state.subtitle, "STATUS");
    set_linef(0, "TEAM %s", title);
    set_linef(1,
              "ROLE %s  MEMBERS %u",
              snapshot.self_is_leader ? "LEADER" : "MEMBER",
              static_cast<unsigned>(snapshot.members.size()));
    set_linef(2,
              "KEYS %s  ROUND %lu",
              snapshot.has_team_psk ? "READY" : "WAITING",
              static_cast<unsigned long>(snapshot.security_round));
    set_linef(3, "UNREAD %lu", static_cast<unsigned long>(snapshot.team_chat_unread));
    if (snapshot.pending_join)
    {
        const ::team::TeamPairingStatus pairing = ::app::teamFacade().getTeamPairing() != nullptr
                                                      ? ::app::teamFacade().getTeamPairing()->getStatus()
                                                      : ::team::TeamPairingStatus{};
        set_linef(4, "PAIRING %s", pairing_state_text(pairing.state));
    }
    else
    {
        set_line(4, "STATUS ACTIVE");
    }

    size_t line = 5;
    for (const auto& member : snapshot.members)
    {
        if (line >= 9)
        {
            break;
        }
        const char* const name = member.name.empty() ? "UNKNOWN" : member.name.c_str();
        set_linef(line++,
                  "%c %s %s",
                  member.leader ? '*' : ' ',
                  name,
                  member.online ? "ONLINE" : "OFFLINE");
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "CHAT / LEAVE");
}

void configure_team_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != PageKind::Team ||
        s_state.action_count < 5)
    {
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::Chat)
    {
        if (!ensure_team_chat_flow())
        {
            return;
        }
        ChatFlowState& flow = team_chat_flow();
        if (flow.route == ChatRoute::Compose)
        {
            configure_chat_compose_actions(flow, "TEAM");
            return;
        }
        set_action(0, "OLDER", Action::ChatPrevious);
        set_action(1, "NEWER", Action::ChatNext);
        set_action(2, "COMPOSE", Action::ChatType);
        set_action(3, "SYNC", Action::Refresh);
        set_action(4, "TEAM", Action::Back);
        for (size_t index = 0; index < 5; ++index)
        {
            set_action_visible(index, true);
        }
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::Members)
    {
        set_action(0, "PREV", Action::TeamMemberPrevious);
        set_action(1, "NEXT", Action::TeamMemberNext);
        set_action(2, "OPEN", Action::TeamMemberOpen);
        set_action(3, "SYNC", Action::Refresh);
        set_action(4, "BACK", Action::Back);
        for (size_t index = 0; index < 5; ++index)
        {
            set_action_visible(index, true);
        }
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::MemberDetail)
    {
        set_action(0, "BACK", Action::Back);
        set_action(1, "SYNC", Action::Refresh);
        for (size_t index = 0; index < 2; ++index)
        {
            set_action_visible(index, true);
        }
        for (size_t index = 2; index < s_state.action_count; ++index)
        {
            set_action_visible(index, false);
        }
        return;
    }

    if (s_team_page_state.route == TeamPageState::Route::LeaveConfirm)
    {
        set_action(0, "CANCEL", Action::TeamLeaveCancel);
        set_action(1, "LEAVE", Action::TeamLeaveConfirm);
        for (size_t index = 0; index < 2; ++index)
        {
            set_action_visible(index, true);
        }
        for (size_t index = 2; index < s_state.action_count; ++index)
        {
            set_action_visible(index, false);
        }
        return;
    }

    const bool joined = s_team_page_state.snapshot.in_team || s_team_page_state.snapshot.pending_join;
    set_action(0, joined ? "MEMBERS" : "CREATE", joined ? Action::TeamMembers : Action::TeamCreate);
    set_action(1, joined ? "CHAT" : "JOIN", joined ? Action::TeamChat : Action::TeamJoin);
    set_action(2, joined ? "LEAVE" : "SYNC", joined ? Action::TeamLeave : Action::Refresh);
    set_action(3, "BACK", Action::Back);
    for (size_t index = 0; index < 4; ++index)
    {
        set_action_visible(index, true);
    }
    set_action_visible(4, false);
}

::ui::chat::ChatWorkspaceModel* ensure_team_chat_model()
{
    return s_team_chat_runtime.ensure();
}

void add_team_actions()
{
    add_action("CREATE", Action::TeamCreate, kMargin, kActionTop, 70);
    add_action("JOIN", Action::TeamJoin, 84, kActionTop, 54);
    add_action("REFRESH", Action::Refresh, 144, kActionTop, 86);
    add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
    add_action("", Action::Refresh, 62, kActionTop + 22, 48);
}

bool handle_team_action(Action action)
{
    if (s_team_page_state.route == TeamPageState::Route::Chat)
    {
        if (!ensure_team_chat_flow())
        {
            set_notice("TEAM CHAT MEMORY UNAVAILABLE");
            return action != Action::Back;
        }
        ChatFlowState& flow = team_chat_flow();
        ::ui::chat::ChatWorkspaceModel* const model = s_team_chat_runtime.ensure();
        if (flow.route == ChatRoute::Compose)
        {
            return handle_chat_compose_action(action, flow, model, "TEAM MESSAGE QUEUED");
        }
        switch (action)
        {
        case Action::Back:
            s_team_page_state.route = TeamPageState::Route::Status;
            flow.route = ChatRoute::Conversation;
            set_notice("TEAM STATUS");
            return true;
        case Action::ChatPrevious:
        case Action::ChatNext:
            if (model == nullptr)
            {
                set_notice("TEAM CHAT UNAVAILABLE");
                return true;
            }
            {
                const uint16_t offset = model->messageOffset();
                const uint16_t next = action == Action::ChatPrevious
                                          ? (offset < 10U ? 0U : static_cast<uint16_t>(offset - 10U))
                                          : static_cast<uint16_t>(offset + 10U);
                model->setMessageOffset(next);
                set_notice(action == Action::ChatPrevious ? "OLDER MESSAGES" : "NEWER MESSAGES");
            }
            return true;
        case Action::ChatType:
            if (!flow.snapshot.composer_enabled)
            {
                set_notice("COMPOSER UNAVAILABLE");
                return true;
            }
            enter_chat_compose_page(flow);
            set_notice("TYPE TEAM MESSAGE");
            return true;
        default:
            return false;
        }
    }

    if (s_team_page_state.route == TeamPageState::Route::Members)
    {
        const size_t member_count = s_team_page_state.snapshot.members.size();
        switch (action)
        {
        case Action::Back:
            s_team_page_state.route = TeamPageState::Route::Status;
            set_notice("TEAM STATUS");
            return true;
        case Action::TeamMemberPrevious:
        case Action::TeamMemberNext:
            if (member_count == 0)
            {
                set_notice("NO TEAM MEMBERS");
                return true;
            }
            s_team_page_state.selected_member_index = action == Action::TeamMemberPrevious
                                                          ? (s_team_page_state.selected_member_index == 0
                                                                 ? member_count - 1U
                                                                 : s_team_page_state.selected_member_index - 1U)
                                                          : (s_team_page_state.selected_member_index + 1U) % member_count;
            set_notice("");
            return true;
        case Action::TeamMemberOpen:
            if (member_count == 0 || s_team_page_state.selected_member_index >= member_count)
            {
                set_notice("NO MEMBER SELECTED");
                return true;
            }
            s_team_page_state.route = TeamPageState::Route::MemberDetail;
            set_notice("MEMBER DETAIL");
            return true;
        default:
            return false;
        }
    }

    if (s_team_page_state.route == TeamPageState::Route::MemberDetail)
    {
        if (action == Action::Back)
        {
            s_team_page_state.route = TeamPageState::Route::Members;
            set_notice("TEAM MEMBERS");
            return true;
        }
        return false;
    }

    if (s_team_page_state.route == TeamPageState::Route::LeaveConfirm)
    {
        if (action == Action::Back || action == Action::TeamLeaveCancel)
        {
            s_team_page_state.route = TeamPageState::Route::Status;
            set_notice("LEAVE CANCELLED");
            return true;
        }
        if (action != Action::TeamLeaveConfirm)
        {
            return false;
        }
        action = Action::TeamLeave;
    }

    if (s_team_page_state.route == TeamPageState::Route::Status && action == Action::TeamLeave)
    {
        s_team_page_state.route = TeamPageState::Route::LeaveConfirm;
        set_notice("CONFIRM LEAVE");
        return true;
    }

    switch (action)
    {
    case Action::TeamCreate:
    {
        if (!::app::hasAppFacade())
        {
            set_notice("TEAM RUNTIME UNAVAILABLE");
            return true;
        }

        if (!::team::ui::team_ui_snapshot_store().load(s_team_page_state.snapshot))
        {
            s_team_page_state.snapshot = ::team::ui::TeamUiSnapshot{};
        }
        copy_team_command_state(s_team_page_state.snapshot, s_team_action_scratch.command_state);
        s_team_action_scratch.key_event_state.team_id = s_team_action_scratch.command_state.team_id;
        s_team_action_scratch.key_event_state.has_team_id =
            s_team_action_scratch.command_state.has_team_id;
        s_team_action_scratch.key_event_state.last_event_seq =
            s_team_action_scratch.command_state.last_event_seq;
        s_team_action_scratch.key_event_state.security_round =
            s_team_action_scratch.command_state.security_round;

        const ::team::ui::TeamPageCommandContext context{
            team_now_s(), ::app::messagingFacade().getSelfNodeId()};
        const ::team::ui::TeamPageCommandReducer reducer(context);
        const ::team::ui::TeamPageRuntimePort runtime = team_runtime_port();
        static TeamKeyEventWriter event_writer;
        static TeamRandom random;
        const ::team::ui::TeamPageKeyEventLog event_log(event_writer, team_now_s());
        s_team_action_scratch.create_effects = ::team::ui::TeamPageCreateTeamAction().createTeam(
            s_team_action_scratch.command_state,
            s_team_action_scratch.key_event_state,
            reducer,
            runtime,
            event_log,
            random,
            ::app::messagingFacade().getSelfNodeId());
        save_team_command_state(s_team_action_scratch.command_state,
                                s_team_page_state.snapshot.team_chat_unread);
        if (s_team_action_scratch.create_effects.command.clear_keys ||
            s_team_action_scratch.create_effects.command.reset_controller_ui ||
            s_team_action_scratch.create_effects.command.stop_pairing)
        {
            apply_team_runtime_effects(s_team_action_scratch.create_effects.command, runtime);
        }
        set_notice(s_team_action_scratch.create_effects.accepted
                       ? (s_team_action_scratch.create_effects.started_pairing
                              ? "TEAM CREATED; INVITE BEACON ACTIVE"
                              : "TEAM CREATED")
                       : "TEAM CREATE REJECTED");
        return true;
    }
    case Action::TeamJoin:
    {
        if (!::app::hasAppFacade())
        {
            set_notice("TEAM RUNTIME UNAVAILABLE");
            return true;
        }

        if (!::team::ui::team_ui_snapshot_store().load(s_team_page_state.snapshot))
        {
            s_team_page_state.snapshot = ::team::ui::TeamUiSnapshot{};
        }
        copy_team_command_state(s_team_page_state.snapshot, s_team_action_scratch.command_state);
        const ::team::ui::TeamPageCommandContext context{
            team_now_s(), ::app::messagingFacade().getSelfNodeId()};

        const ::team::ui::TeamPageCommandReducer reducer(context);
        const ::team::ui::TeamPageRuntimePort runtime = team_runtime_port();
        s_team_action_scratch.pairing_effects =
            ::team::ui::TeamPagePairingCommandAction().startPairing(
                s_team_action_scratch.command_state,
                reducer,
                runtime,
                ::team::ui::TeamPagePairingCommandRole::Member,
                ::app::messagingFacade().getSelfNodeId());
        save_team_command_state(s_team_action_scratch.command_state,
                                s_team_page_state.snapshot.team_chat_unread);
        set_notice(s_team_action_scratch.pairing_effects.started_pairing
                       ? "TEAM SCAN ACTIVE"
                       : "TEAM JOIN UNAVAILABLE");
        return true;
    }
    case Action::TeamLeave:
    {
        if (!::team::ui::team_ui_snapshot_store().load(s_team_page_state.snapshot))
        {
            set_notice("NO TEAM TO LEAVE");
            return true;
        }
        copy_team_command_state(s_team_page_state.snapshot, s_team_action_scratch.command_state);
        const ::team::ui::TeamPageCommandContext context{
            team_now_s(), ::app::messagingFacade().getSelfNodeId()};
        const ::team::ui::TeamPageCommandReducer reducer(context);
        const ::team::ui::TeamPageRuntimePort runtime = team_runtime_port();
        s_team_action_scratch.command_effects =
            reducer.reduceLeave(s_team_action_scratch.command_state);
        apply_team_runtime_effects(s_team_action_scratch.command_effects, runtime);
        save_team_command_state(s_team_action_scratch.command_state, 0);
        set_notice("LEFT TEAM");
        s_team_page_state.route = TeamPageState::Route::Status;
        return true;
    }
    case Action::TeamMembers:
        s_team_page_state.route = TeamPageState::Route::Members;
        s_team_page_state.selected_member_index = 0;
        set_notice("TEAM MEMBERS");
        return true;
    case Action::TeamChat:
        if (!ensure_team_chat_flow())
        {
            set_notice("TEAM CHAT MEMORY UNAVAILABLE");
            return true;
        }
        s_team_page_state.route = TeamPageState::Route::Chat;
        team_chat_flow().route = ChatRoute::Conversation;
        set_notice("TEAM CHAT OPEN");
        return true;

    default:
        return false;
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
