#pragma once

#include "chat/domain/chat_types.h"
#include "sys/clock.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace sys
{

enum class EventType
{
    ChatSendResult,
    KeyVerificationNumberRequest,
    KeyVerificationNumberInform,
    KeyVerificationFinal,
};

struct Event
{
    EventType type;
    uint32_t timestamp;

    explicit Event(EventType t) : type(t), timestamp(sys::millis_now()) {}
    virtual ~Event() = default;
};

struct ChatSendResultEvent : public Event
{
    chat::MessageId msg_id;
    bool success;
    chat::MessageStatus status;

    ChatSendResultEvent(chat::MessageId id, bool ok)
        : Event(EventType::ChatSendResult), msg_id(id), success(ok),
          status(ok ? chat::MessageStatus::Sent : chat::MessageStatus::Failed) {}

    ChatSendResultEvent(chat::MessageId id, chat::MessageStatus result_status)
        : Event(EventType::ChatSendResult), msg_id(id),
          success(result_status != chat::MessageStatus::Failed),
          status(result_status) {}
};

struct KeyVerificationNumberRequestEvent : public Event
{
    uint32_t node_id;
    uint64_t nonce;

    KeyVerificationNumberRequestEvent(uint32_t id, uint64_t n)
        : Event(EventType::KeyVerificationNumberRequest), node_id(id), nonce(n) {}
};

struct KeyVerificationNumberInformEvent : public Event
{
    uint32_t node_id;
    uint64_t nonce;
    uint32_t security_number;

    KeyVerificationNumberInformEvent(uint32_t id, uint64_t n, uint32_t number)
        : Event(EventType::KeyVerificationNumberInform), node_id(id), nonce(n), security_number(number) {}
};

struct KeyVerificationFinalEvent : public Event
{
    uint32_t node_id;
    uint64_t nonce;
    bool is_sender;
    char verification_code[12];

    KeyVerificationFinalEvent(uint32_t id, uint64_t n, bool sender, const char* code)
        : Event(EventType::KeyVerificationFinal), node_id(id), nonce(n), is_sender(sender)
    {
        if (code)
        {
            std::strncpy(verification_code, code, sizeof(verification_code) - 1);
            verification_code[sizeof(verification_code) - 1] = '\0';
        }
        else
        {
            verification_code[0] = '\0';
        }
    }
};

class EventBus
{
  public:
    static constexpr size_t kMaxEvents = 32;

    static bool init(size_t queue_size = 32)
    {
        instance_.queue_size_ = normalizeQueueSize(queue_size);
        instance_.trimToCapacity();
        return true;
    }

    static bool publish(Event* event, uint32_t timeout_ms = 0)
    {
        (void)timeout_ms;
        if (!event)
        {
            return false;
        }
        while (instance_.event_count_ >= instance_.queue_size_)
        {
            instance_.dropOldest();
        }
        instance_.pushBack(event);
        return true;
    }

    static bool subscribe(Event** event_out, uint32_t timeout_ms = 0)
    {
        (void)timeout_ms;
        if (!event_out || instance_.event_count_ == 0)
        {
            return false;
        }
        *event_out = instance_.popFront();
        return true;
    }

    static size_t pendingCount()
    {
        return instance_.event_count_;
    }

    static void clear()
    {
        while (instance_.event_count_ > 0)
        {
            instance_.dropOldest();
        }
    }

  private:
    static size_t normalizeQueueSize(size_t queue_size)
    {
        if (queue_size == 0 || queue_size > kMaxEvents)
        {
            return kMaxEvents;
        }
        return queue_size;
    }

    void trimToCapacity()
    {
        while (event_count_ > queue_size_)
        {
            dropOldest();
        }
    }

    void dropOldest()
    {
        Event* event = events_[event_head_];
        events_[event_head_] = nullptr;
        event_head_ = (event_head_ + 1) % kMaxEvents;
        if (event_count_ > 0)
        {
            --event_count_;
        }
        delete event;
        if (event_count_ == 0)
        {
            event_head_ = 0;
        }
    }

    void pushBack(Event* event)
    {
        const size_t index = (event_head_ + event_count_) % kMaxEvents;
        events_[index] = event;
        ++event_count_;
    }

    Event* popFront()
    {
        Event* event = events_[event_head_];
        events_[event_head_] = nullptr;
        event_head_ = (event_head_ + 1) % kMaxEvents;
        --event_count_;
        if (event_count_ == 0)
        {
            event_head_ = 0;
        }
        return event;
    }

    std::array<Event*, kMaxEvents> events_{};
    size_t event_head_ = 0;
    size_t event_count_ = 0;
    size_t queue_size_ = 32;
    static EventBus instance_;
};

} // namespace sys
