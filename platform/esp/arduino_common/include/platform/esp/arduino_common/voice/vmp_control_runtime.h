/**
 * @file vmp_control_runtime.h
 * @brief Pager-owned control-plane handoff for VMP v1.
 *
 * VMP envelopes are removed from AppTasks before any MT, MC, or RT adapter is
 * reached.  This runtime has no mesh send API and no forwarding API: a
 * consumed voice control packet is delivered only to its local VMP handler.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::arduino_common::voice::vmp_control
{

inline constexpr std::size_t kControlEnvelopeSize = 95U;

struct Envelope
{
    uint8_t bytes[kControlEnvelopeSize] = {};
    float rssi = 0.0F;
    float snr = 0.0F;
};

using EnvelopeHandler = void (*)(const Envelope& envelope, void* context);

/**
 * @brief Starts the fixed-depth control dispatcher and raw-RF interceptor.
 *
 * On boards other than the LR1121 Pager the function is a harmless false
 * return.  Calling it more than once is safe.
 */
bool initialize();

/**
 * @brief Installs the only local VMP control consumer.
 *
 * The callback executes on the VMP-owned task, never in AppTasks::meshTask.
 * Replacing a handler is atomic with respect to a future dispatch; callers
 * must ensure an old handler/context remains alive until it is replaced.
 */
void setEnvelopeHandler(EnvelopeHandler handler, void* context);

} // namespace platform::esp::arduino_common::voice::vmp_control
