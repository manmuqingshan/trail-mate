/**
 * @file vmp_rs_fec.cpp
 * @brief Fixed-size Reed-Solomon (10,8) erasure coding for VMP media.
 */

#include "chat/infra/voice/vmp_rs_fec.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

constexpr uint8_t kFieldPolynomialLow = 0x1DU;

uint8_t gfMultiply(uint8_t left, uint8_t right)
{
    uint8_t product = 0;
    for (uint8_t bit = 0; bit < 8U; ++bit)
    {
        if ((right & 1U) != 0U)
        {
            product ^= left;
        }
        const bool high_bit = (left & 0x80U) != 0U;
        left = static_cast<uint8_t>(left << 1U);
        if (high_bit)
        {
            left ^= kFieldPolynomialLow;
        }
        right = static_cast<uint8_t>(right >> 1U);
    }
    return product;
}

uint8_t gfPower(uint8_t base, uint16_t exponent)
{
    uint8_t result = 1;
    while (exponent != 0U)
    {
        if ((exponent & 1U) != 0U)
        {
            result = gfMultiply(result, base);
        }
        base = gfMultiply(base, base);
        exponent = static_cast<uint16_t>(exponent >> 1U);
    }
    return result;
}

uint8_t gfInverse(uint8_t value)
{
    return value == 0U ? 0U : gfPower(value, 254U);
}

uint8_t parityCoefficient(uint8_t source_index)
{
    // The second parity row is [1, 2, 4, ..., 2^7].  Together with the
    // all-one first row, any pair of data columns forms an invertible matrix.
    return gfPower(2U, source_index);
}

bool validShardPointers(uint8_t* const shards[kTotalShardsPerBlock],
                        std::size_t shard_size)
{
    if (!shards || shard_size == 0U || shard_size > kMaxShardPayloadSize)
    {
        return false;
    }
    for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        if (!shards[index])
        {
            return false;
        }
    }
    return true;
}

} // namespace

bool encodeRs10_8(const uint8_t* const source_shards[kSourceShardsPerBlock],
                  std::size_t shard_size,
                  uint8_t* out_parity0,
                  uint8_t* out_parity1)
{
    if (!source_shards || !out_parity0 || !out_parity1 || shard_size == 0U ||
        shard_size > kMaxShardPayloadSize || out_parity0 == out_parity1)
    {
        return false;
    }

    for (std::size_t source = 0; source < kSourceShardsPerBlock; ++source)
    {
        if (!source_shards[source])
        {
            return false;
        }
    }

    std::memset(out_parity0, 0, shard_size);
    std::memset(out_parity1, 0, shard_size);
    for (uint8_t source = 0; source < kSourceShardsPerBlock; ++source)
    {
        const uint8_t coefficient = parityCoefficient(source);
        const uint8_t* const input = source_shards[source];
        for (std::size_t byte = 0; byte < shard_size; ++byte)
        {
            out_parity0[byte] ^= input[byte];
            out_parity1[byte] ^= gfMultiply(coefficient, input[byte]);
        }
    }
    return true;
}

bool recoverRs10_8(uint8_t* shards[kTotalShardsPerBlock],
                   bool present[kTotalShardsPerBlock],
                   std::size_t shard_size)
{
    if (!validShardPointers(shards, shard_size) || !present)
    {
        return false;
    }

    uint8_t missing[kTotalShardsPerBlock] = {};
    std::size_t missing_count = 0;
    for (uint8_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        if (!present[index])
        {
            if (missing_count == 2U)
            {
                return false;
            }
            missing[missing_count++] = index;
        }
    }
    if (missing_count == 0U)
    {
        return true;
    }

    uint8_t missing_data[kSourceShardsPerBlock] = {};
    std::size_t missing_data_count = 0;
    for (std::size_t index = 0; index < missing_count; ++index)
    {
        if (missing[index] < kSourceShardsPerBlock)
        {
            missing_data[missing_data_count++] = missing[index];
        }
    }

    if (missing_data_count == 0U)
    {
        const uint8_t* sources[kSourceShardsPerBlock] = {};
        for (std::size_t index = 0; index < kSourceShardsPerBlock; ++index)
        {
            sources[index] = shards[index];
        }
        if (!encodeRs10_8(sources, shard_size, shards[8], shards[9]))
        {
            return false;
        }
    }
    else if (missing_data_count == 1U)
    {
        const uint8_t missing_source = missing_data[0];
        if (present[8])
        {
            std::memcpy(shards[missing_source], shards[8], shard_size);
            for (uint8_t source = 0; source < kSourceShardsPerBlock; ++source)
            {
                if (source != missing_source)
                {
                    for (std::size_t byte = 0; byte < shard_size; ++byte)
                    {
                        shards[missing_source][byte] ^= shards[source][byte];
                    }
                }
            }
        }
        else if (present[9])
        {
            const uint8_t inverse = gfInverse(parityCoefficient(missing_source));
            if (inverse == 0U)
            {
                return false;
            }
            std::memcpy(shards[missing_source], shards[9], shard_size);
            for (uint8_t source = 0; source < kSourceShardsPerBlock; ++source)
            {
                if (source != missing_source)
                {
                    const uint8_t coefficient = parityCoefficient(source);
                    for (std::size_t byte = 0; byte < shard_size; ++byte)
                    {
                        shards[missing_source][byte] ^=
                            gfMultiply(coefficient, shards[source][byte]);
                    }
                }
            }
            for (std::size_t byte = 0; byte < shard_size; ++byte)
            {
                shards[missing_source][byte] =
                    gfMultiply(inverse, shards[missing_source][byte]);
            }
        }
        else
        {
            return false;
        }

        const uint8_t* sources[kSourceShardsPerBlock] = {};
        for (std::size_t index = 0; index < kSourceShardsPerBlock; ++index)
        {
            sources[index] = shards[index];
        }
        if (!encodeRs10_8(sources, shard_size, shards[8], shards[9]))
        {
            return false;
        }
    }
    else
    {
        // Two data erasures require both parity rows.  Let S0 = Da ^ Db and
        // S1 = Ca*Da ^ Cb*Db.  Solving the 2x2 GF(256) system recovers both.
        if (!present[8] || !present[9])
        {
            return false;
        }

        const uint8_t first = missing_data[0];
        const uint8_t second = missing_data[1];
        const uint8_t first_coefficient = parityCoefficient(first);
        const uint8_t second_coefficient = parityCoefficient(second);
        const uint8_t denominator = first_coefficient ^ second_coefficient;
        const uint8_t inverse = gfInverse(denominator);
        if (inverse == 0U)
        {
            return false;
        }

        for (std::size_t byte = 0; byte < shard_size; ++byte)
        {
            uint8_t sum0 = shards[8][byte];
            uint8_t sum1 = shards[9][byte];
            for (uint8_t source = 0; source < kSourceShardsPerBlock; ++source)
            {
                if (source == first || source == second)
                {
                    continue;
                }
                sum0 ^= shards[source][byte];
                sum1 ^= gfMultiply(parityCoefficient(source), shards[source][byte]);
            }
            const uint8_t first_value = gfMultiply(
                inverse,
                static_cast<uint8_t>(sum1 ^ gfMultiply(second_coefficient, sum0)));
            shards[first][byte] = first_value;
            shards[second][byte] = static_cast<uint8_t>(sum0 ^ first_value);
        }
    }

    for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        present[index] = true;
    }
    return true;
}

} // namespace chat::voice::vmp
