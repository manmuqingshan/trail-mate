# Use Case: Handle the device storage safely to the USB host

Status: **confirmed**
Business boundary: device maintenance and data ownership

## User Goals

Let the PC access the device SD card using USB Mass Storage while preventing the device's own GPS, track, radio or file worker from concurrently writing the same media with the host; resume device use after exiting.

## Main scene

1. Only USB support and SD ready targets display entries.
2. `prepare_mass_storage_mode` requests relevant workers to stop/flush, pause GPS/radio tasks, screen sleep and other activities that will touch shared resources.
3. device unmount/deinit application SD owner, USB backend takes over the media and reports Active.
4. Stop backend when the user exits or the host is disconnected; remount the application SD and restore tasks and screen policy.

## Failure and recovery

- USB backend is not started when either owner fails to stop or SD cannot be unmounted.
 - USB boot failure must resume application mount and suspended tasks.
- Exit is an asynchronous process; the page displays stopping before restore is completed, and cannot return early to allow the application to access the SD.

Source code: `modules/core_sys/include/platform/ui/usb_support_runtime.h`, `platform/esp/arduino_common/src/platform_ui_usb_support_runtime.cpp`, `modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp`.

## Drill down

- [Activity](expose-usb-storage/activity.md)
- [Sequence](expose-usb-storage/sequences/sequence-expose-usb-storage.md)
- [State Machine](expose-usb-storage/state-machines/usb-storage-ownership.md)
