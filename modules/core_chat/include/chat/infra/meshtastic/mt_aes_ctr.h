/**
 * @file mt_aes_ctr.h
 * @brief Allocation-free AES-CTR primitive for Meshtastic payloads.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat
{
namespace meshtastic
{

/**
 * Encrypts or decrypts a payload in place using AES-CTR.
 *
 * The counter is read but never modified. Supported keys are AES-128 and
 * AES-256 (16 or 32 bytes). The ESP implementation deliberately avoids the
 * hardware AES/DMA backend because receiving LoRa packets must remain
 * reliable while display and SD I/O are allocating DMA-capable memory.
 */
bool aesCtrCryptInPlace(const uint8_t* key, size_t key_len,
                        const uint8_t counter[16],
                        uint8_t* buffer, size_t len);

} // namespace meshtastic
} // namespace chat
