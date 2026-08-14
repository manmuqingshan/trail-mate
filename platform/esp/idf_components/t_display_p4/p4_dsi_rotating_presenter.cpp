#include "p4_dsi_rotating_presenter.h"

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"

namespace boards::t_display_p4
{
namespace
{

constexpr const char* kTag = "p4-dsi-present";

} // namespace

bool P4DsiRotatingPresenter::init(esp_lcd_panel_handle_t panel,
                                  uint32_t physical_hres,
                                  uint32_t physical_vres,
                                  uint8_t bits_per_pixel,
                                  lv_color_format_t color_format,
                                  lv_display_rotation_t rotation)
{
    if (panel == nullptr || physical_hres == 0 || physical_vres == 0 ||
        (bits_per_pixel != 16 && bits_per_pixel != 24) ||
        (rotation != LV_DISPLAY_ROTATION_90 && rotation != LV_DISPLAY_ROTATION_270))
    {
        return false;
    }

    panel_ = panel;
    physical_hres_ = physical_hres;
    physical_vres_ = physical_vres;
    color_format_ = color_format;
    rotation_ = rotation;
    frame_bytes_ = static_cast<std::size_t>(physical_hres_) * physical_vres_ *
                   (bits_per_pixel / 8U);

    void* first_scanout = nullptr;
    void* logical_buffer = nullptr;
    void* second_scanout = nullptr;
    if (esp_lcd_dpi_panel_get_frame_buffer(panel_,
                                           3,
                                           &first_scanout,
                                           &logical_buffer,
                                           &second_scanout) != ESP_OK ||
        first_scanout == nullptr || logical_buffer == nullptr || second_scanout == nullptr)
    {
        ESP_LOGE(kTag, "failed to acquire the three DSI frame buffers");
        return false;
    }

    display_ = lv_display_create(physical_hres_, physical_vres_);
    if (display_ == nullptr)
    {
        ESP_LOGE(kTag, "failed to create LVGL display");
        return false;
    }

    scanout_buffers_[0] = first_scanout;
    scanout_buffers_[1] = second_scanout;
    logical_buffer_ = logical_buffer;
    front_buffer_.store(first_scanout, std::memory_order_release);

    lv_display_set_color_format(display_, color_format_);
    lv_display_set_buffers(display_,
                           logical_buffer_,
                           nullptr,
                           frame_bytes_,
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_user_data(display_, this);
    lv_display_set_flush_cb(display_, flush_callback);

    esp_lcd_dpi_panel_event_callbacks_t callbacks{};
    callbacks.on_refresh_done = refresh_done_callback;
    if (esp_lcd_dpi_panel_register_event_callbacks(panel_, &callbacks, this) != ESP_OK)
    {
        ESP_LOGE(kTag, "failed to register DSI refresh callback");
        return false;
    }

    lv_display_set_rotation(display_, rotation_);
    ESP_LOGI(kTag,
             "ready: three panel-owned buffers, logical=%p front=%p back=%p bytes=%u rotation=software",
             logical_buffer_,
             scanout_buffers_[0],
             scanout_buffers_[1],
             static_cast<unsigned>(frame_bytes_));
    return true;
}

lv_display_t* P4DsiRotatingPresenter::display() const
{
    return display_;
}

std::size_t P4DsiRotatingPresenter::frame_bytes() const
{
    return frame_bytes_;
}

void P4DsiRotatingPresenter::flush_callback(lv_display_t* display,
                                            const lv_area_t* area,
                                            uint8_t* pixel_map)
{
    auto* presenter = static_cast<P4DsiRotatingPresenter*>(lv_display_get_user_data(display));
    if (presenter == nullptr || !presenter->present_full_frame(area, pixel_map))
    {
        // A failed present must not leave the LVGL task permanently blocked.
        // The next invalidation retries against the still-valid front buffer.
        lv_display_flush_ready(display);
    }
}

bool IRAM_ATTR P4DsiRotatingPresenter::refresh_done_callback(
    esp_lcd_panel_handle_t panel,
    esp_lcd_dpi_panel_event_data_t* event_data,
    void* user_context)
{
    (void)panel;
    (void)event_data;
    auto* presenter = static_cast<P4DsiRotatingPresenter*>(user_context);
    if (presenter == nullptr ||
        !presenter->awaiting_refresh_.exchange(false, std::memory_order_acq_rel))
    {
        return false;
    }

    void* const completed_front =
        presenter->pending_front_buffer_.exchange(nullptr, std::memory_order_acq_rel);
    if (completed_front != nullptr)
    {
        presenter->front_buffer_.store(completed_front, std::memory_order_release);
    }
    if (presenter->display_ != nullptr)
    {
        lv_display_flush_ready(presenter->display_);
    }
    return false;
}

bool P4DsiRotatingPresenter::present_full_frame(const lv_area_t* area,
                                                uint8_t* pixel_map)
{
    if (area == nullptr || pixel_map == nullptr || display_ == nullptr || panel_ == nullptr ||
        awaiting_refresh_.load(std::memory_order_acquire))
    {
        return false;
    }

    const int32_t logical_hres = lv_display_get_horizontal_resolution(display_);
    const int32_t logical_vres = lv_display_get_vertical_resolution(display_);
    const bool complete_logical_frame = area->x1 == 0 && area->y1 == 0 &&
                                        area->x2 == logical_hres - 1 &&
                                        area->y2 == logical_vres - 1;
    if (!complete_logical_frame)
    {
        ESP_LOGE(kTag,
                 "full presenter received partial flush area=(%d,%d)-(%d,%d) expected=%ldx%ld",
                 area->x1,
                 area->y1,
                 area->x2,
                 area->y2,
                 static_cast<long>(logical_hres),
                 static_cast<long>(logical_vres));
        return false;
    }

    void* const visible = front_buffer_.load(std::memory_order_acquire);
    void* const target = visible == scanout_buffers_[0] ? scanout_buffers_[1]
                                                        : scanout_buffers_[0];
    if (target == nullptr)
    {
        return false;
    }
    rotate_with_lvgl(target, pixel_map);

    // draw_bitmap sees target as one of the DPI panel frame buffers. It does a
    // cache write-back and records the target as the frame selected at the next
    // DMA refresh boundary; it does not copy over the visible front buffer.
    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel_,
                                                    0,
                                                    0,
                                                    static_cast<int>(physical_hres_),
                                                    static_cast<int>(physical_vres_),
                                                    target);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "failed to submit back frame: %s", esp_err_to_name(err));
        return false;
    }

    // A refresh interrupt that occurs just before this store is intentionally
    // ignored. The following refresh boundary releases LVGL, which is safe and
    // preserves the no-reuse-before-scanout invariant.
    pending_front_buffer_.store(target, std::memory_order_release);
    awaiting_refresh_.store(true, std::memory_order_release);
    return true;
}

void P4DsiRotatingPresenter::rotate_with_lvgl(void* output_buffer,
                                              const uint8_t* input_buffer) const
{
    const int32_t logical_hres = lv_display_get_horizontal_resolution(display_);
    const int32_t logical_vres = lv_display_get_vertical_resolution(display_);
    const uint32_t input_stride =
        lv_draw_buf_width_to_stride(logical_hres, color_format_);
    const uint32_t output_stride =
        lv_draw_buf_width_to_stride(static_cast<int32_t>(physical_hres_), color_format_);
    lv_draw_sw_rotate(input_buffer,
                      output_buffer,
                      logical_hres,
                      logical_vres,
                      input_stride,
                      output_stride,
                      rotation_,
                      color_format_);
}

} // namespace boards::t_display_p4
