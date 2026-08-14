/**
 * @file vmp_control_auth.h
 * @brief Integrity encoding/verification for VMP Sub-GHz control envelopes.
 */

#pragma once

#include "chat/infra/voice/vmp_private_crypto.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

/**
 * @brief Serializes a private OFFER/ACCEPT/CANCEL with its control AEAD tag.
 */
bool encodePrivateControlFrame(const ControlFrame& frame,
                               const PrivateSessionKeys& keys,
                               PrivateFrameDirection direction,
                               uint8_t* out,
                               std::size_t* inout_len);

/**
 * @brief Decodes and authenticates a private control envelope before RF work.
 */
bool decodePrivateControlFrame(const uint8_t* data,
                               std::size_t len,
                               const PrivateSessionKeys& keys,
                               PrivateFrameDirection direction,
                               ControlFrame* out_frame);

/**
 * @brief Serializes an intentionally public ANNOUNCE/CANCEL with CRC-64/ECMA.
 */
bool encodePublicControlFrame(const ControlFrame& frame,
                              uint8_t* out,
                              std::size_t* inout_len);

/**
 * @brief Decodes a public control envelope and checks corruption only.
 */
bool decodePublicControlFrame(const uint8_t* data,
                              std::size_t len,
                              ControlFrame* out_frame);

} // namespace chat::voice::vmp
