#pragma once

#include "chat/domain/reticulum_identity.h"

#include <cstdint>

namespace ui::widgets::reticulum_ping
{

void show_loading(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize],
    const char* display_name);
void show_send_failure(const char* detail);
void show_delivered(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize],
    std::uint32_t latency_ms,
    std::uint8_t hops);
void show_timeout(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize],
    std::uint32_t elapsed_ms);
void dismiss();
bool active_for(
    const std::uint8_t destination_hash[::chat::kReticulumPeerHashSize]);

} // namespace ui::widgets::reticulum_ping
