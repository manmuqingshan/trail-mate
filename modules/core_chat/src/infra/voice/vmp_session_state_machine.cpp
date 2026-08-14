/**
 * @file vmp_session_state_machine.cpp
 * @brief Side-effect-free VMP v1 private/broadcast radio session state machine.
 */

#include "chat/infra/voice/vmp_session_state_machine.h"

namespace chat::voice::vmp
{

bool SessionTransition::hasAction(SessionAction action) const
{
    for (std::size_t index = 0; index < action_count; ++index)
    {
        if (actions[index] == action)
        {
            return true;
        }
    }
    return false;
}

void SessionStateMachine::addAction(SessionTransition& transition,
                                    SessionAction action)
{
    if (transition.action_count < SessionTransition::kMaxActions)
    {
        transition.actions[transition.action_count++] = action;
    }
}

SessionTransition SessionStateMachine::transition(SessionState next,
                                                  SessionFailure failure)
{
    SessionTransition result{};
    result.accepted = true;
    result.previous = state_;
    state_ = next;
    result.current = state_;
    result.failure = failure;
    return result;
}

SessionTransition SessionStateMachine::startSender(DeliveryMode mode)
{
    if (state_ != SessionState::Idle)
    {
        return SessionTransition{false, state_, state_, SessionFailure::UnexpectedEvent};
    }

    role_ = SessionRole::Sender;
    mode_ = mode;
    if (mode == DeliveryMode::Private)
    {
        SessionTransition result = transition(SessionState::AwaitingSubGhzAccept);
        addAction(result, SessionAction::SendSubGhzOffer);
        return result;
    }

    SessionTransition result = transition(SessionState::AwaitingBroadcastDataWindow);
    addAction(result, SessionAction::SendSubGhzAnnounce);
    return result;
}

SessionTransition SessionStateMachine::startReceiver(DeliveryMode mode)
{
    if (state_ != SessionState::Idle)
    {
        return SessionTransition{false, state_, state_, SessionFailure::UnexpectedEvent};
    }

    role_ = SessionRole::Receiver;
    mode_ = mode;
    SessionTransition result = transition(SessionState::Awaiting2GhzProbe);
    if (mode == DeliveryMode::Private)
    {
        addAction(result, SessionAction::SendSubGhzAccept);
    }
    addAction(result, SessionAction::SwitchTo2GhzRx);
    return result;
}

SessionTransition SessionStateMachine::dispatch(SessionEvent event)
{
    if (!active())
    {
        return SessionTransition{false, state_, state_, SessionFailure::UnexpectedEvent};
    }

    if (event == SessionEvent::RadioFailure)
    {
        SessionTransition result = transition(SessionState::Failed, SessionFailure::Radio);
        addAction(result, SessionAction::RestoreSubGhzRx);
        return result;
    }
    if (event == SessionEvent::LocalStorageFailure)
    {
        SessionTransition result = transition(SessionState::Failed, SessionFailure::Storage);
        addAction(result, SessionAction::RestoreSubGhzRx);
        return result;
    }

    if (role_ == SessionRole::Sender)
    {
        switch (state_)
        {
        case SessionState::AwaitingSubGhzAccept:
            if (event == SessionEvent::SubGhzAcceptAuthenticated)
            {
                SessionTransition result = transition(SessionState::Awaiting2GhzReady);
                addAction(result, SessionAction::SwitchTo2GhzTx);
                addAction(result, SessionAction::Send2GhzReadyProbeTrain);
                return result;
            }
            if (event == SessionEvent::ControlDeadlineExpired)
            {
                SessionTransition result = transition(SessionState::Failed,
                                                      SessionFailure::AcceptTimeout);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            break;

        case SessionState::Awaiting2GhzReady:
            if (event == SessionEvent::TwoGhzReadyAuthenticated)
            {
                SessionTransition result = transition(SessionState::SendingVoiceMedia);
                addAction(result, SessionAction::BeginVoiceMediaTx);
                return result;
            }
            if (event == SessionEvent::ReadyDeadlineExpired)
            {
                SessionTransition result = transition(SessionState::Failed,
                                                      SessionFailure::ReadyTimeout);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            break;

        case SessionState::AwaitingBroadcastDataWindow:
            if (event == SessionEvent::BroadcastDataWindowReady)
            {
                SessionTransition result = transition(SessionState::SendingVoiceMedia);
                addAction(result, SessionAction::SwitchTo2GhzTx);
                addAction(result, SessionAction::Send2GhzReadyProbeTrain);
                addAction(result, SessionAction::BeginVoiceMediaTx);
                return result;
            }
            break;

        case SessionState::SendingVoiceMedia:
            if (event == SessionEvent::VoiceDataTrainComplete)
            {
                SessionTransition result = transition(SessionState::Completed);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            break;

        default:
            break;
        }
    }
    else
    {
        switch (state_)
        {
        case SessionState::Awaiting2GhzProbe:
            if (event == SessionEvent::TwoGhzReadyProbeAuthenticated)
            {
                SessionTransition result = transition(SessionState::AwaitingVoiceMedia);
                if (mode_ == DeliveryMode::Private)
                {
                    addAction(result, SessionAction::Send2GhzReady);
                }
                return result;
            }
            if (event == SessionEvent::TwoGhzVoiceShardAuthenticated &&
                mode_ == DeliveryMode::Broadcast)
            {
                return transition(SessionState::ReceivingVoiceMedia);
            }
            if (event == SessionEvent::MediaDeadlineExpired)
            {
                SessionTransition result = transition(SessionState::Failed,
                                                      SessionFailure::NoVoiceMedia);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            break;

        case SessionState::AwaitingVoiceMedia:
            if (event == SessionEvent::TwoGhzVoiceShardAuthenticated)
            {
                return transition(SessionState::ReceivingVoiceMedia);
            }
            if (event == SessionEvent::MediaDeadlineExpired)
            {
                SessionTransition result = transition(SessionState::Failed,
                                                      SessionFailure::NoVoiceMedia);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            break;

        case SessionState::ReceivingVoiceMedia:
            if (event == SessionEvent::FecBlockRecovered)
            {
                SessionTransition result = transition(SessionState::Completed);
                addAction(result, SessionAction::CommitCompleteIncomingVoice);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            if (event == SessionEvent::MediaDeadlineExpiredWithRecoverableData)
            {
                SessionTransition result = transition(SessionState::Completed);
                addAction(result, SessionAction::CommitPartialIncomingVoice);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            if (event == SessionEvent::MediaDeadlineExpired)
            {
                SessionTransition result = transition(SessionState::Failed,
                                                      SessionFailure::NoVoiceMedia);
                addAction(result, SessionAction::RestoreSubGhzRx);
                return result;
            }
            break;

        default:
            break;
        }
    }

    return SessionTransition{false, state_, state_, SessionFailure::UnexpectedEvent};
}

bool SessionStateMachine::active() const
{
    return state_ != SessionState::Idle && state_ != SessionState::Completed &&
           state_ != SessionState::Failed;
}

} // namespace chat::voice::vmp
