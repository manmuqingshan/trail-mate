#pragma once

#include <cstddef>
#include <cstdint>

#include "platform/ui/capability_status.h"

namespace platform::ui::lora
{

struct ReceiveConfig
{
    float bw_khz = 125.0f;
    uint8_t sf = 11;
    uint8_t cr = 5;
    int8_t tx_power = 14;
    uint16_t preamble_len = 8;
    uint8_t sync_word = 0x12;
    uint8_t crc_len = 2;
};

/// Metadata for one CRC-accepted LoRa packet copied into caller-owned storage.
/// `poll_received_packet()` never fabricates packets on simulator targets.
struct ReceivedPacket
{
    std::size_t size = 0;
    float rssi_dbm = 0.0f;
    float snr_db = 0.0f;
};

bool is_supported();
bool acquire();
bool is_online();
bool configure_receive(float freq_mhz, const ReceiveConfig& config);
bool transmit_packet(const uint8_t* data, std::size_t size);
bool poll_received_packet(uint8_t* buffer, std::size_t capacity, ReceivedPacket* out_packet);
float read_instant_rssi();
void release();

/// Honest capability status for the current target.
/// May return Unsupported, Simulated, Available, Degraded, or Error.
CapabilityStatus capability_status();

} // namespace platform::ui::lora
