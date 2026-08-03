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
// Invalidates the saver for the normal display owner to present on its next
// frame. It does not force a refresh from a screen-sleep timer callback.
void request_present();

} // namespace ui::components::screen_saver_overlay
