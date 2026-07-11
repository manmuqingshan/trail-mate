#include "ui/widgets/progress_overlay_presenter.h"

#include "lvgl.h"
#include "sys/clock.h"
#include "ui/widgets/busy_overlay.h"

namespace ui::widgets
{
namespace
{

bool s_presenting = false;

} // namespace

ProgressOverlayPresenter::ProgressOverlayPresenter(bool present_on_change,
                                                   uint8_t present_frame_count,
                                                   uint32_t present_frame_delay_ms)
    : present_on_change_(present_on_change),
      present_frame_count_(present_frame_count == 0 ? 1 : present_frame_count),
      present_frame_delay_ms_(present_frame_delay_ms)
{
}

ProgressOverlayPresenter::~ProgressOverlayPresenter()
{
    hide();
}

void ProgressOverlayPresenter::show_or_update(const char* title,
                                              const char* detail,
                                              int progress_percent)
{
    if (!active_)
    {
        ::ui::widgets::busy_overlay::show(title, detail);
        active_ = true;
    }
    else
    {
        ::ui::widgets::busy_overlay::update(title, detail);
    }
    ::ui::widgets::busy_overlay::set_progress(progress_percent);
    if (present_on_change_)
    {
        present_now(present_frame_count_, present_frame_delay_ms_);
    }
}

void ProgressOverlayPresenter::hide()
{
    if (!active_)
    {
        return;
    }
    ::ui::widgets::busy_overlay::hide();
    active_ = false;
    if (present_on_change_)
    {
        present_now(present_frame_count_, present_frame_delay_ms_);
    }
}

bool ProgressOverlayPresenter::active() const
{
    return active_;
}

void ProgressOverlayPresenter::present_now(uint8_t frame_count, uint32_t frame_delay_ms)
{
    if (s_presenting)
    {
        return;
    }
    s_presenting = true;
    const uint8_t frames = frame_count == 0 ? 1 : frame_count;
    for (uint8_t frame = 0; frame < frames; ++frame)
    {
        if (lv_obj_t* top = lv_layer_top())
        {
            lv_obj_invalidate(top);
        }
        lv_timer_handler();
        lv_refr_now(nullptr);
        if (frame + 1U < frames && frame_delay_ms > 0)
        {
            sys::sleep_ms(frame_delay_ms);
        }
    }
    s_presenting = false;
}

} // namespace ui::widgets
