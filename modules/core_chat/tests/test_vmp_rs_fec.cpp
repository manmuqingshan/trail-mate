#include "chat/infra/voice/vmp_rs_fec.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

constexpr std::size_t kShardSize = kMaxShardPayloadSize;
using Shard = std::array<uint8_t, kShardSize>;
using Block = std::array<Shard, kTotalShardsPerBlock>;

Block makeEncodedBlock()
{
    Block block{};
    for (std::size_t source = 0; source < kSourceShardsPerBlock; ++source)
    {
        for (std::size_t byte = 0; byte < kShardSize; ++byte)
        {
            block[source][byte] = static_cast<uint8_t>(
                (source * 67U + byte * 29U + (byte >> 2U)) & 0xFFU);
        }
    }

    const uint8_t* sources[kSourceShardsPerBlock] = {};
    for (std::size_t source = 0; source < kSourceShardsPerBlock; ++source)
    {
        sources[source] = block[source].data();
    }
    assert(encodeRs10_8(sources,
                        kShardSize,
                        block[8].data(),
                        block[9].data()));
    return block;
}

void recoverAndVerify(uint8_t missing_first, uint8_t missing_second)
{
    const Block expected = makeEncodedBlock();
    Block actual = expected;
    uint8_t* shards[kTotalShardsPerBlock] = {};
    bool present[kTotalShardsPerBlock] = {};
    for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        shards[index] = actual[index].data();
        present[index] = true;
    }
    present[missing_first] = false;
    present[missing_second] = false;
    std::memset(actual[missing_first].data(), 0, kShardSize);
    std::memset(actual[missing_second].data(), 0, kShardSize);

    assert(recoverRs10_8(shards, present, kShardSize));
    for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        assert(present[index]);
        assert(actual[index] == expected[index]);
    }
}

void test_all_single_and_double_erasures()
{
    for (uint8_t first = 0; first < kTotalShardsPerBlock; ++first)
    {
        for (uint8_t second = static_cast<uint8_t>(first + 1U);
             second < kTotalShardsPerBlock;
             ++second)
        {
            recoverAndVerify(first, second);
        }
    }
}

void test_three_erasures_fail()
{
    Block block = makeEncodedBlock();
    uint8_t* shards[kTotalShardsPerBlock] = {};
    bool present[kTotalShardsPerBlock] = {};
    for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        shards[index] = block[index].data();
        present[index] = true;
    }
    present[0] = false;
    present[1] = false;
    present[8] = false;
    assert(!recoverRs10_8(shards, present, kShardSize));
}

void test_invalid_arguments_fail()
{
    assert(!encodeRs10_8(nullptr, kShardSize, nullptr, nullptr));

    Block block = makeEncodedBlock();
    uint8_t* shards[kTotalShardsPerBlock] = {};
    bool present[kTotalShardsPerBlock] = {};
    for (std::size_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        shards[index] = block[index].data();
        present[index] = true;
    }
    assert(!recoverRs10_8(shards, present, 0));
}

} // namespace

int main()
{
    test_all_single_and_double_erasures();
    test_three_erasures_fail();
    test_invalid_arguments_fail();
    return 0;
}
