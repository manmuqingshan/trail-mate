# Activity: Backup, restore and reset
```mermaid
flowchart TD
  Intent{"Backup / Restore / Reset?"}
  Intent -- Backup --> SD{"SD ready?"}
 SD -- No --> Error
 SD -- Yes --> Temp["serialize supported settings to temp"]
  Temp --> Atomic{"atomic replace success?"}
 Atomic -- Yes --> Success
 Atomic -- No --> Error
  Intent -- Restore --> Parse{"backup parse + validate?"}
 Parse -- No --> Keep["Keep current settings"]
 Parse -- Yes --> Apply["apply supported settings"]
  Apply --> Reinit["reinitialize/reboot affected owners"]
 Intent -- Reset --> Confirm{"Show specific damage scope and confirm?"}
 Confirm -- No --> Cancel
 Confirm -- Yes --> Scoped["Execute mesh/nodes/messages/factory Corresponding to cleanup"]
```

## Questions answered by this picture

How do backup, recovery and reset of four different damage scopes share configuration boundaries while avoiding bad backups from overwriting current settings or vague confirmations leading to excessive deletion.

## Backup

Backup only serializes explicitly supported and migrable fields, writes them to a temporary file and replaces them atomically after a successful flush/close. The inclusion of sensitive keys must be determined by the format version and user intent. Old backups remain available until new files are committed.

## Recovery

 Recovery first parses the version, verification structure, target compatibility and each field constraint, and then constructs a complete candidate configuration. Any validation failures retain the current settings. Reinitialize in the order of affected owners after Apply; changes that require reboot are not pretended to have taken effect immediately.

## Reset range

| Type | Only deletion allowed |
| --- | --- |
| Mesh reset | Clear mesh data such as protocol configuration, identity and channel |
| Nodes reset | peer/node directory and local relationship |
| Messages reset | Session, message and delivery ledger |
| Factory reset | All user configuration and data defined in the document |

The confirmation interface must display the specific range and cannot be replaced by the same "Are you sure?" Cancellation does not have any persistence side effects.

## Failure and recovery

SD unavailability, temporary write failures, atomic replacement failures, and reinitialize failures are reported separately. If the recovery spans multiple owners, complete candidates must be verified in advance and the stages must be recorded; unexplained mixed configurations cannot be left behind after a failure occurs midway.

## Testing

 Covers format upgrades, unknown fields, damaged backups, atomic replacement failures, sensitive field policies, negative deletion assertions for each reset, and configurations that require restarting.
