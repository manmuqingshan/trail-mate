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
    bool hangup_requested = false;
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
    s_state.snapshot.updated_ms = now_ms();
}

void stop_media_outside_lock(MediaHooks hooks)
{
    if (hooks.stop)
    {
        hooks.stop();
    }
}

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
                       s_state.snapshot.state == State::Active &&
                       !s_state.snapshot.media_active;
    }
    if (!should_start)
    {
        return true;
    }
    if (!hooks.start || !hooks.start())
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        s_state.snapshot.state = State::Failed;
        s_state.snapshot.media_active = false;
        s_state.snapshot.updated_ms = now_ms();
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    s_state.snapshot.media_active = true;
    s_state.snapshot.updated_ms = now_ms();
    return true;
}

void close_call(State final_state, bool request_remote_close)
{
    MediaHooks hooks{};
    bool stop_media = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        hooks = s_state.hooks;
        stop_media = s_state.snapshot.media_active;
        s_state.snapshot.media_active = false;
        s_state.snapshot.accepted = false;
        s_state.snapshot.state = final_state;
        s_state.snapshot.updated_ms = now_ms();
        s_state.hangup_requested = request_remote_close &&
                                   !hash_is_empty(s_state.snapshot.link_id);
        clear_queues_locked();
    }
    if (stop_media)
    {
        stop_media_outside_lock(hooks);
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
    if (!hashes_equal(link_id, s_state.snapshot.link_id))
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

void set_wifi_ready(bool ready)
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    s_state.snapshot.wifi_ready = ready;
    s_state.snapshot.updated_ms = now_ms();
}

bool begin_incoming(const Peer& peer)
{
    if (hash_is_empty(peer.link_id))
    {
        return false;
    }
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
        s_state.snapshot.media_supported =
            s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
        apply_peer_locked(peer);
        clear_queues_locked();
        s_state.hangup_requested = false;
    }
    screen::wake_saver();
    return true;
}

bool begin_outgoing(const Peer& peer)
{
    if (hash_is_empty(peer.link_id) || hash_is_empty(peer.destination_hash))
    {
        return false;
    }
    std::lock_guard<std::mutex> lock(s_state.mutex);
    if (s_state.snapshot.state == State::Active ||
        s_state.snapshot.state == State::Incoming ||
        s_state.snapshot.state == State::Outgoing)
    {
        return false;
    }
    s_state.snapshot = Snapshot{};
    s_state.snapshot.supported = true;
    s_state.snapshot.incoming = false;
    s_state.snapshot.accepted = false;
    s_state.snapshot.state = State::Outgoing;
    s_state.snapshot.media_supported =
        s_state.hooks.is_supported ? s_state.hooks.is_supported() : false;
    apply_peer_locked(peer);
    clear_queues_locked();
    s_state.hangup_requested = false;
    return true;
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
    bool should_start = false;
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (!hashes_equal(s_state.snapshot.link_id, link_id))
        {
            return;
        }
        if (s_state.snapshot.state == State::Outgoing)
        {
            s_state.snapshot.accepted = true;
            s_state.snapshot.state = State::Active;
            should_start = true;
        }
        else if (s_state.snapshot.state == State::Incoming &&
                 s_state.snapshot.accepted)
        {
            s_state.snapshot.state = State::Active;
            should_start = true;
        }
        s_state.snapshot.updated_ms = now_ms();
    }
    if (should_start)
    {
        (void)start_media_if_needed();
    }
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

bool accept()
{
    {
        std::lock_guard<std::mutex> lock(s_state.mutex);
        if (s_state.snapshot.state != State::Incoming &&
            s_state.snapshot.state != State::Active)
        {
            return false;
        }
        s_state.snapshot.accepted = true;
        s_state.snapshot.state = State::Active;
        s_state.snapshot.updated_ms = now_ms();
    }
    return start_media_if_needed();
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

bool media_should_run()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.snapshot.state == State::Active &&
           s_state.snapshot.accepted &&
           s_state.snapshot.media_active;
}

bool realtime_mode_active()
{
    std::lock_guard<std::mutex> lock(s_state.mutex);
    return s_state.hangup_requested ||
           s_state.snapshot.state == State::Incoming ||
           s_state.snapshot.state == State::Outgoing ||
           s_state.snapshot.state == State::Active;
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
