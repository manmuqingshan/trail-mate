# Sequence Diagram: Protocol observation, directory and Contacts projection

```mermaid
sequenceDiagram
 participant Backend as active protocol backend
  participant Directory as IMeshPeerDirectory
  participant Contact as ContactService
  participant Stores as NodeStore / ContactStore
  participant UI as Contacts / Node Info
  Backend->>Directory: record(protocol identity, observed facts)
  Directory->>Directory: preserve firstSeen; update lastSeen/facts
  Directory-->>Contact: directory record / node update
  Contact->>Stores: persist observation and local flags
  UI->>Contact: getContacts/getNearby/getIgnoredNodes
  Contact-->>UI: active-protocol projection
 alt user save or rename
    UI->>Contact: addContact/editContact(nodeId,nickname)
    Contact->>Stores: commit nickname relation
 else user ignore
    UI->>Contact: setNodeIgnored(nodeId,true)
    Contact->>Stores: commit ignored flag
 else user manual verification
    UI->>Contact: setNodeKeyManuallyVerified(nodeId,true)
    Contact->>Stores: commit only if node exists
  end
```

## Scenario

This sequence covers two different timelines: the background protocol continuously records observations, and the front desk queries and submits local relationships on demand. The two can happen concurrently, so the ContactService cannot overwrite the protocol facts in the update with a single UI snapshot.

## Participant Responsibilities

- **Backend** only provides protocol-parsed observed facts and does not have nicknames, ignored, or human verification.
- **Directory** maintains protocol-scoped identity, first/last seen, and observation facts.
- **ContactService** combines directory and local relationships to execute business preconditions for user commands.
- **Stores** is the persistence submission boundary.
- **UI** only consumes active protocol projections and sends explicit commands.

## Sequence and concurrency rules

Observations must first pass the identity shape verification and then upsert Directory. User actions are positioned by stable NodeId/identity, not by list position. Background updates to `lastSeen` are concurrent with user renames; the merge strategy is field ownership merging rather than last-write-wins overwriting the entire record.

## Submission point

ContactService only returns command completion after Store success. UI list refresh comes from reprojection or submit events, button clicks cannot be regarded as saved. Manual verification fails explicitly when the node does not exist, and no evidence-free nodes cannot be secretly created to satisfy the command.

## Late and duplicate messages

Duplicate protocol observations should be updated idempotently; old observations should not regress `lastSeen` or overwrite new facts. Duplicate `setNodeIgnored(true)`, the same nickname edit, and the same validation flag should all be safe. Old cache events that are late after `removeNode` require a revision/time check, otherwise deleted records will be resurrected immediately.

## Key points of verification

Tests should interleave observation, rename, ignore and remove to prove that local fields will not be overwritten by protocol refresh, and verify that each protocol namespace has an independent key space.
