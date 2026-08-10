/**
 * @file vmp_pager_session.cpp
 * @brief Pager VMP session: LR1121 direct RF or SX1262 MQTT-only carriage.
 */

#include "platform/esp/arduino_common/voice/vmp_pager_session.h"

#if defined(ARDUINO_T_LORA_PAGER)

#include "platform/esp/arduino_common/voice/vmp_control_runtime.h"
#include "platform/esp/arduino_common/voice/vmp_pager_audio.h"
#include "platform/esp/arduino_common/voice/vmp_radio_lease.h"

#include "chat/infra/voice/vmp_control_auth.h"
#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/infra/voice/vmp_mqtt_transport.h"
#include "chat/infra/voice/vmp_receive_block.h"
#include "platform/esp/arduino_common/chat/infra/store/message_attachment_store.h"

#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <new>

namespace platform::esp::arduino_common::voice::vmp_session
{
namespace
{

namespace vmp = ::chat::voice::vmp;
namespace control = ::platform::esp::arduino_common::voice::vmp_control;
namespace radio = ::platform::esp::arduino_common::voice::vmp_radio;
namespace audio = ::platform::esp::arduino_common::voice::vmp_audio;

constexpr uint32_t kReceiveWindowMs = 5000U;
constexpr uint8_t kDefaultPhyProfile = 1U;
constexpr uint8_t kMaxChannelIndex = 39U;
constexpr std::size_t kMaxRadioFrameSize = 255U;
constexpr uint32_t kPrivateAcceptWindowMs = 1500U;
constexpr uint32_t kReadyProbeSpacingMs = 40U;
constexpr uint8_t kReadyProbeCount = 3U;
constexpr uint32_t kOutboundTaskStackWords = 4096U;
constexpr UBaseType_t kOutboundTaskPriority = 4U;
constexpr uint32_t kPlaybackTaskStackWords = 3072U;
constexpr UBaseType_t kPlaybackTaskPriority = 3U;
constexpr uint32_t kPersistentInboxRetryMs = 5000U;

bool deadlineExpired(uint32_t deadline)
{
    return static_cast<int32_t>(millis() - deadline) >= 0;
}

bool profileFor(const vmp::ControlFrame& control, radio::PhyProfile* out_profile)
{
    if (!out_profile || control.phy_profile_id != kDefaultPhyProfile ||
        control.channel_index > kMaxChannelIndex)
    {
        return false;
    }

    radio::PhyProfile profile{};
    profile.frequency_mhz = 2402.0F +
                            static_cast<float>(control.channel_index) * 2.0F;
    if (profile.frequency_mhz > 2480.0F)
    {
        return false;
    }
    *out_profile = profile;
    return true;
}

void secureClear(uint8_t* bytes, std::size_t size)
{
    volatile uint8_t* cursor = bytes;
    while (cursor && size-- != 0U)
    {
        *cursor++ = 0U;
    }
}

/**
 * Bulk VMP state is deliberately external: it holds user media, FEC blocks,
 * and asynchronous carrier plans but no radio DMA buffer, ISR state, or
 * FreeRTOS synchronization primitive. Pager hardware requires PSRAM, so VMP
 * refuses to enable rather than silently consuming the scarce internal heap.
 */
struct PagerMediaStorage final
{
    vmp::VoiceMessageInbox inbox{};
    audio::PagerCodec2Audio audio{};
    vmp::TransmitBlock transmit_block{};
    vmp::MqttTransmitTransfer mqtt_transmit{};
    vmp::MqttReceiveTransfer mqtt_receive{};
    vmp::ReceiveBlock receive_block{};
    uint8_t received_media[vmp::kMaxEncodedMediaSize] = {};
    uint8_t playback_media[vmp::kMaxEncodedMediaSize] = {};
    uint8_t mqtt_received_media[vmp::kMaxEncodedMediaSize] = {};
    vmp::VoiceMessageMetadata persistence_metadata[vmp::kVoiceInboxCapacity] = {};
};

PagerMediaStorage* allocatePagerMediaStorage()
{
    static PagerMediaStorage* storage = []() -> PagerMediaStorage*
    {
        void* const raw = heap_caps_malloc(sizeof(PagerMediaStorage),
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return raw ? new (raw) PagerMediaStorage{} : nullptr;
    }();
    return storage;
}

class PagerReceiveSession final
{
  public:
    bool initialize(uint32_t self_node_id, bool durable_attachment_store)
    {
        if (!media_)
        {
            media_ = allocatePagerMediaStorage();
        }
        if (!state_mutex_)
        {
            state_mutex_ = xSemaphoreCreateMutexStatic(&state_mutex_storage_);
        }
        direct_rf_voice_supported_ = radio::isSupported();
        if (self_node_id == 0U || !media_ || !media_->audio.isSupported() ||
            (direct_rf_voice_supported_ && !control::initialize()) || !state_mutex_)
        {
            return false;
        }
        self_node_id_ = self_node_id;
        requires_durable_attachment_store_ = durable_attachment_store;
        inbox_ready_ = !requires_durable_attachment_store_;
        if (direct_rf_voice_supported_)
        {
            control::setEnvelopeHandler(&PagerReceiveSession::controlEnvelopeReceived, this);
        }
        initialized_ = true;
        return true;
    }

    bool canRecordAndSend() const
    {
        if (!initialized_ || !media_ || !inbox_ready_ ||
            !media_->audio.isSupported() || !lockState())
        {
            return false;
        }
        // SX1262 does not have the LR1121 2.4 GHz path.  Its Pager voice
        // compose action becomes available only while MT MQTT uplink is
        // genuinely enabled; it cannot fall through to RF or LXMF.
        const bool available = direct_rf_voice_supported_ || mqtt_uplink_enabled_;
        unlockState();
        return available;
    }

    void onPersistentStorageReady()
    {
        if (!initialized_ || !requires_durable_attachment_store_)
        {
            return;
        }
        if (lockState())
        {
            attachment_store_ready_ = true;
            last_persistent_inbox_attempt_ms_ = 0U;
            unlockState();
        }
        servicePersistentInbox();
    }

    void servicePersistentInbox()
    {
        if (!initialized_ || !requires_durable_attachment_store_ || !media_ ||
            !lockState())
        {
            return;
        }
        const uint32_t now_ms = millis();
        const bool retry_due = attachment_store_ready_ && !inbox_ready_ &&
                               (last_persistent_inbox_attempt_ms_ == 0U ||
                                now_ms - last_persistent_inbox_attempt_ms_ >=
                                    kPersistentInboxRetryMs);
        if (!retry_due)
        {
            unlockState();
            return;
        }
        last_persistent_inbox_attempt_ms_ = now_ms;
        const auto result = ::platform::esp::arduino_common::chat_attachment::
            restoreVoiceInbox(&media_->inbox,
                              media_->received_media,
                              sizeof(media_->received_media));
        secureClear(media_->received_media, sizeof(media_->received_media));
        if (result == ::platform::esp::arduino_common::chat_attachment::
                          VoiceInboxLoadResult::Restored ||
            result == ::platform::esp::arduino_common::chat_attachment::
                          VoiceInboxLoadResult::Empty)
        {
            inbox_ready_ = true;
            Serial.printf("[VMP] attachment inbox restore=%s\n",
                          result == ::platform::esp::arduino_common::chat_attachment::
                                        VoiceInboxLoadResult::Restored
                              ? "restored"
                              : "empty");
        }
        else
        {
            Serial.printf("[VMP] attachment inbox restore deferred=%u\n",
                          static_cast<unsigned>(result));
        }
        unlockState();
    }

    bool provisionVerifiedContactSecret(
        uint32_t peer_id,
        const uint8_t secret[vmp::kPrivateKeySize])
    {
        return contacts_.upsertVerifiedContactSecret(peer_id, secret);
    }

    void setVerifiedContactSecretDeriver(VerifiedContactSecretDeriver deriver,
                                         void* context)
    {
        if (lockState())
        {
            contact_secret_deriver_ = deriver;
            contact_secret_deriver_context_ = context;
            unlockState();
        }
    }

    void invalidateContactSecretCache()
    {
        if (lockState())
        {
            contact_secret_cache_stale_ = true;
            if (!active_)
            {
                contacts_.clear();
                contact_secret_cache_stale_ = false;
            }
            unlockState();
        }
    }

    const vmp::VoiceMessageInbox* inbox() const
    {
        return initialized_ && media_ && inbox_ready_ ? &media_->inbox : nullptr;
    }

    std::size_t listInboxMetadata(vmp::VoiceMessageMetadata* out_metadata,
                                  std::size_t capacity) const
    {
        if (!initialized_ || !inbox_ready_ || !out_metadata || capacity == 0U ||
            !lockState())
        {
            return 0U;
        }
        const std::size_t listed = media_->inbox.listMetadata(out_metadata, capacity);
        unlockState();
        return listed;
    }

    bool playInboxMessage(uint64_t local_id, uint8_t volume_percent)
    {
        vmp::VoiceMessageView view{};
        return initialized_ && media_ && inbox_ready_ &&
               media_->inbox.get(local_id, &view) &&
               view.encoded_media &&
               media_->audio.play(view.encoded_media,
                                  view.metadata.encoded_media_len,
                                  view.metadata.codec,
                                  volume_percent) == audio::PlaybackResult::Complete;
    }

    bool requestPlayback(uint64_t local_id)
    {
        if (!initialized_ || !inbox_ready_ || local_id == 0U || !lockState())
        {
            return false;
        }
        const bool unavailable = active_ || playback_task_ != nullptr;
        unlockState();
        if (unavailable)
        {
            return false;
        }

        if (!lockState())
        {
            return false;
        }
        if (active_ || playback_task_)
        {
            unlockState();
            return false;
        }
        vmp::VoiceMessageView view{};
        if (!media_->inbox.get(local_id, &view) || !view.encoded_media ||
            view.metadata.encoded_media_len > sizeof(media_->playback_media))
        {
            unlockState();
            return false;
        }
        std::memcpy(media_->playback_media,
                    view.encoded_media,
                    view.metadata.encoded_media_len);
        playback_media_len_ = view.metadata.encoded_media_len;
        playback_codec_ = view.metadata.codec;
        playback_local_id_ = local_id;
        if (xTaskCreatePinnedToCore(&PagerReceiveSession::playbackTaskEntry,
                                    "vmp_play",
                                    kPlaybackTaskStackWords,
                                    this,
                                    kPlaybackTaskPriority,
                                    &playback_task_,
                                    tskNO_AFFINITY) != pdPASS)
        {
            playback_local_id_ = 0U;
            playback_task_ = nullptr;
            unlockState();
            return false;
        }
        unlockState();
        return true;
    }

    bool peekMqttEnvelope(uint8_t* out, std::size_t* inout_len)
    {
        if (!initialized_ || !out || !inout_len || !lockState())
        {
            return false;
        }
        const bool emitted = mqtt_uplink_enabled_ &&
                             media_->mqtt_transmit.copyNextEnvelope(out, inout_len);
        unlockState();
        return emitted;
    }

    bool acknowledgeMqttEnvelope()
    {
        if (!initialized_ || !lockState())
        {
            return false;
        }
        if (!media_->mqtt_transmit.commitNextEnvelope())
        {
            unlockState();
            return false;
        }
        if (!media_->mqtt_transmit.hasNext())
        {
            media_->mqtt_transmit.clear();
        }
        unlockState();
        return true;
    }

    void setMqttUplinkEnabled(bool enabled)
    {
        if (lockState())
        {
            mqtt_uplink_enabled_ = enabled;
            if (!enabled && !lxmf_carrier_enabled_)
            {
                media_->mqtt_transmit.clear();
            }
            unlockState();
        }
    }

    void setLxmfEnvelopeSender(LxmfEnvelopeSender sender, void* context)
    {
        if (lockState())
        {
            lxmf_sender_ = sender;
            lxmf_sender_context_ = context;
            if (!lxmf_sender_)
            {
                lxmf_carrier_enabled_ = false;
                if (!mqtt_uplink_enabled_)
                {
                    media_->mqtt_transmit.clear();
                }
            }
            unlockState();
        }
    }

    void setLxmfCarrierEnabled(bool enabled)
    {
        if (lockState())
        {
            lxmf_carrier_enabled_ = enabled && lxmf_sender_ != nullptr;
            if (!lxmf_carrier_enabled_ && !mqtt_uplink_enabled_)
            {
                media_->mqtt_transmit.clear();
            }
            unlockState();
        }
    }

    bool acceptMqttEnvelope(const uint8_t* envelope, std::size_t envelope_len)
    {
        return acceptStoreForwardEnvelope(0U, envelope, envelope_len);
    }

    bool acceptLxmfEnvelope(uint32_t source_id,
                            const uint8_t* envelope,
                            std::size_t envelope_len)
    {
        return direct_rf_voice_supported_ && source_id != 0U &&
               acceptStoreForwardEnvelope(source_id, envelope, envelope_len);
    }

    bool acceptStoreForwardEnvelope(uint32_t source_id,
                                    const uint8_t* envelope,
                                    std::size_t envelope_len)
    {
        if (!initialized_ || !inbox_ready_ || !envelope || envelope_len == 0U ||
            !lockState())
        {
            return false;
        }

        vmp::MqttEnvelopeView envelope_view{};
        vmp::DeliveryMode transport_delivery_mode = vmp::DeliveryMode::Private;
        if (!vmp::parseMqttEnvelope(envelope, envelope_len, &envelope_view) ||
            (envelope_view.kind == vmp::MqttEnvelopeKind::Control &&
             !vmp::decodeControlFrame(envelope_view.payload,
                                      envelope_view.payload_len,
                                      &transport_candidate_control_)) ||
            (envelope_view.kind == vmp::MqttEnvelopeKind::Control &&
             !vmp::deliveryModeFor(transport_candidate_control_,
                                   &transport_delivery_mode)) ||
            (source_id != 0U &&
             ((envelope_view.kind == vmp::MqttEnvelopeKind::Control &&
               transport_candidate_control_.sender_id != source_id) ||
              (envelope_view.kind == vmp::MqttEnvelopeKind::Shard &&
               (!media_->mqtt_receive.active() ||
                media_->mqtt_receive.control().sender_id != source_id)))))
        {
            unlockState();
            return false;
        }
        const bool require_contact_secret =
            envelope_view.kind == vmp::MqttEnvelopeKind::Control &&
            transport_delivery_mode == vmp::DeliveryMode::Private;
        const uint32_t sender_id = transport_candidate_control_.sender_id;
        unlockState();
        if (require_contact_secret && !ensureVerifiedContactSecret(sender_id))
        {
            return false;
        }
        if (!lockState())
        {
            return false;
        }
        const vmp::MqttTransferResult accepted = media_->mqtt_receive.acceptEnvelope(
            envelope, envelope_len, self_node_id_, contacts_);
        bool stored = accepted == vmp::MqttTransferResult::Accepted ||
                      accepted == vmp::MqttTransferResult::Duplicate;
        if (accepted == vmp::MqttTransferResult::Complete)
        {
            std::size_t media_len = 0U;
            const bool recovered = media_->mqtt_receive.recover(media_->mqtt_received_media,
                                                                sizeof(media_->mqtt_received_media),
                                                                &media_len);
            const bool stored_voice =
                recovered && storeCompletedVoice(media_->mqtt_receive.control(),
                                                 media_->mqtt_received_media,
                                                 media_len,
                                                 millis() / 1000U);
            secureClear(media_->mqtt_received_media, sizeof(media_->mqtt_received_media));
            media_->mqtt_receive.clear();
            stored = stored_voice;
        }
        unlockState();
        return stored;
    }

    void discardMqttPublication()
    {
        if (lockState())
        {
            media_->mqtt_transmit.clear();
            unlockState();
        }
    }

    StartSendResult requestRecordAndSend(uint32_t target_id)
    {
        const bool broadcast = target_id == vmp::kBroadcastTargetId;
        if (!initialized_ || !media_ || !inbox_ready_ ||
            !media_->audio.isSupported())
        {
            return StartSendResult::Unsupported;
        }
        if ((!broadcast && target_id == 0U) || !lockState())
        {
            return StartSendResult::Busy;
        }
        const bool unavailable = active_ || outbound_task_ || playback_task_ ||
                                 (!direct_rf_voice_supported_ && !mqtt_uplink_enabled_);
        unlockState();
        if (unavailable)
        {
            return StartSendResult::Busy;
        }
        if (!broadcast && !ensureVerifiedContactSecret(target_id))
        {
            return StartSendResult::PrivateContactUnverified;
        }

        if (!lockState())
        {
            return StartSendResult::Busy;
        }
        if (active_ || outbound_task_ || playback_task_)
        {
            unlockState();
            return StartSendResult::Busy;
        }
        outbound_target_id_ = target_id;
        outbound_is_broadcast_ = broadcast;
        active_ = true;
        unlockState();
        if (xTaskCreatePinnedToCore(&PagerReceiveSession::outboundTaskEntry,
                                    "vmp_tx",
                                    kOutboundTaskStackWords,
                                    this,
                                    kOutboundTaskPriority,
                                    nullptr,
                                    tskNO_AFFINITY) != pdPASS)
        {
            setActive(false);
            return StartSendResult::Unsupported;
        }
        return StartSendResult::Queued;
    }

  private:
    static void controlEnvelopeReceived(const control::Envelope& envelope,
                                        void* context)
    {
        auto* const self = static_cast<PagerReceiveSession*>(context);
        if (self)
        {
            self->handleControl(envelope);
        }
    }

    void handleControl(const control::Envelope& envelope)
    {
        if (!initialized_ || !direct_rf_voice_supported_ || !vmp::decodeControlFrame(envelope.bytes, sizeof(envelope.bytes), &candidate_control_))
        {
            return;
        }

        if (candidate_control_.type == vmp::ControlType::Accept &&
            outboundAcceptMatchesCandidate())
        {
            handlePrivateAccept(envelope.bytes);
            return;
        }
        if (isActive())
        {
            return;
        }

        vmp::DeliveryMode mode = vmp::DeliveryMode::Private;
        if (!vmp::deliveryModeFor(candidate_control_, &mode))
        {
            return;
        }
        if (mode == vmp::DeliveryMode::Private)
        {
            if (candidate_control_.type != vmp::ControlType::Offer ||
                candidate_control_.target_id != self_node_id_ || !tryBeginInbound())
            {
                return;
            }
            handlePrivateOffer(envelope.bytes);
            return;
        }

        if (candidate_control_.type == vmp::ControlType::Announce &&
            candidate_control_.target_id == vmp::kBroadcastTargetId &&
            vmp::decodePublicControlFrame(
                envelope.bytes, sizeof(envelope.bytes), &incoming_control_) &&
            tryBeginInbound())
        {
            receiveBroadcast();
        }
    }

    static void outboundTaskEntry(void* context)
    {
        auto* const self = static_cast<PagerReceiveSession*>(context);
        if (self)
        {
            self->setOutboundTask(xTaskGetCurrentTaskHandle());
            self->runOutbound();
        }
        vTaskDelete(nullptr);
    }

    static void playbackTaskEntry(void* context)
    {
        auto* const self = static_cast<PagerReceiveSession*>(context);
        if (self)
        {
            self->runPlayback();
        }
        vTaskDelete(nullptr);
    }

    void runOutbound()
    {
        bool sent = false;
        bool used_lxmf = false;
        if (media_->audio.capture(nullptr) == audio::CaptureResult::Complete &&
            media_->audio.hasEncodedMedia() &&
            media_->transmit_block.prepare(media_->audio.encodedMedia(), media_->audio.encodedMediaSize()) &&
            prepareOutboundControl())
        {
            if (!direct_rf_voice_supported_)
            {
                // SX1262 can encode and publish the VMP object through an
                // explicitly enabled MT MQTT uplink, but has no legal RF or
                // LXMF voice carrier.  No READY/control/2.4 GHz operation is
                // reachable from this branch.
                sent = queueMqttPublication();
            }
            else
            {
                used_lxmf = shouldUseLxmfCarrier();
                sent = used_lxmf ? sendLxmfVoice()
                                 : (outbound_is_broadcast_ ? sendBroadcastVoice()
                                                           : sendPrivateVoice());
                if (sent && !used_lxmf)
                {
                    (void)queueMqttPublication();
                }
            }
        }
        (void)sent;
        clearOutboundAcceptWait();
        media_->audio.clearEncodedMedia();
        media_->transmit_block.clear();
        releaseRadio();
        resetEphemeralState();
        finishOutbound();
    }

    void runPlayback()
    {
        if (playback_local_id_ != 0U && playback_media_len_ != 0U)
        {
            (void)media_->audio.play(media_->playback_media,
                                     playback_media_len_,
                                     playback_codec_,
                                     70U);
        }
        clearPlaybackTask();
    }

    bool queueMqttPublication()
    {
        if (!media_->audio.hasEncodedMedia() || !lockState())
        {
            return false;
        }
        if (!mqtt_uplink_enabled_)
        {
            unlockState();
            return false;
        }
        const bool prepared = outbound_is_broadcast_
                                  ? media_->mqtt_transmit.prepareBroadcast(outgoing_control_,
                                                                           media_->audio.encodedMedia(),
                                                                           media_->audio.encodedMediaSize())
                                  : media_->mqtt_transmit.preparePrivate(outgoing_control_,
                                                                         contact_secret_,
                                                                         media_->audio.encodedMedia(),
                                                                         media_->audio.encodedMediaSize());
        if (!prepared)
        {
            media_->mqtt_transmit.clear();
        }
        unlockState();
        return prepared;
    }

    bool shouldUseLxmfCarrier() const
    {
        if (!lockState())
        {
            return false;
        }
        const bool use_lxmf = direct_rf_voice_supported_ && !outbound_is_broadcast_ &&
                              lxmf_carrier_enabled_ &&
                              lxmf_sender_ != nullptr;
        unlockState();
        return use_lxmf;
    }

    bool ensureVerifiedContactSecret(uint32_t peer_id)
    {
        if (peer_id == 0U || !lockState())
        {
            return false;
        }
        if (contact_secret_cache_stale_)
        {
            contacts_.clear();
            contact_secret_cache_stale_ = false;
        }
        if (contacts_.hasVerifiedContactSecret(peer_id))
        {
            unlockState();
            return true;
        }
        const VerifiedContactSecretDeriver deriver = contact_secret_deriver_;
        void* const context = contact_secret_deriver_context_;
        const bool derived = deriver &&
                             deriver(context, peer_id, contact_derivation_scratch_);
        if (!derived)
        {
            secureClear(contact_derivation_scratch_, sizeof(contact_derivation_scratch_));
            unlockState();
            return false;
        }
        const bool stored = contacts_.upsertVerifiedContactSecret(
            peer_id, contact_derivation_scratch_);
        secureClear(contact_derivation_scratch_, sizeof(contact_derivation_scratch_));
        unlockState();
        return stored;
    }

    bool sendLxmfVoice()
    {
        LxmfEnvelopeSender sender = nullptr;
        void* sender_context = nullptr;
        uint32_t target_id = 0U;
        if (!lockState())
        {
            return false;
        }
        if (outbound_is_broadcast_ || !lxmf_carrier_enabled_ || !lxmf_sender_ ||
            !media_->mqtt_transmit.preparePrivate(outgoing_control_,
                                                  contact_secret_,
                                                  media_->audio.encodedMedia(),
                                                  media_->audio.encodedMediaSize()))
        {
            media_->mqtt_transmit.clear();
            unlockState();
            return false;
        }
        sender = lxmf_sender_;
        sender_context = lxmf_sender_context_;
        target_id = outbound_target_id_;
        unlockState();

        while (true)
        {
            std::size_t envelope_len = sizeof(data_wire_);
            if (!lockState())
            {
                return false;
            }
            if (!media_->mqtt_transmit.copyNextEnvelope(data_wire_, &envelope_len))
            {
                media_->mqtt_transmit.clear();
                unlockState();
                return false;
            }
            unlockState();

            // LXMF owns its link/session delivery policy.  VMP only emits the
            // original bounded object once and never creates an application
            // acknowledgement, a relay, or a VMP resend from a received item.
            if (!sender(sender_context, target_id, data_wire_, envelope_len))
            {
                if (lockState())
                {
                    media_->mqtt_transmit.clear();
                    unlockState();
                }
                return false;
            }

            if (!lockState())
            {
                return false;
            }
            if (!media_->mqtt_transmit.commitNextEnvelope())
            {
                media_->mqtt_transmit.clear();
                unlockState();
                return false;
            }
            if (!media_->mqtt_transmit.hasNext())
            {
                media_->mqtt_transmit.clear();
                unlockState();
                return true;
            }
            unlockState();
        }
    }

    bool prepareOutboundControl()
    {
        outgoing_control_ = {};
        outgoing_control_.type = outbound_is_broadcast_ ? vmp::ControlType::Announce
                                                        : vmp::ControlType::Offer;
        outgoing_control_.flags = outbound_is_broadcast_
                                      ? static_cast<uint8_t>(vmp::ControlFlagBroadcast |
                                                             vmp::ControlFlagPublicBroadcast)
                                      : static_cast<uint8_t>(vmp::ControlFlagPrivate);
        outgoing_control_.sender_id = self_node_id_;
        outgoing_control_.target_id = outbound_target_id_;
        esp_fill_random(&outgoing_control_.session_id,
                        sizeof(outgoing_control_.session_id));
        if (outgoing_control_.session_id == 0U)
        {
            outgoing_control_.session_id = 1U;
        }
        esp_fill_random(outgoing_control_.session_nonce,
                        sizeof(outgoing_control_.session_nonce));
        outgoing_control_.phy_profile_id = kDefaultPhyProfile;
        outgoing_control_.channel_index = static_cast<uint8_t>(
            outgoing_control_.session_id % (static_cast<uint64_t>(kMaxChannelIndex) + 1U));
        outgoing_control_.encoded_media_len = static_cast<uint16_t>(media_->audio.encodedMediaSize());
        outgoing_control_.codec = vmp::Codec::Codec2_1300;
        outgoing_control_.fec_layout = vmp::kFecLayoutRs10_8;
        outgoing_control_.total_blocks = 1U;
        outgoing_control_.data_start_delay_ms =
            outbound_is_broadcast_ ? 700U : 120U;
        outgoing_control_.object_fingerprint = static_cast<uint32_t>(
            outgoing_control_.session_id ^ (outgoing_control_.session_id >> 32U));

        if (outbound_is_broadcast_)
        {
            return true;
        }
        return contacts_.lookupVerifiedContactSecret(outbound_target_id_, contact_secret_) &&
               vmp::generateEphemeralKeyPair(&local_ephemeral_) &&
               vmp::derivePrivateControlKey(contact_secret_,
                                            outgoing_control_.session_nonce,
                                            outgoing_control_.session_id,
                                            session_keys_.control_key) &&
               copyOutboundEphemeralPublicKey();
    }

    bool copyOutboundEphemeralPublicKey()
    {
        std::memcpy(outgoing_control_.ephemeral_public_key,
                    local_ephemeral_.public_key,
                    sizeof(outgoing_control_.ephemeral_public_key));
        return true;
    }

    bool sendPrivateVoice()
    {
        if (!direct_rf_voice_supported_)
        {
            return false;
        }
        std::size_t control_len = sizeof(control_wire_);
        if (!vmp::encodePrivateControlFrame(
                outgoing_control_,
                session_keys_,
                vmp::PrivateFrameDirection::SenderToReceiver,
                control_wire_,
                &control_len) ||
            !radio::tryAcquire(&radio_lease_))
        {
            return false;
        }

        beginOutboundAcceptWait();
        if (!radio::transmit(&radio_lease_, control_wire_, control_len))
        {
            clearOutboundAcceptWait();
            releaseRadio();
            return false;
        }
        releaseRadio();

        if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kPrivateAcceptWindowMs)) == 0U ||
            !takeOutboundAccept() ||
            !vmp::derivePrivateSessionKeys(contact_secret_,
                                           local_ephemeral_.private_key,
                                           peer_accept_.ephemeral_public_key,
                                           outgoing_control_.session_nonce,
                                           outgoing_control_.session_id,
                                           &session_keys_))
        {
            clearOutboundAcceptWait();
            return false;
        }
        return transmitPrivateDataTrain();
    }

    bool sendBroadcastVoice()
    {
        if (!direct_rf_voice_supported_)
        {
            return false;
        }
        std::size_t control_len = sizeof(control_wire_);
        if (!vmp::encodePublicControlFrame(outgoing_control_, control_wire_, &control_len) ||
            !radio::tryAcquire(&radio_lease_) ||
            !radio::transmit(&radio_lease_, control_wire_, control_len))
        {
            releaseRadio();
            return false;
        }
        releaseRadio();

        vTaskDelay(pdMS_TO_TICKS(outgoing_control_.data_start_delay_ms));
        radio::PhyProfile profile{};
        if (!profileFor(outgoing_control_, &profile) ||
            !radio::tryAcquire(&radio_lease_) ||
            !radio::switchTo2Ghz(&radio_lease_, profile))
        {
            releaseRadio();
            return false;
        }
        for (uint8_t probe = 0U; probe < kReadyProbeCount; ++probe)
        {
            data_header_ = {};
            data_header_.type = vmp::DataType::ReadyProbe;
            data_header_.session_id = outgoing_control_.session_id;
            std::size_t probe_len = sizeof(data_wire_);
            if (!vmp::buildPublicReadyFrame(data_header_, data_wire_, &probe_len) ||
                !radio::transmit(&radio_lease_, data_wire_, probe_len))
            {
                releaseRadio();
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(kReadyProbeSpacingMs));
        }
        return transmitDataShards(false);
    }

    bool transmitPrivateDataTrain()
    {
        radio::PhyProfile profile{};
        if (!profileFor(outgoing_control_, &profile) ||
            !radio::tryAcquire(&radio_lease_) ||
            !radio::switchTo2Ghz(&radio_lease_, profile))
        {
            releaseRadio();
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(outgoing_control_.data_start_delay_ms));
        for (uint8_t probe = 0U; probe < kReadyProbeCount; ++probe)
        {
            if (!sendPrivateReadyProbe() || !waitForPrivateReady())
            {
                if (probe + 1U == kReadyProbeCount)
                {
                    releaseRadio();
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(kReadyProbeSpacingMs));
                continue;
            }
            return transmitDataShards(true);
        }
        releaseRadio();
        return false;
    }

    bool sendPrivateReadyProbe()
    {
        data_header_ = {};
        data_header_.type = vmp::DataType::ReadyProbe;
        data_header_.session_id = outgoing_control_.session_id;
        std::size_t probe_len = vmp::kDataHeaderSize;
        return vmp::encodeDataHeader(data_header_, data_wire_, &probe_len) &&
               vmp::tagPrivateReady(session_keys_,
                                    outgoing_control_.session_nonce,
                                    vmp::PrivateFrameDirection::SenderToReceiver,
                                    data_header_,
                                    data_wire_ + probe_len) &&
               radio::transmit(&radio_lease_,
                               data_wire_,
                               probe_len + vmp::kPrivateDataAuthTagSize) &&
               radio::startReceive(&radio_lease_);
    }

    bool waitForPrivateReady()
    {
        const uint32_t deadline = millis() + kReadyProbeSpacingMs;
        while (!deadlineExpired(deadline))
        {
            const int packet_len = radio::packetLength(&radio_lease_);
            if (packet_len == static_cast<int>(vmp::kDataHeaderSize +
                                               vmp::kPrivateDataAuthTagSize) &&
                radio::readPacket(&radio_lease_, data_wire_,
                                  static_cast<std::size_t>(packet_len)) &&
                vmp::decodeDataHeader(data_wire_, vmp::kDataHeaderSize, &data_header_) &&
                data_header_.type == vmp::DataType::Ready &&
                data_header_.session_id == outgoing_control_.session_id &&
                vmp::verifyPrivateReadyTag(
                    session_keys_,
                    outgoing_control_.session_nonce,
                    vmp::PrivateFrameDirection::ReceiverToSender,
                    data_header_,
                    data_wire_ + vmp::kDataHeaderSize))
            {
                return true;
            }
            (void)radio::startReceive(&radio_lease_);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        return false;
    }

    bool transmitDataShards(bool private_mode)
    {
        for (uint8_t shard_index = 0U; shard_index < vmp::kTotalShardsPerBlock;
             ++shard_index)
        {
            std::size_t frame_len = sizeof(data_wire_);
            const bool built = private_mode
                                   ? media_->transmit_block.buildPrivateShardFrame(
                                         session_keys_,
                                         outgoing_control_.session_nonce,
                                         outgoing_control_.session_id,
                                         shard_index,
                                         data_wire_,
                                         &frame_len)
                                   : media_->transmit_block.buildPublicShardFrame(
                                         outgoing_control_.session_id,
                                         shard_index,
                                         data_wire_,
                                         &frame_len);
            if (!built || !radio::transmit(&radio_lease_, data_wire_, frame_len))
            {
                releaseRadio();
                return false;
            }
        }
        releaseRadio();
        return true;
    }

    void handlePrivateAccept(const uint8_t* raw_control)
    {
        if (!raw_control || !lockState())
        {
            return;
        }
        TaskHandle_t outbound_task = nullptr;
        if (!outbound_waiting_accept_ ||
            !vmp::decodePrivateControlFrame(raw_control,
                                            vmp::kControlFrameSize,
                                            session_keys_,
                                            vmp::PrivateFrameDirection::ReceiverToSender,
                                            &peer_accept_) ||
            peer_accept_.object_fingerprint != outgoing_control_.object_fingerprint ||
            std::memcmp(peer_accept_.session_nonce,
                        outgoing_control_.session_nonce,
                        sizeof(peer_accept_.session_nonce)) != 0)
        {
            unlockState();
            return;
        }
        outbound_accept_received_ = true;
        outbound_waiting_accept_ = false;
        outbound_task = outbound_task_;
        unlockState();
        if (outbound_task)
        {
            xTaskNotifyGive(outbound_task);
        }
    }

    void handlePrivateOffer(const uint8_t* raw_control)
    {
        resetEphemeralState();
        if (!ensureVerifiedContactSecret(candidate_control_.sender_id) ||
            !contacts_.lookupVerifiedContactSecret(candidate_control_.sender_id,
                                                   contact_secret_) ||
            !vmp::derivePrivateControlKey(contact_secret_,
                                          candidate_control_.session_nonce,
                                          candidate_control_.session_id,
                                          session_keys_.control_key) ||
            !vmp::decodePrivateControlFrame(raw_control,
                                            vmp::kControlFrameSize,
                                            session_keys_,
                                            vmp::PrivateFrameDirection::SenderToReceiver,
                                            &incoming_control_) ||
            !vmp::generateEphemeralKeyPair(&local_ephemeral_) ||
            !vmp::derivePrivateSessionKeys(contact_secret_,
                                           local_ephemeral_.private_key,
                                           incoming_control_.ephemeral_public_key,
                                           incoming_control_.session_nonce,
                                           incoming_control_.session_id,
                                           &session_keys_) ||
            !prepareReceiveBlock())
        {
            resetEphemeralState();
            setActive(false);
            return;
        }

        outgoing_control_ = incoming_control_;
        outgoing_control_.type = vmp::ControlType::Accept;
        outgoing_control_.sender_id = self_node_id_;
        outgoing_control_.target_id = incoming_control_.sender_id;
        std::memcpy(outgoing_control_.ephemeral_public_key,
                    local_ephemeral_.public_key,
                    sizeof(outgoing_control_.ephemeral_public_key));

        std::size_t control_len = sizeof(control_wire_);
        if (!vmp::encodePrivateControlFrame(
                outgoing_control_,
                session_keys_,
                vmp::PrivateFrameDirection::ReceiverToSender,
                control_wire_,
                &control_len) ||
            !radio::tryAcquire(&radio_lease_) ||
            !radio::transmit(&radio_lease_, control_wire_, control_len))
        {
            releaseRadio();
            resetEphemeralState();
            setActive(false);
            return;
        }

        radio::PhyProfile profile{};
        if (!profileFor(incoming_control_, &profile) ||
            !radio::switchTo2Ghz(&radio_lease_, profile) ||
            !radio::startReceive(&radio_lease_))
        {
            releaseRadio();
            resetEphemeralState();
            setActive(false);
            return;
        }
        receivePrivateMedia();
        setActive(false);
        releaseRadio();
        resetEphemeralState();
    }

    void receiveBroadcast()
    {
        if (!prepareReceiveBlock())
        {
            setActive(false);
            return;
        }
        radio::PhyProfile profile{};
        if (!profileFor(incoming_control_, &profile) ||
            !radio::tryAcquire(&radio_lease_) ||
            !radio::switchTo2Ghz(&radio_lease_, profile) ||
            !radio::startReceive(&radio_lease_))
        {
            releaseRadio();
            setActive(false);
            return;
        }
        receivePublicMedia();
        setActive(false);
        releaseRadio();
    }

    bool prepareReceiveBlock()
    {
        vmp::MediaLayout layout{};
        return vmp::planMediaLayout(incoming_control_.encoded_media_len, &layout) &&
               media_->receive_block.begin(layout);
    }

    void receivePrivateMedia()
    {
        bool ready_sent = false;
        const uint32_t deadline = millis() + kReceiveWindowMs;
        while (!deadlineExpired(deadline))
        {
            const int packet_len = radio::packetLength(&radio_lease_);
            if (packet_len <= 0 ||
                static_cast<std::size_t>(packet_len) > sizeof(data_wire_) ||
                !radio::readPacket(&radio_lease_, data_wire_,
                                   static_cast<std::size_t>(packet_len)))
            {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            if (static_cast<std::size_t>(packet_len) ==
                vmp::kDataHeaderSize + vmp::kPrivateDataAuthTagSize)
            {
                handlePrivateReadyProbe(&ready_sent);
            }
            else if (ready_sent && static_cast<std::size_t>(packet_len) == vmp::kPrivateShardFrameSize &&
                     handlePrivateShard())
            {
                return;
            }
            (void)radio::startReceive(&radio_lease_);
        }
    }

    void receivePublicMedia()
    {
        const uint32_t deadline = millis() + kReceiveWindowMs;
        while (!deadlineExpired(deadline))
        {
            const int packet_len = radio::packetLength(&radio_lease_);
            if (packet_len <= 0 ||
                static_cast<std::size_t>(packet_len) > sizeof(data_wire_) ||
                !radio::readPacket(&radio_lease_, data_wire_,
                                   static_cast<std::size_t>(packet_len)))
            {
                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }

            if (static_cast<std::size_t>(packet_len) == vmp::kPublicShardFrameSize &&
                handlePublicShard())
            {
                return;
            }
            (void)radio::startReceive(&radio_lease_);
        }
    }

    void handlePrivateReadyProbe(bool* ready_sent)
    {
        if (!ready_sent || *ready_sent ||
            !vmp::decodeDataHeader(data_wire_, vmp::kDataHeaderSize, &data_header_) ||
            data_header_.type != vmp::DataType::ReadyProbe ||
            data_header_.session_id != incoming_control_.session_id ||
            !vmp::verifyPrivateReadyTag(
                session_keys_,
                incoming_control_.session_nonce,
                vmp::PrivateFrameDirection::SenderToReceiver,
                data_header_,
                data_wire_ + vmp::kDataHeaderSize))
        {
            return;
        }

        data_header_.type = vmp::DataType::Ready;
        std::size_t ready_len = vmp::kDataHeaderSize;
        if (!vmp::encodeDataHeader(data_header_, data_wire_, &ready_len) ||
            !vmp::tagPrivateReady(session_keys_,
                                  incoming_control_.session_nonce,
                                  vmp::PrivateFrameDirection::ReceiverToSender,
                                  data_header_,
                                  data_wire_ + ready_len) ||
            !radio::transmit(&radio_lease_,
                             data_wire_,
                             ready_len + vmp::kPrivateDataAuthTagSize))
        {
            return;
        }
        *ready_sent = true;
    }

    bool handlePrivateShard()
    {
        if (!vmp::decodeDataHeader(data_wire_, vmp::kDataHeaderSize, &data_header_) ||
            data_header_.session_id != incoming_control_.session_id ||
            !vmp::openPrivateShard(
                session_keys_,
                incoming_control_.session_nonce,
                vmp::PrivateFrameDirection::SenderToReceiver,
                data_header_,
                data_wire_ + vmp::kDataHeaderSize,
                vmp::kMaxShardPayloadSize,
                data_wire_ + vmp::kDataHeaderSize + vmp::kMaxShardPayloadSize,
                shard_plaintext_))
        {
            return false;
        }
        return acceptShardAndStore(data_header_, shard_plaintext_);
    }

    bool handlePublicShard()
    {
        const uint8_t* shard = nullptr;
        if (!vmp::parsePublicShardFrame(data_wire_,
                                        vmp::kPublicShardFrameSize,
                                        &data_header_,
                                        &shard) ||
            data_header_.session_id != incoming_control_.session_id)
        {
            return false;
        }
        return acceptShardAndStore(data_header_, shard);
    }

    bool acceptShardAndStore(const vmp::DataHeader& header, const uint8_t* shard)
    {
        const vmp::ReceiveBlockResult result = media_->receive_block.accept(
            header, shard, vmp::kMaxShardPayloadSize);
        if (result != vmp::ReceiveBlockResult::Complete)
        {
            return false;
        }
        std::size_t media_len = 0U;
        if (!media_->receive_block.recover(media_->received_media,
                                           sizeof(media_->received_media),
                                           &media_len))
        {
            return false;
        }
        if (!lockState())
        {
            return false;
        }
        const bool stored = storeCompletedVoice(incoming_control_,
                                                media_->received_media,
                                                media_len,
                                                millis() / 1000U);
        secureClear(media_->received_media, sizeof(media_->received_media));
        unlockState();
        return stored;
    }

    void releaseRadio()
    {
        if (radio_lease_.implementation)
        {
            radio::release(&radio_lease_);
        }
    }

    void resetEphemeralState()
    {
        vmp::clearPrivateSessionKeys(&session_keys_);
        secureClear(contact_secret_, sizeof(contact_secret_));
        secureClear(local_ephemeral_.private_key, sizeof(local_ephemeral_.private_key));
        secureClear(local_ephemeral_.public_key, sizeof(local_ephemeral_.public_key));
        media_->receive_block.clear();
    }

    bool lockState() const
    {
        return state_mutex_ && xSemaphoreTake(state_mutex_, portMAX_DELAY) == pdTRUE;
    }

    void unlockState() const
    {
        if (state_mutex_)
        {
            (void)xSemaphoreGive(state_mutex_);
        }
    }

    bool isActive() const
    {
        if (!lockState())
        {
            return true;
        }
        const bool active = active_;
        unlockState();
        return active;
    }

    void setActive(bool active)
    {
        if (lockState())
        {
            active_ = active;
            unlockState();
        }
    }

    bool tryBeginInbound()
    {
        if (!lockState())
        {
            return false;
        }
        if (active_ || !inbox_ready_)
        {
            unlockState();
            return false;
        }
        active_ = true;
        unlockState();
        return true;
    }

    bool outboundAcceptMatchesCandidate() const
    {
        if (!lockState())
        {
            return false;
        }
        const bool matches = outbound_waiting_accept_ &&
                             candidate_control_.target_id == self_node_id_ &&
                             candidate_control_.sender_id == outbound_target_id_ &&
                             candidate_control_.session_id == outgoing_control_.session_id;
        unlockState();
        return matches;
    }

    void setOutboundTask(TaskHandle_t task)
    {
        if (lockState())
        {
            outbound_task_ = task;
            unlockState();
        }
    }

    void beginOutboundAcceptWait()
    {
        if (lockState())
        {
            outbound_accept_received_ = false;
            outbound_waiting_accept_ = true;
            unlockState();
        }
    }

    void clearOutboundAcceptWait()
    {
        if (lockState())
        {
            outbound_waiting_accept_ = false;
            outbound_accept_received_ = false;
            unlockState();
        }
    }

    bool takeOutboundAccept()
    {
        if (!lockState())
        {
            return false;
        }
        const bool accepted = outbound_accept_received_;
        outbound_waiting_accept_ = false;
        outbound_accept_received_ = false;
        unlockState();
        return accepted;
    }

    void finishOutbound()
    {
        if (lockState())
        {
            outbound_waiting_accept_ = false;
            outbound_accept_received_ = false;
            outbound_task_ = nullptr;
            active_ = false;
            unlockState();
        }
    }

    void clearPlaybackTask()
    {
        if (lockState())
        {
            secureClear(media_->playback_media, sizeof(media_->playback_media));
            playback_media_len_ = 0U;
            playback_local_id_ = 0U;
            playback_task_ = nullptr;
            unlockState();
        }
    }

    bool storeCompletedVoice(const vmp::ControlFrame& control,
                             const uint8_t* encoded_media,
                             std::size_t encoded_media_len,
                             uint32_t received_at_seconds)
    {
        if (!media_ || !inbox_ready_)
        {
            return false;
        }
        uint64_t local_id = 0U;
        const vmp::VoiceInboxStoreResult result = media_->inbox.store(
            control,
            encoded_media,
            encoded_media_len,
            true,
            received_at_seconds,
            &local_id);
        if (result == vmp::VoiceInboxStoreResult::Duplicate)
        {
            return true;
        }
        if (result != vmp::VoiceInboxStoreResult::Stored)
        {
            return false;
        }

        if (!requires_durable_attachment_store_)
        {
            return true;
        }
        const bool persisted =
            ::platform::esp::arduino_common::chat_attachment::persistVoiceInbox(
                media_->inbox,
                media_->persistence_metadata,
                vmp::kVoiceInboxCapacity);
        if (!persisted)
        {
            // Match the text ledger's durable-incoming boundary: a completed
            // object is not exposed locally until both its payload and index
            // have been committed. There is no VMP ACK or retransmit here.
            (void)media_->inbox.erase(local_id);
        }
        return persisted;
    }

    uint32_t self_node_id_ = 0U;
    bool initialized_ = false;
    bool direct_rf_voice_supported_ = false;
    bool active_ = false;
    bool requires_durable_attachment_store_ = false;
    bool attachment_store_ready_ = false;
    bool inbox_ready_ = false;
    uint32_t last_persistent_inbox_attempt_ms_ = 0U;
    StaticSemaphore_t state_mutex_storage_{};
    SemaphoreHandle_t state_mutex_ = nullptr;
    vmp::FixedVerifiedContactSecretDirectory contacts_{};
    PagerMediaStorage* media_ = nullptr;
    radio::Lease radio_lease_{};
    TaskHandle_t outbound_task_ = nullptr;
    TaskHandle_t playback_task_ = nullptr;
    uint64_t playback_local_id_ = 0U;
    uint16_t playback_media_len_ = 0U;
    vmp::Codec playback_codec_ = vmp::Codec::Codec2_1300;
    uint32_t outbound_target_id_ = 0U;
    bool outbound_is_broadcast_ = false;
    bool mqtt_uplink_enabled_ = false;
    bool lxmf_carrier_enabled_ = false;
    bool contact_secret_cache_stale_ = false;
    bool outbound_waiting_accept_ = false;
    bool outbound_accept_received_ = false;
    vmp::ControlFrame candidate_control_{};
    vmp::ControlFrame incoming_control_{};
    vmp::ControlFrame outgoing_control_{};
    vmp::ControlFrame peer_accept_{};
    vmp::ControlFrame transport_candidate_control_{};
    vmp::DataHeader data_header_{};
    vmp::EphemeralKeyPair local_ephemeral_{};
    vmp::PrivateSessionKeys session_keys_{};
    uint8_t contact_secret_[vmp::kPrivateKeySize] = {};
    uint8_t contact_derivation_scratch_[vmp::kPrivateKeySize] = {};
    uint8_t control_wire_[vmp::kControlFrameSize] = {};
    uint8_t data_wire_[kMaxRadioFrameSize] = {};
    uint8_t shard_plaintext_[vmp::kMaxShardPayloadSize] = {};
    LxmfEnvelopeSender lxmf_sender_ = nullptr;
    void* lxmf_sender_context_ = nullptr;
    VerifiedContactSecretDeriver contact_secret_deriver_ = nullptr;
    void* contact_secret_deriver_context_ = nullptr;
};

PagerReceiveSession s_session{};

} // namespace

bool initialize(uint32_t self_node_id, bool durable_attachment_store)
{
    return s_session.initialize(self_node_id, durable_attachment_store);
}

bool canRecordAndSend()
{
    return s_session.canRecordAndSend();
}

void onPersistentStorageReady()
{
    s_session.onPersistentStorageReady();
}

void servicePersistentInbox()
{
    s_session.servicePersistentInbox();
}

bool provisionVerifiedContactSecret(
    uint32_t peer_id,
    const uint8_t secret[vmp::kPrivateKeySize])
{
    return s_session.provisionVerifiedContactSecret(peer_id, secret);
}

void setVerifiedContactSecretDeriver(VerifiedContactSecretDeriver deriver,
                                     void* context)
{
    s_session.setVerifiedContactSecretDeriver(deriver, context);
}

void invalidateContactSecretCache()
{
    s_session.invalidateContactSecretCache();
}

const vmp::VoiceMessageInbox* inbox()
{
    return s_session.inbox();
}

std::size_t listInboxMetadata(vmp::VoiceMessageMetadata* out_metadata,
                              std::size_t capacity)
{
    return s_session.listInboxMetadata(out_metadata, capacity);
}

bool playInboxMessage(uint64_t local_id, uint8_t volume_percent)
{
    return s_session.playInboxMessage(local_id, volume_percent);
}

bool requestPlayback(uint64_t local_id)
{
    return s_session.requestPlayback(local_id);
}

bool peekMqttEnvelope(uint8_t* out, std::size_t* inout_len)
{
    return s_session.peekMqttEnvelope(out, inout_len);
}

bool acknowledgeMqttEnvelope()
{
    return s_session.acknowledgeMqttEnvelope();
}

void setMqttUplinkEnabled(bool enabled)
{
    s_session.setMqttUplinkEnabled(enabled);
}

void setLxmfEnvelopeSender(LxmfEnvelopeSender sender, void* context)
{
    s_session.setLxmfEnvelopeSender(sender, context);
}

void setLxmfCarrierEnabled(bool enabled)
{
    s_session.setLxmfCarrierEnabled(enabled);
}

bool acceptMqttEnvelope(const uint8_t* envelope, std::size_t envelope_len)
{
    return s_session.acceptMqttEnvelope(envelope, envelope_len);
}

bool acceptLxmfEnvelope(uint32_t source_id,
                        const uint8_t* envelope,
                        std::size_t envelope_len)
{
    return s_session.acceptLxmfEnvelope(source_id, envelope, envelope_len);
}

void discardMqttPublication()
{
    s_session.discardMqttPublication();
}

StartSendResult requestRecordAndSend(uint32_t target_id)
{
    return s_session.requestRecordAndSend(target_id);
}

} // namespace platform::esp::arduino_common::voice::vmp_session

#else

namespace platform::esp::arduino_common::voice::vmp_session
{

bool initialize(uint32_t, bool)
{
    return false;
}

bool canRecordAndSend()
{
    return false;
}

void onPersistentStorageReady()
{
}

void servicePersistentInbox()
{
}

bool provisionVerifiedContactSecret(
    uint32_t,
    const uint8_t[chat::voice::vmp::kPrivateKeySize])
{
    return false;
}

void setVerifiedContactSecretDeriver(VerifiedContactSecretDeriver, void*)
{
}

void invalidateContactSecretCache()
{
}

const chat::voice::vmp::VoiceMessageInbox* inbox()
{
    return nullptr;
}

std::size_t listInboxMetadata(chat::voice::vmp::VoiceMessageMetadata*, std::size_t)
{
    return 0U;
}

bool playInboxMessage(uint64_t, uint8_t)
{
    return false;
}

bool requestPlayback(uint64_t)
{
    return false;
}

bool peekMqttEnvelope(uint8_t*, std::size_t*)
{
    return false;
}

bool acknowledgeMqttEnvelope()
{
    return false;
}

void setMqttUplinkEnabled(bool)
{
}

void setLxmfEnvelopeSender(LxmfEnvelopeSender, void*)
{
}

void setLxmfCarrierEnabled(bool)
{
}

bool acceptMqttEnvelope(const uint8_t*, std::size_t)
{
    return false;
}

bool acceptLxmfEnvelope(uint32_t, const uint8_t*, std::size_t)
{
    return false;
}

void discardMqttPublication()
{
}

StartSendResult requestRecordAndSend(uint32_t)
{
    return StartSendResult::Unsupported;
}

} // namespace platform::esp::arduino_common::voice::vmp_session

#endif
