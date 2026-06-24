# Chat Delivery Feedback Specification

## Purpose

Define the global, event-driven feedback mechanism for chat message send
results.

The delivery feedback mechanism answers one question:

```text
When the runtime learns that an outgoing chat message was sent or failed,
what user-visible feedback should the product show?
```

This mechanism is independent of the screen that initiated the send action and
independent of the screen that is currently visible.

## Core Rule

Chat send result feedback is delivery runtime feedback, not page behavior.

```text
SendText command accepted/rejected
  -> local command result feedback for immediate rejection only

Protocol/runtime delivery result event
  -> ChatService message status update
  -> ChatDeliveryFeedbackController
  -> IChatDeliveryFeedbackPort
  -> UI feedback runtime
  -> platform feedback presenter
```

The final `Sent` / `Send failed` prompt must be driven by delivery result
events such as `ChatSendResultEvent`.

It must not be driven by:

- the page that submitted the command
- the page that is currently visible
- a compose widget timer polling `ChatService`
- disabling a page until ACK
- a renderer-local Toast tied to a page object

## Objects

### Send Command

The UI expresses the user intent as a send command through an action sink.

The command may be rejected synchronously for local reasons:

- invalid input
- unsupported protocol
- missing peer or channel key
- radio unavailable
- local identity missing
- queue rejected by the runtime

Synchronous rejection may show an immediate failure prompt.

Synchronous acceptance means only that the runtime accepted the command and
created or queued an outgoing message. It does not mean final delivery success.

### Delivery Result Event

`ChatSendResultEvent` is the current compatibility event for final send
outcome.

Semantics:

- `success == true` means the runtime classified the outgoing message as sent.
- `success == false` means the runtime classified the outgoing message as
  failed.
- The event must be processed after or together with the corresponding
  `ChatService::handleSendResult(...)` state update.

Future protocol runtimes may publish richer delivery events, but they must
preserve the same boundary: final user feedback is produced from runtime
delivery facts, not from page polling.

### ChatDeliveryFeedbackController

`ChatDeliveryFeedbackController` owns user-visible delivery result feedback
decisions.

It may:

- receive delivery result facts
- ignore events for unknown messages
- ignore incoming messages
- deduplicate repeated final results for the same message id
- call `IChatDeliveryFeedbackPort`

It must not:

- send messages
- retry messages
- update `ChatService`
- render LVGL, GTK, or mono widgets directly
- depend on the current page
- query renderer state
- start timers waiting for ACK

### IChatDeliveryFeedbackPort

`IChatDeliveryFeedbackPort` is the Bridge from product feedback semantics to a
UI feedback intent.

Platform implementations post to the UI feedback runtime described in
`UI_FEEDBACK_RUNTIME_SPEC.md`. Active LVGL implementations use:

- ESP32/LVGL shell: `ui::feedback::NoticeIntent`
- Linux LVGL simulator shell: `ui::feedback::NoticeIntent`
- nRF52 mono shell: `ChatSendResultEvent` -> `AppFacadeRuntime` feedback
  intent -> mono transient popup
- headless tests: capture/no-op presenter

The port receives stable product-level feedback:

```text
showChatDeliverySent(message_id)
showChatDeliveryFailed(message_id)
```

It must not decide whether a message is outgoing, whether a duplicate should be
suppressed, or whether `ChatService` should update status.

It also must not call a concrete notification widget directly. On LVGL targets,
the port posts a feedback intent and lets the UI feedback runtime schedule the
renderer asynchronously.

Non-LVGL targets must install an equivalent feedback sink before exposing
delivery feedback in an active UI. The sink may be app-local when that target
does not link the LVGL chat runtime, but it must preserve the same boundary:
runtime delivery fact first, core state update second, UI notice from the UI
tick last. A target without an active notification surface must be explicit
about using a no-op sink instead of directly calling display or buzzer code
from protocol/runtime producers.

## Event Flow

### Accepted Send

```text
UI page
  -> ChatWorkspaceModel / action sink
    -> ChatService::sendTextDetailed(...)
      -> outgoing message queued
        -> no final success prompt yet
```

The page may close, navigate, or be destroyed immediately after acceptance.
No widget must wait for ACK.

### Rejected Send

```text
UI page
  -> ChatWorkspaceModel / action sink
    -> command rejected
      -> immediate local failure prompt
```

This is command rejection feedback, not delivery result feedback.

### Final Send Result

```text
Protocol runtime / mesh adapter
  -> ChatSendResultEvent
    -> ChatService::handleSendResult(...)
      -> ChatDeliveryFeedbackController::onChatSendResult(...)
        -> IChatDeliveryFeedbackPort
          -> UI feedback runtime
          -> user-visible Sent / Send failed notification
    -> Chat UI runtime refresh, if active
```

Chat UI refresh is optional. Global feedback is not optional when a platform
feedback port is available.

On nRF52 mono, the active build does not link the LVGL chat runtime. The same
`ChatSendResultEvent` still drives the flow: `AppFacadeRuntime` updates
`ChatService`, records a pending feedback intent, and `ui::mono::Runtime`
renders `Sent` / `Send failed` during the next UI tick.

## Blocking Prohibition

The send flow must be non-blocking.

Forbidden behaviors:

- disabling compose controls while waiting for ACK
- disabling the back button while waiting for ACK
- polling message status from a UI timer to decide final send feedback
- delaying navigation until send result
- tying final feedback to a page-local Toast host

Allowed behaviors:

- closing the compose view immediately after the command is accepted
- refreshing message rows when a delivery event arrives
- showing final feedback from the global delivery feedback controller
- showing local rejection immediately when the command is not accepted

## Page Responsibilities

Pages may:

- collect input
- submit send commands
- close compose surfaces after local acceptance
- refresh visible message rows when the runtime reports a change
- show local validation/rejection failures

Pages must not:

- decide final send success/failure
- wait for ACK
- own delivery feedback deduplication
- suppress final feedback because the page is not visible

## Compatibility Burn-Down

The following legacy mechanisms are not valid delivery feedback mechanisms:

- `chat_send_flow::begin_local_text_send`
- `ChatComposeScreen::beginSend`
- `ChatComposeScreen::finishSend`
- `ChatComposeScreen::on_send_timer`
- `ChatComposeScreen::showSendToast`
- any `Toast` whose purpose is final send success/failure feedback

After this specification is implemented, those mechanisms must not remain on
the active build path.

## Non-Goals

This specification does not:

- redesign packet formats
- redesign ACK policy
- implement full retry policy
- require Linux GTK to use the same widget class as LVGL
- require a prompt for every intermediate queued/sending state

It does require final outgoing send success/failure feedback to be global,
event-driven, and independent of page lifecycle.
