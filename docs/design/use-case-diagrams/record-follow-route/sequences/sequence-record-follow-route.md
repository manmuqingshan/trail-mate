# Sequence: Location to Track Store
```mermaid
sequenceDiagram
 actor U as user
  participant UI as Tracker UI
  participant SM as TrackStateMachine
  participant Location as LocationService
  participant Recorder as TrackRecorder
  participant Worker as TrackStorageWorker
  participant Store as Track Store
  U->>UI: Start
  UI->>SM: Start command
  SM->>Store: open track
  loop each location revision
    Location->>Recorder: valid fix
    Recorder->>Recorder: sampling policy + fixed buffer
    Worker->>Recorder: drain batch
    Worker->>Store: append batch
  end
  U->>UI: Stop
  UI->>SM: Stop command
  SM->>Worker: drain
  Worker->>Store: flush + close
```

#

TrackStateMachine owns the session state, LocationService publishes revision, Recorder performs sampling and fixed buffering, Worker is the only storage writer, and Store owns file format and durable close.

## Startup sequence

Start will only enter Recording after the Store open is successful. The UI submits the result according to the state machine and shows that it is being recorded. You cannot draw a red dot first and then wait for the file to be created. Same as Start in the active session is rejected or returns idempotently to the existing session.

## Data handover

Location callback only completes verification, sampling determination and bounded enqueue, and does not perform SD I/O. Worker drains in batches, and advances the durable point count after success; partial writes must return the committed range, and the entire batch cannot be appended repeatedly.

## Stop barrier

Stop first closes the Recorder's queue gate, then waits for the Worker to drain, and finally flush + close. The Close completion event causes the state machine to enter Completed. A final path is shared once when write failure competes with Stop.

## Testing

 Covers open failure, repeated Start, sampling drop, partial batch writing, Stop fence, close failure, incomplete state after power-off and fixed buffer ownership.
