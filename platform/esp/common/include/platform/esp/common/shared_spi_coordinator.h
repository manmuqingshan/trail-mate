#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys/runtime_async.h"

#include <cstdint>

namespace platform::esp::common
{

// One coordinator represents one physical shared SPI controller. Feature code
// must use this object through sys::runtime::IBusArbiter; it must not create a
// feature-local mutex or call a physical lock directly.
class SharedSpiCoordinator final : public sys::runtime::IBusArbiter
{
  public:
    static constexpr uint32_t kSharedBusResource = 1U;

    SharedSpiCoordinator();

    SharedSpiCoordinator(const SharedSpiCoordinator&) = delete;
    SharedSpiCoordinator& operator=(const SharedSpiCoordinator&) = delete;

    sys::runtime::BusAcquireResult acquire(
        const sys::runtime::BusAcquireRequest& request) override;
    void release(const sys::runtime::BusAccessToken& token) override;
    sys::runtime::StorageHealthState health() const override;

    uint32_t displayFrameRequests() const;
    uint32_t displayFrameCompletions() const;
    uint32_t displayFrameBusyRetries() const;
    uint32_t displayFrameFailures() const;
    uint32_t releaseMismatches() const;
    uint32_t maximumHoldMs() const;
    const char* ownerLabel() const;
    const char* ownerTaskName() const;
    uint32_t ownerHeldMs(uint32_t now_ms) const;

    void noteDisplayFrameCompleted();
    void noteDisplayFrameBusy();
    void noteDisplayFrameFailed();

  private:
    static constexpr uint8_t kMaxWaiters = 8U;
    static constexpr uint8_t kOwnerLabelCapacity = 31U;

    struct Waiter
    {
        StaticSemaphore_t wake_storage{};
        SemaphoreHandle_t wake = nullptr;
        TaskHandle_t task = nullptr;
        sys::runtime::BusAccessPolicy policy =
            sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
        uint32_t sequence = 0;
        uint32_t deadline_ms = 0;
        bool used = false;
    };

    static uint8_t priorityFor(sys::runtime::BusAccessPolicy policy);
    static uint32_t timeoutFor(sys::runtime::BusAccessPolicy policy);
    static const char* taskName(TaskHandle_t task);

    int findBestWaiterLocked() const;
    int reserveWaiterLocked(TaskHandle_t task,
                            sys::runtime::BusAccessPolicy policy,
                            uint32_t deadline_ms);
    void clearWaiterLocked(int index);
    bool grantWaiterLocked(int index,
                           TaskHandle_t task,
                           const sys::runtime::BusAcquireRequest& request,
                           uint32_t now_ms,
                           sys::runtime::BusAcquireResult& result);
    bool tokenMatchesLocked(const sys::runtime::BusAccessToken& token,
                            TaskHandle_t current) const;
    void recordOwnerLocked(const char* label,
                           TaskHandle_t task,
                           sys::runtime::BusAccessPolicy policy,
                           uint32_t now_ms);
    void clearOwnerLocked(uint32_t now_ms);
    void notifyBestWaiter();
    void copyOwnerLabelLocked(const char* label);

    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
    Waiter waiters_[kMaxWaiters]{};
    TaskHandle_t owner_task_ = nullptr;
    sys::runtime::BusAccessPolicy owner_policy_ =
        sys::runtime::BusAccessPolicy::BackgroundWorkerBounded;
    uint32_t owner_generation_ = 0;
    uint32_t owner_acquired_ms_ = 0;
    uint32_t owner_depth_ = 0;
    uint32_t next_sequence_ = 1;
    char owner_label_[kOwnerLabelCapacity + 1U]{};
    char owner_task_name_[configMAX_TASK_NAME_LEN]{};

    sys::runtime::StorageHealthState health_{};
    uint32_t consecutive_timeouts_ = 0;
    uint32_t display_frame_requests_ = 0;
    uint32_t display_frame_completions_ = 0;
    uint32_t display_frame_busy_retries_ = 0;
    uint32_t display_frame_failures_ = 0;
    uint32_t release_mismatches_ = 0;
    uint32_t maximum_hold_ms_ = 0;
};

SharedSpiCoordinator& shared_spi_coordinator();

} // namespace platform::esp::common
