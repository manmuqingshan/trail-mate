# Use Case: Send decentralized messages and track delivery

Status: **confirmed**

Business Boundary: Communications, Media and Delivery

Main participants: device user
Core responsibilities: Chat intent, protocol backend, radio send queue, ChatMessageLedger

## User Goals

Send text from a channel, private message, Reticulum destination/group, or Team session and see queued, sent, delivered, or failed consistent with the protocol facts, rather than the UI guessing success on its own.

## Success Scenario

1. Compose verifies non-empty text, current session, target identity, protocol partition, and required key.
2. Establish a stable local message identity and protocol association key, submit `Queued`/outbox first, and then request the activity backend encoding.
3. The backend generates frames according to Meshtastic, MeshCore, LXMF or Team contract; the radio owner queues and transmits boundedly.
4. Only `Sent` can be submitted after local transmission is completed; only `Delivered` can be submitted after protocol ACK/receipt.
5. `ChatMessageLedger` merges out-of-order/duplicate receipts and protects the final state, persists the results and then drives the UI.

## Failure and recovery

- Target/key mismatch: reject before creating message.
- The queue is full and the radio is unavailable: the record can explain the failure or remain queued and can be retried, and the identity association cannot be lost.
- A timeout cannot overwrite an already arrived delivered; a late ACK cannot resurrect an explicitly failed non-retryable message unless allowed by the protocol policy.
- `Durable / Deferred / Rejected` is the persistent submission result, not the second set of MessageStatus.

## Source code evidence

- `modules/core_chat/include/chat/usecase/chat_message_ledger.h`
- `modules/core_mesh/include/mesh/usecase/send_message_service.h`
- `modules/core_mesh/src/usecase/send_message_service.cpp`
- `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`

## Drill down

- [Activity: Message submission and sending](send-text-message/activity.md)
- [Sequence: Compose to Ledger](send-text-message/sequences/sequence-send-text-message.md)
- [Sequence: ACK, timeout and final state competition](send-text-message/sequences/sequence-send-text-message-sequence.md)
