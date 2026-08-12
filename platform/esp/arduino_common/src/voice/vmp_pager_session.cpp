/**
 * @file vmp_pager_session.cpp
 * @brief Pager VMP session: LR1121 direct RF or SX1262 MQTT-only carriage.
 */

#include "platform/esp/arduino_common/voice/vmp_pager_session.h"

#if defined(ARDUINO_T_LORA_PAGER)

#include "platform/esp/arduino_common/storage/storage_runtime.h"
#include "platform/esp/arduino_common/voice/vmp_control_runtime.h"
#include "platform/esp/arduino_common/voice/vmp_pager_audio.h"
#include "platform/esp/arduino_common/voice/vmp_radio_lease.h"

#include "chat/infra/voice/vmp_control_auth.h"
#include "chat/infra/voice/vmp_media_frames.h"
#include "chat/infra/voice/vmp_mqtt_transport.h"
#include "chat/infra/voice/vmp_receive_block.h"
#include "platform/esp/arduino_common/chat/infra/store/message_attachment_store.h"
#include "sys/clock.h"

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
// ESP-IDF's xTaskCreatePinnedToCore stack-depth argument is expressed in
// bytes. Codec2 plus the Pager I2S/codec/I2C driver call chain overflows the
// former 4 KiB VMP task before its first capture frame. The VMP media/FEC
// state and non-DMA Codec2 workspaces are PSRAM-backed, but this stack must
// remain internal because the Arduino I2S/codec path may enter
// ROM/cache-disabled code. Keeping Codec2's large automatic workspaces out
// of the task leaves a bounded 8 KiB stack with useful guard space while it
// still fits the Pager's Wi-Fi-fragmented internal heap. These stacks exist
// only for an active record/play operation and are released by vTaskDelete.
constexpr uint32_t kOutboundTaskStackBytes = 8U * 1024U;
constexpr UBaseType_t kOutboundTaskPriority = 4U;
constexpr uint32_t kPlaybackTaskStackBytes = 8U * 1024U;
constexpr UBaseType_t kPlaybackTaskPriority = 3U;
constexpr uint32_t kPersistentInboxRetryMs = 5000U;

bool deadlineExpired(uint32_t deadline)
{
    return static_cast<int32_t>(millis() - deadline) >= 0;
}

void logCurrentTaskStack(const char* task, const char* phase)
{
    const UBaseType_t free_words = uxTaskGetStackHighWaterMark(nullptr);
    Serial.printf("[VMP][Mem] task=%s phase=%s stack_free_words=%u stack_free_bytes=%u\n",
                  task ? task : "-",
                  phase ? phase : "-",
                  static_cast<unsigned>(free_words),
                  static_cast<unsigned>(free_words * sizeof(StackType_t)));
}

void logTaskCreateFailure(const char* task, uint32_t stack_bytes)
{
    const std::size_t internal_free =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t internal_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const std::size_t psram_free =
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const std::size_t psram_largest =
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    Serial.printf("[VMP][Mem] task=%s create_failed stack_bytes=%u internal_free=%u internal_largest=%u psram_free=%u psram_largest=%u\n",
                  task ? task : "-",
                  static_cast<unsigned>(stack_bytes),
                  static_cast<unsigned>(internal_free),
                  static_cast<unsigned>(internal_largest),
                  static_cast<unsigned>(psram_free),
                  static_cast<unsigned>(psram_largest));
}

uint32_t voiceTimestampSeconds()
{
    // Use the same real-time source as text-message persistence. A missing
    // clock deliberately remains zero instead of fabricating an uptime value
    // that would sort a voice attachment ahead of dated text history.
    return ::sys::epoch_seconds_now();
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

enum class OutboundCarrier : uint8_t
{
    None = 0U,
    Mqtt,
    Lxmf,
    DirectRf,
};

const char* outboundCarrierName(OutboundCarrier carrier)
{
    switch (carrier)
    {
    case OutboundCarrier::Mqtt:
        return "mqtt";
    case OutboundCarrier::Lxmf:
        return "lxmf";
    case OutboundCarrier::DirectRf:
        return "lr1121_rf";
    case OutboundCarrier::None:
    default:
        return "none";
    }
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
        attachment_store_ready_ = !requires_durable_attachment_store_;
        inbox_ready_ = !requires_durable_attachment_store_;
        attachment_store_wait_logged_ = false;
        last_persistent_inbox_attempt_ms_ = 0U;
        if (direct_rf_voice_supported_)
        {
            control::setEnvelopeHandler(&PagerReceiveSession::controlEnvelopeReceived, this);
        }
        initialized_ = true;
        Serial.printf("[VMP] init carrier=%s durable_inbox=%u inbox_ready=%u audio=%u\n",
                      direct_rf_voice_supported_ ? "lr1121_rf" : "sx1262_mqtt_only",
                      requires_durable_attachment_store_ ? 1U : 0U,
                      inbox_ready_ ? 1U : 0U,
                      media_->audio.isSupported() ? 1U : 0U);
        return true;
    }

    bool canRecordAndSend() const
    {
        if (!initialized_ || !media_ || !inbox_ready_ ||
            !media_->audio.isSupported() || !lockState())
        {
            return false;
        }
        // SX1262 does not have the LR1121 2.4 GHz path. Its Pager voice
        // compose action becomes available only after the MT MQTT uplink has
        // completed CONNACK; it cannot fall through to RF or LXMF. LR1121
        // remains recordable through its direct-RF fallback while MQTT is
        // disconnected.
        const bool available = direct_rf_voice_supported_ ||
                               (mqtt_uplink_enabled_ && mqtt_uplink_online_);
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
            attachment_store_wait_logged_ = false;
            last_persistent_inbox_attempt_ms_ = 0U;
            unlockState();
        }
        Serial.printf("[VMP][Store] deferred hydration edge received\n");
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
        if (!attachment_store_ready_ &&
            ::platform::esp::arduino_common::storage::initial_hydration_ready())
        {
            // The hydration-ready edge is intentionally one-shot and may have
            // been consumed before VMP's UI runtime became active. Read the
            // durable completion state as a subscriber-independent fallback.
            attachment_store_ready_ = true;
            attachment_store_wait_logged_ = false;
            last_persistent_inbox_attempt_ms_ = 0U;
            Serial.printf("[VMP][Store] durable hydration observed after edge\n");
        }
        if (!attachment_store_ready_)
        {
            if (!attachment_store_wait_logged_)
            {
                attachment_store_wait_logged_ = true;
                Serial.printf("[VMP][Store] waiting hydration pending=%u active=%u\n",
                              ::platform::esp::arduino_common::storage::
                                      initial_hydration_pending()
                                  ? 1U
                                  : 0U,
                              ::platform::esp::arduino_common::storage::
                                      hydration_active()
                                  ? 1U
                                  : 0U);
            }
            unlockState();
            return;
        }
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
            const bool rebound_legacy =
                presentation_protocol_ != vmp::VoicePresentationProtocol::Unknown &&
                media_->inbox.bindUnassignedMessages(
                    presentation_protocol_,
                    vmp::kVoicePresentationPrimaryChannel,
                    true);
            if (result == ::platform::esp::arduino_common::chat_attachment::
                              VoiceInboxLoadResult::Restored &&
                !::platform::esp::arduino_common::chat_attachment::persistVoiceInbox(
                    media_->inbox,
                    media_->persistence_metadata,
                    vmp::kVoiceInboxCapacity))
            {
                // Restore converts an interrupted outgoing `Sending` state to
                // local `Failed`; retain the safe RAM state even if the first
                // healing snapshot cannot be committed yet.
                Serial.printf("[VMP] attachment inbox status-heal deferred\n");
            }
            Serial.printf("[VMP] attachment inbox restore=%s legacy_rebound=%u\n",
                          result == ::platform::esp::arduino_common::chat_attachment::
                                        VoiceInboxLoadResult::Restored
                              ? "restored"
                              : "empty",
                          rebound_legacy ? 1U : 0U);
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
            Serial.printf("[VMP][PLAY] rejected local_id=%llu reason=voice_busy\n",
                          static_cast<unsigned long long>(local_id));
            return false;
        }

        if (!lockState())
        {
            return false;
        }
        if (active_ || playback_task_)
        {
            unlockState();
            Serial.printf("[VMP][PLAY] rejected local_id=%llu reason=became_busy\n",
                          static_cast<unsigned long long>(local_id));
            return false;
        }
        vmp::VoiceMessageView view{};
        if (!media_->inbox.get(local_id, &view) || !view.encoded_media ||
            view.metadata.encoded_media_len > sizeof(media_->playback_media))
        {
            unlockState();
            Serial.printf("[VMP][PLAY] rejected local_id=%llu reason=not_found\n",
                          static_cast<unsigned long long>(local_id));
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
                                    kPlaybackTaskStackBytes,
                                    this,
                                    kPlaybackTaskPriority,
                                    &playback_task_,
                                    tskNO_AFFINITY) != pdPASS)
        {
            playback_local_id_ = 0U;
            playback_task_ = nullptr;
            unlockState();
            Serial.printf("[VMP][PLAY] rejected local_id=%llu reason=worker_create\n",
                          static_cast<unsigned long long>(local_id));
            return false;
        }
        unlockState();
        Serial.printf("[VMP][PLAY] queued local_id=%llu bytes=%u\n",
                      static_cast<unsigned long long>(local_id),
                      static_cast<unsigned>(playback_media_len_));
        return true;
    }

    bool peekMqttEnvelope(uint8_t* out, std::size_t* inout_len)
    {
        if (!initialized_ || !out || !inout_len || !lockState())
        {
            return false;
        }
        const bool emitted = mqtt_uplink_enabled_ && mqtt_uplink_online_ &&
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
            commitMqttDeliveryLocked(vmp::VoiceDeliveryState::Sent);
        }
        unlockState();
        return true;
    }

    void setMqttUplinkEnabled(bool enabled)
    {
        if (lockState())
        {
            mqtt_uplink_enabled_ = enabled;
            if (!enabled)
            {
                mqtt_uplink_online_ = false;
                discardMqttDeliveryLocked("uplink_disabled");
            }
            unlockState();
        }
    }

    void setMqttUplinkOnline(bool online)
    {
        if (!lockState())
        {
            return;
        }
        const bool next_online = online && mqtt_uplink_enabled_;
        if (mqtt_uplink_online_ != next_online)
        {
            mqtt_uplink_online_ = next_online;
            Serial.printf("[VMP][MQTT] carrier online=%u policy_enabled=%u\n",
                          mqtt_uplink_online_ ? 1U : 0U,
                          mqtt_uplink_enabled_ ? 1U : 0U);
        }
        unlockState();
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
                    discardMqttDeliveryLocked("no_carrier");
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
                discardMqttDeliveryLocked("no_carrier");
            }
            unlockState();
        }
    }

    void setPresentationProtocol(uint8_t protocol)
    {
        const auto presentation =
            static_cast<vmp::VoicePresentationProtocol>(protocol);
        if (!vmp::isValidVoicePresentationBinding(
                presentation, vmp::kVoicePresentationPrimaryChannel) ||
            !lockState())
        {
            return;
        }
        presentation_protocol_ = presentation;
        unlockState();
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
                                                 voiceTimestampSeconds());
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
            discardMqttDeliveryLocked("discarded");
            unlockState();
        }
    }

    StartSendResult requestRecordAndSend(uint32_t target_id,
                                         uint8_t presentation_protocol,
                                         uint8_t presentation_channel)
    {
        const bool broadcast = target_id == vmp::kBroadcastTargetId;
        const auto presentation =
            static_cast<vmp::VoicePresentationProtocol>(presentation_protocol);
        if (!initialized_ || !media_ || !inbox_ready_ ||
            !media_->audio.isSupported())
        {
            Serial.printf("[VMP][TX] hold begin rejected reason=unavailable initialized=%u media=%u inbox_ready=%u audio=%u durable=%u store_ready=%u\n",
                          initialized_ ? 1U : 0U,
                          media_ ? 1U : 0U,
                          inbox_ready_ ? 1U : 0U,
                          (media_ && media_->audio.isSupported()) ? 1U : 0U,
                          requires_durable_attachment_store_ ? 1U : 0U,
                          attachment_store_ready_ ? 1U : 0U);
            return StartSendResult::Unsupported;
        }
        if ((!broadcast && target_id == 0U) ||
            !vmp::isValidVoicePresentationBinding(presentation,
                                                  presentation_channel) ||
            !lockState())
        {
            Serial.printf("[VMP][TX] hold begin rejected reason=invalid_target_or_lock\n");
            return StartSendResult::Busy;
        }
        const OutboundCarrier candidate_carrier =
            selectOutboundCarrierLocked(broadcast);
        // There is one bounded MQTT transfer slot. Do not start a second
        // record/send session while it owns an earlier clip, even if MQTT has
        // subsequently gone offline and LR1121 RF would otherwise be usable.
        // This avoids overwriting the clip, dual live media allocations, and
        // an implicit alternate-carrier copy under memory pressure.
        const bool mqtt_delivery_pending =
            mqtt_pending_local_id_ != 0U || media_->mqtt_transmit.hasNext();
        const bool unavailable = presentation != presentation_protocol_ || active_ ||
                                 outbound_task_ || playback_task_ ||
                                 candidate_carrier == OutboundCarrier::None ||
                                 mqtt_delivery_pending;
        unlockState();
        if (unavailable)
        {
            Serial.printf("[VMP][TX] hold begin rejected reason=busy_or_no_carrier\n");
            return StartSendResult::Busy;
        }
        if (!broadcast && !ensureVerifiedContactSecret(target_id))
        {
            Serial.printf("[VMP][TX] hold begin rejected reason=private_contact_unverified\n");
            return StartSendResult::PrivateContactUnverified;
        }

        if (!lockState())
        {
            return StartSendResult::Busy;
        }
        const OutboundCarrier selected_carrier =
            selectOutboundCarrierLocked(broadcast);
        const bool selected_mqtt_delivery_pending =
            mqtt_pending_local_id_ != 0U || media_->mqtt_transmit.hasNext();
        if (active_ || outbound_task_ || playback_task_ ||
            selected_carrier == OutboundCarrier::None || selected_mqtt_delivery_pending)
        {
            unlockState();
            Serial.printf("[VMP][TX] hold begin rejected reason=became_busy_or_no_carrier\n");
            return StartSendResult::Busy;
        }
        outbound_target_id_ = target_id;
        outbound_is_broadcast_ = broadcast;
        outbound_presentation_protocol_ = presentation;
        outbound_presentation_channel_ = presentation_channel;
        outbound_carrier_ = selected_carrier;
        record_stop_requested_ = false;
        active_ = true;
        outbound_active_ = true;
        unlockState();
        Serial.printf("[VMP][TX] hold begin accepted mode=%s target=%08lX carrier=%s rf_suppressed=%u\n",
                      broadcast ? "broadcast" : "private",
                      static_cast<unsigned long>(target_id),
                      outboundCarrierName(selected_carrier),
                      selected_carrier == OutboundCarrier::Mqtt &&
                              direct_rf_voice_supported_
                          ? 1U
                          : 0U);
        if (xTaskCreatePinnedToCore(&PagerReceiveSession::outboundTaskEntry,
                                    "vmp_tx",
                                    kOutboundTaskStackBytes,
                                    this,
                                    kOutboundTaskPriority,
                                    nullptr,
                                    tskNO_AFFINITY) != pdPASS)
        {
            if (lockState())
            {
                active_ = false;
                outbound_active_ = false;
                unlockState();
            }
            logTaskCreateFailure("vmp_tx", kOutboundTaskStackBytes);
            Serial.printf("[VMP][TX] hold begin failed reason=worker_create\n");
            return StartSendResult::ResourceUnavailable;
        }
        Serial.printf("[VMP][TX] capture worker queued\n");
        return StartSendResult::Queued;
    }

    bool isOutboundActive() const
    {
        if (!initialized_ || !lockState())
        {
            return false;
        }
        const bool active = outbound_active_;
        unlockState();
        return active;
    }

    bool requestStopRecording()
    {
        if (!initialized_ || !lockState())
        {
            Serial.printf("[VMP][TX] hold release ignored reason=unavailable\n");
            return false;
        }
        const bool active = active_;
        if (active)
        {
            // The capture loop observes this without taking the state mutex,
            // so a release cannot block the LVGL task behind an I2S read.
            record_stop_requested_ = true;
        }
        unlockState();
        Serial.printf("[VMP][TX] hold release stop_requested=%u\n",
                      active ? 1U : 0U);
        return active;
    }

    bool markConversationRead(uint8_t presentation_protocol,
                              uint8_t presentation_channel,
                              uint32_t peer_id,
                              bool broadcast)
    {
        const auto protocol =
            static_cast<vmp::VoicePresentationProtocol>(presentation_protocol);
        if (!initialized_ || !media_ || !inbox_ready_ ||
            !vmp::isValidVoicePresentationBinding(protocol,
                                                  presentation_channel) ||
            !lockState())
        {
            return false;
        }
        const bool changed = media_->inbox.markConversationRead(protocol,
                                                                presentation_channel,
                                                                peer_id,
                                                                broadcast);
        const bool persisted =
            !changed || !requires_durable_attachment_store_ ||
            ::platform::esp::arduino_common::chat_attachment::persistVoiceInbox(
                media_->inbox,
                media_->persistence_metadata,
                vmp::kVoiceInboxCapacity);
        unlockState();
        if (changed)
        {
            Serial.printf("[VMP][UI] conversation_read protocol=%u channel=%u peer=%08lX broadcast=%u durable=%u\n",
                          static_cast<unsigned>(presentation_protocol),
                          static_cast<unsigned>(presentation_channel),
                          static_cast<unsigned long>(peer_id),
                          broadcast ? 1U : 0U,
                          persisted ? 1U : 0U);
        }
        return persisted;
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
            Serial.printf("[VMP][RX] private offer accepted source=%08lX channel=%u media_bytes=%u\n",
                          static_cast<unsigned long>(candidate_control_.sender_id),
                          static_cast<unsigned>(candidate_control_.conversation_channel),
                          static_cast<unsigned>(candidate_control_.encoded_media_len));
            handlePrivateOffer(envelope.bytes);
            return;
        }

        if (candidate_control_.type == vmp::ControlType::Announce &&
            candidate_control_.target_id == vmp::kBroadcastTargetId &&
            vmp::decodePublicControlFrame(
                envelope.bytes, sizeof(envelope.bytes), &incoming_control_) &&
            tryBeginInbound())
        {
            Serial.printf("[VMP][RX] broadcast announce accepted source=%08lX channel=%u media_bytes=%u\n",
                          static_cast<unsigned long>(incoming_control_.sender_id),
                          static_cast<unsigned>(incoming_control_.conversation_channel),
                          static_cast<unsigned>(incoming_control_.encoded_media_len));
            receiveBroadcast();
        }
    }

    static void outboundTaskEntry(void* context)
    {
        auto* const self = static_cast<PagerReceiveSession*>(context);
        if (self)
        {
            self->setOutboundTask(xTaskGetCurrentTaskHandle());
            Serial.printf("[VMP][TX] capture worker start stack_budget_bytes=%u\n",
                          static_cast<unsigned>(kOutboundTaskStackBytes));
            self->runOutbound();
        }
        vTaskDelete(nullptr);
    }

    static void playbackTaskEntry(void* context)
    {
        auto* const self = static_cast<PagerReceiveSession*>(context);
        if (self)
        {
            Serial.printf("[VMP][PLAY] worker start stack_budget_bytes=%u\n",
                          static_cast<unsigned>(kPlaybackTaskStackBytes));
            self->runPlayback();
        }
        vTaskDelete(nullptr);
    }

    void runOutbound()
    {
        bool sent = false;
        bool mqtt_queued = false;
        bool local_object_stored = false;
        const uint32_t started_ms = millis();
        const audio::CaptureResult capture_result =
            media_->audio.capture(&record_stop_requested_);
        logCurrentTaskStack("vmp_tx", "after_capture");
        if (capture_result != audio::CaptureResult::Complete ||
            !media_->audio.hasEncodedMedia())
        {
            Serial.printf("[VMP][TX] capture discarded result=%u bytes=%u\n",
                          static_cast<unsigned>(capture_result),
                          static_cast<unsigned>(media_->audio.encodedMediaSize()));
        }
        else if (!media_->transmit_block.prepare(media_->audio.encodedMedia(),
                                                 media_->audio.encodedMediaSize()))
        {
            Serial.printf("[VMP][TX] encode rejected reason=fec_prepare bytes=%u\n",
                          static_cast<unsigned>(media_->audio.encodedMediaSize()));
        }
        else if (!prepareOutboundControl())
        {
            Serial.printf("[VMP][TX] encode rejected reason=control_prepare\n");
        }
        else if (!storeOutboundVoice())
        {
            Serial.printf("[VMP][TX] local message commit failed; carrier skipped\n");
        }
        else
        {
            local_object_stored = true;
            Serial.printf("[VMP][TX] encoded bytes=%u shards=%u mode=%s\n",
                          static_cast<unsigned>(media_->audio.encodedMediaSize()),
                          static_cast<unsigned>(vmp::kTotalShardsPerBlock),
                          outbound_is_broadcast_ ? "broadcast" : "private");
            switch (outbound_carrier_)
            {
            case OutboundCarrier::Mqtt:
                // A live MT MQTT session is the one selected carrier. In
                // particular, LR1121 never starts its Sub-GHz/2.4 GHz voice
                // session for this clip, and an MQTT-plan failure cannot turn
                // into an automatic RF duplicate.
                Serial.printf("[VMP][TX] carrier=mqtt plan_begin rf_suppressed=%u\n",
                              direct_rf_voice_supported_ ? 1U : 0U);
                mqtt_queued = queueMqttPublication();
                break;
            case OutboundCarrier::Lxmf:
                Serial.printf("[VMP][TX] carrier=lxmf begin\n");
                sent = sendLxmfVoice();
                break;
            case OutboundCarrier::DirectRf:
                Serial.printf("[VMP][TX] carrier=%s begin\n",
                              outbound_is_broadcast_ ? "lr1121_rf_broadcast"
                                                     : "lr1121_rf_private");
                sent = outbound_is_broadcast_ ? sendBroadcastVoice()
                                              : sendPrivateVoice();
                break;
            case OutboundCarrier::None:
            default:
                Serial.printf("[VMP][TX] carrier unavailable after capture\n");
                break;
            }
        }
        if (local_object_stored)
        {
            if (mqtt_queued)
            {
                Serial.printf("[VMP][TX] local delivery awaiting_mqtt_socket\n");
            }
            else
            {
                commitDelivery(outbound_local_id_,
                               sent ? vmp::VoiceDeliveryState::Sent
                                    : vmp::VoiceDeliveryState::Failed,
                               outboundCarrierName(outbound_carrier_));
            }
        }
        Serial.printf("[VMP][TX] outbound end carrier=%s sent=%u mqtt_pending=%u elapsed_ms=%lu\n",
                      outboundCarrierName(outbound_carrier_),
                      sent ? 1U : 0U,
                      mqtt_queued ? 1U : 0U,
                      static_cast<unsigned long>(millis() - started_ms));
        clearOutboundAcceptWait();
        media_->audio.clearEncodedMedia();
        media_->transmit_block.clear();
        releaseRadio();
        resetEphemeralState();
        finishOutbound();
        logCurrentTaskStack("vmp_tx", "complete");
    }

    void runPlayback()
    {
        audio::PlaybackResult result = audio::PlaybackResult::InvalidMedia;
        if (playback_local_id_ != 0U && playback_media_len_ != 0U)
        {
            Serial.printf("[VMP][PLAY] begin local_id=%llu bytes=%u\n",
                          static_cast<unsigned long long>(playback_local_id_),
                          static_cast<unsigned>(playback_media_len_));
            result = media_->audio.play(media_->playback_media,
                                        playback_media_len_,
                                        playback_codec_,
                                        100U);
        }
        logCurrentTaskStack("vmp_play", "after_playback");
        Serial.printf("[VMP][PLAY] end local_id=%llu result=%u\n",
                      static_cast<unsigned long long>(playback_local_id_),
                      static_cast<unsigned>(result));
        clearPlaybackTask();
        logCurrentTaskStack("vmp_play", "complete");
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
            Serial.printf("[VMP][MQTT] plan rejected reason=uplink_disabled\n");
            return false;
        }
        if (!mqtt_uplink_online_)
        {
            unlockState();
            Serial.printf("[VMP][MQTT] plan rejected reason=offline\n");
            return false;
        }
        if (mqtt_pending_local_id_ != 0U || media_->mqtt_transmit.hasNext())
        {
            unlockState();
            Serial.printf("[VMP][MQTT] plan rejected reason=previous_delivery_pending\n");
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
        else
        {
            mqtt_pending_local_id_ = outbound_local_id_;
        }
        unlockState();
        Serial.printf("[VMP][MQTT] plan %s mode=%s local_id=%llu\n",
                      prepared ? "ready" : "rejected",
                      outbound_is_broadcast_ ? "broadcast" : "private",
                      static_cast<unsigned long long>(outbound_local_id_));
        return prepared;
    }

    OutboundCarrier selectOutboundCarrierLocked(bool broadcast) const
    {
        // MQTT priority is deliberately a session-admission decision. Once a
        // clip is accepted, its selected carrier is immutable: a later MQTT
        // loss records failure/retry state but never creates an RF duplicate.
        if (mqtt_uplink_enabled_ && mqtt_uplink_online_)
        {
            return OutboundCarrier::Mqtt;
        }
        if (!direct_rf_voice_supported_)
        {
            return OutboundCarrier::None;
        }
        if (!broadcast && lxmf_carrier_enabled_ && lxmf_sender_ != nullptr)
        {
            return OutboundCarrier::Lxmf;
        }
        return OutboundCarrier::DirectRf;
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
        outgoing_control_.conversation_channel = outbound_presentation_channel_;
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
            Serial.printf("[VMP][RF] private offer prepare_or_lease_failed\n");
            return false;
        }

        beginOutboundAcceptWait();
        Serial.printf("[VMP][RF] private offer tx; wait_accept_ms=%lu\n",
                      static_cast<unsigned long>(kPrivateAcceptWindowMs));
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
            Serial.printf("[VMP][RF] private accept failed_or_timed_out\n");
            return false;
        }
        Serial.printf("[VMP][RF] private accept authenticated; enter_2g_ready\n");
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
            Serial.printf("[VMP][RF] broadcast announce failed\n");
            return false;
        }
        releaseRadio();
        Serial.printf("[VMP][RF] broadcast announce sent; enter_2g_after_ms=%lu\n",
                      static_cast<unsigned long>(outgoing_control_.data_start_delay_ms));

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
                Serial.printf("[VMP][RF] broadcast ready_probe failed index=%u\n",
                              static_cast<unsigned>(probe));
                return false;
            }
            Serial.printf("[VMP][RF] broadcast ready_probe sent index=%u\n",
                          static_cast<unsigned>(probe + 1U));
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
            Serial.printf("[VMP][RF] private ready_probe attempt=%u\n",
                          static_cast<unsigned>(probe + 1U));
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
            Serial.printf("[VMP][RF] private ready authenticated\n");
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
        Serial.printf("[VMP][RF] shard_train begin mode=%s count=%u\n",
                      private_mode ? "private" : "broadcast",
                      static_cast<unsigned>(vmp::kTotalShardsPerBlock));
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
                Serial.printf("[VMP][RF] shard_train failed index=%u\n",
                              static_cast<unsigned>(shard_index));
                return false;
            }
        }
        releaseRadio();
        Serial.printf("[VMP][RF] shard_train complete\n");
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
            Serial.printf("[VMP][RX] private offer rejected reason=auth_key_or_fec_prepare\n");
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
            Serial.printf("[VMP][RX] private accept tx failed\n");
            releaseRadio();
            resetEphemeralState();
            setActive(false);
            return;
        }
        Serial.printf("[VMP][RX] private accept sent; switch_2g_rx\n");

        radio::PhyProfile profile{};
        if (!profileFor(incoming_control_, &profile) ||
            !radio::switchTo2Ghz(&radio_lease_, profile) ||
            !radio::startReceive(&radio_lease_))
        {
            Serial.printf("[VMP][RX] private 2g rx setup failed\n");
            releaseRadio();
            resetEphemeralState();
            setActive(false);
            return;
        }
        Serial.printf("[VMP][RX] private 2g rx ready window_ms=%lu\n",
                      static_cast<unsigned long>(kReceiveWindowMs));
        receivePrivateMedia();
        setActive(false);
        releaseRadio();
        resetEphemeralState();
    }

    void receiveBroadcast()
    {
        if (!prepareReceiveBlock())
        {
            Serial.printf("[VMP][RX] broadcast announce rejected reason=fec_prepare\n");
            setActive(false);
            return;
        }
        radio::PhyProfile profile{};
        if (!profileFor(incoming_control_, &profile) ||
            !radio::tryAcquire(&radio_lease_) ||
            !radio::switchTo2Ghz(&radio_lease_, profile) ||
            !radio::startReceive(&radio_lease_))
        {
            Serial.printf("[VMP][RX] broadcast 2g rx setup failed\n");
            releaseRadio();
            setActive(false);
            return;
        }
        Serial.printf("[VMP][RX] broadcast 2g rx ready window_ms=%lu\n",
                      static_cast<unsigned long>(kReceiveWindowMs));
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
                Serial.printf("[VMP][RX] private media complete unique_shards=%u\n",
                              static_cast<unsigned>(media_->receive_block.receivedShardCount()));
                return;
            }
            (void)radio::startReceive(&radio_lease_);
        }
        Serial.printf("[VMP][RX] private media timeout ready=%u unique_shards=%u\n",
                      ready_sent ? 1U : 0U,
                      static_cast<unsigned>(media_->receive_block.receivedShardCount()));
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
                Serial.printf("[VMP][RX] broadcast media complete unique_shards=%u\n",
                              static_cast<unsigned>(media_->receive_block.receivedShardCount()));
                return;
            }
            (void)radio::startReceive(&radio_lease_);
        }
        Serial.printf("[VMP][RX] broadcast media timeout unique_shards=%u\n",
                      static_cast<unsigned>(media_->receive_block.receivedShardCount()));
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
            Serial.printf("[VMP][RX] private ready tx failed\n");
            return;
        }
        *ready_sent = true;
        Serial.printf("[VMP][RX] private ready sent; wait_first_voice\n");
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
        Serial.printf("[VMP][RX] private shard authenticated index=%u\n",
                      static_cast<unsigned>(data_header_.shard_index));
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
        Serial.printf("[VMP][RX] broadcast shard valid index=%u\n",
                      static_cast<unsigned>(data_header_.shard_index));
        return acceptShardAndStore(data_header_, shard);
    }

    bool acceptShardAndStore(const vmp::DataHeader& header, const uint8_t* shard)
    {
        const vmp::ReceiveBlockResult result = media_->receive_block.accept(
            header, shard, vmp::kMaxShardPayloadSize);
        if (result != vmp::ReceiveBlockResult::Complete)
        {
            if (result == vmp::ReceiveBlockResult::Accepted)
            {
                Serial.printf("[VMP][RX] shard accepted index=%u unique_shards=%u\n",
                              static_cast<unsigned>(header.shard_index),
                              static_cast<unsigned>(media_->receive_block.receivedShardCount()));
            }
            return false;
        }
        Serial.printf("[VMP][RX] shard quorum reached unique_shards=%u; recover\n",
                      static_cast<unsigned>(media_->receive_block.receivedShardCount()));
        std::size_t media_len = 0U;
        if (!media_->receive_block.recover(media_->received_media,
                                           sizeof(media_->received_media),
                                           &media_len))
        {
            Serial.printf("[VMP][RX] shard recovery failed\n");
            return false;
        }
        if (!lockState())
        {
            return false;
        }
        const bool stored = storeCompletedVoice(incoming_control_,
                                                media_->received_media,
                                                media_len,
                                                voiceTimestampSeconds());
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
            outbound_active_ = false;
            outbound_local_id_ = 0U;
            outbound_carrier_ = OutboundCarrier::None;
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
            presentation_protocol_,
            control.conversation_channel,
            &local_id);
        if (result == vmp::VoiceInboxStoreResult::Duplicate)
        {
            Serial.printf("[VMP][RX] inbox duplicate bytes=%u\n",
                          static_cast<unsigned>(encoded_media_len));
            return true;
        }
        if (result != vmp::VoiceInboxStoreResult::Stored)
        {
            Serial.printf("[VMP][RX] inbox rejected result=%u bytes=%u\n",
                          static_cast<unsigned>(result),
                          static_cast<unsigned>(encoded_media_len));
            return false;
        }

        if (!requires_durable_attachment_store_)
        {
            Serial.printf("[VMP][RX] inbox stored volatile bytes=%u\n",
                          static_cast<unsigned>(encoded_media_len));
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
            Serial.printf("[VMP][RX] inbox persistence failed rollback=1\n");
        }
        else
        {
            Serial.printf("[VMP][RX] inbox durable_commit local_id=%llu bytes=%u\n",
                          static_cast<unsigned long long>(local_id),
                          static_cast<unsigned>(encoded_media_len));
        }
        return persisted;
    }

    bool storeOutboundVoice()
    {
        if (!media_ || !inbox_ready_ || !media_->audio.hasEncodedMedia())
        {
            return false;
        }

        uint64_t local_id = 0U;
        const vmp::VoiceInboxStoreResult result = media_->inbox.storeOutgoing(
            outgoing_control_,
            media_->audio.encodedMedia(),
            media_->audio.encodedMediaSize(),
            voiceTimestampSeconds(),
            outbound_presentation_protocol_,
            outbound_presentation_channel_,
            &local_id);
        if (result != vmp::VoiceInboxStoreResult::Stored)
        {
            Serial.printf("[VMP][TX] local message store rejected result=%u\n",
                          static_cast<unsigned>(result));
            return false;
        }

        if (requires_durable_attachment_store_ &&
            !::platform::esp::arduino_common::chat_attachment::persistVoiceInbox(
                media_->inbox,
                media_->persistence_metadata,
                vmp::kVoiceInboxCapacity))
        {
            (void)media_->inbox.erase(local_id);
            Serial.printf("[VMP][TX] local message persistence failed rollback=1\n");
            return false;
        }

        outbound_local_id_ = local_id;
        Serial.printf("[VMP][TX] local message committed local_id=%llu delivery=sending\n",
                      static_cast<unsigned long long>(local_id));
        return true;
    }

    void commitMqttDeliveryLocked(vmp::VoiceDeliveryState delivery)
    {
        if (mqtt_pending_local_id_ == 0U)
        {
            Serial.printf("[VMP][MQTT] delivery update skipped reason=no_pending_local_id\n");
            return;
        }
        const uint64_t local_id = mqtt_pending_local_id_;
        mqtt_pending_local_id_ = 0U;
        commitDelivery(local_id, delivery, "mqtt_socket");
    }

    void discardMqttDeliveryLocked(const char* reason)
    {
        if (media_)
        {
            media_->mqtt_transmit.clear();
        }
        if (mqtt_pending_local_id_ == 0U)
        {
            return;
        }
        Serial.printf("[VMP][MQTT] delivery discarded reason=%s local_id=%llu\n",
                      reason ? reason : "unknown",
                      static_cast<unsigned long long>(mqtt_pending_local_id_));
        commitMqttDeliveryLocked(vmp::VoiceDeliveryState::Failed);
    }

    void commitDelivery(uint64_t local_id,
                        vmp::VoiceDeliveryState delivery,
                        const char* carrier)
    {
        if (!media_ || local_id == 0U ||
            !media_->inbox.updateDeliveryState(local_id, delivery))
        {
            Serial.printf("[VMP][TX] local delivery update skipped state=%u\n",
                          static_cast<unsigned>(delivery));
            return;
        }

        if (requires_durable_attachment_store_ &&
            !::platform::esp::arduino_common::chat_attachment::persistVoiceInbox(
                media_->inbox,
                media_->persistence_metadata,
                vmp::kVoiceInboxCapacity))
        {
            // Do not report a durable terminal status that was not committed.
            // A later UI refresh keeps the safe `Sending` state rather than
            // inventing a successful or failed history entry.
            (void)media_->inbox.updateDeliveryState(
                local_id, vmp::VoiceDeliveryState::Sending);
            Serial.printf("[VMP][TX] local delivery persistence deferred state=%u\n",
                          static_cast<unsigned>(delivery));
            return;
        }
        Serial.printf("[VMP][TX] local delivery committed local_id=%llu state=%u carrier=%s\n",
                      static_cast<unsigned long long>(local_id),
                      static_cast<unsigned>(delivery),
                      carrier ? carrier : "unknown");
    }

    uint32_t self_node_id_ = 0U;
    bool initialized_ = false;
    bool direct_rf_voice_supported_ = false;
    bool active_ = false;
    bool outbound_active_ = false;
    volatile bool record_stop_requested_ = false;
    bool requires_durable_attachment_store_ = false;
    bool attachment_store_ready_ = false;
    bool attachment_store_wait_logged_ = false;
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
    vmp::VoicePresentationProtocol presentation_protocol_ =
        vmp::VoicePresentationProtocol::Unknown;
    vmp::VoicePresentationProtocol outbound_presentation_protocol_ =
        vmp::VoicePresentationProtocol::Unknown;
    uint8_t outbound_presentation_channel_ = vmp::kVoicePresentationPrimaryChannel;
    uint64_t outbound_local_id_ = 0U;
    bool outbound_is_broadcast_ = false;
    OutboundCarrier outbound_carrier_ = OutboundCarrier::None;
    bool mqtt_uplink_enabled_ = false;
    bool mqtt_uplink_online_ = false;
    uint64_t mqtt_pending_local_id_ = 0U;
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

void setMqttUplinkOnline(bool online)
{
    s_session.setMqttUplinkOnline(online);
}

void setLxmfEnvelopeSender(LxmfEnvelopeSender sender, void* context)
{
    s_session.setLxmfEnvelopeSender(sender, context);
}

void setLxmfCarrierEnabled(bool enabled)
{
    s_session.setLxmfCarrierEnabled(enabled);
}

void setPresentationProtocol(uint8_t protocol)
{
    s_session.setPresentationProtocol(protocol);
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

StartSendResult requestRecordAndSend(uint32_t target_id,
                                     uint8_t presentation_protocol,
                                     uint8_t presentation_channel)
{
    return s_session.requestRecordAndSend(target_id,
                                          presentation_protocol,
                                          presentation_channel);
}

bool markConversationRead(uint8_t presentation_protocol,
                          uint8_t presentation_channel,
                          uint32_t peer_id,
                          bool broadcast)
{
    return s_session.markConversationRead(presentation_protocol,
                                          presentation_channel,
                                          peer_id,
                                          broadcast);
}

bool requestStopRecording()
{
    return s_session.requestStopRecording();
}

bool isOutboundActive()
{
    return s_session.isOutboundActive();
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

void setMqttUplinkOnline(bool)
{
}

void setLxmfEnvelopeSender(LxmfEnvelopeSender, void*)
{
}

void setLxmfCarrierEnabled(bool)
{
}

void setPresentationProtocol(uint8_t)
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

StartSendResult requestRecordAndSend(uint32_t, uint8_t, uint8_t)
{
    return StartSendResult::Unsupported;
}

bool markConversationRead(uint8_t, uint8_t, uint32_t, bool)
{
    return false;
}

bool requestStopRecording()
{
    return false;
}

bool isOutboundActive()
{
    return false;
}

} // namespace platform::esp::arduino_common::voice::vmp_session

#endif
