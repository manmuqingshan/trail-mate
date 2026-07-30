#pragma once

#include <cstdint>

namespace platform::esp::idf_common::reticulum_call_support
{

void ensure_registered();
void set_speaker_volume(uint8_t volume_percent);
bool play_message_notification();
bool play_incoming_notification(const volatile bool* stop_requested = nullptr);

} // namespace platform::esp::idf_common::reticulum_call_support
