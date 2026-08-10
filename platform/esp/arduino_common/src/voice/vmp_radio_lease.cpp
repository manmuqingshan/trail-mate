/**
 * @file vmp_radio_lease.cpp
 * @brief Exclusive LR1121 2.4 GHz lease for VMP control/data transfers.
 */

#include "platform/esp/arduino_common/voice/vmp_radio_lease.h"

#if defined(ARDUINO_T_LORA_PAGER) && defined(ARDUINO_LILYGO_LORA_LR1121)

#include <RadioLib.h>

#include "boards/tlora_pager/tlora_pager_board.h"
#include "platform/esp/arduino_common/exclusive_lora_runtime.h"

namespace platform::esp::arduino_common::voice::vmp_radio
{
namespace
{

constexpr float kMinVmpFrequencyMhz = 2400.0f;
constexpr float kMaxVmpFrequencyMhz = 2483.5f;
constexpr std::size_t kMaxVmpFrameSize = 255U;
constexpr uint8_t kVmpSyncWord[] = {'V', 'M', 'P', 1U};

struct LeaseImplementation
{
    exclusive_lora_runtime::Session exclusive = {};
    ::boards::tlora_pager::TLoRaPagerBoard* board = nullptr;
};

bool validProfile(const PhyProfile& profile)
{
    return profile.frequency_mhz >= kMinVmpFrequencyMhz &&
           profile.frequency_mhz <= kMaxVmpFrequencyMhz &&
           profile.bit_rate_kbps > 0.0f && profile.bit_rate_kbps <= 2000.0f &&
           profile.frequency_deviation_khz > 0.0f &&
           profile.receive_bandwidth_khz > 0.0f &&
           profile.preamble_length >= 8U;
}

LeaseImplementation* implementation(Lease* lease)
{
    return lease ? static_cast<LeaseImplementation*>(lease->implementation) : nullptr;
}

const LeaseImplementation* implementation(const Lease* lease)
{
    return lease ? static_cast<const LeaseImplementation*>(lease->implementation) : nullptr;
}

} // namespace

bool isSupported()
{
    return true;
}

bool tryAcquire(Lease* out_lease)
{
    if (!out_lease)
    {
        return false;
    }
    *out_lease = {};

    static LeaseImplementation storage{};
    if (storage.exclusive.lora != nullptr)
    {
        return false;
    }
    if (!exclusive_lora_runtime::tryAcquire(&storage.exclusive))
    {
        return false;
    }

    storage.board = static_cast<::boards::tlora_pager::TLoRaPagerBoard*>(
        storage.exclusive.lora);
    if (!storage.board || !storage.board->isRadioOnline())
    {
        exclusive_lora_runtime::release(&storage.exclusive);
        storage.board = nullptr;
        return false;
    }

    out_lease->implementation = &storage;
    out_lease->owns_radio_tasks = storage.exclusive.paused_radio_tasks;
    return true;
}

bool switchTo2Ghz(Lease* lease, const PhyProfile& profile)
{
    LeaseImplementation* const state = implementation(lease);
    if (!state || !state->board || !validProfile(profile))
    {
        return false;
    }
    // beginGFSK() may have changed the radio even when a later configuration
    // step reports an error.  Mark the lease before the call so release() is
    // guaranteed to restore the cached Sub-GHz profile on every error path.
    lease->switched_to_2ghz = true;
    const int result = state->board->configureFskRadio(profile.frequency_mhz,
                                                       profile.bit_rate_kbps,
                                                       profile.frequency_deviation_khz,
                                                       profile.receive_bandwidth_khz,
                                                       profile.tx_power_dbm,
                                                       profile.preamble_length,
                                                       3.0f,
                                                       kVmpSyncWord,
                                                       sizeof(kVmpSyncWord),
                                                       2U);
    if (result != RADIOLIB_ERR_NONE)
    {
        return false;
    }
    return true;
}

bool transmit(Lease* lease, const uint8_t* data, std::size_t size)
{
    LeaseImplementation* const state = implementation(lease);
    return state && state->board && data && size != 0U && size <= kMaxVmpFrameSize &&
           state->board->transmitRadio(data, size) == RADIOLIB_ERR_NONE;
}

bool startReceive(Lease* lease)
{
    LeaseImplementation* const state = implementation(lease);
    return state && state->board &&
           state->board->startRadioReceive() == RADIOLIB_ERR_NONE;
}

int packetLength(Lease* lease)
{
    LeaseImplementation* const state = implementation(lease);
    return state && state->board ? state->board->getRadioPacketLength(true) : -1;
}

bool readPacket(Lease* lease, uint8_t* out, std::size_t size)
{
    LeaseImplementation* const state = implementation(lease);
    return state && state->board && out && size != 0U && size <= kMaxVmpFrameSize &&
           state->board->readRadioData(out, size) == RADIOLIB_ERR_NONE;
}

void clearIrq(Lease* lease)
{
    LeaseImplementation* const state = implementation(lease);
    if (state && state->board)
    {
        state->board->clearRadioIrqFlags(0xFFFFFFFFU);
    }
}

void release(Lease* lease)
{
    LeaseImplementation* const state = implementation(lease);
    if (!lease || !state)
    {
        return;
    }
    if (state->board && lease->switched_to_2ghz)
    {
        (void)state->board->restoreLoRaRadio();
    }
    exclusive_lora_runtime::release(&state->exclusive);
    state->board = nullptr;
    *lease = {};
}

} // namespace platform::esp::arduino_common::voice::vmp_radio

#else

namespace platform::esp::arduino_common::voice::vmp_radio
{

bool isSupported()
{
    return false;
}

bool tryAcquire(Lease*)
{
    return false;
}

bool switchTo2Ghz(Lease*, const PhyProfile&)
{
    return false;
}

bool transmit(Lease*, const uint8_t*, std::size_t)
{
    return false;
}

bool startReceive(Lease*)
{
    return false;
}

int packetLength(Lease*)
{
    return -1;
}

bool readPacket(Lease*, uint8_t*, std::size_t)
{
    return false;
}

void clearIrq(Lease*)
{
}

void release(Lease*)
{
}

} // namespace platform::esp::arduino_common::voice::vmp_radio

#endif
