/**
 * @file vmp_rs_fec.h
 * @brief Fixed-size Reed-Solomon (10,8) erasure coding for VMP media.
 *
 * VMP has exactly eight 160-byte source shards and two parity shards.  This
 * narrow API deliberately does not expose a generic allocator-backed FEC
 * codec: all storage belongs to the caller's bounded session slot and the
 * decoder can recover any two erased shards without an ARQ exchange.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

/**
 * @brief Generates parity shards 8 and 9 from the eight source shards.
 *
 * Every input and output slot is exactly @p shard_size bytes.  Slots must not
 * overlap.  The first eight slots must be present and contain the padded
 * source object before this function is called.
 */
bool encodeRs10_8(const uint8_t* const source_shards[kSourceShardsPerBlock],
                  std::size_t shard_size,
                  uint8_t* out_parity0,
                  uint8_t* out_parity1);

/**
 * @brief Recovers missing shards in one VMP `(10,8)` media block.
 *
 * `shards` contains ten caller-owned, writable slots of `shard_size` bytes;
 * `present` says which slots arrived with valid authentication/CRC.  The
 * decoder accepts at least eight present slots and reconstructs up to two
 * missing slots in place.  On success, all ten `present` entries become true.
 * The function performs no dynamic allocation and has no radio side effects.
 */
bool recoverRs10_8(uint8_t* shards[kTotalShardsPerBlock],
                   bool present[kTotalShardsPerBlock],
                   std::size_t shard_size);

} // namespace chat::voice::vmp
