#include "ui/runtime/ui_feedback.h"

#include "lvgl.h"
#include "sys/feedback_runtime.h"
#include "ui/widgets/system_notification.h"

#include <atomic>
#include <cstddef>

namespace ui::feedback
{
namespace
{

constexpr size_t kNoticeQueueCapacity = 8;
constexpr uint32_t kDrainPeriodMs = 20;

class LvglSystemNotificationPresenter final : public IFeedbackPresenter
{
  public:
    void init() override
    {
        ::ui::SystemNotification::init();
    }

    void show_notice(const NoticeIntent& intent) override
    {
        ::ui::SystemNotification::show(intent.text, intent.duration_ms);
    }

    void hide_notice() override
    {
        ::ui::SystemNotification::hide();
    }
};

LvglSystemNotificationPresenter s_default_presenter;
IFeedbackPresenter* s_presenter = &s_default_presenter;
bool s_ready = false;
lv_timer_t* s_drain_timer = nullptr;
std::atomic_flag s_queue_lock = ATOMIC_FLAG_INIT;
std::atomic<uint32_t> s_hide_requests{0};
sys::runtime::FeedbackEvent s_last_feedback_event{};

IFeedbackPresenter& active_presenter();
void ensure_presenter_ready();

class QueueLock
{
  public:
    QueueLock()
    {
        while (s_queue_lock.test_and_set(std::memory_order_acquire))
        {
        }
    }

    ~QueueLock()
    {
        s_queue_lock.clear(std::memory_order_release);
    }

    QueueLock(const QueueLock&) = delete;
    QueueLock& operator=(const QueueLock&) = delete;
};

sys::runtime::NoticeSeverity to_runtime_severity(Severity severity)
{
    switch (severity)
    {
    case Severity::Success:
        return sys::runtime::NoticeSeverity::Success;
    case Severity::Warning:
        return sys::runtime::NoticeSeverity::Warning;
    case Severity::Error:
        return sys::runtime::NoticeSeverity::Error;
    case Severity::Info:
    default:
        return sys::runtime::NoticeSeverity::Info;
    }
}

Severity from_runtime_severity(sys::runtime::NoticeSeverity severity)
{
    switch (severity)
    {
    case sys::runtime::NoticeSeverity::Success:
        return Severity::Success;
    case sys::runtime::NoticeSeverity::Warning:
        return Severity::Warning;
    case sys::runtime::NoticeSeverity::Error:
        return Severity::Error;
    case sys::runtime::NoticeSeverity::Info:
    default:
        return Severity::Info;
    }
}

class RuntimeFeedbackEventSink final : public sys::runtime::IFeedbackEventSink
{
  public:
    bool publish(const sys::runtime::FeedbackEvent& event) override
    {
        s_last_feedback_event = event;
        return true;
    }
};

class RuntimeFeedbackPresenter final : public sys::runtime::IFeedbackPresenter
{
  public:
    bool present(const sys::runtime::NoticeIntent& intent) override
    {
        ensure_presenter_ready();
        NoticeIntent ui_intent{};
        ui_intent.text = intent.message;
        ui_intent.duration_ms = intent.duration_ms;
        ui_intent.severity = from_runtime_severity(intent.severity);
        active_presenter().show_notice(ui_intent);
        return true;
    }
};

sys::runtime::FeedbackQueue<kNoticeQueueCapacity> s_feedback_queue;
sys::runtime::DefaultFeedbackPolicy s_feedback_policy;
RuntimeFeedbackEventSink s_feedback_events;
RuntimeFeedbackPresenter s_feedback_presenter;
sys::runtime::FeedbackRuntime<kNoticeQueueCapacity> s_feedback_runtime(s_feedback_queue,
                                                                       s_feedback_policy,
                                                                       s_feedback_presenter,
                                                                       s_feedback_events);

IFeedbackPresenter& active_presenter()
{
    if (!s_presenter)
    {
        s_presenter = &s_default_presenter;
    }
    return *s_presenter;
}

void ensure_presenter_ready()
{
    if (s_ready)
    {
        return;
    }
    active_presenter().init();
    s_ready = true;
}

void drain_queued_notices()
{
    QueueLock lock;
    (void)s_feedback_runtime.drainToUi(lv_tick_get());
    const uint32_t hide_count = s_hide_requests.exchange(0, std::memory_order_acq_rel);
    if (hide_count > 0)
    {
        ensure_presenter_ready();
        active_presenter().hide_notice();
    }
}

void drain_timer_cb(lv_timer_t* /*timer*/)
{
    drain_queued_notices();
}

void ensure_drain_timer()
{
    if (s_drain_timer)
    {
        return;
    }
    s_drain_timer = lv_timer_create(drain_timer_cb, kDrainPeriodMs, nullptr);
    if (s_drain_timer)
    {
        lv_timer_set_repeat_count(s_drain_timer, -1);
    }
}

} // namespace

void set_presenter(IFeedbackPresenter* presenter)
{
    s_presenter = presenter ? presenter : &s_default_presenter;
    s_ready = false;
}

IFeedbackPresenter& presenter()
{
    return active_presenter();
}

void init()
{
    ensure_presenter_ready();
    ensure_drain_timer();
    drain_queued_notices();
}

bool is_ready()
{
    return s_ready;
}

bool show_notice(const NoticeIntent& intent)
{
    sys::runtime::NoticeIntent runtime_intent{};
    sys::runtime::setNoticeMessage(runtime_intent, intent.text);
    runtime_intent.category = sys::runtime::NoticeCategory::General;
    runtime_intent.severity = to_runtime_severity(intent.severity);
    runtime_intent.duration_ms = intent.duration_ms;
    runtime_intent.created_at_ms = lv_tick_get();

    QueueLock lock;
    return s_feedback_runtime.post(runtime_intent);
}

bool show_notice(const char* text, uint32_t duration_ms)
{
    NoticeIntent intent{};
    intent.text = text ? text : "";
    intent.duration_ms = duration_ms;
    intent.severity = Severity::Info;
    return show_notice(intent);
}

bool hide_notice()
{
    s_hide_requests.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

} // namespace ui::feedback
