# Use Case: Receive, verify and submit decentralized messages

Status: **confirmed**

Business Boundary: Communications, Media and Delivery

Triggered by: Radio IRQ / transport event / active protocol backend
Main beneficiaries: device users

## User Goals

Only see messages that have been verified by the current protocol, have no duplicates, have been reliably submitted to the correct session, and can identify the sender, unread status, and necessary receipts.

## Success Scenario

1. Radio/transport hands the frame to the current active backend; the UI is not the initiator of the receiving chain.
2. The backend first checks the length, purpose, and protocol framing, and then performs decryption, signature/key or LXMF proof verification.
3. `ReceivePacketService`/protocol adapter establishes protocol identity and message identity; the deduplication key contains the protocol context.
4. Receive submissions to update messages, peer/contact facts, session metadata, unread and required ACKs in bounded order.
5. Only publish UI events after successful submission or after entering an explicit deferred recovery path.

## Rejection and recovery

- malformed, purpose mismatch, authentication failure are rejected before modifying the business status.
- duplicate does not create a second message; protocol-level ACK can still be responded to if necessary.
- Storage Busy/temporarily unavailable returns Deferred and puts it into bounded deferred slot; overflow must be counted and can be diagnosed.
- Rejected cannot trigger unread, contact promotion, or "message received" prompts.
 - ESP hot path uses scratch/ring storage and does not create large protobuf/frames in the task stack.

## Source code evidence

- `modules/core_mesh/include/mesh/usecase/receive_packet_service.h`
- `modules/core_mesh/src/usecase/receive_packet_service.cpp`
- `modules/core_chat/include/chat/usecase/chat_message_ledger.h`
- `platform/esp/arduino_common/src/chat/infra/mesh_adapter_router.cpp`

## Drill down

- [Activity: Frame verification, deduplication and submission](receive-text-message/activity.md)
- [Sequence: Radio to Ledger](receive-text-message/sequences/sequence-receive-text-message.md)
- [Sequence: Deferred Storage recovery](receive-text-message/sequences/sequence-receive-text-message-sequence.md)
-
