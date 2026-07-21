#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_announce_scheduler.h"

#include <cassert>

int main()
{
    chat::lxmf::runtime::AnnounceScheduler scheduler;

    scheduler.resetAfterConfig(1000, false);
    assert(!scheduler.next(2000, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(2500, false, 120000, 1500, 30000).should_send);

    scheduler.completeAttempt(2500, true, false);
    assert(!scheduler.next(32000, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(32500, false, 120000, 1500, 30000).should_send);

    scheduler.completeAttempt(32500, true, true);
    assert(!scheduler.next(150000, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(153000, false, 120000, 1500, 30000).should_send);

    scheduler.resetAfterConfig(200000, true);
    assert(!scheduler.next(400000, true, 120000, 1500, 30000).should_send);
    assert(!scheduler.beginManualBroadcast(true));

    scheduler.resetAfterConfig(400000, false);
    scheduler.markIdentityChanged();
    assert(!scheduler.next(400500, false, 120000, 1500, 30000).should_send);
    assert(scheduler.next(401500, false, 120000, 1500, 30000).should_send);

    assert(scheduler.rebroadcastDue(500000, 60000));
    assert(!scheduler.rebroadcastDue(550000, 60000));
    assert(scheduler.rebroadcastDue(560000, 60000));

    return 0;
}
