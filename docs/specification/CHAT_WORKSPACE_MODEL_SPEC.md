# Chat Workspace Model Specification

## Purpose

`ChatWorkspaceModel` is a presentation-layer ViewModel.

It owns UI-local workspace state only:

- selected conversation
- conversation list offset
- message list offset

It does not own:

- ChatService
- ContactService
- TeamService
- MeshSession
- message repository
- pending queue
- ACK tracker
- retry state
- failure inference
- read/unread ledger

## Pattern

This model follows:

- MVVM / Passive View
- Source/Sink Port
- Immutable Snapshot
- CQRS Read/Command split

## Snapshot Flow

Renderer calls:

```text
ChatWorkspaceModel::snapshot()
    -> IChatPresentationSource::buildChatWorkspaceSnapshot(request, out)
```

The returned `ChatWorkspaceSnapshot` is an immutable value object for UI
rendering.

## Action Flow

Renderer calls:

```text
ChatWorkspaceModel::selectConversation(id)
ChatWorkspaceModel::sendMessage(text)
ChatWorkspaceModel::markRead(id)
```

The model forwards actions to `IChatActionSink`.

## Read State Authority

`ChatWorkspaceModel::markRead(id)` is a UI intent. It is not the authoritative
read-state mutation.

The authoritative read/unread owner is `ReadStateLedger`, as frozen by
`RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`.

Required flow:

```text
Renderer
    -> ChatWorkspaceModel::markRead(...)
    -> IChatActionSink
    -> app/runtime read command
    -> ReadStateLedger commit or pending result
    -> ConversationProjectionStore / ChatPresentationSource
    -> unread badge projection
```

The workspace model may optimistically keep local selection and offsets, but it
must not claim durable read success. Conversation index entries, SD file
headers, unread counters, and app badges are projections of the ledger. They
may cache or mirror read state, but they must be rebuildable from
`MessageLedger + ReadStateLedger`.

Renderers must not hide an unread badge as the source of truth. They may only
show the projection returned by `IChatPresentationSource`, or a clearly pending
state produced by the read command path.

Mark-read failure semantics must stay explicit:

- committed: projection may clear the unread badge.
- pending: projection may show a temporary pending read state.
- failed: projection must not pretend the conversation is read.

This rule applies equally to Meshtastic, MeshCore, and Reticulum conversations.
The read reference must preserve protocol identity; a bare message id or peer id
is not a valid cross-protocol read key.

## Protocol Send Eligibility

Conversation protocol and active send protocol are separate facts.

`ConversationId.protocol` identifies what protocol produced or owns the
conversation. The active runtime protocol identifies which transport can send
right now. If they differ, the conversation remains visible and selectable, but
it is read-only for chat commands.

Required behavior:

- Read paths must continue to show cross-protocol conversations and messages.
- Compose, reply, retry, and `sendMessage` must be disabled or rejected for
  cross-protocol conversations.
- `IChatActionSink` adapters must map `SendMessageView.conversation` back to a
  full core `chat::ConversationId`; they must not drop the protocol and send by
  bare `channel + peer`.
- Devices that lack a channel creation entry may add one in their renderer, but
  renderers that already provide channel selection must not grow a second
  duplicate entry.

## Optimistic Selection

`selectConversation(id)` uses optimistic UI selection.

Semantics:

1. Validate the presentation `ConversationId`.
2. Update local `selected_conversation`.
3. Reset `message_offset`.
4. Notify `IChatActionSink::selectConversation(id)`.
5. Do not roll back local selection if the sink returns failure.

Rationale:

- selected conversation is presentation-local state.
- sink selection is a synchronization hook for business-side side effects.
- unsupported conversations may still be visible/selectable in UI.

## Message Paging

`ChatWorkspaceRequest::message_offset` is reserved for future message paging.

`ChatPresentationSource` currently ignores `message_offset` and returns the
recent message window.

This is intentional. The closeout phase must not change `ChatService` storage
or paging behavior.

Renderers must not assume `message_offset` is already honored by the chat read
projection.

## Send Feedback

`ChatWorkspaceModel::sendMessage(...)` submits a command and returns only local
command acceptance/rejection.

It must not:

- wait for ACK
- infer final send success
- show send success/failure feedback
- depend on the active page after the command is accepted

Final outgoing send feedback is produced from runtime delivery result events as
specified by `CHAT_DELIVERY_FEEDBACK_SPEC.md`.

## Source/Sink Adapter Contract

`ChatPresentationSource` is the product chat read projection adapter. It may
read `ChatService` and `ContactService`, and may use
`chat_presentation_adapters` to map core chat types into `ui_presentation`
rows.

It must not:

- send messages
- mark conversations read
- mutate `ChatService`
- access LVGL widgets
- access radio, mesh adapters, PKI, or packet builders

`RuntimeChatActionSink` is the runtime command adapter. It may
translate UI actions into `ChatService` commands.

It must not:

- build `ChatWorkspaceSnapshot`
- format UI labels
- access LVGL widgets
- inspect renderer state
- build radio packets or perform PKI logic

## Pending / Failure

`ChatWorkspaceModel` must not own pending messages, ACK tracking, retry state,
or failure inference.

Pending/failure projection must flow through:

```text
ChatService / MeshSession / ACK tracker / pending store
    -> IChatPresentationSource
        -> MessageRow.delivery
        -> MessageRow.failure
```

Until structured failure ownership is explicit, the compatibility projection is
limited to the coarse `chat::MessageStatus` available from `ChatMessage`.

## Team Chat

Team chat is not part of the generic DirectPeer/Channel path.

Team rows use:

```text
ConversationKind::Team
```

Team send/read/projection semantics are handled by a dedicated Team Chat
Presentation phase.

## Contract Role

This file is an Architecture Decision Record and ViewModel contract. It
constrains later implementation; it is not only descriptive documentation.
