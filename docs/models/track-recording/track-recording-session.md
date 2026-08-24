# TrackCommand / TrackEvent / RecorderStatus

```mermaid
flowchart LR
  Runtime["TrackRuntime"] --> Command["TrackCommand"]
  Command --> Worker["TrackStorageWorker"]
  Worker --> Files["ITrackFileAdapter"]
  Worker --> Event["TrackEvent"]
  Event --> State["TrackStateMachine"]
  State -->|Started| Recording
  State -->|ResourceBusy| Recovering
  State -->|FlushFailed / Failed| Error
  State -->|Stopped| Stopped
  Buffer["TrackPointBuffer&lt;N&gt;"] --> Command
  Policy["TrackFlushPolicy"] --> Buffer
```

T
