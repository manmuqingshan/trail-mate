#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::reticulum_receive
{

constexpr std::size_t kHashSize = 16;

struct Snapshot
{
    bool active = false;
    bool cancellable = false;
    uint8_t link_id[kHashSize] = {};
    uint8_t resource_hash[32] = {};
    uint32_t received_parts = 0;
    uint32_t total_parts = 0;
    uint32_t received_bytes = 0;
    uint32_t total_bytes = 0;
    int progress_percent = -1;
};

using CancelHandler = bool (*)(const uint8_t link_id[kHashSize],
                               const uint8_t resource_hash[32],
                               void* context);

void bind_cancel_handler(CancelHandler handler, void* context);
void begin(const uint8_t link_id[kHashSize],
           const uint8_t resource_hash[32],
           uint32_t total_parts,
           uint32_t total_bytes);
void update(const uint8_t resource_hash[32],
            uint32_t received_parts,
            uint32_t received_bytes);
void complete(const uint8_t resource_hash[32]);
Snapshot snapshot();
bool cancel();

} // namespace platform::ui::reticulum_receive
