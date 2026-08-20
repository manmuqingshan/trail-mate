# Sequence: Application SD to USB Host
```mermaid
sequenceDiagram
 actor U as user
  participant UI as USB Page
  participant USB as USB Support Runtime
  participant Owners as GPS/Radio/Track/File Workers
  participant SD as Application SD Host
  participant MSC as USB MSC Backend
  U->>UI: enter USB Disk
  UI->>USB: start
  USB->>Owners: quiesce + flush + pause
  USB->>SD: unmount/deinit
  USB->>MSC: start(media)
  MSC-->>UI: Active / failed
  U->>UI: exit
  UI->>USB: stop
  USB->>MSC: stop
  USB->>SD: remount application SD
  USB->>Owners: resume
```

## Scenarios and participants

USB Support Runtime is the coordinator of ownership switching; GPS/Radio/Track/File Workers are owners who may hold files or tasks; Application SD Host and USB MSC Backend are mutually exclusive media owners; UI only sends start/stop.

## Handover fence

Owners' quiesce returns token/confirmation, proving that no new I/O is generated and has been flushed. Unmount/deinit only after confirming everything. MSC is only started after successful unmount. The absence of any acknowledgment resumes the suspended owner in reverse order.

## Return fence

stop MSC must wait for host I/O to terminate before remounting and checking the file system; resume only after remounting is successful. Host disconnect also follows the same sequence and cannot skip stop.

## Failure compensation

 MSC start fails to execute stop-if-needed + remount. Remount failure leaves Owners paused and displays recovery-required. Repeated stop/start per session generation is idempotent.

## Tests

Cover an owner's rejection of quiesce, unmount failure, MSC start failure, sudden disconnection, remount failure, repeated exit and ownership mutual exclusion assertion.
