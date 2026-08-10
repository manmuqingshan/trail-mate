/**
 * @file vmp_control_ingress.h
 * @brief Bounded Sub-GHz VMP control-envelope classifier.
 *
 * It is deliberately not a mesh adapter: once it positively recognizes a VMP
 * v1 envelope, that packet must not be handed to MT, MC, or RT.  Authentication
 * and RF switching happen later in a dedicated VMP task through the sink.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

struct ControlRxMetadata
{
    float rssi = 0.0f;
    float snr = 0.0f;
};

/**
 * @brief A non-blocking hand-off into VMP-owned task storage.
 *
 * The borrowed bytes remain valid only until enqueueControl() returns.  An
 * implementation must copy them into a bounded slot/queue if it accepts them;
 * it must never call a mesh adapter or a radio TX function from this method.
 */
class IControlEnvelopeSink
{
  public:
    virtual ~IControlEnvelopeSink() = default;
    virtual bool enqueueControl(const uint8_t* data,
                                std::size_t size,
                                const ControlRxMetadata& metadata) = 0;
};

/**
 * @brief Recognizes VMP v1 control envelopes before generic mesh parsing.
 */
class ControlIngress
{
  public:
    void setSink(IControlEnvelopeSink* sink) { sink_ = sink; }

    /**
     * @return true if this is a VMP v1-shaped packet and must be removed from
     *         the generic mesh path, including when the bounded sink is full.
     */
    bool tryConsume(const uint8_t* data,
                    std::size_t size,
                    const ControlRxMetadata& metadata) const;

  private:
    IControlEnvelopeSink* sink_ = nullptr;
};

} // namespace chat::voice::vmp
