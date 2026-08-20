# GNSS revision processing chain

```mermaid
sequenceDiagram
  participant Driver as GNSS byte source
  participant Service as LocationService
  participant Parser as NmeaParser
  participant Filter as GpsJitterFilter
  participant Events as ILocationEventSink
  participant Time as ITimeAuthorityUpdater
  Driver->>Service: onGnssBytes(bytes, len, now_ms, last_motion_ms)
  Service->>Parser: parse input / observe revisions
  alt fix revision changed
    Service->>Filter: evaluate LocationFix
    Service->>Events: publish accepted fix event
    Service->>Service: update latest_fix_ and last_fix_revision_
  end
  alt time revision changed
    Service->>Time: update time authority
    Service->>Service: update last_time_revision_
  end
```

This is not a `NoFix / Valid / Stale` state machine; the current model uses revision, filter and output ports as facts.
