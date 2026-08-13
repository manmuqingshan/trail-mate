#include "platform/esp/arduino_common/hostlink/hostlink_bridge_radio.h"

#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/usecase/chat_service.h"
#include "hostlink/hostlink_app_data_codec.h"
#include "hostlink/hostlink_event_codec.h"
#include "hostlink/hostlink_types.h"
#include "platform/esp/arduino_common/hostlink/hostlink_service.h"
#include "platform/ui/team_ui_store_runtime.h"
#include "sys/clock.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_mgmt.h"
#include "team/protocol/team_portnum.h"
#include "team/protocol/team_waypoint.h"
#include "ui/team_presence/team_presence_model.h"

#include <string>
#include <utility>
#include <vector>

namespace hostlink::bridge
{

namespace
{
constexpr uint8_t kAppFlagTeamMeta = 1 << 0;
constexpr uint8_t kAppFlagWantResponse = 1 << 1;
constexpr uint8_t kAppFlagWasEncrypted = 1 << 2;
constexpr uint32_t kMinValidEpoch = 1577836800U; // 2020-01-01

uint32_t s_team_state_hash = 0;
bool s_team_state_has_hash = false;
AppRxStats s_app_rx_stats{};

uint32_t presenceNowForMember(uint32_t last_seen_s)
{
    if (last_seen_s >= kMinValidEpoch)
    {
        return sys::epoch_seconds_now();
    }
    return sys::uptime_seconds_now();
}

void update_app_rx_stats(const chat::RxMeta* rx_meta)
{
    s_app_rx_stats.total++;
    if (!rx_meta)
    {
        return;
    }
    if (rx_meta->from_is)
    {
        s_app_rx_stats.from_is++;
    }
    if (rx_meta->hop_count != 0xFF)
    {
        if (rx_meta->direct)
        {
            s_app_rx_stats.direct++;
        }
        else
        {
            s_app_rx_stats.relayed++;
        }
    }
}

hostlink::TeamStateSnapshot make_team_state_snapshot(const team::ui::TeamUiSnapshot& snap)
{
    hostlink::TeamStateSnapshot snapshot;
    snapshot.in_team = snap.in_team;
    snapshot.pending_join = snap.pending_join;
    snapshot.kicked_out = snap.kicked_out;
    snapshot.self_is_leader = snap.self_is_leader;
    snapshot.has_team_id = snap.has_team_id;
    snapshot.self_id = app::messagingFacade().getSelfNodeId();
    snapshot.team_id = snap.team_id.data();
    snapshot.team_id_len = snap.team_id.size();
    snapshot.security_round = snap.security_round;
    snapshot.last_event_seq = snap.last_event_seq;
    snapshot.last_update_s = snap.last_update_s;
    snapshot.team_name = snap.team_name;
    snapshot.members.reserve(snap.members.size());

    for (const auto& member : snap.members)
    {
        hostlink::TeamStateMemberSnapshot item;
        item.node_id = member.node_id;
        item.leader = member.leader;
        item.online = ::ui::team_presence::isTeamMemberOnline(
            presenceNowForMember(member.last_seen_s),
            member.last_seen_s);
        item.last_seen_s = member.last_seen_s;
        item.name = member.name;
        snapshot.members.push_back(std::move(item));
    }

    return snapshot;
}
void maybe_send_team_state(bool force)
{
    team::ui::TeamUiSnapshot snap{};
    (void)team::ui::team_ui_snapshot_store().load(snap);

    std::vector<uint8_t> payload;
    const hostlink::TeamStateSnapshot snapshot = make_team_state_snapshot(snap);
    if (!hostlink::build_team_state_payload(snapshot, payload))
    {
        return;
    }
    uint32_t hash = hostlink::hash_bytes(payload.data(), payload.size());
    if (!force && s_team_state_has_hash && hash == s_team_state_hash)
    {
        return;
    }
    s_team_state_has_hash = true;
    s_team_state_hash = hash;
    hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvTeamState),
                            payload.data(), payload.size(), false);
}

void send_app_data(uint32_t portnum,
                   uint32_t from,
                   uint32_t to,
                   uint8_t channel,
                   uint8_t flags,
                   const uint8_t* team_id,
                   uint32_t team_key_id,
                   uint32_t timestamp_s,
                   uint32_t packet_id,
                   const chat::RxMeta* rx_meta,
                   const uint8_t* payload,
                   size_t payload_len)
{
    hostlink::AppDataFrameEncodeRequest request;
    request.portnum = portnum;
    request.from = from;
    request.to = to;
    request.channel = channel;
    request.flags = flags;
    request.team_id = team_id;
    request.team_key_id = team_key_id;
    request.timestamp_s = timestamp_s;
    request.packet_id = packet_id;
    request.rx_meta = rx_meta;
    request.payload = payload;
    request.payload_len = payload_len;

    std::vector<std::vector<uint8_t>> frames;
    if (!hostlink::encode_app_data_frames(request,
                                          hostlink::kMaxFrameLen,
                                          team::proto::kTeamIdSize,
                                          frames))
    {
        return;
    }

    update_app_rx_stats(rx_meta);
    for (const auto& frame : frames)
    {
        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvAppData),
                                frame.data(), frame.size(), false);
    }
}

template <typename Msg, bool (*Encoder)(const Msg&, std::vector<uint8_t>&)>
bool encode_team_mgmt_wire(team::proto::TeamMgmtType type, const Msg& msg, std::vector<uint8_t>& out)
{
    std::vector<uint8_t> payload;
    if (!Encoder(msg, payload))
    {
        return false;
    }
    return team::proto::encodeTeamMgmtMessage(type, payload, out);
}
} // namespace

void on_event(const sys::Event& event)
{
    if (!hostlink::is_active())
    {
        return;
    }
    hostlink::Status st = hostlink::get_status();
    if (st.state != hostlink::LinkState::Ready)
    {
        return;
    }

    switch (event.type)
    {
    case sys::EventType::ChatNewMessage:
    {
        const auto& msg_evt = static_cast<const sys::ChatNewMessageEvent&>(event);
        // HostLink's EvRxMsg has a text-ledger payload only.  Do not serialize
        // a voice inbox identifier as a synthetic text message; the UI still
        // receives the shared ChatNewMessage event locally.
        if (msg_evt.content_kind != sys::ChatMessageContentKind::Text)
        {
            break;
        }
        const chat::ChatMessage* msg =
            app::messagingFacade().getChatService().getMessage(msg_evt.msg_id);

        uint32_t msg_id = msg ? msg->msg_id : msg_evt.msg_id;
        uint32_t from = msg ? msg->from : 0;
        uint32_t to = msg ? msg->peer : 0;
        uint8_t channel = msg ? static_cast<uint8_t>(msg->channel) : msg_evt.channel;
        uint32_t ts = msg ? msg->timestamp : 0;
        const std::string text = msg ? msg->text : std::string(msg_evt.text);

        std::vector<uint8_t> meta_tlv;
        hostlink::build_rx_meta_tlvs(msg_evt.rx_meta, msg_id, meta_tlv);
        update_app_rx_stats(&msg_evt.rx_meta);

        hostlink::RxMessageEventPayload snapshot;
        snapshot.msg_id = msg_id;
        snapshot.from = from;
        snapshot.to = to;
        snapshot.channel = channel;
        snapshot.timestamp_s = ts;
        snapshot.text = text;
        snapshot.meta_tlv = meta_tlv.empty() ? nullptr : meta_tlv.data();
        snapshot.meta_tlv_len = meta_tlv.size();

        std::vector<uint8_t> payload;
        if (!hostlink::build_rx_message_payload(snapshot, payload))
        {
            break;
        }

        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvRxMsg),
                                payload.data(), payload.size(), false);
        break;
    }
    case sys::EventType::ChatSendResult:
    {
        const auto& res_evt = static_cast<const sys::ChatSendResultEvent&>(event);
        std::vector<uint8_t> payload;
        if (!hostlink::build_tx_result_payload(res_evt.msg_id, res_evt.success, payload))
        {
            break;
        }
        hostlink::enqueue_event(static_cast<uint8_t>(hostlink::FrameType::EvTxResult),
                                payload.data(), payload.size(), true);
        break;
    }
    case sys::EventType::AppData:
    {
        const auto& data_evt = static_cast<const sys::AppDataEvent&>(event);
        uint8_t flags = 0;
        if (data_evt.want_response)
        {
            flags |= kAppFlagWantResponse;
        }
        send_app_data(data_evt.portnum,
                      data_evt.from,
                      data_evt.to,
                      data_evt.channel,
                      flags,
                      nullptr,
                      0,
                      event.timestamp / 1000,
                      data_evt.packet_id,
                      &data_evt.rx_meta,
                      data_evt.payload.data(),
                      data_evt.payload.size());
        break;
    }
    case sys::EventType::TeamKick:
    {
        const auto& team_evt = static_cast<const sys::TeamKickEvent&>(event);
        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamKick, team::proto::encodeTeamKick>(
                team::proto::TeamMgmtType::Kick, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamTransferLeader:
    {
        const auto& team_evt = static_cast<const sys::TeamTransferLeaderEvent&>(event);
        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamTransferLeader, team::proto::encodeTeamTransferLeader>(
                team::proto::TeamMgmtType::TransferLeader, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamKeyDist:
    {
        const auto& team_evt = static_cast<const sys::TeamKeyDistEvent&>(event);
        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamKeyDist, team::proto::encodeTeamKeyDist>(
                team::proto::TeamMgmtType::KeyDist, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamKeyRequest:
    {
        const auto& team_evt = static_cast<const sys::TeamKeyRequestEvent&>(event);
        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamKeyRequest, team::proto::encodeTeamKeyRequest>(
                team::proto::TeamMgmtType::KeyRequest, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamStatus:
    {
        const auto& team_evt = static_cast<const sys::TeamStatusEvent&>(event);
        std::vector<uint8_t> wire;
        if (encode_team_mgmt_wire<team::proto::TeamStatus, team::proto::encodeTeamStatus>(
                team::proto::TeamMgmtType::Status, team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta;
            if (team_evt.data.ctx.key_id != 0)
            {
                flags |= kAppFlagWasEncrypted;
            }
            send_app_data(team::proto::TEAM_MGMT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    case sys::EventType::TeamPosition:
    {
        const auto& team_evt = static_cast<const sys::TeamPositionEvent&>(event);
        uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
        send_app_data(team::proto::TEAM_POSITION_APP,
                      team_evt.data.ctx.from,
                      0,
                      0,
                      flags,
                      team_evt.data.ctx.team_id.data(),
                      team_evt.data.ctx.key_id,
                      event.timestamp / 1000,
                      0,
                      &team_evt.data.ctx.rx_meta,
                      team_evt.data.payload.data(),
                      team_evt.data.payload.size());
        break;
    }
    case sys::EventType::TeamWaypoint:
    {
        const auto& team_evt = static_cast<const sys::TeamWaypointEvent&>(event);
        std::vector<uint8_t> payload;
        if (!team::proto::encodeTeamWaypointMessage(team_evt.data.msg, payload))
        {
            break;
        }
        uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
        send_app_data(team::proto::TEAM_WAYPOINT_APP,
                      team_evt.data.ctx.from,
                      0,
                      0,
                      flags,
                      team_evt.data.ctx.team_id.data(),
                      team_evt.data.ctx.key_id,
                      event.timestamp / 1000,
                      0,
                      &team_evt.data.ctx.rx_meta,
                      payload.data(),
                      payload.size());
        break;
    }
    case sys::EventType::TeamTrack:
    {
        const auto& team_evt = static_cast<const sys::TeamTrackEvent&>(event);
        uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
        send_app_data(team::proto::TEAM_TRACK_APP,
                      team_evt.data.ctx.from,
                      0,
                      0,
                      flags,
                      team_evt.data.ctx.team_id.data(),
                      team_evt.data.ctx.key_id,
                      event.timestamp / 1000,
                      0,
                      &team_evt.data.ctx.rx_meta,
                      team_evt.data.payload.data(),
                      team_evt.data.payload.size());
        break;
    }
    case sys::EventType::TeamChat:
    {
        const auto& team_evt = static_cast<const sys::TeamChatEvent&>(event);
        std::vector<uint8_t> wire;
        if (team::proto::encodeTeamChatMessage(team_evt.data.msg, wire))
        {
            uint8_t flags = kAppFlagTeamMeta | kAppFlagWasEncrypted;
            send_app_data(team::proto::TEAM_CHAT_APP,
                          team_evt.data.ctx.from,
                          0,
                          0,
                          flags,
                          team_evt.data.ctx.team_id.data(),
                          team_evt.data.ctx.key_id,
                          event.timestamp / 1000,
                          0,
                          &team_evt.data.ctx.rx_meta,
                          wire.data(),
                          wire.size());
        }
        break;
    }
    default:
        break;
    }
}

void on_team_state_changed()
{
    if (!hostlink::is_active())
    {
        return;
    }
    hostlink::Status st = hostlink::get_status();
    if (st.state != hostlink::LinkState::Ready)
    {
        return;
    }
    maybe_send_team_state(false);
}

void on_link_ready()
{
    if (!hostlink::is_active())
    {
        return;
    }
    maybe_send_team_state(true);
}

AppRxStats get_app_rx_stats()
{
    return s_app_rx_stats;
}

} // namespace hostlink::bridge
