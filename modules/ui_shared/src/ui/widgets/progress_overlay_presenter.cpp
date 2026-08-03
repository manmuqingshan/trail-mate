#include "ui/widgets/progress_overlay_presenter.h"

#include "lvgl.h"
#include "ui/widgets/busy_overlay.h"

namespace ui::widgets
{
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
        request_present();
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
        request_present();
    }
}

bool ProgressOverlayPresenter::active() const
{
    return active_;
}

void ProgressOverlayPresenter::request_present()
{
    if (lv_obj_t* top = lv_layer_top())
    {
        lv_obj_invalidate(top);
    }
}

} // namespace ui::widgets
