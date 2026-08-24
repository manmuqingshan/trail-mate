# LinkState and handshake life cycle

```mermaid
stateDiagram-v2
  [*] --> Stopped
  Stopped --> Waiting: reset_session
  Waiting --> Connected: transport reports connection
  Connected --> Handshaking: mark_handshake_started
  Handshaking --> Ready: mark_handshake_complete
  Handshaking --> Error: handshake_expired / note_error
  Ready --> Waiting: mark_disconnected
  Error --> Waiting: reset/retry
  Waiting --> Stopped: stop_session
```

Observed status is only `Stopped / Waiting / Connected / Handshaking / Ready / Error`. Offline, Recovering, and Incompatible in the original image are not in `LinkState` and have been removed.
