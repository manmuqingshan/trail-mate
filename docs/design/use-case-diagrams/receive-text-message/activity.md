# Activity Diagram: Frame verification, deduplication and submission

```mermaid
flowchart TD
 Frame["Radio / transport frame"] --> Active{"From active protocol?"}
 Active -- No --> Ignore["Reject cross-protocol pollution"]
 Active -- Yes --> Decode{"framing / destination / length valid?"}
 Decode -- No --> Reject["Rejected; do not change business status"]
 Decode -- Yes --> Auth{"Decryption, signature or key verification passed?"}
 Auth -- No --> Reject
 Auth -- Yes --> Dedup{"protocol-scoped identity seen?"}
 Dedup -- Yes --> AckOnly["Do not recreate message; handle ACK according to protocol"]
 Dedup -- No --> Commit{"Message/peer/session submission result"}
 Commit -- Durable --> Publish["Publish messages and unread events"]
 Commit -- Deferred --> Buffer{"Enter bounded deferred slot"]
  Commit -- Rejected --> Reject
 Buffer --> Retry{"Resource recovery?"}
 Retry -- Yes --> Commit
 Retry -- Overflow --> Drop["Count and expose diagnostics"]
```

## Questions answered by this picture

What conditions does an asynchronous wireless frame meet to become a user-visible message? Reception is not a user command: entry is a radio/transport event, exit is a Durable commit, controlled Deferred, or reject with diagnostics.

## Verification level

1. **Protocol ownership**: Only accept events from the active backend to prevent the identity and dedup spaces of different protocols from contaminating each other.
2. **Frame structure**: length, target, version and framing must be complete.
3. **Authenticity**: Decryption, signature, key or authentication is performed according to the protocol; "decodable" does not mean trustworthy.
4. **Deduplication**: Use protocol-scoped message identity instead of text content or arrival time.

## Submit results

Durable indicates that the message, peer association, and session state have entered the persistence boundary before unread events can be published. Deferred means that the business fact has not yet been submitted and the frame is put into a fixed-capacity slot; the UI must not display the message first. Rejected does not modify the business status, and the necessary protocol-level ACK/NACK is handled separately by the backend.

## Duplication, reordering, and capacity

Duplicate frames do not create a second message, but may require a protocol ACK to be resent. Out-of-order messages are projected according to protocol sequence number or message time, without changing the storage and submission order. The observable drop count must be incremented when a bounded deferred slot is full, unprocessed frames cannot be overwritten, and large frames cannot be copied on the ESP task stack.

## Recovery rules

After the resource is restored, retry submission according to the original protocol identity; repeated retries must be idempotent. Restart recovery only handles explicitly persisted inbox/outbox or supported durable deferred data and cannot assume that the RAM slot will survive.

## Source code evidence and testing

Activities span backend decode, authentication/deduplication, message receiving service and Ledger/Store. Tests need to inject cross-protocol frames, invalid signatures, duplications, out-of-order, storage Deferred, slot overflow and publishing event failures.
