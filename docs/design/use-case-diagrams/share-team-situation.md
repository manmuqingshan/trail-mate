# Use Case: Share team position, waypoint, trajectory and chat

Status: **confirmed behavior**
Business boundary: team collaboration

## User goal

On the premise that valid Team keys are already held, the position, waypoint, trajectory segment or chat are sent to members as Team business messages, and the corresponding map/chat projection is updated on the receiving end.

## Main scene

1. The user selects share position/waypoint/track/chat, or TeamTrackSampler reaches the sampling time.
2. TeamService verifies keys, payload type, target/channel, and whether to request a business response.
3. Team codec encodes the independent payload, Team crypto authenticates and encrypts it and then delivers it to the active mesh transport.
4. The receiving end first performs Team envelope/key verification, and then dispatches events according to Position, Waypoint, Track, Chat, and Status.
5. EventBus/UI reducer updates map or chat; delivery ACK remains separate from Team `want_response`.

Failure: no keys, encryption failed, transport unavailable, payload invalid does not generate "shared"; failure to receive verification does not update the map/chat.

Source code: `modules/core_team/src/usecase/team_service.cpp`, `modules/core_team/src/usecase/team_track_sampler.cpp`, `apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp`.

## Drill down

- [Activity](share-team-situation/activity.md)
- [Sequence](share-team-situation/sequences/sequence-share-team-situation.md)
