/**
 * @file vmp_control_ingress.cpp
 * @brief Bounded Sub-GHz VMP control-envelope classifier.
 */

#include "chat/infra/voice/vmp_control_ingress.h"

namespace chat::voice::vmp
{

bool ControlIngress::tryConsume(const uint8_t* data,
                                std::size_t size,
                                const ControlRxMetadata& metadata) const
{
    // Classify only a complete VMP v1-sized envelope.  This keeps normal mesh
    // traffic byte-for-byte unchanged while malformed VMP candidates are
    // dropped locally rather than being reinterpreted as MT/MC/RT payloads.
    if (!data || size != kControlFrameSize || data[0] != static_cast<uint8_t>('V') ||
        data[1] != static_cast<uint8_t>('M') || data[2] != kVersion)
    {
        return false;
    }

    if (sink_)
    {
        (void)sink_->enqueueControl(data, size, metadata);
    }
    return true;
}

} // namespace chat::voice::vmp
