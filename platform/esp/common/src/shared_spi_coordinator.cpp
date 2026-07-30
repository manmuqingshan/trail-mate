#include "platform/esp/common/shared_spi_coordinator.h"

#include "sys/clock.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform::esp::common
{
namespace
{

uint32_t remaining_ms(uint32_t now_ms, uint32_t deadline_ms)
{
    if (deadline_ms == 0U)
    {
        return 0U;
    }
    const int32_t remaining = static_cast<int32_t>(deadline_ms - now_ms);
    return remaining > 0 ? static_cast<uint32_t>(remaining) : 0U;
}

} // namespace

SharedSpiCoordinator::SharedSpiCoordinator()
{
    for (Waiter& waiter : waiters_)
    {
        waiter.wake = xSemaphoreCreateBinaryStatic(&waiter.wake_storage);
    }
    std::snprintf(owner_label_, sizeof(owner_label_), "-");
    std::snprintf(owner_task_name_, sizeof(owner_task_name_), "-");
}

uint8_t SharedSpiCoordinator::priorityFor(sys::runtime::BusAccessPolicy policy)
{
    switch (policy)
    {
    case sys::runtime::BusAccessPolicy::UiNeverBlock:
    case sys::runtime::BusAccessPolicy::DisplayFrameCritical:
        return 0U;
    case sys::runtime::BusAccessPolicy::InteractiveWorkerBounded:
        return 1U;
    case sys::runtime::BusAccessPolicy::DurableCommit:
        return 2U;
    case sys::runtime::BusAccessPolicy::RecoveryExclusive:
        return 3U;
    case sys::runtime::BusAccessPolicy::BackgroundWorkerBounded:
    default:
        return 4U;
    }
}

uint32_t SharedSpiCoordinator::timeoutFor(sys::runtime::BusAccessPolicy policy)
{
    switch (policy)
    {
    case sys::runtime::BusAccessPolicy::UiNeverBlock:
        return 0U;
    case sys::runtime::BusAccessPolicy::DisplayFrameCritical:
        return 45U;
    case sys::runtime::BusAccessPolicy::InteractiveWorkerBounded:
        return 200U;
    case sys::runtime::BusAccessPolicy::DurableCommit:
        return 250U;
    case sys::runtime::BusAccessPolicy::RecoveryExclusive:
        return 500U;
    case sys::runtime::BusAccessPolicy::BackgroundWorkerBounded:
    default:
        return 25U;
    }
}

const char* SharedSpiCoordinator::taskName(TaskHandle_t task)
{
    return task != nullptr ? pcTaskGetName(task) : "-";
}

int SharedSpiCoordinator::findBestWaiterLocked() const
{
    int best = -1;
    for (int i = 0; i < static_cast<int>(kMaxWaiters); ++i)
    {
        const Waiter& candidate = waiters_[i];
        if (!candidate.used)
        {
            continue;
        }
        if (best < 0)
        {
            best = i;
            continue;
        }
        const Waiter& current = waiters_[best];
        const uint8_t candidate_priority = priorityFor(candidate.policy);
        const uint8_t current_priority = priorityFor(current.policy);
        if (candidate_priority < current_priority ||
            (candidate_priority == current_priority &&
             candidate.sequence < current.sequence))
        {
            best = i;
        }
    }
    return best;
}

int SharedSpiCoordinator::reserveWaiterLocked(TaskHandle_t task,
                                              sys::runtime::BusAccessPolicy policy,
                                              uint32_t deadline_ms)
{
    for (int i = 0; i < static_cast<int>(kMaxWaiters); ++i)
    {
        Waiter& waiter = waiters_[i];
        if (waiter.used)
        {
            continue;
        }
        while (xSemaphoreTake(waiter.wake, 0) == pdTRUE)
        {
        }
        waiter.task = task;
        waiter.policy = policy;
        waiter.sequence = next_sequence_++;
        if (next_sequence_ == 0U)
        {
            next_sequence_ = 1U;
        }
        waiter.deadline_ms = deadline_ms;
        waiter.used = true;
        return i;
    }
    return -1;
}

void SharedSpiCoordinator::clearWaiterLocked(int index)
{
    if (index < 0 || index >= static_cast<int>(kMaxWaiters))
    {
        return;
    }
    Waiter& waiter = waiters_[index];
    waiter.used = false;
    waiter.task = nullptr;
    waiter.sequence = 0;
    waiter.deadline_ms = 0;
}

void SharedSpiCoordinator::copyOwnerLabelLocked(const char* label)
{
    const char* source = label && label[0] != '\0' ? label : "shared";
    std::snprintf(owner_label_, sizeof(owner_label_), "%s", source);
}

void SharedSpiCoordinator::recordOwnerLocked(const char* label,
                                             TaskHandle_t task,
                                             sys::runtime::BusAccessPolicy policy,
                                             uint32_t now_ms)
{
    owner_task_ = task;
    owner_policy_ = policy;
    owner_generation_++;
    if (owner_generation_ == 0U)
    {
        owner_generation_ = 1U;
    }
    owner_acquired_ms_ = now_ms;
    owner_depth_ = 1U;
    copyOwnerLabelLocked(label);
    std::snprintf(owner_task_name_, sizeof(owner_task_name_), "%s", taskName(task));
}

void SharedSpiCoordinator::clearOwnerLocked(uint32_t now_ms)
{
    const uint32_t hold_ms =
        owner_acquired_ms_ == 0U ? 0U : static_cast<uint32_t>(now_ms - owner_acquired_ms_);
    maximum_hold_ms_ = std::max(maximum_hold_ms_, hold_ms);
    owner_task_ = nullptr;
    owner_depth_ = 0U;
    owner_acquired_ms_ = 0U;
    owner_policy_ = sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    std::snprintf(owner_label_, sizeof(owner_label_), "-");
    std::snprintf(owner_task_name_, sizeof(owner_task_name_), "-");
}

bool SharedSpiCoordinator::grantWaiterLocked(
    int index,
    TaskHandle_t task,
    const sys::runtime::BusAcquireRequest& request,
    uint32_t now_ms,
    sys::runtime::BusAcquireResult& result)
{
    if (index < 0 || index >= static_cast<int>(kMaxWaiters) ||
        !waiters_[index].used || waiters_[index].task != task)
    {
        return false;
    }
    recordOwnerLocked(request.owner_label ? request.owner_label : "shared_spi",
                      task,
                      request.policy,
                      now_ms);
    result.status = sys::runtime::BusAcquireStatus::Acquired;
    result.token.resource = request.resource;
    result.token.owner = request.command_id;
    result.token.acquired_ms = now_ms;
    result.token.generation = owner_generation_;
    result.token.depth = owner_depth_;
    result.token.task_id = reinterpret_cast<uintptr_t>(task);
    result.token.valid = true;
    result.diagnostics.resource = request.resource;
    result.diagnostics.owner = request.origin;
    result.diagnostics.command_id = request.command_id;
    result.diagnostics.policy = request.policy;
    clearWaiterLocked(index);
    return true;
}

bool SharedSpiCoordinator::tokenMatchesLocked(const sys::runtime::BusAccessToken& token,
                                              TaskHandle_t current) const
{
    return token.valid && owner_task_ == current &&
           token.task_id == reinterpret_cast<uintptr_t>(current) &&
           token.generation == owner_generation_ && token.depth == owner_depth_;
}

sys::runtime::BusAcquireResult SharedSpiCoordinator::acquire(
    const sys::runtime::BusAcquireRequest& request)
{
    const uint32_t start_ms = sys::millis_now();
    uint32_t timeout_ms = timeoutFor(request.policy);
    if (request.deadline_ms != 0U)
    {
        timeout_ms = std::min(timeout_ms, remaining_ms(start_ms, request.deadline_ms));
    }

    const uint32_t deadline_ms =
        timeout_ms == 0U ? start_ms : static_cast<uint32_t>(start_ms + timeout_ms);
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    sys::runtime::BusAcquireResult result{};
    result.diagnostics.resource = request.resource;
    result.diagnostics.owner = request.origin;
    result.diagnostics.command_id = request.command_id;
    result.diagnostics.policy = request.policy;

    if (request.policy == sys::runtime::BusAccessPolicy::DisplayFrameCritical)
    {
        portENTER_CRITICAL(&mux_);
        ++display_frame_requests_;
        portEXIT_CRITICAL(&mux_);
    }

    int waiter_index = -1;
    for (;;)
    {
        portENTER_CRITICAL(&mux_);
        if (owner_task_ == current)
        {
            ++owner_depth_;
            result.status = sys::runtime::BusAcquireStatus::Acquired;
            result.token.resource = request.resource;
            result.token.owner = request.command_id;
            result.token.acquired_ms = sys::millis_now();
            result.token.generation = owner_generation_;
            result.token.depth = owner_depth_;
            result.token.task_id = reinterpret_cast<uintptr_t>(current);
            result.token.valid = true;
            result.diagnostics.wait_ms = sys::millis_now() - start_ms;
            portEXIT_CRITICAL(&mux_);
            return result;
        }

        if (waiter_index < 0)
        {
            waiter_index = reserveWaiterLocked(current, request.policy, deadline_ms);
        }

        const int best = findBestWaiterLocked();
        const uint32_t now_ms = sys::millis_now();
        if (waiter_index >= 0 && owner_task_ == nullptr && best == waiter_index)
        {
            result.diagnostics.wait_ms = now_ms - start_ms;
            const bool granted =
                grantWaiterLocked(waiter_index, current, request, now_ms, result);
            portEXIT_CRITICAL(&mux_);
            if (granted)
            {
                return result;
            }
            continue;
        }

        const uint32_t remaining = remaining_ms(now_ms, deadline_ms);
        if (waiter_index < 0 || remaining == 0U)
        {
            if (waiter_index >= 0)
            {
                clearWaiterLocked(waiter_index);
            }
            result.status = timeout_ms == 0U ? sys::runtime::BusAcquireStatus::Busy
                                             : sys::runtime::BusAcquireStatus::TimedOut;
            result.diagnostics.wait_ms = now_ms - start_ms;
            ++consecutive_timeouts_;
            health_.last_transition_ms = now_ms;
            health_.last_error = -2;
            health_.status = consecutive_timeouts_ >= 3
                                 ? sys::runtime::StorageHealthStatus::Degraded
                                 : sys::runtime::StorageHealthStatus::Slow;
            if (request.policy == sys::runtime::BusAccessPolicy::DisplayFrameCritical)
            {
                ++display_frame_busy_retries_;
            }
            portEXIT_CRITICAL(&mux_);
            return result;
        }
        const TickType_t wait_ticks =
            std::max<TickType_t>(1, pdMS_TO_TICKS(remaining));
        SemaphoreHandle_t wake = waiters_[waiter_index].wake;
        portEXIT_CRITICAL(&mux_);
        (void)xSemaphoreTake(wake, wait_ticks);
    }
}

void SharedSpiCoordinator::notifyBestWaiter()
{
    SemaphoreHandle_t wake = nullptr;
    portENTER_CRITICAL(&mux_);
    const int best = findBestWaiterLocked();
    if (best >= 0)
    {
        wake = waiters_[best].wake;
    }
    portEXIT_CRITICAL(&mux_);
    if (wake != nullptr)
    {
        (void)xSemaphoreGive(wake);
    }
}

void SharedSpiCoordinator::release(const sys::runtime::BusAccessToken& token)
{
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    bool notify = false;
    bool mismatch = false;
    char mismatch_owner[sizeof(owner_label_)]{};
    char mismatch_owner_task[sizeof(owner_task_name_)]{};
    uint32_t current_generation = 0;
    const uint32_t now_ms = sys::millis_now();
    portENTER_CRITICAL(&mux_);
    if (!tokenMatchesLocked(token, current))
    {
        ++release_mismatches_;
        mismatch = true;
        std::snprintf(mismatch_owner, sizeof(mismatch_owner), "%s", owner_label_);
        std::snprintf(mismatch_owner_task,
                      sizeof(mismatch_owner_task),
                      "%s",
                      owner_task_name_);
        current_generation = owner_generation_;
        portEXIT_CRITICAL(&mux_);
        if (mismatch)
        {
            std::printf("[SPI][COORD] release_mismatch task=%s owner=%s owner_task=%s "
                        "token_generation=%lu current_generation=%lu token_task=%p\n",
                        taskName(current),
                        mismatch_owner,
                        mismatch_owner_task,
                        static_cast<unsigned long>(token.generation),
                        static_cast<unsigned long>(current_generation),
                        reinterpret_cast<void*>(token.task_id));
        }
        return;
    }
    if (owner_depth_ > 1U)
    {
        --owner_depth_;
        portEXIT_CRITICAL(&mux_);
        return;
    }

    const uint32_t hold_ms =
        owner_acquired_ms_ == 0U ? 0U : static_cast<uint32_t>(now_ms - owner_acquired_ms_);
    clearOwnerLocked(now_ms);
    consecutive_timeouts_ = 0;
    if (hold_ms > kSlowHoldBudgetMs)
    {
        health_.status = sys::runtime::StorageHealthStatus::Slow;
        health_.last_error = -4;
    }
    else
    {
        health_.status = sys::runtime::StorageHealthStatus::Healthy;
        health_.last_error = 0;
    }
    health_.last_transition_ms = now_ms;
    notify = findBestWaiterLocked() >= 0;
    portEXIT_CRITICAL(&mux_);
    if (notify)
    {
        notifyBestWaiter();
    }
}

sys::runtime::StorageHealthState SharedSpiCoordinator::health() const
{
    portENTER_CRITICAL(&mux_);
    const auto result = health_;
    portEXIT_CRITICAL(&mux_);
    return result;
}

uint32_t SharedSpiCoordinator::displayFrameRequests() const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = display_frame_requests_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

uint32_t SharedSpiCoordinator::displayFrameCompletions() const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = display_frame_completions_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

uint32_t SharedSpiCoordinator::displayFrameBusyRetries() const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = display_frame_busy_retries_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

uint32_t SharedSpiCoordinator::displayFrameFailures() const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = display_frame_failures_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

uint32_t SharedSpiCoordinator::releaseMismatches() const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = release_mismatches_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

uint32_t SharedSpiCoordinator::maximumHoldMs() const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = maximum_hold_ms_;
    portEXIT_CRITICAL(&mux_);
    return value;
}

const char* SharedSpiCoordinator::ownerLabel() const
{
    return owner_label_;
}

const char* SharedSpiCoordinator::ownerTaskName() const
{
    return owner_task_name_;
}

uint32_t SharedSpiCoordinator::ownerHeldMs(uint32_t now_ms) const
{
    portENTER_CRITICAL(&mux_);
    const uint32_t value = owner_acquired_ms_ == 0U
                               ? 0U
                               : static_cast<uint32_t>(now_ms - owner_acquired_ms_);
    portEXIT_CRITICAL(&mux_);
    return value;
}

void SharedSpiCoordinator::noteDisplayFrameCompleted()
{
    portENTER_CRITICAL(&mux_);
    ++display_frame_completions_;
    portEXIT_CRITICAL(&mux_);
}

void SharedSpiCoordinator::noteDisplayFrameBusy()
{
    portENTER_CRITICAL(&mux_);
    ++display_frame_busy_retries_;
    portEXIT_CRITICAL(&mux_);
}

void SharedSpiCoordinator::noteDisplayFrameFailed()
{
    portENTER_CRITICAL(&mux_);
    ++display_frame_failures_;
    portEXIT_CRITICAL(&mux_);
}

SharedSpiCoordinator& shared_spi_coordinator()
{
    static SharedSpiCoordinator coordinator;
    return coordinator;
}

} // namespace platform::esp::common
