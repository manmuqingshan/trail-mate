# Use Case: Initiate or respond to a Reticulum real-time call

Status: **confirmed behavior / model classification pending**

Business Boundary: Communication, Media and Delivery
Participants: caller, called party, LXST Call Session, Realtime Resource Hooks, Audio Engine

## User goal

Establish a low-latency voice call from the Reticulum peer, see the real stages of identification, ringing, answering, activity, end or failure, and ensure that audio, Wi-Fi, radio and dormant resources do not destroy each other during the call.

## Main scene

1. The caller selects a resolvable peer and creates an outgoing call; or the adapter uses link/destination/identity facts to create incoming identifying.
2. incoming After completing the identity resolution, enter the ringing soft-preempt; the user can accept or reject it.
3. `accept()` only obtains exclusive resources when the current link is consistent and the phase is legal; link active and user accept can arrive out of order.
4. After the media engine starts, it enters `Active/ActiveCall`, and audio packets flow through a fixed-size queue.
5. Hangup, remote close, link loss or media failure enter closing and release exclusive, wake/sleep lease and media resources.

## Failure and recovery

- Unrecognized/unmatched link MUST NOT operate on the current call.
- If the media is not supported, exclusive acquisition fails, or audio start fails, it enters Failed and notifies the remote end to close.
- Closure must be idempotent; late packets cannot restore the ended session.
- Call exclusive takes precedence over background HTTP/long connections; resource management returns to normal after it ends.

## Source code evidence

- `modules/core_sys/include/platform/ui/reticulum_call_runtime.h`
- `modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp`
- `platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter_call.cpp`
- `platform/esp/common/src/reticulum_call_audio_engine.cpp`

## Drill down

- [Activity: Convergence of incoming and outgoing calls](realtime-audio-call/activity.md)
- [Sequence: Answering and resource exclusive](realtime-audio-call/sequences/sequence-realtime-audio-call.md)
- [State Machine: Call State and RealtimePhase](realtime-audio-call/state-machines/call-session.md)
