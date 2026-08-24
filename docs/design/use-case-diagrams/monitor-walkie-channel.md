# Use Case: Listen to the analog talkback channel

Status: **confirmed receive-only behavior**
Business Boundary: Communication, Media and Delivery

## User goal

Listen to the current analog voice frequency, observe RSSI/audio levels and control the monitor on a device that supports the walkie runtime; the current page is not exaggerated by the documentation as a complete PTT transmit use case.

## Behavior and Rules

1. Check `platform::ui::walkie::is_supported()` before entering the page.
2. The runtime starts the receiver, and the page reads frequency, RSSI, squelch/monitor and volume level.
3. User switches monitor; the status will only be updated after confirmation by the platform.
4. Leave the page to stop the monitor and release audio/radio resources.

Display runtime error when failed, and do not retain the false status of "listened".

Source code: `modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp`, `modules/core_sys/include/platform/ui/walkie_runtime.h`.

#

- [Activity](monitor-walkie-channel/activity.md)
- [Sequence](monitor-walkie-channel/sequences/sequence-monitor-walkie-channel.md)
