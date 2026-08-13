#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "esp_lcd_mipi_dsi.h"
#include "lvgl.h"

namespace boards::t_display_p4
{

// Presents a landscape LVGL scene to the portrait-native DSI panel without
// ever modifying the frame that the DSI DMA is currently scanning.
//
// The three panel-owned PSRAM frame buffers have fixed roles:
//   - one front buffer is scanned by DSI;
//   - one logical buffer is owned by LVGL;
//   - the other physical buffer is the software-rotation target.
//
// After a frame is prepared, it becomes visible only at the next DSI refresh
// boundary. LVGL is released from its flush only after that boundary, so the
// old front buffer cannot be reused while the panel is still reading it.
class P4DsiRotatingPresenter final
{
public:
    bool init(esp_lcd_panel_handle_t panel,
              uint32_t physical_hres,
              uint32_t physical_vres,
              uint8_t bits_per_pixel,
              lv_color_format_t color_format,
              lv_display_rotation_t rotation);

    lv_display_t* display() const;
    std::size_t frame_bytes() const;

private:
    static void flush_callback(lv_display_t* display,
                               const lv_area_t* area,
                               uint8_t* pixel_map);
    static bool IRAM_ATTR refresh_done_callback(
        esp_lcd_panel_handle_t panel,
        esp_lcd_dpi_panel_event_data_t* event_data,
        void* user_context);

    bool present_full_frame(const lv_area_t* area, uint8_t* pixel_map);
    void rotate_with_lvgl(void* output_buffer, const uint8_t* input_buffer) const;

    esp_lcd_panel_handle_t panel_ = nullptr;
    lv_display_t* display_ = nullptr;
    void* scanout_buffers_[2] = {nullptr, nullptr};
    void* logical_buffer_ = nullptr;
    std::atomic<void*> front_buffer_{nullptr};
    std::atomic<void*> pending_front_buffer_{nullptr};
    std::atomic<bool> awaiting_refresh_{false};
    uint32_t physical_hres_ = 0;
    uint32_t physical_vres_ = 0;
    lv_color_format_t color_format_ = LV_COLOR_FORMAT_UNKNOWN;
    lv_display_rotation_t rotation_ = LV_DISPLAY_ROTATION_0;
    std::size_t frame_bytes_ = 0;
};

} // namespace boards::t_display_p4
