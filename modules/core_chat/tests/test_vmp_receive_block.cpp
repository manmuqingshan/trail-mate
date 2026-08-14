#include "chat/infra/voice/vmp_receive_block.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

using Shard = std::array<uint8_t, kMaxShardPayloadSize>;
using Block = std::array<Shard, kTotalShardsPerBlock>;

Block makeBlock()
{
    Block block{};
    for (uint8_t source = 0; source < kSourceShardsPerBlock; ++source)
    {
        for (std::size_t byte = 0; byte < kMaxShardPayloadSize; ++byte)
        {
            block[source][byte] = static_cast<uint8_t>(source * 31U + byte);
        }
    }
    const uint8_t* sources[kSourceShardsPerBlock] = {};
    for (std::size_t source = 0; source < kSourceShardsPerBlock; ++source)
    {
        sources[source] = block[source].data();
    }
    assert(encodeRs10_8(sources,
                        kMaxShardPayloadSize,
                        block[8].data(),
                        block[9].data()));
    return block;
}

DataHeader shardHeader(uint8_t index)
{
    DataHeader header{};
    header.type = DataType::Shard;
    header.session_id = 1234U;
    header.block_index = 0;
    header.shard_index = index;
    header.payload_len = kMaxShardPayloadSize;
    header.flags = DataFlagFinalBlock;
    return header;
}

void test_recovers_two_lost_shards()
{
    MediaLayout layout{};
    assert(planMediaLayout(813U, &layout));
    ReceiveBlock receiver{};
    assert(receiver.begin(layout));
    const Block block = makeBlock();

    for (uint8_t index = 0; index < kTotalShardsPerBlock; ++index)
    {
        if (index == 2U || index == 8U)
        {
            continue;
        }
        const ReceiveBlockResult result = receiver.accept(
            shardHeader(index), block[index].data(), block[index].size());
        assert(result == ReceiveBlockResult::Accepted ||
               result == ReceiveBlockResult::Complete);
    }
    assert(receiver.receivedShardCount() == kSourceShardsPerBlock);

    std::array<uint8_t, kMaxEncodedMediaSize> decoded{};
    std::size_t decoded_len = 0;
    assert(receiver.recover(decoded.data(), decoded.size(), &decoded_len));
    assert(decoded_len == layout.encoded_media_len);

    std::array<uint8_t, kMaxEncodedMediaSize> expected{};
    for (uint8_t source = 0; source < layout.source_shard_count; ++source)
    {
        std::memcpy(expected.data() + source * kMaxShardPayloadSize,
                    block[source].data(),
                    kMaxShardPayloadSize);
    }
    assert(std::memcmp(decoded.data(), expected.data(), decoded_len) == 0);
}

void test_duplicates_and_variable_shards_are_rejected()
{
    MediaLayout layout{};
    assert(planMediaLayout(160U, &layout));
    ReceiveBlock receiver{};
    assert(receiver.begin(layout));
    const Block block = makeBlock();
    const DataHeader header = shardHeader(0U);
    assert(receiver.accept(header, block[0].data(), block[0].size()) ==
           ReceiveBlockResult::Accepted);
    assert(receiver.accept(header, block[0].data(), block[0].size()) ==
           ReceiveBlockResult::Duplicate);
    assert(receiver.accept(header, block[0].data(), block[0].size() - 1U) ==
           ReceiveBlockResult::Invalid);
    assert(receiver.receivedShardCount() == 1U);
}

} // namespace

int main()
{
    test_recovers_two_lost_shards();
    test_duplicates_and_variable_shards_are_rejected();
    return 0;
}
