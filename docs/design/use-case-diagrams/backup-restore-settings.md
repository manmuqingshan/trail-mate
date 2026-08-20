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
   it. A valid SD document loads before NVS; its values then mirror to NVS. If no valid
   document exists, current NVS values are imported once into a canonical document.
3. Every NVS-backed settings owner emits a coalesced change notification. A tiny
   write-ahead marker is committed immediately; the foreground lifecycle atomically
   rewrites the SD document. Therefore power loss before the SD write makes boot retain
   newer NVS instead of resurrecting an older SD projection.
4. JSON is not used and no whole-document object is constructed. Parsing uses one
   bounded line, while the only multi-record staging area is a static/PSRAM Wi-Fi
   profile set needed to validate all ten profiles before replacement.
5. Reset Mesh, Reset Nodes, Clear Messages, and Factory Reset remain distinct confirmed
   actions. Factory Reset first removes `config.tms` and its commit metadata, then clears
   NVS. It never restarts into an old SD configuration.

There is no Settings Backup/Restore operation. It duplicated configuration schema and
was intended to protect NVS from cross-firmware writes; the complete SD working file is
already the durable authority for that failure model. A second file on the same SD card
would not protect against loss or corruption of that card.

## Drill down

- [Activity](backup-restore-settings/activity.md)
- [Sequence](backup-restore-settings/sequences/sequence-backup-restore-settings.md)
