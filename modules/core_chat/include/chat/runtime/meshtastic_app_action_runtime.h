#pragma once

#include "chat/domain/chat_types.h"
#include "meshtastic/mesh.pb.h"
#include "meshtastic/portnums.pb.h"
#include "pb_decode.h"

#include <cstdint>

namespace chat::runtime
{

constexpr uint32_t kMeshtasticAppActionTimeoutMs = 30UL * 1000UL;

enum class MeshtasticAppActionKind : uint8_t
{
    None = 0,
    TraceRoute,
    PositionExchange,
};

enum class MeshtasticAppActionState : uint8_t
{
    Idle = 0,
    Pending,
    Delivered,
    Completed,
    Failed,
    TimedOut,
};

enum class MeshtasticAppActionReason : uint8_t
{
    None = 0,
    Started,
    RoutingAck,
    RoutingError,
    AppResponse,
    Timeout,
    LocalSendFailed,
};

struct MeshtasticAppActionSnapshot
{
    MeshtasticAppActionKind kind = MeshtasticAppActionKind::None;
    MeshtasticAppActionState state = MeshtasticAppActionState::Idle;
    MeshtasticAppActionReason reason = MeshtasticAppActionReason::None;
    NodeId peer = 0;
    MessageId request_id = 0;
    uint32_t started_ms = 0;
    uint32_t updated_ms = 0;
    meshtastic_Routing_Error routing_error = meshtastic_Routing_Error_NONE;
};

class MeshtasticAppActionRuntime
{
  public:
    void startTraceRoute(MessageId request_id,
                         NodeId peer,
                         uint32_t now_ms,
                         uint32_t timeout_ms = kMeshtasticAppActionTimeoutMs)
    {
        start(MeshtasticAppActionKind::TraceRoute, request_id, peer, now_ms, timeout_ms);
    }

    void startPositionExchange(MessageId request_id,
                               NodeId peer,
                               uint32_t now_ms,
                               uint32_t timeout_ms = kMeshtasticAppActionTimeoutMs)
    {
        start(MeshtasticAppActionKind::PositionExchange, request_id, peer, now_ms, timeout_ms);
    }

    void markLocalSendFailed(MeshtasticAppActionKind kind,
                             MessageId request_id,
                             NodeId peer,
                             uint32_t now_ms)
    {
        snapshot_.kind = kind;
        snapshot_.state = MeshtasticAppActionState::Failed;
        snapshot_.reason = MeshtasticAppActionReason::LocalSendFailed;
        snapshot_.peer = peer;
        snapshot_.request_id = request_id;
        snapshot_.started_ms = now_ms;
        snapshot_.updated_ms = now_ms;
        snapshot_.routing_error = meshtastic_Routing_Error_NONE;
        active_ = false;
    }

    bool consumeIncomingData(const MeshIncomingData& data,
                             uint32_t now_ms,
                             MeshtasticAppActionSnapshot* out_snapshot = nullptr)
    {
        if (!active_ || snapshot_.request_id == 0)
        {
            return false;
        }

        if (data.portnum == meshtastic_PortNum_ROUTING_APP)
        {
            if (data.request_id != snapshot_.request_id)
            {
                return false;
            }
            return consumeRoutingResult(data, now_ms, out_snapshot);
        }

        if (isExpectedAppResponse(data))
        {
            snapshot_.state = MeshtasticAppActionState::Completed;
            snapshot_.reason = MeshtasticAppActionReason::AppResponse;
            snapshot_.updated_ms = now_ms;
            snapshot_.routing_error = meshtastic_Routing_Error_NONE;
            active_ = false;
            copySnapshot(out_snapshot);
            return true;
        }

        return false;
    }

    bool tick(uint32_t now_ms, MeshtasticAppActionSnapshot* out_snapshot = nullptr)
    {
        if (!active_ || timeout_ms_ == 0)
        {
            return false;
        }

        if ((now_ms - snapshot_.started_ms) < timeout_ms_)
        {
            return false;
        }

        snapshot_.state = MeshtasticAppActionState::TimedOut;
        snapshot_.reason = MeshtasticAppActionReason::Timeout;
        snapshot_.updated_ms = now_ms;
        snapshot_.routing_error = meshtastic_Routing_Error_TIMEOUT;
        active_ = false;
        copySnapshot(out_snapshot);
        return true;
    }

    MeshtasticAppActionSnapshot snapshot() const
    {
        return snapshot_;
    }

    bool active() const
    {
        return active_;
    }

  private:
    void start(MeshtasticAppActionKind kind,
               MessageId request_id,
               NodeId peer,
               uint32_t now_ms,
               uint32_t timeout_ms)
    {
        snapshot_.kind = kind;
        snapshot_.state = MeshtasticAppActionState::Pending;
        snapshot_.reason = MeshtasticAppActionReason::Started;
        snapshot_.peer = peer;
        snapshot_.request_id = request_id;
        snapshot_.started_ms = now_ms;
        snapshot_.updated_ms = now_ms;
        snapshot_.routing_error = meshtastic_Routing_Error_NONE;
        timeout_ms_ = timeout_ms;
        active_ = request_id != 0;
    }

    bool consumeRoutingResult(const MeshIncomingData& data,
                              uint32_t now_ms,
                              MeshtasticAppActionSnapshot* out_snapshot)
    {
        if (data.payload.empty())
        {
            return false;
        }

        meshtastic_Routing routing = meshtastic_Routing_init_zero;
        pb_istream_t stream = pb_istream_from_buffer(data.payload.data(), data.payload.size());
        if (!pb_decode(&stream, meshtastic_Routing_fields, &routing) ||
            routing.which_variant != meshtastic_Routing_error_reason_tag)
        {
            return false;
        }

        snapshot_.updated_ms = now_ms;
        snapshot_.routing_error = routing.error_reason;
        if (routing.error_reason == meshtastic_Routing_Error_NONE)
        {
            if (snapshot_.state == MeshtasticAppActionState::Delivered)
            {
                return false;
            }
            snapshot_.state = MeshtasticAppActionState::Delivered;
            snapshot_.reason = MeshtasticAppActionReason::RoutingAck;
            copySnapshot(out_snapshot);
            return true;
        }

        snapshot_.state = MeshtasticAppActionState::Failed;
        snapshot_.reason = MeshtasticAppActionReason::RoutingError;
        active_ = false;
        copySnapshot(out_snapshot);
        return true;
    }

    bool isExpectedAppResponse(const MeshIncomingData& data) const
    {
        if (snapshot_.kind == MeshtasticAppActionKind::TraceRoute)
        {
            return data.portnum == meshtastic_PortNum_TRACEROUTE_APP &&
                   data.request_id == snapshot_.request_id;
        }
        if (snapshot_.kind == MeshtasticAppActionKind::PositionExchange)
        {
            return data.portnum == meshtastic_PortNum_POSITION_APP &&
                   data.request_id == snapshot_.request_id;
        }
        return false;
    }

    void copySnapshot(MeshtasticAppActionSnapshot* out_snapshot) const
    {
        if (out_snapshot)
        {
            *out_snapshot = snapshot_;
        }
    }

    MeshtasticAppActionSnapshot snapshot_{};
    uint32_t timeout_ms_ = kMeshtasticAppActionTimeoutMs;
    bool active_ = false;
};

} // namespace chat::runtime
