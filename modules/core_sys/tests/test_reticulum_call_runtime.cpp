#include "platform/ui/reticulum_call_runtime.h"
#include "platform/ui/screen_runtime.h"
#include "sys/clock.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

struct Harness
{
    bool media_supported = true;
    bool media_start_ok = true;
    bool exclusive_ok = true;
    int media_start_count = 0;
    int media_stop_count = 0;
    int ringing_count = 0;
    int exclusive_count = 0;
    int closing_count = 0;
    int realtime_end_count = 0;
    int modal_wake_count = 0;
    int sleep_disable_count = 0;
    int sleep_enable_count = 0;
    int activity_count = 0;
    uint32_t now_ms = 100;
};

Harness g;

uint32_t fake_millis()
{
    return ++g.now_ms;
}

bool media_supported()
{
    return g.media_supported;
}

bool media_start()
{
    ++g.media_start_count;
    return g.media_start_ok;
}

void media_stop()
{
    ++g.media_stop_count;
}

bool begin_ringing(const uint8_t*)
{
    ++g.ringing_count;
    return true;
}

bool begin_exclusive(const uint8_t*)
{
    ++g.exclusive_count;
    return g.exclusive_ok;
}

void begin_closing(const uint8_t*, bool)
{
    ++g.closing_count;
}

void end_realtime(const uint8_t*)
{
    ++g.realtime_end_count;
}

void configure_hooks()
{
    platform::ui::reticulum_call::MediaHooks media{};
    media.is_supported = media_supported;
    media.start = media_start;
    media.stop = media_stop;
    platform::ui::reticulum_call::set_media_hooks(media);

    platform::ui::reticulum_call::RealtimeHooks realtime{};
    realtime.begin_ringing = begin_ringing;
    realtime.begin_exclusive = begin_exclusive;
    realtime.begin_closing = begin_closing;
    realtime.end = end_realtime;
    platform::ui::reticulum_call::set_realtime_hooks(realtime);
}

platform::ui::reticulum_call::Peer make_peer(uint8_t seed, bool incoming)
{
    platform::ui::reticulum_call::Peer peer{};
    for (std::size_t index = 0;
         index < platform::ui::reticulum_call::kHashSize;
         ++index)
    {
        peer.link_id[index] = static_cast<uint8_t>(seed + index);
        peer.destination_hash[index] = static_cast<uint8_t>(seed + 0x20U + index);
    }
    peer.display_name = incoming ? "Incoming peer" : "Outgoing peer";
    peer.incoming = incoming;
    return peer;
}

void complete_local_close(const platform::ui::reticulum_call::Peer& peer)
{
    uint8_t link_id[platform::ui::reticulum_call::kHashSize] = {};
    assert(platform::ui::reticulum_call::consume_hangup_request(link_id));
    assert(std::memcmp(link_id, peer.link_id, sizeof(link_id)) == 0);
    platform::ui::reticulum_call::notify_link_closed(peer.link_id);
    platform::ui::reticulum_call::service_ui_runtime();
    assert(platform::ui::reticulum_call::realtime_phase() ==
           platform::ui::reticulum_call::RealtimePhase::Idle);
}

void test_link_active_before_accept()
{
    const auto peer = make_peer(0x10, true);
    assert(platform::ui::reticulum_call::begin_incoming(peer));
    platform::ui::reticulum_call::service_ui_runtime();

    auto snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Incoming);
    assert(!snapshot.accepted);
    assert(!snapshot.link_active);
    assert(!snapshot.media_active);
    assert(snapshot.wire_profile ==
           platform::ui::reticulum_call::WireProfile::SidebandLxst);
    assert(snapshot.codec2_mode ==
           platform::ui::reticulum_call::Codec2Mode::Mode3200);

    platform::ui::reticulum_call::mark_link_active(peer.link_id);
    snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Incoming);
    assert(snapshot.link_active);
    assert(g.media_start_count == 0);

    assert(platform::ui::reticulum_call::accept());
    snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Active);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::ActiveCall);
    assert(snapshot.media_active);
    assert(g.media_start_count == 1);

    platform::ui::reticulum_call::hangup();
    assert(g.media_stop_count == 1);
    assert(platform::ui::reticulum_call::realtime_phase() ==
           platform::ui::reticulum_call::RealtimePhase::ClosingCall);
    complete_local_close(peer);
}

void test_accept_before_link_active()
{
    const int starts_before = g.media_start_count;
    const auto peer = make_peer(0x30, true);
    assert(platform::ui::reticulum_call::begin_incoming(peer));
    platform::ui::reticulum_call::service_ui_runtime();
    assert(platform::ui::reticulum_call::accept());

    auto snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Active);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::AcceptedStarting);
    assert(!snapshot.link_active);
    assert(!snapshot.media_active);
    assert(g.media_start_count == starts_before);

    platform::ui::reticulum_call::mark_link_active(peer.link_id);
    snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.link_active);
    assert(snapshot.media_active);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::ActiveCall);
    assert(g.media_start_count == starts_before + 1);

    platform::ui::reticulum_call::hangup();
    complete_local_close(peer);
}

void test_exclusive_denial_closes_call()
{
    const auto peer = make_peer(0x50, true);
    assert(platform::ui::reticulum_call::begin_incoming(peer));
    platform::ui::reticulum_call::service_ui_runtime();
    g.exclusive_ok = false;
    assert(!platform::ui::reticulum_call::accept());

    const auto snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Failed);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::ClosingCall);
    assert(!snapshot.accepted);
    assert(!snapshot.media_active);

    g.exclusive_ok = true;
    complete_local_close(peer);
}

void test_media_start_failure_closes_link()
{
    const auto peer = make_peer(0x70, true);
    assert(platform::ui::reticulum_call::begin_incoming(peer));
    platform::ui::reticulum_call::service_ui_runtime();
    platform::ui::reticulum_call::mark_link_active(peer.link_id);
    g.media_start_ok = false;
    const int stops_before = g.media_stop_count;
    assert(!platform::ui::reticulum_call::accept());

    const auto snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Failed);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::ClosingCall);
    assert(!snapshot.accepted);
    assert(!snapshot.link_active);
    assert(!snapshot.media_active);
    assert(g.media_stop_count == stops_before + 1);

    g.media_start_ok = true;
    complete_local_close(peer);
}

void test_outgoing_hard_preempt_waits_for_link()
{
    const int starts_before = g.media_start_count;
    auto peer = make_peer(0x90, false);
    peer.wire_profile =
        platform::ui::reticulum_call::WireProfile::MeshChatCallAudio;
    peer.codec2_mode =
        platform::ui::reticulum_call::Codec2Mode::Mode1200;
    assert(platform::ui::reticulum_call::begin_outgoing(peer));
    platform::ui::reticulum_call::service_ui_runtime();

    auto snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Outgoing);
    assert(snapshot.accepted);
    assert(!snapshot.link_active);
    assert(!snapshot.media_active);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::AcceptedStarting);
    assert(snapshot.wire_profile ==
           platform::ui::reticulum_call::WireProfile::MeshChatCallAudio);
    assert(snapshot.codec2_mode ==
           platform::ui::reticulum_call::Codec2Mode::Mode1200);

    platform::ui::reticulum_call::mark_link_active(peer.link_id);
    snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Active);
    assert(snapshot.media_active);
    assert(g.media_start_count == starts_before + 1);

    platform::ui::reticulum_call::notify_media_failed();
    snapshot = platform::ui::reticulum_call::snapshot();
    assert(snapshot.state == platform::ui::reticulum_call::State::Failed);
    assert(snapshot.realtime_phase ==
           platform::ui::reticulum_call::RealtimePhase::ClosingCall);
    complete_local_close(peer);
}

} // namespace

namespace platform::ui::screen
{

void wake_for_modal()
{
    ++g.modal_wake_count;
}

void disable_sleep()
{
    ++g.sleep_disable_count;
}

void enable_sleep()
{
    ++g.sleep_enable_count;
}

void update_user_activity()
{
    ++g.activity_count;
}

} // namespace platform::ui::screen

int main()
{
    sys::set_millis_provider(fake_millis);
    configure_hooks();

    test_link_active_before_accept();
    test_accept_before_link_active();
    test_exclusive_denial_closes_call();
    test_media_start_failure_closes_link();
    test_outgoing_hard_preempt_waits_for_link();

    assert(g.modal_wake_count == 5);
    assert(g.sleep_disable_count == 5);
    assert(g.sleep_enable_count == 5);
    assert(g.activity_count == 5);
    return 0;
}
