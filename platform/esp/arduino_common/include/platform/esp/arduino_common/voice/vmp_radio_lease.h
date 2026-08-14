/**
 * @file vmp_radio_lease.h
 * @brief Exclusive LR1121 2.4 GHz lease for VMP control/data transfers.
 *
 * This is deliberately a narrow Pager-only adapter.  It borrows the current
 * Sub-GHz configuration for one VMP control exchange, switches only the same
 * LR1121 to VMP GFSK, and restores the captured LoRa configuration before
 * normal AppTasks RX resumes.  It exposes no method for received media to
 * re-enter the generic mesh TX queue.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::arduino_common::voice::vmp_radio
{

struct PhyProfile
{
    float frequency_mhz = 0.0f;
    float bit_rate_kbps = 500.0f;
    float frequency_deviation_khz = 250.0f;
    float receive_bandwidth_khz = 800.0f;
    int8_t tx_power_dbm = 10;
    uint16_t preamble_length = 16;
};

struct Lease
{
    void* implementation = nullptr;
    bool owns_radio_tasks = false;
    bool switched_to_2ghz = false;
};

bool isSupported();
bool tryAcquire(Lease* out_lease);

/** @brief Applies a bounded regional 2.4 GHz VMP GFSK profile. */
bool switchTo2Ghz(Lease* lease, const PhyProfile& profile);

/** @brief Sends one VMP control or media frame on the currently active PHY. */
bool transmit(Lease* lease, const uint8_t* data, std::size_t size);

bool startReceive(Lease* lease);
int packetLength(Lease* lease);
bool readPacket(Lease* lease, uint8_t* out, std::size_t size);
void clearIrq(Lease* lease);

/** @brief Always restores cached Sub-GHz LoRa and resumes normal task RX. */
void release(Lease* lease);

} // namespace platform::esp::arduino_common::voice::vmp_radio
