#include "ui/widgets/route_image_operation_presenter.h"

#include "lvgl.h"
#include "ui/widgets/foreground_operation_overlay.h"

#include <algorithm>
#include <cstdint>

namespace ui::widgets::route_image_operation
{
namespace
{

constexpr uint32_t kRouteImageOperationPollMs = 350;

lv_timer_t* s_poll_timer = nullptr;

using ::platform::ui::route_storage::RouteImageDownloadPhase;
using ::platform::ui::route_storage::RouteImageDownloadStatus;
using ::platform::ui::route_storage::RouteImageTaskPresentation;

const char* route_image_title(RouteImageDownloadPhase phase)
{
    switch (phase)
    {
    case RouteImageDownloadPhase::Downloading:
        return "Downloading route images...";
    case RouteImageDownloadPhase::Caching:
        return "Caching route images...";
    default:
        break;
    }
    return "Processing route images...";
}

int route_image_progress_percent(const RouteImageDownloadStatus& status)
{
    if (status.total == 0)
    {
        return -1;
    }

    std::uint32_t current_percent = 0;
    if (status.busy && status.current_total_bytes > 0)
    {
        const std::uint32_t current_bytes =
            std::min(status.current_bytes, status.current_total_bytes);
        current_percent = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                100U,
                (static_cast<std::uint64_t>(current_bytes) * 100U) /
                    status.current_total_bytes));
    }

    const std::size_t completed = std::min(status.processed, status.total);
    const std::size_t scaled =
        std::min<std::size_t>(status.total * 100U,
                              completed * 100U + current_percent);
    return static_cast<int>(scaled / status.total);
}

void pause_timer()
{
    if (s_poll_timer)
    {
        lv_timer_pause(s_poll_timer);
    }
}

void poll_cb(lv_timer_t*)
{
    refresh();
}

void ensure_timer()
{
    if (s_poll_timer)
    {
        lv_timer_resume(s_poll_timer);
        return;
    }
    s_poll_timer = lv_timer_create(poll_cb, kRouteImageOperationPollMs, nullptr);
    if (s_poll_timer)
    {
        lv_timer_set_repeat_count(s_poll_timer, -1);
    }
}

} // namespace

void sync(const RouteImageDownloadStatus& status)
{
    namespace foreground = ::ui::widgets::foreground_operation;

    if (!status.busy ||
        status.presentation != RouteImageTaskPresentation::UserVisible)
    {
        foreground::clear(foreground::Slot::RouteImage);
        pause_timer();
        return;
    }

    const char* detail = nullptr;
    if (!status.message.empty())
    {
        detail = status.message.c_str();
    }
    else if (!status.error.empty())
    {
        detail = status.error.c_str();
    }

    foreground::publish(
        foreground::make_snapshot(foreground::Slot::RouteImage,
                                  foreground::Policy::Overlay,
                                  foreground::Priority::Foreground,
                                  route_image_title(status.phase),
                                  detail,
                                  route_image_progress_percent(status)));
    ensure_timer();
}

void refresh()
{
    sync(::platform::ui::route_storage::route_image_download_status());
}

void clear()
{
    namespace foreground = ::ui::widgets::foreground_operation;
    foreground::clear(foreground::Slot::RouteImage);
    pause_timer();
}

} // namespace ui::widgets::route_image_operation
