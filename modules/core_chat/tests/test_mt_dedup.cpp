#include "chat/infra/meshtastic/mt_dedup.h"
#include "sys/clock.h"

#include <cassert>
#include <cstdint>

namespace
{

uint32_t g_now_ms = 1000;

uint32_t fake_millis()
{
    return g_now_ms;
}

} // namespace

int main()
{
    sys::set_millis_provider(fake_millis);

    chat::meshtastic::MtDedup dedup;
    constexpr chat::NodeId from = 0xA1B3B57C;
    constexpr uint32_t packet_id = 0x10203040;

    assert(!dedup.isDuplicate(from, packet_id, 0x01));
    dedup.markSeen(from, packet_id, 0x01);
    assert(dedup.isDuplicate(from, packet_id, 0x01));
    assert(!dedup.isDuplicate(from, packet_id, 0x02));

    dedup.markSeen(from, packet_id, 0x02);
    assert(dedup.isDuplicate(from, packet_id, 0x01));
    assert(dedup.isDuplicate(from, packet_id, 0x02));

    g_now_ms += chat::meshtastic::MtDedup::CACHE_TIMEOUT_MS + 30001U;
    assert(!dedup.isDuplicate(from, packet_id, 0x01));
    assert(!dedup.isDuplicate(from, packet_id, 0x02));

    return 0;
}
