/**
 * @file reticulum_call_runtime.cpp
 * @brief Shared Reticulum call state, UI facts, and audio packet queues.
 */

#include "platform/ui/reticulum_call_runtime.h"

#include "platform/ui/screen_runtime.h"
#include "sys/clock.h"
#include "sys/ringbuf.h"

#include <cstring>
#include <mutex>

namespace platform::ui::reticulum_call
{
namespace
{

constexpr std::size_t kQueueDepth = 8;

struct RuntimeState
{
    std::mutex mutex;
    Snapshot snapshot;
    MediaHooks hooks;
    RealtimeHooks realtime_hooks;
    bool hangup_requested = false;
    bool closing_keep_exclusive = false;
    bool sleep_wake_lease = false;
    bool sleep_wake_lease_applied = false;
    sys::RingBuffer<AudioPacket, kQueueDepth> inbound;
    sys::RingBuffer<AudioPacket, kQueueDepth> outbound;
};

RuntimeState s_state;

bool hashes_equal(const uint8_t* a, const uint8_t* b)
{
    return a && b && std::memcmp(a, b, kHashSize) == 0;
}

bool hash_is_empty(const uint8_t* hash)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < kHashSize; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

void copy_hash(uint8_t* out, const uint8_t* in)
{
    if (out && in)
    {
        std::memcpy(out, in, kHashSize);
    }
}

void copy_name(char* out, std::size_t out_len, const char* in)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!in)
    {
        return;
    }
    std::strncpy(out, in, out_len - 1);
    out[out_len - 1] = '\0';
}

uint32_t now_ms()
{
    return sys::millis_now();
}

uint8_t clamp_volume(uint8_t volume_percent)
{
    return volume_percent > 100U ? 100U : volume_percent;
}

void clear_queues_locked()
{
    s_state.inbound.clear();
    s_state.outbound.clear();
}

void apply_peer_locked(const Peer& peer)
{
    copy_hash(s_state.snapshot.link_id, peer.link_id);
    if (!hash_is_empty(peer.destination_hash))
    {
        copy_hash(s_state.snapshot.peer_destination_hash, peer.destination_hash);
    }
    if (!hash_is_empty(peer.identity_hash))
    {
        copy_hash(s_state.snapshot.peer_identity_hash, peer.identity_hash);
    }
    if (peer.display_name && peer.display_name[0] != '\0')
    {
        copy_name(s_state.snapshot.peer_name,
                  sizeof(s_state.snapshot.peer_name),
                  peer.display_name);
    }
    s_state.snapshot.codec2_mode = peer.codec2_mode;
    s_state.snapshot.wire_profile = peer.wire_profile;
    s_state.snapshot.updated_ms = now_ms();
}

void stop_media_outside_lock(MediaHooks hooks)
{
    if (hooks.stop)
    {
        hooks.stop();
    }
}

bool begin_soft_preempt_outside_lock(RealtimeHooks hooks,
                                     const uint8_t link_id[kHashSize])
{
    return !hooks.begin_soft_preempt || hooks.begin_soft_preempt(link_id);
}

bool begin_ringing_alert_outside_lock(RealtimeHooks hooks,
                                      const uint8_t link_id[kHashSize])
{
    return !hooks.begin_ringing_alert || hooks.begin_ringing_alert(link_id);
}

bool begin_exclusive_outside_lock(RealtimeHooks hooks,
                                  const uint8_t link_id[kHashSize])
{
    return !hooks.begin_exclusive || hooks.begin_exclusive(link_id);
}

void begin_closing_outside_lock(RealtimeHooks hooks,
                                const uint8_t link_id[kHashSize],
                                bool keep_exclusive)
{
    if (hooks.begin_closing)
    {
        hooks.begin_closing(link_id, keep_exclusive);
    }
}

void end_realtime_outside_lock(RealtimeHooks hooks,
                               const uint8_t link_id[kHashSize])
{
    if (hooks.end)
    {
        hooks.end(link_id);
    }
}

bool acquire_sleep_wake_lease_locked()
{
    if (s_state.sleep_wake_lease)
    {
        return false;
    }
    s_state.sleep_wake_lease = true;
    return true;
}

bool release_sleep_wake_lease_locked()
{
    if (!s_state.sleep_wake_lease)
    {
        return false;
    }
    s_state.sleep_wake_lease = false;
    return true;
}

void close_call(State final_state, bool request_remote_close);

bool start_media_if_needed()
{
    MediaHooks hooks{};
    bool should_start = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        hooks = s_state.hooks;
        s_state.snapshot.media_supported =
            hooks.is_supported ? hooks.is_supported() : false;
        should_start = s_state.snapshot.accepted &&
                       s_state.snapshot.link_active &&
                       !s_state.snapshot.media_active &&
                       (s_state.snapshot.realtime_phase ==
                            RealtimePhase::AcceptedStarting ||
                        s_state.snapshot.realtime_phase ==
                            RealtimePhase::ActiveCall);
        if (should_start)
        {
            // Reserve media ownership before the task starts so a newly scheduled
            // task cannot observe a transient inactive state and exit immediately.
            s_state.snapshot.media_active = true;
            s_state.snapshot.updated_ms = now_ms();
        }
    }
    if (!should_start)
    {
        return true;
    }
    if (!hooks.start || !hooks.start())
    {
        close_call(State::Failed, true);
        return false;
    }
    return true;
}

void close_call(State final_state, bool request_remote_close)
{
    MediaHooks hooks{};
    RealtimeHooks realtime_hooks{};
    uint8_t link_id[kHashSize] = {};
    bool stop_media = false;
    bool end_realtime = false;
    bool begin_closing = false;
    bool keep_exclusive = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        hooks = s_state.hooks;
        realtime_hooks = s_state.realtime_hooks;
        copy_hash(link_id, s_state.snapshot.link_id);
        stop_media = s_state.snapshot.media_active;
        keep_exclusive =
            s_state.snapshot.realtime_phase == RealtimePhase::AcceptedStarting ||
            s_state.snapshot.realtime_phase == RealtimePhase::ActiveCall ||
            s_state.snapshot.realtime_phase == RealtimePhase::ClosingCall;
        s_state.snapshot.media_active = false;
        s_state.snapshot.accepted = false;
        s_state.snapshot.link_active = false;
        s_state.snapshot.state = final_state;
        s_state.snapshot.updated_ms = now_ms();
        s_state.hangup_requested = request_remote_close &&
                                   !hash_is_empty(s_state.snapshot.link_id);
        if (s_state.hangup_requested)
        {
            s_state.snapshot.realtime_phase = RealtimePhase::ClosingCall;
            s_state.closing_keep_exclusive = keep_exclusive;
            begin_closing = true;
        }
        else
        {
            end_realtime =
                s_state.snapshot.realtime_phase != RealtimePhase::Idle &&
                !hash_is_empty(link_id);
            s_state.snapshot.realtime_phase = RealtimePhase::Idle;
            s_state.closing_keep_exclusive = false;
            (void)release_sleep_wake_lease_locked();
        }
        clear_queues_locked();
    }
    if (stop_media)
    {
        stop_media_outside_lock(hooks);
    }
    if (begin_closing)
    {
        begin_closing_outside_lock(realtime_hooks, link_id, keep_exclusive);
    }
    else if (end_realtime)
    {
        end_realtime_outside_lock(realtime_hooks, link_id);
    }
}

bool enqueue_packet(sys::RingBuffer<AudioPacket, kQueueDepth>& queue,
                    const uint8_t link_id[kHashSize],
                    const uint8_t* data,
                    std::size_t len,
                    bool inbound)
{
    if (!link_id || !data || len == 0 || len > kAudioPacketMaxLen)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (!hashes_equal(link_id, s_state.snapshot.link_id) ||
        !s_state.snapshot.accepted ||
        !s_state.snapshot.link_active ||
        !s_state.snapshot.media_active)
    {
        return false;
    }

    AudioPacket packet{};
    copy_hash(packet.link_id, link_id);
    std::memcpy(packet.data, data, len);
    packet.len = len;
    bool dropped = false;
    queue.pushDropOldest(packet, &dropped);
    if (inbound)
    {
        ++s_state.snapshot.rx_packets;
        if (dropped)
        {
            ++s_state.snapshot.rx_dropped;
        }
    }
    else if (dropped)
    {
        ++s_state.snapshot.tx_dropped;
    }
    s_state.snapshot.updated_ms = now_ms();
    return true;
}

} // namespace

void set_media_hooks(const MediaHooks& hooks)
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    s_state.hooks = hooks;
    s_state.snapshot.media_supported =
        hooks.is_supported ? hooks.is_supported() : false;
}

uint8_t speaker_volume()
{
    MediaHooks hooks{};
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        hooks = s_state.hooks;
    }
    return hooks.speaker_volume ? hooks.speaker_volume() : 0U;
}

void set_speaker_volume(uint8_t volume_percent)
{
    MediaHooks hooks{};
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        hooks = s_state.hooks;
    }
    if (hooks.set_speaker_volume)
    {
        hooks.set_speaker_volume(clamp_volume(volume_percent));
    }
}

void set_realtime_hooks(const RealtimeHooks& hooks)
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    s_state.realtime_hooks = hooks;
}

void set_wifi_ready(bool ready)
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    s_state.snapshot.wifi_ready = ready;
    s_state.snapshot.updated_ms = now_ms();
}

bool begin_incoming(const Peer& peer)
{
    if (!begin_incoming_identifying(peer))
    {
        return false;
    }
    update_peer(peer);
    return mark_incoming_ringing(peer.link_id);
}

bool begin_incoming_identifying(const Peer& peer)
{
    if (hash_is_empty(peer.link_id))
    {
        return false;
    }
    RealtimeHooks realtime_hooks{};
    uint8_t link_id[kHashSize] = {};
    bool start_soft_preempt = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (s_state.snapshot.state == State::Active ||
            s_state.snapshot.state == State::Incoming ||
            s_state.snapshot.state == State::Outgoing)
        {
            return hashes_equal(s_state.snapshot.link_id, peer.link_id);
        }
        s_state.snapshot = Snapshot{};
        s_state.snapshot.supported = true;
        s_state.snapshot.incoming = true;
        s_state.snapshot.state = State::Incoming;
        s_state.snapshot.realtime_phase = RealtimePhase::IncomingIdentifying;
        s_state.snapshot.media_supported =
            s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
        apply_peer_locked(peer);
        realtime_hooks = s_state.realtime_hooks;
        copy_hash(link_id, s_state.snapshot.link_id);
        clear_queues_locked();
        s_state.hangup_requested = false;
        s_state.closing_keep_exclusive = false;
        (void)acquire_sleep_wake_lease_locked();
        start_soft_preempt = true;
    }
    if (start_soft_preempt &&
        !begin_soft_preempt_outside_lock(realtime_hooks, link_id))
    {
        close_call(State::Failed, true);
        return false;
    }
    return true;
}

bool mark_incoming_ringing(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return false;
    }

    RealtimeHooks realtime_hooks{};
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
            s_state.snapshot.state != State::Incoming ||
            (s_state.snapshot.realtime_phase !=
                 RealtimePhase::IncomingIdentifying &&
             s_state.snapshot.realtime_phase !=
                 RealtimePhase::IncomingRinging))
        {
            return false;
        }
        if (s_state.snapshot.realtime_phase == RealtimePhase::IncomingRinging)
        {
            return true;
        }
        s_state.snapshot.realtime_phase = RealtimePhase::IncomingRinging;
        s_state.snapshot.updated_ms = now_ms();
        realtime_hooks = s_state.realtime_hooks;
    }

    if (!begin_ringing_alert_outside_lock(realtime_hooks, link_id))
    {
        close_call(State::Failed, true);
        return false;
    }
    return true;
}

bool begin_outgoing(const Peer& peer)
{
    if (hash_is_empty(peer.link_id) || hash_is_empty(peer.destination_hash))
    {
        return false;
    }
    RealtimeHooks realtime_hooks{};
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (s_state.snapshot.state == State::Active ||
            s_state.snapshot.state == State::Incoming ||
            s_state.snapshot.state == State::Outgoing)
        {
            return hashes_equal(s_state.snapshot.link_id, peer.link_id);
        }
        realtime_hooks = s_state.realtime_hooks;
    }
    if (!begin_exclusive_outside_lock(realtime_hooks, peer.link_id))
    {
        return false;
    }
    set_speaker_volume(100);
    bool started = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (s_state.snapshot.state == State::Active ||
            s_state.snapshot.state == State::Incoming ||
            s_state.snapshot.state == State::Outgoing)
        {
            if (hashes_equal(s_state.snapshot.link_id, peer.link_id))
            {
                return true;
            }
            realtime_hooks = s_state.realtime_hooks;
        }
        else
        {
            s_state.snapshot = Snapshot{};
            s_state.snapshot.supported = true;
            s_state.snapshot.incoming = false;
            s_state.snapshot.accepted = true;
            s_state.snapshot.state = State::Outgoing;
            s_state.snapshot.realtime_phase = RealtimePhase::AcceptedStarting;
            s_state.snapshot.media_supported =
                s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
            apply_peer_locked(peer);
            clear_queues_locked();
            s_state.hangup_requested = false;
            s_state.closing_keep_exclusive = false;
            (void)acquire_sleep_wake_lease_locked();
            started = true;
        }
    }
    if (started)
    {
        return true;
    }
    end_realtime_outside_lock(realtime_hooks, peer.link_id);
    return false;
}

void update_peer(const Peer& peer)
{
    if (hash_is_empty(peer.link_id))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (!hashes_equal(s_state.snapshot.link_id, peer.link_id))
    {
        return;
    }
    apply_peer_locked(peer);
}

void mark_link_active(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (!hashes_equal(s_state.snapshot.link_id, link_id))
    {
        return;
    }
    if (s_state.snapshot.state != State::Incoming &&
        s_state.snapshot.state != State::Outgoing &&
        s_state.snapshot.state != State::Active)
    {
        return;
    }
    s_state.snapshot.link_active = true;
    s_state.snapshot.updated_ms = now_ms();
}

bool prepare_media(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
            !s_state.snapshot.accepted ||
            !s_state.snapshot.link_active ||
            (s_state.snapshot.realtime_phase !=
                 RealtimePhase::AcceptedStarting &&
             s_state.snapshot.realtime_phase != RealtimePhase::ActiveCall))
        {
            return false;
        }
        if (s_state.snapshot.media_active)
        {
            return true;
        }
    }
    if (!start_media_if_needed())
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return hashes_equal(s_state.snapshot.link_id, link_id) &&
           s_state.snapshot.accepted && s_state.snapshot.link_active &&
           s_state.snapshot.media_active;
}

bool mark_call_active(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
        !s_state.snapshot.accepted ||
        !s_state.snapshot.link_active ||
        !s_state.snapshot.media_active ||
        (s_state.snapshot.state != State::Incoming &&
         s_state.snapshot.state != State::Outgoing &&
         s_state.snapshot.state != State::Active))
    {
        return false;
    }
    s_state.snapshot.state = State::Active;
    s_state.snapshot.realtime_phase = RealtimePhase::ActiveCall;
    s_state.snapshot.updated_ms = now_ms();
    return true;
}

void notify_link_closed(const uint8_t link_id[kHashSize])
{
    if (hash_is_empty(link_id))
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id))
        {
            return;
        }
    }
    close_call(State::Ended, false);
}

void notify_media_failed()
{
    bool should_close = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        should_close = s_state.snapshot.accepted &&
                       s_state.snapshot.media_active;
    }
    if (should_close)
    {
        close_call(State::Failed, true);
    }
}

void service_ui_runtime()
{
    bool apply_lease = false;
    bool release_lease = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (s_state.sleep_wake_lease_applied != s_state.sleep_wake_lease)
        {
            s_state.sleep_wake_lease_applied = s_state.sleep_wake_lease;
            apply_lease = s_state.sleep_wake_lease;
            release_lease = !s_state.sleep_wake_lease;
        }
    }

    if (apply_lease)
    {
        screen::wake_for_modal();
        screen::disable_sleep();
        screen::update_user_activity();
    }
    else if (release_lease)
    {
        screen::enable_sleep();
    }
}

bool accept()
{
    RealtimeHooks realtime_hooks{};
    uint8_t link_id[kHashSize] = {};
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (s_state.snapshot.state != State::Incoming ||
            s_state.snapshot.realtime_phase !=
                RealtimePhase::IncomingRinging)
        {
            return false;
        }
        realtime_hooks = s_state.realtime_hooks;
        copy_hash(link_id, s_state.snapshot.link_id);
    }
    if (!begin_exclusive_outside_lock(realtime_hooks, link_id))
    {
        close_call(State::Failed, true);
        return false;
    }
    set_speaker_volume(100);
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id) ||
            s_state.snapshot.state != State::Incoming ||
            s_state.snapshot.realtime_phase !=
                RealtimePhase::IncomingRinging)
        {
            realtime_hooks = s_state.realtime_hooks;
        }
        else
        {
            s_state.snapshot.accepted = true;
            s_state.snapshot.realtime_phase =
                RealtimePhase::AcceptedStarting;
            s_state.snapshot.updated_ms = now_ms();
            (void)acquire_sleep_wake_lease_locked();
            accepted = true;
        }
    }
    if (accepted)
    {
        return true;
    }
    end_realtime_outside_lock(realtime_hooks, link_id);
    return false;
}

void reject()
{
    close_call(State::Ended, true);
}

void hangup()
{
    close_call(State::Ended, true);
}

bool consume_hangup_request(uint8_t out_link_id[kHashSize])
{
    if (!out_link_id)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (!s_state.hangup_requested ||
        hash_is_empty(s_state.snapshot.link_id))
    {
        return false;
    }
    copy_hash(out_link_id, s_state.snapshot.link_id);
    s_state.hangup_requested = false;
    return true;
}

Snapshot snapshot()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    Snapshot copy = s_state.snapshot;
    copy.media_supported =
        s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
    return copy;
}

State state()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.snapshot.state;
}

RealtimePhase realtime_phase()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.snapshot.realtime_phase;
}

bool media_should_run()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.snapshot.accepted &&
           s_state.snapshot.link_active &&
           s_state.snapshot.media_active &&
           (s_state.snapshot.realtime_phase ==
                RealtimePhase::AcceptedStarting ||
            s_state.snapshot.realtime_phase == RealtimePhase::ActiveCall);
}

bool realtime_mode_active()
{
    return resource_preempt_active();
}

bool modal_active()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.snapshot.realtime_phase != RealtimePhase::Idle;
}

bool resource_preempt_active()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.hangup_requested ||
           s_state.snapshot.realtime_phase != RealtimePhase::Idle;
}

bool wifi_exclusive_active()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.snapshot.realtime_phase == RealtimePhase::AcceptedStarting ||
           s_state.snapshot.realtime_phase == RealtimePhase::ActiveCall ||
           (s_state.snapshot.realtime_phase == RealtimePhase::ClosingCall &&
            s_state.closing_keep_exclusive);
}

bool current_link_id(uint8_t out_link_id[kHashSize])
{
    if (!out_link_id)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (hash_is_empty(s_state.snapshot.link_id))
    {
        return false;
    }
    copy_hash(out_link_id, s_state.snapshot.link_id);
    return true;
}

bool enqueue_inbound_audio(const uint8_t link_id[kHashSize],
                           const uint8_t* data,
                           std::size_t len)
{
    return enqueue_packet(s_state.inbound, link_id, data, len, true);
}

bool dequeue_inbound_audio(AudioPacket* out)
{
    if (!out)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.inbound.popOldest(out);
}

bool enqueue_outbound_audio(const uint8_t link_id[kHashSize],
                            const uint8_t* data,
                            std::size_t len)
{
    return enqueue_packet(s_state.outbound, link_id, data, len, false);
}

bool dequeue_outbound_audio(AudioPacket* out)
{
    if (!out)
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.outbound.popOldest(out);
}

void note_tx_sent()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    ++s_state.snapshot.tx_packets;
    s_state.snapshot.updated_ms = now_ms();
}

} // namespace platform::ui::reticulum_call
