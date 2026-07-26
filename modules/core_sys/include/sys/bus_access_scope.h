#pragma once

#include "sys/shared_spi_access.h"

namespace sys::runtime
{

class ScopedBusAccessToken
{
  public:
    ScopedBusAccessToken(IBusArbiter& arbiter, const BusAcquireRequest& request)
        : arbiter_(&arbiter), result_(arbiter.acquire(request))
    {
    }

    ScopedBusAccessToken(const ScopedBusAccessToken&) = delete;
    ScopedBusAccessToken& operator=(const ScopedBusAccessToken&) = delete;

    ScopedBusAccessToken(ScopedBusAccessToken&& other) noexcept
        : arbiter_(other.arbiter_), result_(other.result_)
    {
        other.arbiter_ = nullptr;
        other.result_.token.valid = false;
    }

    ScopedBusAccessToken& operator=(ScopedBusAccessToken&& other) noexcept
    {
        if (this != &other)
        {
            release();
            arbiter_ = other.arbiter_;
            result_ = other.result_;
            other.arbiter_ = nullptr;
            other.result_.token.valid = false;
        }
        return *this;
    }

    ~ScopedBusAccessToken()
    {
        release();
    }

    bool acquired() const
    {
        return result_.status == BusAcquireStatus::Acquired && result_.token.valid;
    }

    explicit operator bool() const
    {
        return acquired();
    }

    BusAcquireStatus status() const
    {
        return result_.status;
    }

    const BusAcquireResult& result() const
    {
        return result_;
    }

    const BusAccessToken& token() const
    {
        return result_.token;
    }

    const BusDiagnostics& diagnostics() const
    {
        return result_.diagnostics;
    }

    void release()
    {
        if (arbiter_ == nullptr || !result_.token.valid)
        {
            return;
        }
        arbiter_->release(result_.token);
        result_.token.valid = false;
    }

  private:
    IBusArbiter* arbiter_ = nullptr;
    BusAcquireResult result_{};
};

} // namespace sys::runtime
