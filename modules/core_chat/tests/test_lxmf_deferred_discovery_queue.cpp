#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_deferred_discovery_queue.h"

#include <cassert>
#include <cstring>

namespace
{

namespace reticulum = chat::reticulum;

void fillHash(uint8_t hash[reticulum::kFullHashSize], uint8_t seed)
{
    for (std::size_t index = 0; index < reticulum::kFullHashSize; ++index)
    {
        hash[index] = static_cast<uint8_t>(seed + index);
    }
}

reticulum::interfaces::RxPacket makePacket(uint8_t seed)
{
    reticulum::interfaces::RxPacket packet{};
    packet.len = 3;
    packet.data[0] = seed;
    packet.data[1] = static_cast<uint8_t>(seed + 1U);
    packet.data[2] = static_cast<uint8_t>(seed + 2U);
    packet.interface_kind = reticulum::interfaces::InterfaceKind::WifiGateway;
    packet.interface_id = seed;
    packet.rx_meta.rssi_dbm_x10 = static_cast<int16_t>(-600 + seed);
    return packet;
}

} // namespace

int main()
{
    chat::lxmf::runtime::DeferredDiscoveryQueue queue;

    uint8_t first_hash[reticulum::kFullHashSize] = {};
    fillHash(first_hash, 1);
    bool dropped = true;
    assert(queue.push(makePacket(1), first_hash, &dropped));
    assert(!dropped);
    assert(queue.contains(first_hash));

    reticulum::interfaces::RxPacket out{};
    assert(queue.pop(&out));
    assert(out.len == 3);
    assert(out.data[0] == 1);
    assert(out.interface_kind == reticulum::interfaces::InterfaceKind::WifiGateway);
    assert(out.interface_id == 1);
    assert(out.rx_meta.rssi_dbm_x10 == -599);
    assert(!queue.pop(&out));

    for (uint8_t seed = 10; seed < 10 + chat::lxmf::runtime::DeferredDiscoveryQueue::kDepth + 1; ++seed)
    {
        uint8_t hash[reticulum::kFullHashSize] = {};
        fillHash(hash, seed);
        assert(queue.push(makePacket(seed), hash, &dropped));
    }
    assert(dropped);

    uint8_t oldest_hash[reticulum::kFullHashSize] = {};
    fillHash(oldest_hash, 10);
    assert(!queue.contains(oldest_hash));

    uint8_t newest_hash[reticulum::kFullHashSize] = {};
    fillHash(newest_hash, 10 + chat::lxmf::runtime::DeferredDiscoveryQueue::kDepth);
    assert(queue.contains(newest_hash));

    return 0;
}
