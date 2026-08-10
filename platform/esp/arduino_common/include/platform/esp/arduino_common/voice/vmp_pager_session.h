/**
 * @file vmp_pager_session.h
 * @brief Pager VMP session with LR1121 direct RF and SX1262 MQTT-only modes.
 *
 * On LR1121 the service receives direct VMP control through the dedicated
 * control runtime and switches the Pager radio through the VMP-exclusive
 * lease. On SX1262 it has no direct-radio or LXMF carrier and uses only an
 * enabled MT MQTT bridge. Every accepted object terminates in the local-only
 * VMP inbox; the service has no API that forwards or republishes a received
 * object.
 */

#pragma once

#include "chat/infra/voice/vmp_contact_secrets.h"
#include "chat/infra/voice/vmp_voice_inbox.h"

#include <cstddef>
#include <cstdint>

namespace platform::esp::arduino_common::voice::vmp_session
{

enum class StartSendResult : uint8_t
{
    Queued = 1,
    Unsupported = 2,
    Busy = 3,
    PrivateContactUnverified = 4,
};

/** LXMF AppData port reserved exclusively for the VMP VQ envelope carrier. */
inline constexpr uint32_t kLxmfAppDataPort = 0x564D5001UL;

/**
 * Sends one already-framed VMP VQ envelope through an authenticated LXMF
 * unicast.  The callback owns no VMP state and must copy the supplied bytes
 * before returning.
 */
using LxmfEnvelopeSender = bool (*)(void* context,
                                    uint32_t target_id,
                                    const uint8_t* envelope,
                                    std::size_t envelope_len);

/**
 * Derives a VMP-only contact secret from an already verified peer identity.
 *
 * This callback is deliberately scoped to VMP: it neither changes nor exposes
 * MT/MC protocol keys. It must fail unless the enclosing contact service has
 * independently verified the peer identity, keeping private VMP fail-closed.
 */
using VerifiedContactSecretDeriver = bool (*)(
    void* context,
    uint32_t peer_id,
    uint8_t out_secret[chat::voice::vmp::kPrivateKeySize]);

/**
 * Installs the VMP receiver on an already initialized Pager runtime.
 *
 * `durable_attachment_store` must mirror the active text-chat store policy:
 * when text is backed by SdStore, VMP remains unavailable until the same
 * deferred-storage lifecycle has restored its local attachment inbox.
 */
bool initialize(uint32_t self_node_id, bool durable_attachment_store);

/**
 * @brief True when this Pager can currently record and send a VMP object.
 *
 * LR1121 has the direct Sub-GHz/2.4 GHz carrier. SX1262 returns true only
 * while the isolated Meshtastic MQTT uplink is enabled; SX1262 never has a
 * direct-RF or LXMF VMP carrier.
 */
bool canRecordAndSend();

/** Marks the shared chat-storage hydration complete and starts VMP restore. */
void onPersistentStorageReady();

/** Retries a failed bounded VMP attachment restore without blocking UI work. */
void servicePersistentInbox();

/**
 * @brief Adds a key obtained by an already-completed verified contact pairing.
 *
 * This does not perform pairing or discover a peer automatically. Supplying a
 * contact secret is an explicit trusted provisioning action, and private VMP
 * remains unavailable for a contact absent from this directory.
 */
bool provisionVerifiedContactSecret(
    uint32_t peer_id,
    const uint8_t secret[chat::voice::vmp::kPrivateKeySize]);

/** Installs the narrow verified-identity-to-VMP contact-secret bridge. */
void setVerifiedContactSecretDeriver(VerifiedContactSecretDeriver deriver,
                                     void* context);

/**
 * Invalidates RAM-cached VMP contact secrets after an active mesh identity
 * family changes. An in-progress VMP session retains its own session keys.
 */
void invalidateContactSecretCache();

/** @brief Local-only inbox for chat projection and on-demand playback. */
const chat::voice::vmp::VoiceMessageInbox* inbox();

/** @brief Copies newest-first local VMP metadata under the session lock. */
std::size_t listInboxMetadata(chat::voice::vmp::VoiceMessageMetadata* out_metadata,
                              std::size_t capacity);

/** @brief Decodes one already-local VMP inbox object to the Pager speaker. */
bool playInboxMessage(uint64_t local_id, uint8_t volume_percent = 70U);

/**
 * @brief Starts local playback on a VMP worker so the UI task never blocks.
 *
 * Playback reads exclusively from the local-only inbox and cannot transmit or
 * relay the object. It is rejected while voice capture/transmit is active.
 */
bool requestPlayback(uint64_t local_id);

/**
 * @brief Dequeues one bounded VMP MQTT envelope for the MT MQTT bridge.
 *
 * This exposes no MT packet and no radio operation. The envelope is produced
 * only from a completed local VMP send. The caller must acknowledge it only
 * after the MQTT client has written it, so a socket failure retains the same
 * bounded envelope for retry.
 */
bool peekMqttEnvelope(uint8_t* out, std::size_t* inout_len);

/** @brief Commits the envelope only after the MQTT client wrote it to socket. */
bool acknowledgeMqttEnvelope();

/** @brief Mirrors the active MT MQTT uplink policy into the isolated VMP queue. */
void setMqttUplinkEnabled(bool enabled);

/** Installs the narrow RT/LXMF egress bridge; it is never a radio callback. */
void setLxmfEnvelopeSender(LxmfEnvelopeSender sender, void* context);

/**
 * Selects LXMF VQ unicast for a private recording while RT is active.
 * Broadcast remains the direct public 2.4 GHz VMP mode because LXMF has no
 * equivalent one-packet broadcast primitive.
 */
void setLxmfCarrierEnabled(bool enabled);

/**
 * @brief Accepts one subscribed VMP MQTT envelope into local VMP storage.
 *
 * This function never invokes radio TX, MT/MC receive adapters, or a publish
 * path. A completed valid object is stored in the local-only VMP inbox.
 */
bool acceptMqttEnvelope(const uint8_t* envelope, std::size_t envelope_len);

/**
 * Terminates an authenticated LXMF AppData VQ envelope in local storage.
 * `source_id` must agree with the VMP envelope sender.  It never enters the
 * generic AppData queue and never invokes any transmit or relay path.
 */
bool acceptLxmfEnvelope(uint32_t source_id,
                        const uint8_t* envelope,
                        std::size_t envelope_len);

/** @brief Explicitly discards a not-yet-uploaded in-memory VMP cloud object. */
void discardMqttPublication();

/**
 * @brief Queues microphone capture and a one-hop VMP transmit session.
 *
 * `target_id == kBroadcastTargetId` starts a public broadcast. Any other
 * nonzero target starts a private session and requires an already provisioned
 * verified-contact secret. Recording itself is performed by a VMP worker, so
 * this call never blocks a UI task for five seconds.
 */
StartSendResult requestRecordAndSend(uint32_t target_id);

} // namespace platform::esp::arduino_common::voice::vmp_session
