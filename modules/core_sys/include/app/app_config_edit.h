#pragma once

#include "app/app_config.h"
#include "app/app_config_changes.h"

namespace app
{

class AppConfigEdit
{
  public:
    using CommitFn = void (*)(void* context, AppConfigChangeSet changes);
    using CancelFn = void (*)(void* context);

    AppConfigEdit() = default;

    AppConfigEdit(AppConfig* config,
                  void* context,
                  CommitFn commit,
                  CancelFn cancel)
        : config_(config),
          context_(context),
          commit_(commit),
          cancel_(cancel)
    {
    }

    AppConfigEdit(const AppConfigEdit&) = delete;
    AppConfigEdit& operator=(const AppConfigEdit&) = delete;

    AppConfigEdit(AppConfigEdit&& other) noexcept
        : config_(other.config_),
          context_(other.context_),
          commit_(other.commit_),
          cancel_(other.cancel_)
    {
        other.clear();
    }

    AppConfigEdit& operator=(AppConfigEdit&& other) noexcept
    {
        if (this != &other)
        {
            cancel();
            config_ = other.config_;
            context_ = other.context_;
            commit_ = other.commit_;
            cancel_ = other.cancel_;
            other.clear();
        }
        return *this;
    }

    ~AppConfigEdit()
    {
        cancel();
    }

    explicit operator bool() const
    {
        return config_ != nullptr;
    }

    AppConfig& config()
    {
        return *config_;
    }

    const AppConfig& config() const
    {
        return *config_;
    }

    void commit(AppConfigChangeSet changes)
    {
        if (config_ == nullptr)
        {
            return;
        }

        CommitFn commit_fn = commit_;
        void* context = context_;
        clear();
        if (commit_fn)
        {
            commit_fn(context, changes);
        }
    }

  private:
    void cancel()
    {
        if (config_ == nullptr)
        {
            return;
        }

        CancelFn cancel_fn = cancel_;
        void* context = context_;
        clear();
        if (cancel_fn)
        {
            cancel_fn(context);
        }
    }

    void clear()
    {
        config_ = nullptr;
        context_ = nullptr;
        commit_ = nullptr;
        cancel_ = nullptr;
    }

    AppConfig* config_ = nullptr;
    void* context_ = nullptr;
    CommitFn commit_ = nullptr;
    CancelFn cancel_ = nullptr;
};

} // namespace app
