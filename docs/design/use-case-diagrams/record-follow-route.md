# Use Case: Record and reliably save on-site trajectories

Status: **confirmed**
Business boundary: map, positioning and scene awareness

## User goal

Start a trajectory record, maintain explainable behavior when positioning is temporarily lost, storage is temporarily busy, or device resources are limited, and a completely closed local trajectory file is obtained after stopping.

## Main scene

1. User Start; `TrackStateMachine` verifies that it is currently Idle and creates track/session and storage writer.
2. LocationService provides valid fix revisions filtered by transitions.
3. The sampling strategy determines whether to form a `TrackPoint`; the point enters the fixed capacity buffer and does not directly block writing to the disk in the GNSS hot path.
4. The storage worker writes formatted tracks in batches and updates the count, distance and last written result.
5. Stop stops accepting new points, empties the buffer, flush/close, and finally submits the Idle/Stopped result.

#

- No fix: Form a sampling discontinuity and do not forge a two-point straight line.
- buffer full: Execute explicit drop/backpressure strategy and expose count.
- Writing failure: Enter Error/Stop to continue unbounded accumulation; written data remains recoverable.
- The track owner must be stopped/flushed before USB takes over the SD.

Source code: `modules/core_gps/include/gps/domain/track_state_machine.h`, `modules/core_gps/src/usecase/track_recorder.cpp`, `modules/core_gps/src/usecase/track_storage_worker.cpp`.

## Drill down

- [Activity](record-follow-route/activity.md)
- [Sequence](record-follow-route/sequences/sequence-record-follow-route.md)
- [State Machine](record-follow-route/state-machines/track-recording-session.md)
