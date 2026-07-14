/**
 * @file reticulum_call_runtime.h
 * @brief Shared Reticulum call state, UI facts, and audio packet queues.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::reticulum_call
{

constexpr std::size_t kHashSize = 16;
constexpr std::size_t kDisplayNameSize = 32;
constexpr std::size_t kAudioPacketMaxLen = 256;

enum class State : uint8_t
{
    Idle = 0,
    Incoming = 1,
    Outgoing = 2,
    Active = 3,
    Ended = 4,
    Failed = 5,
};

enum class RealtimePhase : uint8_t
{
    Idle = 0,
    IncomingRinging,
    AcceptedStarting,
    ActiveCall,
    ClosingCall,
};

enum class WireProfile : uint8_t
{
    SidebandLxst = 0,
    MeshChatCallAudio = 1,
};

enum class Codec2Mode : uint8_t
{
    Mode700C = 0,
    Mode1200 = 1,
    Mode1600 = 2,
    Mode3200 = 3,
};

struct Peer
{
    uint8_t link_id[kHashSize] = {};
    uint8_t destination_hash[kHashSize] = {};
    uint8_t identity_hash[kHashSize] = {};
    const char* display_name = nullptr;
    bool incoming = false;
    WireProfile wire_profile = WireProfile::SidebandLxst;
    Codec2Mode codec2_mode = Codec2Mode::Mode3200;
};

struct Snapshot
{
    bool supported = true;
    bool media_supported = false;
    bool wifi_ready = false;
    State state = State::Idle;
    bool incoming = false;
    bool accepted = false;
    bool link_active = false;
    bool media_active = false;
    WireProfile wire_profile = WireProfile::SidebandLxst;
    Codec2Mode codec2_mode = Codec2Mode::Mode3200;
    RealtimePhase realtime_phase = RealtimePhase::Idle;
    uint8_t link_id[kHashSize] = {};
    uint8_t peer_destination_hash[kHashSize] = {};
    uint8_t peer_identity_hash[kHashSize] = {};
    char peer_name[kDisplayNameSize] = {};
    uint32_t updated_ms = 0;
    uint32_t rx_packets = 0;
    uint32_t tx_packets = 0;
    uint32_t rx_dropped = 0;
    uint32_t tx_dropped = 0;
};

struct AudioPacket
{
    uint8_t link_id[kHashSize] = {};
    uint8_t data[kAudioPacketMaxLen] = {};
    std::size_t len = 0;
};

struct MediaHooks
{
    bool (*is_supported)() = nullptr;
    bool (*start)() = nullptr;
    void (*stop)() = nullptr;
    uint8_t (*speaker_volume)() = nullptr;
    void (*set_speaker_volume)(uint8_t volume_percent) = nullptr;
};

struct RealtimeHooks
{
    bool (*begin_ringing)(const uint8_t link_id[kHashSize]) = nullptr;
    bool (*begin_exclusive)(const uint8_t link_id[kHashSize]) = nullptr;
    void (*begin_closing)(const uint8_t link_id[kHashSize],
                          bool keep_exclusive) = nullptr;
    void (*end)(const uint8_t link_id[kHashSize]) = nullptr;
};

void set_media_hooks(const MediaHooks& hooks);
void set_realtime_hooks(const RealtimeHooks& hooks);
uint8_t speaker_volume();
void set_speaker_volume(uint8_t volume_percent);
void set_wifi_ready(bool ready);

bool begin_incoming(const Peer& peer);
bool begin_outgoing(const Peer& peer);
void update_peer(const Peer& peer);
void mark_link_active(const uint8_t link_id[kHashSize]);
void notify_link_closed(const uint8_t link_id[kHashSize]);
void notify_media_failed();
void service_ui_runtime();

bool accept();
void reject();
void hangup();
bool consume_hangup_request(uint8_t out_link_id[kHashSize]);

Snapshot snapshot();
State state();
RealtimePhase realtime_phase();
bool media_should_run();
bool realtime_mode_active();
bool modal_active();
bool resource_preempt_active();
bool wifi_exclusive_active();
bool current_link_id(uint8_t out_link_id[kHashSize]);

bool enqueue_inbound_audio(const uint8_t link_id[kHashSize],
                           const uint8_t* data,
                           std::size_t len);
bool dequeue_inbound_audio(AudioPacket* out);
bool enqueue_outbound_audio(const uint8_t link_id[kHashSize],
                            const uint8_t* data,
                            std::size_t len);
bool dequeue_outbound_audio(AudioPacket* out);
void note_tx_sent();

} // namespace platform::ui::reticulum_call
