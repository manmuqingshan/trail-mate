# Use Case: Install, update or uninstall extension package

Status: **confirmed behavior / model classification pending**
Business boundary: device maintenance and data ownership

## User goal

Browse available and installed language, font, IME and other extensions, install, update or uninstall after confirming they are compatible with the current firmware/memory profile, and you can still see the local installed index when offline.

## Main scene

1. Extensions first loads the local `InstalledPackageRecord`, then grabs the remote catalog and merges the status when there is a Wi-Fi lease.
2. The user opens the details; the package that is not compatible with firmware/memory disables the installation action and explains the reason.
3. The installation worker downloads the archive and continuously reports Installing/progress.
4. Download and verify SHA-256, parse and limit the ZIP path, and decompress it to the target storage.
5. Atomicly update the installed index after the payload is visible; retain the previous record during the update, and then remove the old payload after success.
6. Uninstall delete the controlled payload and update the index; if it fails, it cannot disappear from the UI first.

#

Network interruption, hash mismatch, ZIP out of bounds, insufficient storage space, and index saving failure will enter Failed respectively; temporary files/half-decompressed content must be cleaned or kept invisible. There can only be one active install for a background worker.

Source code: `modules/core_sys/include/platform/ui/pack_repository_runtime.h`, `platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp`, `modules/ui_shared/src/ui/screens/extensions/extensions_page_runtime.cpp`.

## Drill down

- [Activity](manage-extension-packages/activity.md)
- [Sequence](manage-extension-packages/sequences/sequence-manage-extension-packages.md)
- [State Machine](manage-extension-packages/state-machines/package-install.md)
