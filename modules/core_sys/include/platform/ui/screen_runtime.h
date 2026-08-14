#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::screen
{

struct Hooks
{
    bool (*format_time)(char* out, std::size_t out_len) = nullptr;
    int (*read_unread_count)() = nullptr;
    void (*show_main_menu)() = nullptr;
    void (*on_wake_from_sleep)() = nullptr;
    void (*show_screen_saver)() = nullptr;
    void (*hide_screen_saver)() = nullptr;
    void (*present_screen_saver)() = nullptr;
};

uint32_t clamp_timeout_ms(uint32_t timeout_ms);
uint32_t timeout_ms();
uint16_t timeout_secs();
bool supports_app_timeout_setting();
void set_timeout_ms(uint32_t timeout_ms);
void init(const Hooks& hooks);
bool is_sleeping();
bool is_sleep_disabled();
bool is_saver_active();
void handle_input();
void handle_confirm_input();
void handle_input_release();
void wake_for_modal();
void record_activity();
void disable_sleep();
void enable_sleep();

} // namespace platform::ui::screen
