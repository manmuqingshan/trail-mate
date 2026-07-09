/**
 * @file audio_call_wire.h
 * @brief MeshChat-compatible Reticulum audio call protobuf subset.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::reticulum::audio_call
{

enum class Codec2Mode : uint8_t
{
    Mode3200 = 0,
    Mode2400 = 1,
    Mode1600 = 2,
    Mode1400 = 3,
    Mode1300 = 4,
    Mode1200 = 5,
    Mode700C = 6,
    Mode450 = 7,
    Mode450Pwb = 8,
};

struct DecodedPayload
{
    Codec2Mode mode = Codec2Mode::Mode1200;
    const uint8_t* encoded = nullptr;
    std::size_t encoded_len = 0;
};

bool encodePayload(Codec2Mode mode,
                   const uint8_t* encoded,
                   std::size_t encoded_len,
                   uint8_t* out,
                   std::size_t* inout_len);

bool decodePayload(const uint8_t* data,
                   std::size_t len,
                   DecodedPayload* out);

} // namespace chat::reticulum::audio_call
