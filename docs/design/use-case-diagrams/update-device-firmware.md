# Use Case: Check and install device firmware updates

Status: **confirmed behavior / model classification pending**
Business boundary: device maintenance and data ownership

## User goal

Check if there are compatible updates for the current target, safely download, install and restart after explicit confirmation; device maintains existing bootable firmware when network, verification or writing fails.

## Main scene

1. Settings displays the current version, and the user triggers Check.
2. Firmware runtime obtains Wi-Fi metadata lease, reads release metadata and compares target/profile/version.
3. Display latest version and UpdateAvailable when there are updates; display UpToDate when there are no updates.
4. The user triggers Install, the runtime obtains OTA download/exclusive ownership, downloads and verifies the image.
5. Write the inactive OTA target, mark the boot partition after completion, and enter Rebooting.

#

- Unsupported target, no network, metadata invalid, version incompatibility, download interruption, image verification failure and OTA write failure enter Error.
- The boot target cannot be modified before verification is complete.
 - Wi-Fi/flash is a non-preemptible activity during installation, live calls and other HTTP requests get specific rejection reasons.

Source code: `modules/core_sys/include/platform/ui/firmware_update_runtime.h`, `platform/esp/arduino_common/src/platform_ui_firmware_update_runtime.cpp`, Settings firmware actions.

## Drill down

- [Activity](update-device-firmware/activity.md)
- [Sequence](update-device-firmware/sequences/sequence-update-device-firmware.md)
- [State Machine](update-device-firmware/state-machines/firmware-update.md)
