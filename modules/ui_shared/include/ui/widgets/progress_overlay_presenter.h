#pragma once

#include <cstdint>

namespace ui::widgets
{

class ProgressOverlayPresenter
{
  public:
    explicit ProgressOverlayPresenter(bool present_on_change = false,
                                      uint8_t present_frame_count = 1,
                                      uint32_t present_frame_delay_ms = 0);
    ~ProgressOverlayPresenter();

    ProgressOverlayPresenter(const ProgressOverlayPresenter&) = delete;
    ProgressOverlayPresenter& operator=(const ProgressOverlayPresenter&) = delete;

    void show_or_update(const char* title, const char* detail = nullptr, int progress_percent = -1);
    void hide();
    bool active() const;

    // Requests a regular LVGL frame. It deliberately never drives LVGL from
    // the caller: the platform display runtime is the sole handler owner.
    static void request_present();

  private:
    bool present_on_change_ = false;
    uint8_t present_frame_count_ = 1;
    uint32_t present_frame_delay_ms_ = 0;
    bool active_ = false;
};

} // namespace ui::widgets
