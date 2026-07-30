#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::components::screen_saver_overlay
{

struct Hooks
{
    bool (*format_time)(char* out, std::size_t out_len) = nullptr;
    int (*read_unread_count)() = nullptr;
};

void init(const Hooks& hooks);
void refresh();
void show();
void hide();
bool is_visible();
void present_now(std::uint8_t frame_count = 2, std::uint32_t frame_delay_ms = 16);

} // namespace ui::components::screen_saver_overlay
