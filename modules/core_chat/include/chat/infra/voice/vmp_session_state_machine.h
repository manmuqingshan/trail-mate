/**
 * @file vmp_session_state_machine.h
 * @brief Side-effect-free VMP v1 private/broadcast radio session state machine.
 *
 * Callers dispatch only already-authenticated control/data events.  The
 * returned actions are executed by the application/radio adapter; this core
 * never obtains an RF handle and therefore cannot accidentally forward a
 * received voice message.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

enum class SessionRole : uint8_t
{
    Sender = 1,
    Receiver = 2,
};

enum class SessionState : uint8_t
{
    Idle = 0,
    AwaitingSubGhzAccept,
    Awaiting2GhzReady,
    AwaitingBroadcastDataWindow,
    SendingVoiceMedia,
    Awaiting2GhzProbe,
    AwaitingVoiceMedia,
    ReceivingVoiceMedia,
    Completed,
    Failed,
};

enum class SessionEvent : uint8_t
{
    SubGhzAcceptAuthenticated = 1,
    BroadcastDataWindowReady = 2,
    TwoGhzReadyProbeAuthenticated = 3,
    TwoGhzReadyAuthenticated = 4,
    TwoGhzVoiceShardAuthenticated = 5,
    VoiceDataTrainComplete = 6,
    FecBlockRecovered = 7,
    ControlDeadlineExpired = 8,
    ReadyDeadlineExpired = 9,
    MediaDeadlineExpired = 10,
    MediaDeadlineExpiredWithRecoverableData = 11,
    RadioFailure = 12,
    LocalStorageFailure = 13,
};

enum class SessionAction : uint8_t
{
    SendSubGhzOffer = 1,
    SendSubGhzAnnounce = 2,
    SendSubGhzAccept = 3,
    SwitchTo2GhzTx = 4,
    SwitchTo2GhzRx = 5,
    Send2GhzReadyProbeTrain = 6,
    Send2GhzReady = 7,
    BeginVoiceMediaTx = 8,
    CommitCompleteIncomingVoice = 9,
    CommitPartialIncomingVoice = 10,
    RestoreSubGhzRx = 11,
};

enum class SessionFailure : uint8_t
{
    None = 0,
    UnexpectedEvent,
    AcceptTimeout,
    ReadyTimeout,
    NoVoiceMedia,
    Radio,
    Storage,
};

struct SessionTransition
{
    static constexpr std::size_t kMaxActions = 3;

    bool accepted = false;
    SessionState previous = SessionState::Idle;
    SessionState current = SessionState::Idle;
    SessionFailure failure = SessionFailure::None;
    SessionAction actions[kMaxActions] = {};
    std::size_t action_count = 0;

    bool hasAction(SessionAction action) const;
};

/**
 * @brief Models a single VMP session from one local device's point of view.
 */
class SessionStateMachine
{
  public:
    SessionTransition startSender(DeliveryMode mode);
    SessionTransition startReceiver(DeliveryMode mode);
    SessionTransition dispatch(SessionEvent event);

    SessionState state() const { return state_; }
    SessionRole role() const { return role_; }
    DeliveryMode mode() const { return mode_; }
    bool active() const;

  private:
    SessionState state_ = SessionState::Idle;
    SessionRole role_ = SessionRole::Sender;
    DeliveryMode mode_ = DeliveryMode::Private;

    SessionTransition transition(SessionState next,
                                 SessionFailure failure = SessionFailure::None);
    static void addAction(SessionTransition& transition, SessionAction action);
};

} // namespace chat::voice::vmp
