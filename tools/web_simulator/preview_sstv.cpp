#include "platform/ui/sstv_runtime.h"
#include "sys/clock.h"
#include <algorithm>
namespace platform::ui::sstv
{
static bool active = false;
static uint32_t began = 0;
static uint16_t image[320 * 256];
bool is_supported() { return true; }
bool start()
{
    active = true;
    began = sys::millis_now();
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 320; ++x) image[y * 320 + x] = static_cast<uint16_t>(((x / 11) & 31) << 11 | ((y / 4) & 63) << 5 | ((x + y) / 19 & 31));
    return true;
}
void stop() { active = false; }
bool is_active() { return active; }
Status get_status()
{
    Status s;
    if (!active) return s;
    s.progress = std::min(1.0f, (sys::millis_now() - began) / 12000.f);
    s.line = static_cast<uint16_t>(s.progress * 256);
    s.audio_level = .45f;
    s.has_image = true;
    s.state = s.progress >= 1 ? State::Complete : State::Receiving;
    return s;
}
const char* last_error() { return ""; }
const char* last_saved_path() { return ""; }
const char* mode_name() { return "Sample RX"; }
const uint16_t* framebuffer() { return image; }
uint16_t frame_width() { return 320; }
uint16_t frame_height() { return 256; }
} // namespace platform::ui::sstv
