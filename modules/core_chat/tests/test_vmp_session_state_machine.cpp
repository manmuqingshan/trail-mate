#include "chat/infra/voice/vmp_session_state_machine.h"

#include <cassert>

namespace
{

using namespace chat::voice::vmp;

void testPrivateSenderRequiresTwoGhzReadyBeforeVoice()
{
    SessionStateMachine session;
    SessionTransition start = session.startSender(DeliveryMode::Private);
    assert(start.accepted);
    assert(start.current == SessionState::AwaitingSubGhzAccept);
    assert(start.hasAction(SessionAction::SendSubGhzOffer));

    SessionTransition accepted = session.dispatch(SessionEvent::SubGhzAcceptAuthenticated);
    assert(accepted.accepted);
    assert(accepted.current == SessionState::Awaiting2GhzReady);
    assert(accepted.hasAction(SessionAction::SwitchTo2GhzTx));
    assert(accepted.hasAction(SessionAction::Send2GhzReadyProbeTrain));
    assert(!accepted.hasAction(SessionAction::BeginVoiceMediaTx));

    SessionTransition media = session.dispatch(SessionEvent::TwoGhzReadyAuthenticated);
    assert(media.accepted);
    assert(media.current == SessionState::SendingVoiceMedia);
    assert(media.hasAction(SessionAction::BeginVoiceMediaTx));

    SessionTransition completed = session.dispatch(SessionEvent::VoiceDataTrainComplete);
    assert(completed.accepted);
    assert(completed.current == SessionState::Completed);
    assert(completed.hasAction(SessionAction::RestoreSubGhzRx));
}

void testPrivateReceiverSendsOnlyReadinessControl()
{
    SessionStateMachine session;
    SessionTransition start = session.startReceiver(DeliveryMode::Private);
    assert(start.accepted);
    assert(start.hasAction(SessionAction::SendSubGhzAccept));
    assert(start.hasAction(SessionAction::SwitchTo2GhzRx));

    SessionTransition probe = session.dispatch(SessionEvent::TwoGhzReadyProbeAuthenticated);
    assert(probe.accepted);
    assert(probe.current == SessionState::AwaitingVoiceMedia);
    assert(probe.hasAction(SessionAction::Send2GhzReady));
    assert(!probe.hasAction(SessionAction::BeginVoiceMediaTx));

    SessionTransition voice = session.dispatch(SessionEvent::TwoGhzVoiceShardAuthenticated);
    assert(voice.accepted);
    assert(voice.current == SessionState::ReceivingVoiceMedia);

    SessionTransition end = session.dispatch(SessionEvent::FecBlockRecovered);
    assert(end.accepted);
    assert(end.hasAction(SessionAction::CommitCompleteIncomingVoice));
    assert(end.hasAction(SessionAction::RestoreSubGhzRx));
}

void testBroadcastNeverSendsReadinessResponse()
{
    SessionStateMachine receiver;
    SessionTransition start = receiver.startReceiver(DeliveryMode::Broadcast);
    assert(start.accepted);
    assert(!start.hasAction(SessionAction::SendSubGhzAccept));
    assert(start.hasAction(SessionAction::SwitchTo2GhzRx));

    SessionTransition probe = receiver.dispatch(SessionEvent::TwoGhzReadyProbeAuthenticated);
    assert(probe.accepted);
    assert(probe.current == SessionState::AwaitingVoiceMedia);
    assert(!probe.hasAction(SessionAction::Send2GhzReady));

    SessionStateMachine sender;
    SessionTransition announce = sender.startSender(DeliveryMode::Broadcast);
    assert(announce.hasAction(SessionAction::SendSubGhzAnnounce));
    SessionTransition media = sender.dispatch(SessionEvent::BroadcastDataWindowReady);
    assert(media.accepted);
    assert(media.hasAction(SessionAction::SwitchTo2GhzTx));
    assert(media.hasAction(SessionAction::Send2GhzReadyProbeTrain));
    assert(media.hasAction(SessionAction::BeginVoiceMediaTx));
}

void testTimeoutRestoresSubGhzWithoutAnyVoiceSend()
{
    SessionStateMachine sender;
    (void)sender.startSender(DeliveryMode::Private);
    SessionTransition timeout = sender.dispatch(SessionEvent::ControlDeadlineExpired);
    assert(timeout.accepted);
    assert(timeout.current == SessionState::Failed);
    assert(timeout.failure == SessionFailure::AcceptTimeout);
    assert(timeout.hasAction(SessionAction::RestoreSubGhzRx));
    assert(!timeout.hasAction(SessionAction::BeginVoiceMediaTx));

    SessionStateMachine receiver;
    (void)receiver.startReceiver(DeliveryMode::Private);
    SessionTransition no_voice = receiver.dispatch(SessionEvent::MediaDeadlineExpired);
    assert(no_voice.accepted);
    assert(no_voice.failure == SessionFailure::NoVoiceMedia);
    assert(no_voice.hasAction(SessionAction::RestoreSubGhzRx));
}

} // namespace

int main()
{
    testPrivateSenderRequiresTwoGhzReadyBeforeVoice();
    testPrivateReceiverSendsOnlyReadinessControl();
    testBroadcastNeverSendsReadinessResponse();
    testTimeoutRestoresSubGhzWithoutAnyVoiceSend();
    return 0;
}
