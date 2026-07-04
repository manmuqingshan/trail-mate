#pragma once

#include "chat/domain/chat_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace chat::runtime
{

constexpr std::size_t kProtocolPayloadMaxLen = 233;
constexpr std::size_t kProtocolPathMaxLen = 64;
constexpr std::size_t kProtocolPublicKeyMaxLen = 64;

template <std::size_t Capacity>
class BoundedBytes
{
  public:
    bool empty() const
    {
        return size_ == 0;
    }

    std::size_t size() const
    {
        return size_;
    }

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

    const uint8_t* data() const
    {
        return bytes_.data();
    }

    uint8_t* data()
    {
        return bytes_.data();
    }

    void clear()
    {
        size_ = 0;
    }

    bool assign(const uint8_t* data, std::size_t len)
    {
        if ((len > 0 && !data) || len > Capacity)
        {
            return false;
        }
        if (len > 0)
        {
            std::memcpy(bytes_.data(), data, len);
        }
        size_ = len;
        return true;
    }

    bool assign(std::initializer_list<uint8_t> values)
    {
        return assign(values.begin(), values.end());
    }

    bool assign(const std::vector<uint8_t>& values)
    {
        return assign(values.empty() ? nullptr : values.data(), values.size());
    }

    template <typename Iterator>
    bool assign(Iterator first, Iterator last)
    {
        clear();
        for (Iterator it = first; it != last; ++it)
        {
            if (size_ >= Capacity)
            {
                clear();
                return false;
            }
            bytes_[size_++] = static_cast<uint8_t>(*it);
        }
        return true;
    }

    bool push_back(uint8_t value)
    {
        if (size_ >= Capacity)
        {
            return false;
        }
        bytes_[size_++] = value;
        return true;
    }

    uint8_t& operator[](std::size_t index)
    {
        return bytes_[index];
    }

    const uint8_t& operator[](std::size_t index) const
    {
        return bytes_[index];
    }

    uint8_t* begin()
    {
        return bytes_.data();
    }

    uint8_t* end()
    {
        return bytes_.data() + size_;
    }

    const uint8_t* begin() const
    {
        return bytes_.data();
    }

    const uint8_t* end() const
    {
        return bytes_.data() + size_;
    }

    std::vector<uint8_t> toVector() const
    {
        return std::vector<uint8_t>(begin(), end());
    }

    operator std::vector<uint8_t>() const
    {
        return toVector();
    }

  private:
    std::array<uint8_t, Capacity> bytes_{};
    std::size_t size_ = 0;
};

using ProtocolPayloadBytes = BoundedBytes<kProtocolPayloadMaxLen>;
using ProtocolPathBytes = BoundedBytes<kProtocolPathMaxLen>;
using ProtocolPublicKeyBytes = BoundedBytes<kProtocolPublicKeyMaxLen>;

template <std::size_t Capacity>
inline bool operator==(const BoundedBytes<Capacity>& lhs, const BoundedBytes<Capacity>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        if (lhs[index] != rhs[index])
        {
            return false;
        }
    }
    return true;
}

template <std::size_t Capacity>
inline bool operator!=(const BoundedBytes<Capacity>& lhs, const BoundedBytes<Capacity>& rhs)
{
    return !(lhs == rhs);
}

template <std::size_t Capacity>
inline bool operator==(const std::vector<uint8_t>& lhs, const BoundedBytes<Capacity>& rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < lhs.size(); ++index)
    {
        if (lhs[index] != rhs[index])
        {
            return false;
        }
    }
    return true;
}

template <std::size_t Capacity>
inline bool operator==(const BoundedBytes<Capacity>& lhs, const std::vector<uint8_t>& rhs)
{
    return rhs == lhs;
}

template <std::size_t Capacity>
inline bool operator!=(const std::vector<uint8_t>& lhs, const BoundedBytes<Capacity>& rhs)
{
    return !(lhs == rhs);
}

template <std::size_t Capacity>
inline bool operator!=(const BoundedBytes<Capacity>& lhs, const std::vector<uint8_t>& rhs)
{
    return !(lhs == rhs);
}

enum class ProtocolActionKind : uint8_t
{
    Unknown = 0,
    SendText,
    RequestNodeInfo,
    TraceRoute,
    ExchangePosition,
    SharePosition,
    ShareWaypoint,
    PkiResync,
    Discover,
};

enum class ProtocolActionState : uint8_t
{
    Pending = 0,
    Delivered,
    Completed,
    Failed,
    TimedOut,
};

struct RuntimeContext
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    NodeId self_node = 0;
    uint32_t now_ms = 0;
    uint8_t meshcore_discover_node_type = 0;
    uint32_t meshcore_local_modified_epoch = 0;
    bool self_position_valid = false;
    double self_latitude_deg = 0.0;
    double self_longitude_deg = 0.0;
    bool self_has_altitude = false;
    double self_altitude_m = 0.0;
    bool self_has_speed = false;
    double self_speed_mps = 0.0;
    bool self_has_course = false;
    double self_course_deg = 0.0;
    uint32_t self_satellites = 0;
    uint32_t self_position_timestamp_s = 0;
};

struct SendTextIntent
{
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    MessageId message_id = 0;
    std::string text;
};

struct RequestNodeInfoIntent
{
    NodeId peer = 0;
    bool want_response = true;
};

struct TraceRouteIntent
{
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    MessageId request_id = 0;
    uint32_t auth = 0;
    uint8_t flags = 0;
    uint32_t timeout_ms = 15000;
};

struct ExchangePositionIntent
{
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    MessageId request_id = 0;
};

struct SharePositionIntent
{
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    bool has_altitude = false;
    double altitude_m = 0.0;
    bool has_speed = false;
    double speed_mps = 0.0;
    bool has_course = false;
    double course_deg = 0.0;
    uint32_t satellites = 0;
    uint32_t timestamp_s = 0;
    bool want_ack = false;
    bool want_response = false;
};

struct ShareWaypointIntent
{
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    bool valid = false;
    double latitude_deg = 0.0;
    double longitude_deg = 0.0;
    uint32_t id = 0;
    uint32_t expire = 0;
    uint32_t locked_to = 0;
    uint32_t icon = 0;
    std::string name;
    std::string description;
    bool want_ack = false;
    bool want_response = false;
};

struct DiscoverIntent
{
    MeshDiscoveryAction action = MeshDiscoveryAction::ScanLocal;
    uint32_t tag = 0;
    uint8_t type_filter = 0xFF;
    bool prefix_only = false;
    uint32_t since = 0;
    uint32_t rx_guard_ms = 5000;
};

struct StartKeyVerificationIntent
{
    NodeId peer = 0;
};

using ProtocolIntent = std::variant<SendTextIntent,
                                    RequestNodeInfoIntent,
                                    TraceRouteIntent,
                                    ExchangePositionIntent,
                                    SharePositionIntent,
                                    ShareWaypointIntent,
                                    DiscoverIntent,
                                    StartKeyVerificationIntent>;

struct IncomingPacket
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId from = 0;
    NodeId to = 0;
    MessageId packet_id = 0;
    MessageId request_id = 0;
    uint32_t portnum = 0;
    uint8_t payload_type = 0;
    bool want_response = false;
    bool encrypted = false;
    ProtocolPayloadBytes payload;
    ProtocolPathBytes path;
    RxMeta rx_meta{};
};

enum class PacketHandling : uint8_t
{
    NotHandled = 0,
    HandledContinue,
    HandledStop,
    DropWithEffects,
};

struct TxResult
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    MessageId request_id = 0;
    NodeId peer = 0;
    bool ok = false;
    int32_t detail = 0;
};

struct SendTextEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    MessageId message_id = 0;
    std::string text;
};

struct SendPacketEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId dest = 0;
    uint32_t portnum = 0;
    MessageId request_id = 0;
    MessageId response_request_id = 0;
    bool want_ack = false;
    bool want_response = false;
    ProtocolPayloadBytes payload;
};

struct SendNodeInfoEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    bool want_response = false;
};

struct SendRoutingErrorEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    MessageId request_id = 0;
    int32_t error_code = 0;
};

struct SendTraceRouteEffect
{
    MeshProtocol protocol = MeshProtocol::MeshCore;
    NodeId peer = 0;
    MessageId request_id = 0;
    uint32_t auth = 0;
    uint8_t flags = 0;
    uint32_t timeout_ms = 0;
};

struct SendDiscoverRequestEffect
{
    MeshProtocol protocol = MeshProtocol::MeshCore;
    uint32_t tag = 0;
    uint8_t type_filter = 0xFF;
    bool prefix_only = false;
    uint32_t since = 0;
    uint32_t rx_guard_ms = 5000;
};

struct SendDiscoverResponseEffect
{
    MeshProtocol protocol = MeshProtocol::MeshCore;
    uint32_t tag = 0;
    bool prefix_only = false;
};

struct SendSelfAnnouncementEffect
{
    MeshProtocol protocol = MeshProtocol::MeshCore;
    bool broadcast = true;
    bool include_location = false;
    int32_t lat_i6 = 0;
    int32_t lon_i6 = 0;
};

struct ForgetPeerKeyEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    NodeId peer = 0;
};

struct RequestPeerNodeInfoEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId peer = 0;
    bool want_response = true;
};

struct PublishIncomingTextEffect
{
    MeshIncomingText text{};
};

struct PublishIncomingDataEffect
{
    MeshIncomingData data{};
};

struct PublishNodeInfoEffect
{
    MeshProtocol protocol = MeshProtocol::MeshCore;
    ChannelId channel = ChannelId::PRIMARY;
    NodeId node_id = 0;
    std::string short_name;
    std::string long_name;
    uint32_t timestamp = 0;
    uint8_t role = 0;
    uint8_t hops = 0;
    RxMeta rx_meta{};
    bool has_public_key = false;
    bool key_manually_verified = false;
};

struct EmitActionResultEffect
{
    MeshProtocol protocol = MeshProtocol::Meshtastic;
    ProtocolActionKind action = ProtocolActionKind::Unknown;
    ProtocolActionState state = ProtocolActionState::Pending;
    NodeId peer = 0;
    MessageId request_id = 0;
    MessageId message_id = 0;
    int32_t detail = 0;
};

struct UpdatePeerRouteEffect
{
    MeshProtocol protocol = MeshProtocol::MeshCore;
    NodeId peer = 0;
    uint8_t peer_hash = 0;
    uint8_t next_hop = 0;
    ChannelId preferred_channel = ChannelId::PRIMARY;
    ProtocolPublicKeyBytes public_key;
    bool public_key_verified = false;
    uint8_t hops = 0xFF;
    MessageId tag = 0;
    ProtocolPayloadBytes payload;
};

using ProtocolEffect = std::variant<SendTextEffect,
                                    SendPacketEffect,
                                    SendNodeInfoEffect,
                                    SendRoutingErrorEffect,
                                    SendTraceRouteEffect,
                                    SendDiscoverRequestEffect,
                                    SendDiscoverResponseEffect,
                                    SendSelfAnnouncementEffect,
                                    ForgetPeerKeyEffect,
                                    RequestPeerNodeInfoEffect,
                                    PublishIncomingTextEffect,
                                    PublishIncomingDataEffect,
                                    PublishNodeInfoEffect,
                                    EmitActionResultEffect,
                                    UpdatePeerRouteEffect>;

constexpr std::size_t kProtocolEffectsMaxItems = 8;
constexpr std::size_t kProtocolTxFeedbackEffectsMaxItems = 1;

template <std::size_t Capacity>
class FixedProtocolEffectList
{
  public:
    bool empty() const
    {
        return count_ == 0;
    }

    std::size_t size() const
    {
        return count_;
    }

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

    bool full() const
    {
        return count_ >= Capacity;
    }

    bool overflowed() const
    {
        return overflowed_;
    }

    void markOverflowed()
    {
        overflowed_ = true;
    }

    void clear()
    {
        count_ = 0;
        overflowed_ = false;
    }

    ProtocolEffect& operator[](std::size_t index)
    {
        return items_[index];
    }

    const ProtocolEffect& operator[](std::size_t index) const
    {
        return items_[index];
    }

    ProtocolEffect* begin()
    {
        return items_.data();
    }

    ProtocolEffect* end()
    {
        return items_.data() + count_;
    }

    const ProtocolEffect* begin() const
    {
        return items_.data();
    }

    const ProtocolEffect* end() const
    {
        return items_.data() + count_;
    }

    template <typename T>
    bool push_back(T&& effect)
    {
        if (full())
        {
            overflowed_ = true;
            return false;
        }
        items_[count_] = ProtocolEffect(std::forward<T>(effect));
        ++count_;
        return true;
    }

    template <typename T>
    bool emplace_back(T&& effect)
    {
        return push_back(std::forward<T>(effect));
    }

  private:
    std::array<ProtocolEffect, Capacity> items_{};
    std::size_t count_ = 0;
    bool overflowed_ = false;
};

template <std::size_t Capacity>
struct ProtocolEffectBatch
{
    FixedProtocolEffectList<Capacity> items;

    bool empty() const
    {
        return items.empty();
    }

    std::size_t size() const
    {
        return items.size();
    }

    bool full() const
    {
        return items.full();
    }

    bool overflowed() const
    {
        return items.overflowed();
    }

    void clear()
    {
        items.clear();
    }

    void markOverflowed()
    {
        items.markOverflowed();
    }

    template <typename T>
    bool add(T&& effect)
    {
        return items.push_back(std::forward<T>(effect));
    }
};

using ProtocolEffects = ProtocolEffectBatch<kProtocolEffectsMaxItems>;
using ProtocolTxFeedbackEffects =
    ProtocolEffectBatch<kProtocolTxFeedbackEffectsMaxItems>;

struct ProtocolEffectWorkspace
{
    ProtocolEffects primary{};
    ProtocolTxFeedbackEffects feedback{};
};

struct IncomingPacketHandlingResult
{
    PacketHandling handling = PacketHandling::NotHandled;

    bool handled() const
    {
        return handling != PacketHandling::NotHandled;
    }

    bool shouldStop() const
    {
        return handling == PacketHandling::HandledStop ||
               handling == PacketHandling::DropWithEffects;
    }
};

template <std::size_t TargetCapacity, std::size_t SourceCapacity>
inline void appendProtocolEffects(ProtocolEffectBatch<TargetCapacity>& target,
                                  const ProtocolEffectBatch<SourceCapacity>& source)
{
    if (source.overflowed())
    {
        target.markOverflowed();
    }
    for (const auto& effect : source.items)
    {
        (void)target.add(effect);
    }
}

inline bool absorbIncomingHandlingResult(IncomingPacketHandlingResult& target,
                                         IncomingPacketHandlingResult source)
{
    const PacketHandling handling = source.handling;
    if (handling == PacketHandling::NotHandled)
    {
        return false;
    }
    target.handling = handling;
    return target.shouldStop();
}

template <typename Visitor>
decltype(auto) visitProtocolEffect(ProtocolEffect& effect, Visitor&& visitor)
{
    return std::visit(std::forward<Visitor>(visitor), effect);
}

template <typename Visitor>
decltype(auto) visitProtocolEffect(const ProtocolEffect& effect, Visitor&& visitor)
{
    return std::visit(std::forward<Visitor>(visitor), effect);
}

class IProtocolRuntime
{
  public:
    virtual ~IProtocolRuntime() = default;

    virtual void prepareOutgoing(const ProtocolIntent& intent,
                                 const RuntimeContext& context,
                                 ProtocolEffects& effects) = 0;
    virtual void handleIncoming(const IncomingPacket& packet,
                                const RuntimeContext& context,
                                ProtocolEffects& effects) = 0;
    virtual IncomingPacketHandlingResult handleIncomingPacket(
        const IncomingPacket& packet,
        const RuntimeContext& context,
        ProtocolEffects& effects)
    {
        const std::size_t before = effects.size();
        IncomingPacketHandlingResult result{};
        handleIncoming(packet, context, effects);
        result.handling = effects.size() == before
                              ? PacketHandling::NotHandled
                              : PacketHandling::HandledStop;
        return result;
    }
    virtual void handleTxResult(const TxResult& result,
                                const RuntimeContext& context,
                                ProtocolTxFeedbackEffects& effects) = 0;
    virtual void tick(const RuntimeContext& context, ProtocolEffects& effects) = 0;
};

class IProtocolEffectExecutor
{
  public:
    virtual ~IProtocolEffectExecutor() = default;
    virtual bool execute(const ProtocolEffect& effect) = 0;
};

} // namespace chat::runtime
