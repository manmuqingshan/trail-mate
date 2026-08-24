# Use Case: Handle the device storage safely to the USB host

Status: **confirmed**
Business boundary: device maintenance and data ownership

## User Goals

Let the PC access the device SD card using USB Mass Storage while preventing
the device's own Wi-Fi, LoRa, GPS, track, or file worker from competing with
the hand-off; resume the device's original runtime configuration after exit.

## Main scene

1. Only USB support and SD ready targets display entries.
2. `prepare_mass_storage_mode` first asks the Wi-Fi runtime to suspend without
   writing a setting, then pauses and verifies LoRa task/RX ownership, suspends
   GPS, and disables screen sleep. A pre-existing exclusive LoRa pause rejects
   USB Disk rather than being taken over.
3. The device unmounts/deinitializes the application SD owner only after that
   complete quiesce succeeds; the USB backend then takes over the media and
   reports Active.
4. Stop the backend when the user exits or the host disconnects; remount the
   application SD, then restore screen policy, GPS, LoRa RX/tasks, and Wi-Fi
   in reverse order. Wi-Fi resumes only if its saved setting remains enabled.

## Failure and recovery

- USB backend is not started when Wi-Fi cannot suspend, LoRa cannot quiesce or
  enter standby, a required storage owner fails to stop, or SD cannot unmount.
- USB boot failure must resume every completed preparation stage, including
  Wi-Fi/LoRa/GPS, as well as the application SD mount.
- Exit is an asynchronous process; the page displays stopping before restore is completed, and cannot return early to allow the application to access the SD.

Source code: `modules/core_sys/include/platform/ui/usb_support_runtime.h`, `platform/esp/arduino_common/src/platform_ui_usb_support_runtime.cpp`, `modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp`.

## Drill down

- [Activity](expose-usb-storage/activity.md)
- [Sequence](expose-usb-storage/sequences/sequence-expose-usb-storage.md)
- [State Machine](expose-usb-storage/state-machines/usb-storage-ownership.md)
