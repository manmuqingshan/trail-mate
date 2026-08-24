# Sequence: Firmware Settings to OTA
```mermaid
sequenceDiagram
 actor U as user
  participant UI as Settings
  participant FW as Firmware Runtime
  participant Access as Wi-Fi Access
  participant HTTP as HTTP Client
  participant OTA as OTA Writer
  U->>UI: Check
  UI->>FW: start_check
  FW->>Access: acquire(HttpMetadata)
  FW->>HTTP: request metadata
  FW-->>UI: UpToDate / UpdateAvailable / Error
  U->>UI: Install
  UI->>FW: start_install
  FW->>Access: acquire(OtaDownload exclusive)
  FW->>HTTP: download image
  FW->>FW: verify image/target
  FW->>OTA: write inactive partition
  OTA-->>FW: complete
  FW->>OTA: set boot partition + reboot
```

## Two-stage scenario

Check and Install are two independent operations. Check uses ordinary metadata to access and returns UpToDate/UpdateAvailable/Error; Install re-verifies the selected metadata and applies for OTA exclusive. The expired "updated" UI status cannot be used to directly write to flash.

## Sequence constraints

Verify the source, target/profile, version and summary of metadata first. After the image download is completed, verify size/hash/signature again, and then start inactive partition write. Writer reports that complete still needs to be finalized/validated; then the boot partition can be set.

## Exclusive and Cancellation

OTA exclusive overrides download, write and boot flags. Safe cancellation is allowed during the download phase; the cancellation policy after starting flash write must be defined by the platform contract, at least the second install cannot be started at the same time. All failed paths release Access and maintain the current boot target.

## Restart semantics

`set boot partition` is successfully submitted and the UI enters RebootPending. The actual update is successful until the new firmware is started and confirmed; the bootloader rollback is another final state that must be read and reported after the application is started.

## Testing

Override metadata changes after Check, exclusive rejection, stream interruption, verification failure, partial write, boot mark failure, power failure before restart, and rollback.
