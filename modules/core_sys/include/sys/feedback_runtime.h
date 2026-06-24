#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sys
{
namespace runtime
{

enum class NoticeCategory : uint8_t
{
    General,
    ChatDelivery,
    Storage,
    Tracker,
    Protocol,
};

enum class NoticeSeverity : uint8_t
{
    Info,
    Success,
    Warning,
    Error,
};

enum class FeedbackEventKind : uint8_t
{
    Posted,
    Presented,
    Dropped,
    Deduped,
};

struct NoticeIntent
{
    static constexpr std::size_t kMaxMessageBytes = 192;

    uint32_t notice_id = 0;
    NoticeCategory category = NoticeCategory::General;
    NoticeSeverity severity = NoticeSeverity::Info;
    char message[kMaxMessageBytes]{};
    uint32_t duration_ms = 0;
    uint32_t dedupe_key = 0;
    uint32_t created_at_ms = 0;
};

inline void setNoticeMessage(NoticeIntent& intent, const char* message)
{
    std::strncpy(intent.message, message ? message : "", NoticeIntent::kMaxMessageBytes - 1);
    intent.message[NoticeIntent::kMaxMessageBytes - 1] = '\0';
}

struct FeedbackEvent
{
    FeedbackEventKind kind = FeedbackEventKind::Dropped;
    uint32_t notice_id = 0;
    uint32_t timestamp_ms = 0;
    int32_t error = 0;
};

class FeedbackPolicy
{
  public:
    virtual ~FeedbackPolicy() = default;

    virtual bool shouldShow(const NoticeIntent& intent) const = 0;
    virtual bool dedupe(const NoticeIntent& previous, const NoticeIntent& next) const = 0;
    virtual uint32_t duration(const NoticeIntent& intent) const = 0;
};

class DefaultFeedbackPolicy : public FeedbackPolicy
{
  public:
    bool shouldShow(const NoticeIntent& intent) const override
    {
        return intent.message[0] != '\0';
    }

    bool dedupe(const NoticeIntent& previous, const NoticeIntent& next) const override
    {
        if (next.dedupe_key != 0 && previous.dedupe_key == next.dedupe_key)
        {
            return true;
        }
        return previous.category == next.category && previous.severity == next.severity &&
               std::strcmp(previous.message, next.message) == 0;
    }

    uint32_t duration(const NoticeIntent& intent) const override
    {
        if (intent.duration_ms != 0)
        {
            return intent.duration_ms;
        }
        return intent.severity == NoticeSeverity::Error ? 2200 : 1400;
    }
};

class IFeedbackPresenter
{
  public:
    virtual ~IFeedbackPresenter() = default;

    virtual bool present(const NoticeIntent& intent) = 0;
};

class IFeedbackEventSink
{
  public:
    virtual ~IFeedbackEventSink() = default;

    virtual bool publish(const FeedbackEvent& event) = 0;
};

template <std::size_t N>
class FeedbackQueue
{
  public:
    bool enqueue(const NoticeIntent& intent)
    {
        if (count_ >= N)
        {
            return false;
        }
        notices_[(head_ + count_) % N] = intent;
        ++count_;
        return true;
    }

    bool popReady(uint32_t now_ms, NoticeIntent& out)
    {
        (void)now_ms;
        if (count_ == 0)
        {
            return false;
        }
        out = notices_[head_];
        head_ = (head_ + 1) % N;
        --count_;
        return true;
    }

    bool dedupe(const NoticeIntent& intent, const FeedbackPolicy& policy)
    {
        for (std::size_t i = 0; i < count_; ++i)
        {
            NoticeIntent& queued = notices_[(head_ + i) % N];
            if (policy.dedupe(queued, intent))
            {
                queued = intent;
                return true;
            }
        }
        return false;
    }

    std::size_t size() const
    {
        return count_;
    }

  private:
    NoticeIntent notices_[N]{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

template <std::size_t N>
class FeedbackRuntime
{
  public:
    FeedbackRuntime(FeedbackQueue<N>& queue,
                    FeedbackPolicy& policy,
                    IFeedbackPresenter& presenter,
                    IFeedbackEventSink& events)
        : queue_(queue), policy_(policy), presenter_(presenter), events_(events)
    {
    }

    bool post(NoticeIntent intent)
    {
        if (!policy_.shouldShow(intent))
        {
            publish(FeedbackEventKind::Dropped, intent.notice_id, intent.created_at_ms, -1);
            return false;
        }

        if (intent.notice_id == 0)
        {
            intent.notice_id = next_notice_id_++;
        }
        intent.duration_ms = policy_.duration(intent);

        if (queue_.dedupe(intent, policy_))
        {
            publish(FeedbackEventKind::Deduped, intent.notice_id, intent.created_at_ms, 0);
            return true;
        }

        const bool queued = queue_.enqueue(intent);
        publish(queued ? FeedbackEventKind::Posted : FeedbackEventKind::Dropped,
                intent.notice_id,
                intent.created_at_ms,
                queued ? 0 : -2);
        return queued;
    }

    void handle(const FeedbackEvent& event)
    {
        last_event_ = event;
    }

    std::size_t drainToUi(uint32_t now_ms)
    {
        std::size_t presented = 0;
        NoticeIntent intent{};
        while (queue_.popReady(now_ms, intent))
        {
            if (presenter_.present(intent))
            {
                ++presented;
                publish(FeedbackEventKind::Presented, intent.notice_id, now_ms, 0);
            }
            else
            {
                publish(FeedbackEventKind::Dropped, intent.notice_id, now_ms, -3);
            }
        }
        return presented;
    }

    FeedbackEvent lastEvent() const
    {
        return last_event_;
    }

  private:
    void publish(FeedbackEventKind kind, uint32_t notice_id, uint32_t timestamp_ms, int32_t error)
    {
        FeedbackEvent event{};
        event.kind = kind;
        event.notice_id = notice_id;
        event.timestamp_ms = timestamp_ms;
        event.error = error;
        last_event_ = event;
        (void)events_.publish(event);
    }

    FeedbackQueue<N>& queue_;
    FeedbackPolicy& policy_;
    IFeedbackPresenter& presenter_;
    IFeedbackEventSink& events_;
    FeedbackEvent last_event_{};
    uint32_t next_notice_id_ = 1;
};

} // namespace runtime
} // namespace sys
