# Sequence: SD working configuration
```mermaid
sequenceDiagram
  participant SD as SD Filesystem
  participant TMS as TMS Runtime
  participant Config as Config Stores
  participant Loop as Foreground lifecycle
  SD->>TMS: config.tms at boot
  TMS->>TMS: stream validate complete document
  TMS->>Config: apply only after validation
  TMS->>SD: atomically write canonical config.tms
  Config->>TMS: setting changed notification
  TMS->>Config: commit small write-ahead marker
  Loop->>TMS: service pending mirror
  TMS->>SD: atomically replace config.tms
```

## Scenarios and responsibilities

The TMS runtime defines one versioned grammar and atomic file replacement. Individual
configuration owners continue to own their runtime application, but cannot drift from
the SD projection because their durable write notifications are coalesced through the
same mirror service.

## Boot and save order

Boot validates before any application. On a normal save, NVS commits first, then a
small metadata record makes an interrupted SD projection safely stale. The foreground
lifecycle performs the SD I/O, so Wi-Fi profile retry timers never access the card.

## Consistency and sensitive data

The document includes all supported sensitive configuration because it is the working
authority. It is plaintext and must never be logged or shared unredacted.

## Tests

Cover temp write/close/replace failure, schema v2-to-v3 migration, unknown fields,
cross-owner validation, all ten Wi-Fi profiles, and factory-reset SD removal.
