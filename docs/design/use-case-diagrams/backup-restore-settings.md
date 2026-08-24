# Use Case: Maintain or reset the SD working configuration

Status: **implemented design**
Business boundary: device configuration ownership and reset semantics

## User Goals

Edit a complete device configuration on a computer when on-device text entry is
impractical, especially for channel keys, MQTT credentials, Wi-Fi passwords, and
cellular credentials. A normal restart must load that configuration even when NVS was
erased or changed by another firmware. Reset operations must delete only their stated
data and cannot be undone by an old SD document on the next boot.

## Behavior and Rules

1. `/trailmate/config.tms` is the only working configuration document. It contains the
   `AppConfig` projection plus every supported independent setting owner, including the
   ordered ten-profile Wi-Fi set and, on supported hardware, A7682E configuration.
2. The firmware validates the full document with bounded line storage before applying
   it. A valid SD document loads before NVS and then refreshes NVS as a compatibility
   cache. NVS is imported only when the card or the document is absent. A present but
   invalid document is preserved for repair and cannot be silently overridden by NVS.
3. Every supported NVS-backed settings owner crosses one synchronous durable commit
   boundary. Multi-key Wi-Fi and cellular changes are coalesced only until their scope
   exits; no foreground loop, deferred retry, or NVS write-ahead marker exists. The
   current call writes a complete SD document through `.new`, `.txn`, and `.bak`
   recovery files.
4. JSON is not used and no whole-document object is constructed. Parsing uses one
   bounded line, while the only multi-record staging area is a static/PSRAM Wi-Fi
   profile set needed to validate all ten profiles before replacement.
5. Reset Mesh, Reset Nodes, Clear Messages, and Factory Reset remain distinct confirmed
   actions. Factory Reset removes `config.tms` and its recovery files before clearing
   NVS. If the card is absent, a tiny reset tombstone removes an old document before it
   can become authoritative when that card returns.

The `.bak` file is one previous committed generation used only to recover an interrupted
replacement. It is never read while a valid primary exists and it is not a second working
configuration authority. A user who needs an archival backup copies `config.tms` from
the SD card before editing it.

## Drill down

- [Activity](backup-restore-settings/activity.md)
- [Sequence](backup-restore-settings/sequences/sequence-backup-restore-settings.md)
