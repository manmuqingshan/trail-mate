# Use Case: Connect to Wi-Fi and arbitrate networking capabilities

Status: **confirmed**

Business boundary: network, identity and directory / device resource management

Main participants: device user
Resource applicant: FirmwareUpdate, PackRepository, RouteStorage, MeshMqtt, ReticulumGateway

## User Goals

Connect to the selected Wi-Fi to make updates, expansion packs, MQTT, route downloads, or Reticulum gateway available without letting background network activity disrupt live calls, OTAs, screen wake protection, or radio responses.

## Success Scenario

1. User enables Wi-Fi, scans for networks, selects SSID and submits credentials.
2. Wi-Fi runtime attempts to connect and returns a clear status; the credentials will only become the automatic connection source after being successfully saved.
3. For functions that require network, submit a Request containing `Client / AccessKind / Priority / allowConnect`.
4. Wi-Fi access runtime returns Lease or specific Decision based on ScreenPhase, OTA, call exclusive, non-preemptible activity and current owner.
5. The caller who obtains the lease performs a bounded operation and releases it after completion; long connections read and write according to the traffic budget.

## Failure and recovery

- No Credentials, Wi-Fi Disabled, Disconnected, Connection Backoff, Screensaver, OTA Exclusive, Call Exclusive, and Busy must be distinguishable.
- When the lease is revoked, the caller stops the current network work and does not continue to reuse the old generation.
- Calls can be upgraded to exclusive when entering ActiveCall from ring soft preemption and make way for background HTTP/long connections.

## Source code evidence

- `modules/core_sys/include/platform/ui/wifi_access_runtime.h`
- `platform/esp/arduino_common/src/platform_ui_wifi_access_runtime.cpp`
- `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp`

## Drill down

- [Activity: Connection and resource request](connect-wifi-services/activity.md)
- [Sequence: Client obtains and releases Lease](connect-wifi-services/sequences/sequence-connect-wifi-services.md)
