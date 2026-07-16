/**
 * @file lxmf_adapter_call.cpp
 * @brief LXST call orchestration for the embedded LXMF adapter
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_adapter.h"

#include "chat/infra/reticulum/audio_call_wire.h"
#include "chat/infra/reticulum/lxst_telephony_wire.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_call_profile.h"
#include "platform/ui/reticulum_call_runtime.h"

#include <cstdio>
#include <cstring>

namespace chat::lxmf
{
namespace
{

void copyHash(uint8_t* out, const uint8_t* in, size_t len)
{
    if (out && in && len != 0)
    {
        std::memcpy(out, in, len);
    }
}

void formatHashPrefix(const uint8_t* hash, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || out_len < 9)
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%02X%02X%02X%02X",
                  hash[0],
                  hash[1],
                  hash[2],
                  hash[3]);
}

} // namespace

void LxmfAdapter::updateCallRuntimePeer(LinkSession& session,
                                        const PeerInfo* peer)
{
    if (session.destination != LocalDestinationKind::CallAudio)
    {
        return;
    }

    ::platform::ui::reticulum_call::Peer call_peer{};
    copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
    copyHash(call_peer.identity_hash,
             session.remote_identity_hash,
             sizeof(call_peer.identity_hash));
    if (peer)
    {
        copyHash(call_peer.destination_hash,
                 peer->destination_hash,
                 sizeof(call_peer.destination_hash));
        call_peer.display_name =
            peer->display_name[0] != '\0' ? peer->display_name : nullptr;
    }
    else
    {
        copyHash(call_peer.destination_hash,
                 session.remote_destination_hash,
                 sizeof(call_peer.destination_hash));
    }
    call_peer.incoming = !session.initiator;
    call_peer.wire_profile =
        call_profile::runtimeWireProfile(session.call_wire_profile);
    call_peer.codec2_mode = call_profile::runtimeCodec2Mode(
        session.call_wire_profile,
        session.lxst_call.profile);
    ::platform::ui::reticulum_call::update_peer(call_peer);
}

bool LxmfAdapter::beginIncomingCallRuntime(LinkSession& session,
                                           const PeerInfo& peer)
{
    if (session.destination != LocalDestinationKind::CallAudio ||
        session.initiator ||
        !session.remote_identity_known)
    {
        return false;
    }
    ::platform::ui::reticulum_call::Peer call_peer{};
    copyHash(call_peer.link_id, session.link_id, sizeof(call_peer.link_id));
    copyHash(call_peer.destination_hash,
             peer.destination_hash,
             sizeof(call_peer.destination_hash));
    copyHash(call_peer.identity_hash,
             peer.identity_hash,
             sizeof(call_peer.identity_hash));
    call_peer.display_name =
        peer.display_name[0] != '\0' ? peer.display_name : nullptr;
    call_peer.incoming = true;
    call_peer.wire_profile =
        call_profile::runtimeWireProfile(session.call_wire_profile);
    call_peer.codec2_mode = call_profile::runtimeCodec2Mode(
        session.call_wire_profile,
        session.lxst_call.profile);
    session.call_runtime_started =
        ::platform::ui::reticulum_call::begin_incoming(call_peer);
    if (session.call_runtime_started && session.state == LinkState::Active)
    {
        ::platform::ui::reticulum_call::mark_link_active(session.link_id);
    }

    char link_hash[12] = {};
    formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
    Serial.printf("[LXMF][CallRX] identified link=%s wire=%u runtime=%u peer=%s\n",
                  link_hash,
                  static_cast<unsigned>(session.call_wire_profile),
                  session.call_runtime_started ? 1U : 0U,
                  call_peer.display_name ? call_peer.display_name : "<unknown>");
    return session.call_runtime_started;
}

bool LxmfAdapter::sendLxstSignal(LinkSession& session,
                                 uint16_t signal,
                                 bool call_admission_control)
{
    if (session.destination != LocalDestinationKind::CallAudio ||
        session.call_wire_profile != ReticulumCallWireProfile::SidebandLxst)
    {
        return false;
    }

    size_t payload_len = sizeof(call_wire_scratch_);
    if (!reticulum::lxst::encodeSignalling(signal,
                                           call_wire_scratch_,
                                           &payload_len))
    {
        return false;
    }
    const bool sent = sendLinkPacket(session,
                                     reticulum::PacketType::Data,
                                     reticulum::PacketContext::None,
                                     call_wire_scratch_,
                                     payload_len,
                                     true,
                                     call_admission_control);

    char link_hash[12] = {};
    formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
    Serial.printf("[LXMF][CallTX] lxst_signal link=%s signal=%u ok=%u\n",
                  link_hash,
                  static_cast<unsigned>(signal),
                  sent ? 1U : 0U);
    return sent;
}

bool LxmfAdapter::dispatchLxstCallEvent(
    LinkSession& session,
    const reticulum::lxst::call::Event& event)
{
    if (session.destination != LocalDestinationKind::CallAudio ||
        session.call_wire_profile != ReticulumCallWireProfile::SidebandLxst)
    {
        return false;
    }

    const auto previous_phase = session.lxst_call.phase;
    const auto transition = reticulum::lxst::call::dispatch(
        &session.lxst_call,
        event,
        millis());
    if (!transition.accepted)
    {
        return false;
    }

    char link_hash[12] = {};
    formatHashPrefix(session.link_id, link_hash, sizeof(link_hash));
    Serial.printf("[LXMF][Call] transition link=%s event=%u phase=%s->%s actions=%u\n",
                  link_hash,
                  static_cast<unsigned>(event.type),
                  reticulum::lxst::call::phaseName(previous_phase),
                  reticulum::lxst::call::phaseName(session.lxst_call.phase),
                  static_cast<unsigned>(transition.action_count));

    bool closes_link = false;
    for (size_t index = 0; index < transition.action_count; ++index)
    {
        if (transition.actions[index] ==
            reticulum::lxst::call::Action::CloseLink)
        {
            closes_link = true;
            break;
        }
    }

    for (size_t index = 0; index < transition.action_count; ++index)
    {
        const auto action = transition.actions[index];
        bool action_ok = true;
        switch (action)
        {
        case reticulum::lxst::call::Action::SendAvailable:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kStatusAvailable,
                true);
            break;
        case reticulum::lxst::call::Action::SendIdentify:
            action_ok = sendLinkIdentify(session);
            break;
        case reticulum::lxst::call::Action::BeginRinging:
        {
            const PeerInfo* peer =
                findPeerByIdentityHash(session.remote_identity_hash);
            const bool admitted =
                peer && beginIncomingCallRuntime(session, *peer);
            return dispatchLxstCallEvent(
                session,
                {admitted
                     ? reticulum::lxst::call::EventType::AdmissionGranted
                     : reticulum::lxst::call::EventType::AdmissionDenied});
        }
        case reticulum::lxst::call::Action::SendRinging:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kStatusRinging,
                true);
            break;
        case reticulum::lxst::call::Action::SendPreferredProfile:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kPreferredProfile +
                    session.lxst_call.profile,
                true);
            break;
        case reticulum::lxst::call::Action::SendConnecting:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kStatusConnecting,
                true);
            break;
        case reticulum::lxst::call::Action::PrepareMedia:
        {
            const bool media_ready =
                ::platform::ui::reticulum_call::prepare_media(
                    session.link_id);
            return dispatchLxstCallEvent(
                session,
                {media_ready
                     ? reticulum::lxst::call::EventType::MediaReady
                     : reticulum::lxst::call::EventType::OperationFailed});
        }
        case reticulum::lxst::call::Action::SendEstablished:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kStatusEstablished,
                true);
            break;
        case reticulum::lxst::call::Action::ActivateMedia:
            action_ok =
                ::platform::ui::reticulum_call::mark_call_active(
                    session.link_id);
            break;
        case reticulum::lxst::call::Action::SendBusy:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kStatusBusy,
                true);
            break;
        case reticulum::lxst::call::Action::SendRejected:
            action_ok = sendLxstSignal(
                session,
                reticulum::lxst::kStatusRejected,
                true);
            break;
        case reticulum::lxst::call::Action::CloseLink:
            (void)sendLinkPacket(session,
                                 reticulum::PacketType::Data,
                                 reticulum::PacketContext::LinkClose,
                                 session.link_id,
                                 sizeof(session.link_id),
                                 true,
                                 true);
            closeLinkSession(session, LinkCloseReason::LocalClose);
            return true;
        }

        if (!action_ok && !closes_link)
        {
            (void)dispatchLxstCallEvent(
                session,
                {reticulum::lxst::call::EventType::OperationFailed});
            return false;
        }
    }
    return true;
}

bool LxmfAdapter::handleLxstPacket(LinkSession& session,
                                   const uint8_t* payload,
                                   size_t payload_len)
{
    reticulum::lxst::DecodedPacket decoded{};
    if (!reticulum::lxst::decodePacket(payload, payload_len, &decoded))
    {
        return false;
    }

    bool handled = false;
    for (size_t index = 0; index < decoded.signal_count; ++index)
    {
        const uint16_t signal = decoded.signals[index];
        handled = true;
        (void)dispatchLxstCallEvent(
            session,
            reticulum::lxst::call::Event::remoteSignal(signal));
        if (signal >= reticulum::lxst::kPreferredProfile)
        {
            updateCallRuntimePeer(
                session,
                findPeerByIdentityHash(session.remote_identity_hash));
        }
    }

    const reticulum::audio_call::Codec2Mode expected_mode =
        call_profile::audioCodec2Mode(session.lxst_call.profile);
    for (size_t index = 0; index < decoded.frame_count; ++index)
    {
        const auto& frame = decoded.frames[index];
        if (session.lxst_call.phase !=
                reticulum::lxst::call::Phase::Active ||
            frame.codec != reticulum::lxst::kCodec2 ||
            !frame.codec2_mode_valid ||
            frame.codec2_mode != expected_mode ||
            !frame.encoded || frame.encoded_len == 0)
        {
            continue;
        }

        size_t normalized_len = sizeof(call_wire_scratch_);
        if (reticulum::audio_call::encodePayload(frame.codec2_mode,
                                                 frame.encoded,
                                                 frame.encoded_len,
                                                 call_wire_scratch_,
                                                 &normalized_len) &&
            ::platform::ui::reticulum_call::enqueue_inbound_audio(
                session.link_id,
                call_wire_scratch_,
                normalized_len))
        {
            handled = true;
        }
    }
    return handled;
}

bool LxmfAdapter::sendCallAudioPacket(LinkSession& session,
                                      const uint8_t* payload,
                                      size_t payload_len)
{
    if (session.call_wire_profile ==
        ReticulumCallWireProfile::MeshChatCallAudio)
    {
        return sendLinkPacket(session,
                              reticulum::PacketType::Data,
                              reticulum::PacketContext::None,
                              payload,
                              payload_len,
                              true);
    }

    reticulum::audio_call::DecodedPayload decoded{};
    if (!reticulum::audio_call::decodePayload(payload,
                                              payload_len,
                                              &decoded) ||
        decoded.mode !=
            call_profile::audioCodec2Mode(session.lxst_call.profile))
    {
        return false;
    }

    size_t lxst_len = sizeof(call_wire_scratch_);
    if (!reticulum::lxst::encodeCodec2Frames(decoded.mode,
                                             decoded.encoded,
                                             decoded.encoded_len,
                                             call_wire_scratch_,
                                             &lxst_len))
    {
        return false;
    }
    return sendLinkPacket(session,
                          reticulum::PacketType::Data,
                          reticulum::PacketContext::None,
                          call_wire_scratch_,
                          lxst_len,
                          true);
}

void LxmfAdapter::pumpReticulumAudioCall()
{
    ::platform::ui::reticulum_call::set_wifi_ready(
        interfaces_.hasReadyWifiGateway());

    uint8_t hangup_link_id[reticulum::kTruncatedHashSize] = {};
    if (::platform::ui::reticulum_call::consume_hangup_request(hangup_link_id))
    {
        if (LinkSession* session = findLinkSession(hangup_link_id))
        {
            if (session->destination == LocalDestinationKind::CallAudio)
            {
                if (session->call_wire_profile ==
                    ReticulumCallWireProfile::SidebandLxst)
                {
                    (void)dispatchLxstCallEvent(
                        *session,
                        {reticulum::lxst::call::EventType::LocalHangup});
                }
                else
                {
                    (void)sendLinkPacket(
                        *session,
                        reticulum::PacketType::Data,
                        reticulum::PacketContext::LinkClose,
                        session->link_id,
                        sizeof(session->link_id),
                        true,
                        true);
                    closeLinkSession(*session,
                                     LinkCloseReason::LocalClose);
                }
            }
            else
            {
                ::platform::ui::reticulum_call::notify_link_closed(
                    hangup_link_id);
            }
        }
        else
        {
            ::platform::ui::reticulum_call::notify_link_closed(hangup_link_id);
        }
    }

    auto call_snapshot = ::platform::ui::reticulum_call::snapshot();
    LinkSession* call_session = findLinkSession(call_snapshot.link_id);
    if (call_session &&
        call_session->destination == LocalDestinationKind::CallAudio &&
        call_session->state == LinkState::Active)
    {
        ::platform::ui::reticulum_call::mark_link_active(
            call_session->link_id);
        call_snapshot = ::platform::ui::reticulum_call::snapshot();

        if (call_snapshot.realtime_phase !=
            ::platform::ui::reticulum_call::RealtimePhase::ClosingCall)
        {
            if (call_session->call_wire_profile ==
                    ReticulumCallWireProfile::MeshChatCallAudio &&
                call_snapshot.accepted &&
                call_snapshot.realtime_phase ==
                    ::platform::ui::reticulum_call::RealtimePhase::
                        AcceptedStarting)
            {
                if (::platform::ui::reticulum_call::prepare_media(
                        call_session->link_id))
                {
                    (void)::platform::ui::reticulum_call::mark_call_active(
                        call_session->link_id);
                }
            }
            else if (call_session->call_wire_profile ==
                         ReticulumCallWireProfile::SidebandLxst &&
                     !call_session->initiator &&
                     call_snapshot.accepted &&
                     call_session->lxst_call.phase ==
                         reticulum::lxst::call::Phase::CalleeRinging)
            {
                (void)dispatchLxstCallEvent(
                    *call_session,
                    {reticulum::lxst::call::EventType::LocalAccepted});
            }
        }
    }

    call_snapshot = ::platform::ui::reticulum_call::snapshot();
    if (call_snapshot.realtime_phase !=
        ::platform::ui::reticulum_call::RealtimePhase::ActiveCall)
    {
        return;
    }

    uint8_t sent_packets = 0;
    ::platform::ui::reticulum_call::AudioPacket audio{};
    while (sent_packets < 2 &&
           ::platform::ui::reticulum_call::dequeue_outbound_audio(&audio))
    {
        LinkSession* session = findLinkSession(audio.link_id);
        if (!session ||
            session->destination != LocalDestinationKind::CallAudio ||
            session->state != LinkState::Active)
        {
            continue;
        }

        if (sendCallAudioPacket(*session, audio.data, audio.len))
        {
            ::platform::ui::reticulum_call::note_tx_sent();
            ++sent_packets;
        }
    }
}

} // namespace chat::lxmf
