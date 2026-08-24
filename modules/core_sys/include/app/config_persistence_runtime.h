#pragma once

#include "app/app_config_changes.h"
#include "sys/persistence_contracts.h"

#include <cstdint>

namespace app
{

enum class ConfigPersistenceState : uint8_t
{
    Idle,
    Debouncing,
    InFlight,
    Backoff,
};

enum class ConfigPersistenceUrgency : uint8_t
{
    Debounced,
    Immediate,
};

using ConfigPersistenceGeneration = sys::PersistenceGeneration;
using ConfigPersistenceResultKind = sys::PersistenceResultKind;

struct ConfigPersistencePolicy
{
    uint32_t debounce_ms = 250U;
    uint32_t retry_delay_ms = 1000U;
};

struct ConfigPersistenceSubmission
{
    bool queued = false;
    ConfigPersistenceGeneration generation = 0U;
    AppConfigChangeSet changes = AppConfigChangeSet::none();
};

struct ConfigPersistenceWork
{
    AppConfigChangeSet changes = AppConfigChangeSet::none();
    ConfigPersistenceGeneration generation = 0U;
};

class ConfigPersistenceRuntime
{
  public:
    explicit ConfigPersistenceRuntime(
        ConfigPersistencePolicy policy = ConfigPersistencePolicy{})
        : policy_(policy)
    {
    }

    void initialize()
    {
        initialized_ = true;
        has_pending_ = false;
        in_flight_ = false;
        pending_changes_ = AppConfigChangeSet::none();
        pending_urgency_ = ConfigPersistenceUrgency::Debounced;
        active_changes_ = AppConfigChangeSet::none();
        generation_ = 0U;
        pending_generation_ = 0U;
        active_generation_ = 0U;
        last_completed_generation_ = 0U;
        due_ms_ = 0U;
        state_ = ConfigPersistenceState::Idle;
    }

    ConfigPersistenceSubmission submit(AppConfigChangeSet requested_changes,
                                       uint32_t now_ms,
                                       ConfigPersistenceUrgency urgency =
                                           ConfigPersistenceUrgency::Debounced)
    {
        if (!initialized_)
        {
            return {};
        }

        if (requested_changes.empty())
        {
            return {};
        }

        ++generation_;
        pending_generation_ = generation_;
        if (has_pending_)
        {
            pending_changes_.mergeIn(requested_changes);
            if (urgency == ConfigPersistenceUrgency::Immediate)
            {
                pending_urgency_ = urgency;
            }
        }
        else
        {
            pending_changes_ = requested_changes;
            pending_urgency_ = urgency;
        }
        has_pending_ = true;
        due_ms_ = pending_urgency_ == ConfigPersistenceUrgency::Immediate
                      ? now_ms
                      : now_ms + policy_.debounce_ms;
        if (!in_flight_)
        {
            state_ = ConfigPersistenceState::Debouncing;
        }

        return {true, pending_generation_, pending_changes_};
    }

    bool takeDue(uint32_t now_ms, ConfigPersistenceWork& out)
    {
        if (!initialized_ || in_flight_ || !has_pending_ ||
            !deadlineReached(now_ms, due_ms_))
        {
            return false;
        }

        active_changes_ = pending_changes_;
        active_generation_ = pending_generation_;
        has_pending_ = false;
        pending_changes_ = AppConfigChangeSet::none();
        pending_urgency_ = ConfigPersistenceUrgency::Debounced;
        in_flight_ = true;
        state_ = ConfigPersistenceState::InFlight;

        out.changes = active_changes_;
        out.generation = active_generation_;
        return true;
    }

    ConfigPersistenceResultKind complete(
        ConfigPersistenceGeneration generation,
        ConfigPersistenceResultKind result,
        uint32_t now_ms)
    {
        if (!in_flight_ || generation != active_generation_)
        {
            return ConfigPersistenceResultKind::StaleGeneration;
        }

        in_flight_ = false;
        if (result == ConfigPersistenceResultKind::Completed)
        {
            last_completed_generation_ = generation;
            schedulePendingAfterSuccess(now_ms);
            return result;
        }

        if (!has_pending_)
        {
            pending_changes_ = active_changes_;
            pending_generation_ = active_generation_;
            pending_urgency_ = ConfigPersistenceUrgency::Debounced;
            has_pending_ = true;
        }
        else
        {
            pending_changes_.mergeIn(active_changes_);
        }
        due_ms_ = now_ms + policy_.retry_delay_ms;
        state_ = ConfigPersistenceState::Backoff;
        return result;
    }

    bool initialized() const { return initialized_; }
    bool hasPending() const { return has_pending_; }
    bool busy() const { return in_flight_; }
    ConfigPersistenceState state() const { return state_; }
    ConfigPersistenceGeneration generation() const { return generation_; }
    ConfigPersistenceGeneration pendingGeneration() const
    {
        return pending_generation_;
    }
    ConfigPersistenceGeneration activeGeneration() const
    {
        return active_generation_;
    }
    ConfigPersistenceGeneration lastCompletedGeneration() const
    {
        return last_completed_generation_;
    }
    uint32_t dueMs() const { return due_ms_; }

  private:
    static bool deadlineReached(uint32_t now_ms, uint32_t due_ms)
    {
        return static_cast<int32_t>(now_ms - due_ms) >= 0;
    }

    void schedulePendingAfterSuccess(uint32_t now_ms)
    {
        if (!has_pending_)
        {
            state_ = ConfigPersistenceState::Idle;
            return;
        }

        due_ms_ = pending_urgency_ == ConfigPersistenceUrgency::Immediate
                      ? now_ms
                      : now_ms + policy_.debounce_ms;
        state_ = ConfigPersistenceState::Debouncing;
    }

    ConfigPersistencePolicy policy_{};
    AppConfigChangeSet pending_changes_ = AppConfigChangeSet::none();
    AppConfigChangeSet active_changes_ = AppConfigChangeSet::none();
    ConfigPersistenceUrgency pending_urgency_ =
        ConfigPersistenceUrgency::Debounced;
    ConfigPersistenceGeneration generation_ = 0U;
    ConfigPersistenceGeneration pending_generation_ = 0U;
    ConfigPersistenceGeneration active_generation_ = 0U;
    ConfigPersistenceGeneration last_completed_generation_ = 0U;
    uint32_t due_ms_ = 0U;
    bool initialized_ = false;
    bool has_pending_ = false;
    bool in_flight_ = false;
    ConfigPersistenceState state_ = ConfigPersistenceState::Idle;
};

static_assert(sizeof(ConfigPersistenceRuntime) <= 64U,
              "Config persistence scheduling must remain metadata-only.");

} // namespace app
