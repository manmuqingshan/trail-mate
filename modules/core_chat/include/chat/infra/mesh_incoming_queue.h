/**
 * @file mesh_incoming_queue.h
 * @brief Fixed-slot queues for app-facing incoming mesh payloads.
 */

#pragma once

#include "chat/domain/chat_types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat
{
namespace infra
{

constexpr std::size_t kIncomingTextMaxLen = 255;
constexpr std::size_t kIncomingDataPayloadMaxLen = 233;

enum class IncomingQueuePriority : uint8_t
{
    P0Critical = 0,
    P1User = 1,
    P2LatestState = 2,
    P3Bulk = 3
};

enum class IncomingQueueKind : uint8_t
{
    Text,
    Data
};

struct IncomingQueuePushReport
{
    bool dropped_existing = false;
    bool dropped_new = false;
    IncomingQueueKind kind = IncomingQueueKind::Data;
    IncomingQueuePriority dropped_priority = IncomingQueuePriority::P3Bulk;
};

template <std::size_t Capacity, std::size_t MaxTextLen = kIncomingTextMaxLen>
class IncomingTextQueue
{
  public:
    bool empty() const { return count_ == 0; }
    std::size_t size() const { return count_; }
    constexpr std::size_t capacity() const { return Capacity; }

    void clear()
    {
        for (auto& slot : slots_)
        {
            slot.used = false;
            slot.text_len = 0;
        }
        count_ = 0;
    }

    bool push(const MeshIncomingText& message,
              IncomingQueuePriority priority,
              IncomingQueuePushReport* report = nullptr)
    {
        return push(message,
                    message.text.data(),
                    message.text.size(),
                    priority,
                    report);
    }

    bool push(const MeshIncomingText& metadata,
              const char* text,
              std::size_t text_len,
              IncomingQueuePriority priority,
              IncomingQueuePushReport* report = nullptr)
    {
        resetReport(report, IncomingQueueKind::Text);
        if ((text_len > 0 && !text) || text_len > MaxTextLen)
        {
            markDropNew(report, priority);
            return false;
        }

        bool replaced = false;
        std::size_t index = 0;
        if (!findWritableSlot(priority, &index, &replaced))
        {
            markDropNew(report, priority);
            return false;
        }

        auto& slot = slots_[index];
        if (replaced)
        {
            markDropExisting(report, slot.priority);
        }
        else
        {
            ++count_;
        }

        slot.used = true;
        slot.priority = priority;
        slot.sequence = nextSequence();
        slot.channel = metadata.channel;
        slot.from = metadata.from;
        slot.to = metadata.to;
        slot.msg_id = metadata.msg_id;
        slot.timestamp = metadata.timestamp;
        slot.hop_limit = metadata.hop_limit;
        slot.encrypted = metadata.encrypted;
        slot.reticulum_identity = metadata.reticulum_identity;
        slot.rx_meta = metadata.rx_meta;
        slot.text_len = text_len;
        if (text_len > 0)
        {
            std::memcpy(slot.text.data(), text, text_len);
        }
        slot.text[text_len] = '\0';
        return true;
    }

    bool pop(MeshIncomingText* out)
    {
        if (!out || count_ == 0)
        {
            return false;
        }

        const std::size_t index = oldestSlot();
        const auto& slot = slots_[index];
        out->channel = slot.channel;
        out->from = slot.from;
        out->to = slot.to;
        out->msg_id = slot.msg_id;
        out->timestamp = slot.timestamp;
        out->hop_limit = slot.hop_limit;
        out->encrypted = slot.encrypted;
        out->reticulum_identity = slot.reticulum_identity;
        out->rx_meta = slot.rx_meta;
        out->text.assign(slot.text.data(), slot.text_len);

        slots_[index].used = false;
        slots_[index].text_len = 0;
        --count_;
        return true;
    }

  private:
    struct Slot
    {
        bool used = false;
        IncomingQueuePriority priority = IncomingQueuePriority::P3Bulk;
        uint32_t sequence = 0;
        ChannelId channel = ChannelId::PRIMARY;
        NodeId from = 0;
        NodeId to = 0;
        MessageId msg_id = 0;
        uint32_t timestamp = 0;
        uint8_t hop_limit = 0xFF;
        bool encrypted = false;
        ReticulumPeerIdentity reticulum_identity{};
        RxMeta rx_meta{};
        std::array<char, MaxTextLen + 1> text{};
        std::size_t text_len = 0;
    };

    static void resetReport(IncomingQueuePushReport* report, IncomingQueueKind kind)
    {
        if (report)
        {
            *report = IncomingQueuePushReport{};
            report->kind = kind;
        }
    }

    static void markDropNew(IncomingQueuePushReport* report, IncomingQueuePriority priority)
    {
        if (report)
        {
            report->dropped_new = true;
            report->dropped_priority = priority;
        }
    }

    static void markDropExisting(IncomingQueuePushReport* report, IncomingQueuePriority priority)
    {
        if (report)
        {
            report->dropped_existing = true;
            report->dropped_priority = priority;
        }
    }

    static uint8_t rank(IncomingQueuePriority priority)
    {
        return static_cast<uint8_t>(priority);
    }

    bool findWritableSlot(IncomingQueuePriority priority,
                          std::size_t* out_index,
                          bool* out_replaced) const
    {
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            if (!slots_[i].used)
            {
                *out_index = i;
                *out_replaced = false;
                return true;
            }
        }

        bool found = false;
        std::size_t victim = 0;
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            if (!canEvict(priority, slots_[i].priority))
            {
                continue;
            }
            if (!found ||
                rank(slots_[i].priority) > rank(slots_[victim].priority) ||
                (rank(slots_[i].priority) == rank(slots_[victim].priority) &&
                 slots_[i].sequence < slots_[victim].sequence))
            {
                victim = i;
                found = true;
            }
        }

        if (!found)
        {
            return false;
        }
        *out_index = victim;
        *out_replaced = true;
        return true;
    }

    static bool canEvict(IncomingQueuePriority incoming, IncomingQueuePriority existing)
    {
        if (existing == IncomingQueuePriority::P0Critical)
        {
            return false;
        }
        return rank(existing) >= rank(incoming);
    }

    std::size_t oldestSlot() const
    {
        std::size_t index = 0;
        bool found = false;
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            if (!slots_[i].used)
            {
                continue;
            }
            if (!found || slots_[i].sequence < slots_[index].sequence)
            {
                index = i;
                found = true;
            }
        }
        return index;
    }

    uint32_t nextSequence()
    {
        const uint32_t sequence = next_sequence_;
        ++next_sequence_;
        if (next_sequence_ == 0)
        {
            next_sequence_ = 1;
        }
        return sequence;
    }

    std::array<Slot, Capacity> slots_{};
    std::size_t count_ = 0;
    uint32_t next_sequence_ = 1;
};

template <std::size_t Capacity, std::size_t MaxPayloadLen = kIncomingDataPayloadMaxLen>
class IncomingDataQueue
{
  public:
    bool empty() const { return count_ == 0; }
    std::size_t size() const { return count_; }
    constexpr std::size_t capacity() const { return Capacity; }

    void clear()
    {
        for (auto& slot : slots_)
        {
            slot.used = false;
            slot.payload_len = 0;
        }
        count_ = 0;
    }

    bool push(const MeshIncomingData& message,
              IncomingQueuePriority priority,
              IncomingQueuePushReport* report = nullptr)
    {
        return push(message,
                    message.payload.empty() ? nullptr : message.payload.data(),
                    message.payload.size(),
                    priority,
                    report);
    }

    bool push(const MeshIncomingData& metadata,
              const uint8_t* payload,
              std::size_t payload_len,
              IncomingQueuePriority priority,
              IncomingQueuePushReport* report = nullptr)
    {
        resetReport(report, IncomingQueueKind::Data);
        if ((payload_len > 0 && !payload) || payload_len > MaxPayloadLen)
        {
            markDropNew(report, priority);
            return false;
        }

        bool replaced = false;
        std::size_t index = 0;
        if (!findWritableSlot(priority, &index, &replaced))
        {
            markDropNew(report, priority);
            return false;
        }

        auto& slot = slots_[index];
        if (replaced)
        {
            markDropExisting(report, slot.priority);
        }
        else
        {
            ++count_;
        }

        slot.used = true;
        slot.priority = priority;
        slot.sequence = nextSequence();
        slot.portnum = metadata.portnum;
        slot.from = metadata.from;
        slot.to = metadata.to;
        slot.packet_id = metadata.packet_id;
        slot.request_id = metadata.request_id;
        slot.channel = metadata.channel;
        slot.channel_hash = metadata.channel_hash;
        slot.hop_limit = metadata.hop_limit;
        slot.want_response = metadata.want_response;
        slot.rx_meta = metadata.rx_meta;
        slot.payload_len = payload_len;
        if (payload_len > 0)
        {
            std::memcpy(slot.payload.data(), payload, payload_len);
        }
        return true;
    }

    bool pop(MeshIncomingData* out)
    {
        if (!out || count_ == 0)
        {
            return false;
        }

        const std::size_t index = oldestSlot();
        const auto& slot = slots_[index];
        out->portnum = slot.portnum;
        out->from = slot.from;
        out->to = slot.to;
        out->packet_id = slot.packet_id;
        out->request_id = slot.request_id;
        out->channel = slot.channel;
        out->channel_hash = slot.channel_hash;
        out->hop_limit = slot.hop_limit;
        out->want_response = slot.want_response;
        out->rx_meta = slot.rx_meta;
        out->payload.assign(slot.payload.begin(), slot.payload.begin() + slot.payload_len);

        slots_[index].used = false;
        slots_[index].payload_len = 0;
        --count_;
        return true;
    }

  private:
    struct Slot
    {
        bool used = false;
        IncomingQueuePriority priority = IncomingQueuePriority::P3Bulk;
        uint32_t sequence = 0;
        uint32_t portnum = 0;
        NodeId from = 0;
        NodeId to = 0;
        MessageId packet_id = 0;
        uint32_t request_id = 0;
        ChannelId channel = ChannelId::PRIMARY;
        uint8_t channel_hash = 0;
        uint8_t hop_limit = 0xFF;
        bool want_response = false;
        RxMeta rx_meta{};
        std::array<uint8_t, MaxPayloadLen> payload{};
        std::size_t payload_len = 0;
    };

    static void resetReport(IncomingQueuePushReport* report, IncomingQueueKind kind)
    {
        if (report)
        {
            *report = IncomingQueuePushReport{};
            report->kind = kind;
        }
    }

    static void markDropNew(IncomingQueuePushReport* report, IncomingQueuePriority priority)
    {
        if (report)
        {
            report->dropped_new = true;
            report->dropped_priority = priority;
        }
    }

    static void markDropExisting(IncomingQueuePushReport* report, IncomingQueuePriority priority)
    {
        if (report)
        {
            report->dropped_existing = true;
            report->dropped_priority = priority;
        }
    }

    static uint8_t rank(IncomingQueuePriority priority)
    {
        return static_cast<uint8_t>(priority);
    }

    bool findWritableSlot(IncomingQueuePriority priority,
                          std::size_t* out_index,
                          bool* out_replaced) const
    {
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            if (!slots_[i].used)
            {
                *out_index = i;
                *out_replaced = false;
                return true;
            }
        }

        bool found = false;
        std::size_t victim = 0;
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            if (!canEvict(priority, slots_[i].priority))
            {
                continue;
            }
            if (!found ||
                rank(slots_[i].priority) > rank(slots_[victim].priority) ||
                (rank(slots_[i].priority) == rank(slots_[victim].priority) &&
                 slots_[i].sequence < slots_[victim].sequence))
            {
                victim = i;
                found = true;
            }
        }

        if (!found)
        {
            return false;
        }
        *out_index = victim;
        *out_replaced = true;
        return true;
    }

    static bool canEvict(IncomingQueuePriority incoming, IncomingQueuePriority existing)
    {
        if (existing == IncomingQueuePriority::P0Critical)
        {
            return false;
        }
        return rank(existing) >= rank(incoming);
    }

    std::size_t oldestSlot() const
    {
        std::size_t index = 0;
        bool found = false;
        for (std::size_t i = 0; i < slots_.size(); ++i)
        {
            if (!slots_[i].used)
            {
                continue;
            }
            if (!found || slots_[i].sequence < slots_[index].sequence)
            {
                index = i;
                found = true;
            }
        }
        return index;
    }

    uint32_t nextSequence()
    {
        const uint32_t sequence = next_sequence_;
        ++next_sequence_;
        if (next_sequence_ == 0)
        {
            next_sequence_ = 1;
        }
        return sequence;
    }

    std::array<Slot, Capacity> slots_{};
    std::size_t count_ = 0;
    uint32_t next_sequence_ = 1;
};

} // namespace infra
} // namespace chat
