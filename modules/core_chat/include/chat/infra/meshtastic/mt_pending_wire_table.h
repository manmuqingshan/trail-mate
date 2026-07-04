#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat::meshtastic
{

enum class PendingWirePriority : uint8_t
{
    P0 = 0,
    P1 = 1,
    P2 = 2,
    P3 = 3,
};

struct PendingWirePushReport
{
    enum class Result : uint8_t
    {
        Stored,
        Replaced,
        DroppedExisting,
        DroppedNew,
    };

    Result result = Result::Stored;
    uint64_t dropped_key = 0;
    PendingWirePriority dropped_priority = PendingWirePriority::P3;
};

template <typename Metadata, std::size_t Capacity, std::size_t MaxWireLen>
class PendingWireTable
{
  public:
    struct Slot
    {
        bool used = false;
        uint64_t key = 0;
        PendingWirePriority priority = PendingWirePriority::P3;
        uint32_t sequence = 0;
        std::array<uint8_t, MaxWireLen> wire{};
        std::size_t wire_size = 0;
        Metadata meta{};

        void clear()
        {
            used = false;
            key = 0;
            priority = PendingWirePriority::P3;
            sequence = 0;
            wire_size = 0;
            meta = Metadata{};
        }
    };

    void clear()
    {
        for (auto& slot : slots_)
        {
            slot.clear();
        }
        count_ = 0;
        next_sequence_ = 1;
    }

    constexpr std::size_t capacity() const
    {
        return Capacity;
    }

    constexpr std::size_t maxWireLen() const
    {
        return MaxWireLen;
    }

    std::size_t size() const
    {
        return count_;
    }

    Slot* slotAt(std::size_t index)
    {
        return index < Capacity ? &slots_[index] : nullptr;
    }

    const Slot* slotAt(std::size_t index) const
    {
        return index < Capacity ? &slots_[index] : nullptr;
    }

    Slot* find(uint64_t key)
    {
        for (auto& slot : slots_)
        {
            if (slot.used && slot.key == key)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    const Slot* find(uint64_t key) const
    {
        for (const auto& slot : slots_)
        {
            if (slot.used && slot.key == key)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    bool erase(uint64_t key)
    {
        for (std::size_t index = 0; index < Capacity; ++index)
        {
            const auto& slot = slots_[index];
            if (slot.used && slot.key == key)
            {
                eraseAt(index);
                return true;
            }
        }
        return false;
    }

    bool eraseAt(std::size_t index)
    {
        if (index >= Capacity || !slots_[index].used)
        {
            return false;
        }
        slots_[index].clear();
        --count_;
        return true;
    }

    Slot* upsert(uint64_t key,
                 PendingWirePriority priority,
                 const uint8_t* wire,
                 std::size_t wire_size,
                 const Metadata& meta,
                 PendingWirePushReport* report = nullptr)
    {
        PendingWirePushReport local_report{};
        if (!wire || wire_size == 0 || wire_size > MaxWireLen)
        {
            local_report.result = PendingWirePushReport::Result::DroppedNew;
            if (report)
            {
                *report = local_report;
            }
            return nullptr;
        }

        if (Slot* existing = find(key))
        {
            storeInSlot(*existing, key, priority, wire, wire_size, meta, false);
            local_report.result = PendingWirePushReport::Result::Replaced;
            if (report)
            {
                *report = local_report;
            }
            return existing;
        }

        if (Slot* free_slot = findFreeSlot())
        {
            storeInSlot(*free_slot, key, priority, wire, wire_size, meta, true);
            ++count_;
            local_report.result = PendingWirePushReport::Result::Stored;
            if (report)
            {
                *report = local_report;
            }
            return free_slot;
        }

        Slot* victim = findDropVictim(priority);
        if (!victim)
        {
            local_report.result = PendingWirePushReport::Result::DroppedNew;
            if (report)
            {
                *report = local_report;
            }
            return nullptr;
        }

        local_report.result = PendingWirePushReport::Result::DroppedExisting;
        local_report.dropped_key = victim->key;
        local_report.dropped_priority = victim->priority;
        storeInSlot(*victim, key, priority, wire, wire_size, meta, true);
        if (report)
        {
            *report = local_report;
        }
        return victim;
    }

  private:
    static uint8_t rank(PendingWirePriority priority)
    {
        return static_cast<uint8_t>(priority);
    }

    Slot* findFreeSlot()
    {
        for (auto& slot : slots_)
        {
            if (!slot.used)
            {
                return &slot;
            }
        }
        return nullptr;
    }

    Slot* findDropVictim(PendingWirePriority incoming)
    {
        Slot* victim = nullptr;
        for (auto& slot : slots_)
        {
            if (!slot.used || rank(slot.priority) <= rank(incoming))
            {
                continue;
            }
            if (!victim || rank(slot.priority) > rank(victim->priority) ||
                (slot.priority == victim->priority && slot.sequence < victim->sequence))
            {
                victim = &slot;
            }
        }
        return victim;
    }

    void storeInSlot(Slot& slot,
                     uint64_t key,
                     PendingWirePriority priority,
                     const uint8_t* wire,
                     std::size_t wire_size,
                     const Metadata& meta,
                     bool new_sequence)
    {
        slot.used = true;
        slot.key = key;
        slot.priority = priority;
        if (new_sequence)
        {
            slot.sequence = next_sequence_++;
            if (next_sequence_ == 0)
            {
                next_sequence_ = 1;
            }
        }
        slot.wire_size = wire_size;
        slot.meta = meta;
        std::memcpy(slot.wire.data(), wire, wire_size);
    }

    std::array<Slot, Capacity> slots_{};
    std::size_t count_ = 0;
    uint32_t next_sequence_ = 1;
};

} // namespace chat::meshtastic
