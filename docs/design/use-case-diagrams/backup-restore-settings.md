# Use Case: Back up, restore or reset device settings

Status: **confirmed behavior; configuration model incomplete**
Business boundary: device maintenance and data ownership

## User Goals

Create a local backup before modifying complex protocol/GNSS/display/network settings; verify and restore when needed; only perform deletion of messages, nodes, mesh settings or factory reset after explicit confirmation.

## Behavior and Rules

1. Backup checks SD availability, aggregates all user settings owner: `AppConfig`, `settings_store` preferences and Reticulum group, and atomically replaces the target backup after writing the temporary file. Each supported key in `settings_store` carries an existing state; the default state is recorded when not explicitly written by the origin.
2. Restore first parses/verifies the backup; the Reticulum group must be handed over to its SD owner to complete the actual placement, and cannot only update the runtime image of `AppConfig`. Only after the SD is successfully written, the NVS preferences are written and `AppConfig` is submitted, so the failure of the SD will not change these two owners. After success, restart/reinitialize owner as needed.
3. Reset Mesh, Reset Nodes, Clear Messages and Factory Reset are different damage scopes and must be confirmed separately.
4. The current `AppConfig` lacks a unified schema version, cross-field validation, and an atomic ConfigurationService across NVS/SD owners; the document cannot promise a complete transaction beyond what the runtime actually supports. Set the backup format to currently be schema v2: Restoring a preference of `present: false` will clear the target NVS override and return to the default state; v1 can be imported and will not clear v2's new fields or its unlisted preferences.

Source code: `modules/core_sys/include/platform/ui/settings_backup_runtime.h`, `platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp`, Settings reset actions.

## Drill down

- [Activity](backup-restore-settings/activity.md)
- [Sequence](backup-restore-settings/sequences/sequence-backup-restore-settings.md)
