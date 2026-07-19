# Chat Runtime Event Pump Specification

## Purpose

Define the Phase 7.7 ownership boundary for Chat runtime event routing, delivery projection, key verification projection, and ChatService scheduling.

## Core Rule

Runtime event projection belongs to the event pump.

UI refresh belongs to the controller.

The controller must not own runtime event projection or runtime scheduling.

The event pump is a router, not a business-state owner. Message delivery,
read/unread, retry, and protocol identity ownership is frozen by
`RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`; this pump must route events into those
owners and notify UI refresh sinks from their projections.

## Types

### `IChatUiRefreshSink`

`IChatUiRefreshSink` is the minimal UI refresh port exposed to runtime event code.

It may:

- notify the UI that a runtime message arrived
- notify the UI that a send result changed
- notify the UI that unread counts changed
- ask the UI to show a key verification snapshot

It must not:

- expose `ChatService`
- expose `ChatDeliveryReadModel`
- expose `ChatDeliveryEventProjector`
- expose `LegacyKeyVerificationSource`
- expose raw EventBus events

### `ChatPageRuntimeEventPump`

`ChatPageRuntimeEventPump` is the chat page event pump and event router.

It may:

- call `ChatService::processIncoming()`
- call `ChatService::flushStore()`
- receive EventBus `sys::Event` objects
- route `ChatSendResultEvent` to `ChatDeliveryEventProjectionAdapter`
- route key verification events to `LegacyKeyVerificationSource`
- select the key verification peer on `KeyVerificationModel`
- notify `IChatUiRefreshSink`
- delete consumed EventBus events

It must not:

- render LVGL widgets
- own `ChatWorkspaceModel`
- encode Team payloads
- retry messages
- implement protocol packet flow

### `ChatPageRuntimeFacade`

`ChatPageRuntimeFacade` implements `IChatUiRuntime` for existing app facade integration.

It calls:

```text
event_pump.update()
controller.update()
```

for ticks, and:

```text
event_pump.handleEvent(event)
```

for EventBus events.

`chat_page_runtime.cpp` registers the facade, not `ChatUiController`, as the active chat UI runtime.

## Event Handling Order

### `ChatSendResultEvent`

1. `ChatPageRuntimeEventPump` calls `ChatDeliveryEventProjectionAdapter::onChatSendResult(...)`.
2. `ChatPageRuntimeEventPump` calls `IChatUiRefreshSink::onRuntimeSendResult(...)`.

The page runtime may refresh visible message state, but it must not own final
send result feedback. Global `Sent` / `Send failed` feedback belongs to the
event-driven delivery feedback mechanism in
`CHAT_DELIVERY_FEEDBACK_SPEC.md`.

### `ChatNewMessageEvent`

1. The message has already entered the message owner path.
2. Read/unread changes, if any, are produced by the read-state owner path.
3. `ChatPageRuntimeEventPump` calls `IChatUiRefreshSink::onRuntimeMessageArrived(...)`.

### `ChatUnreadChangedEvent`

1. `ReadStateLedger` / projection state has changed.
2. `ChatPageRuntimeEventPump` calls `IChatUiRefreshSink::onRuntimeUnreadChanged()`.

The pump must not clear badges or mutate unread counters directly.

### Key Verification Events

1. `ChatPageRuntimeEventPump` updates `LegacyKeyVerificationSource`.
2. `ChatPageRuntimeEventPump` selects peer on `KeyVerificationModel`.
3. `ChatPageRuntimeEventPump` calls `IChatUiRefreshSink::showKeyVerification(...)`.

## Runtime Scheduling

`ChatPageRuntimeEventPump::update()` owns the chat page's legacy runtime tick:

```text
ChatService::processIncoming()
ChatService::flushStore()
```

`ChatUiController::update()` remains UI-only and may refresh dirty UI state.

## Non-goals

Phase 7.7 does not:

- rewrite EventBus
- rewrite `ChatService`
- rewrite `ChatUiController`
- implement a new thread model
- change protocol packet flow
- solve Team rich payload rendering
- solve Map tile/cache ownership
