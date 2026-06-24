# UI Feedback Runtime Specification

## Purpose

Define the product boundary for transient user feedback such as top-level
notices, send-result prompts, validation failures, and low-battery warnings.

The UI feedback runtime answers one question:

```text
When product code decides the user should be briefly informed, who owns the
decision, queueing policy, and final renderer call?
```

## Core Rule

User feedback is a runtime intent, not a page-owned widget operation.

Pages, protocol runtimes, and platform services may request feedback through a
stable feedback boundary. They must not call concrete renderer widgets such as
`SystemNotification` directly.

```text
Feature / page / runtime
  -> ui::feedback notice intent
    -> UI feedback runtime
      -> platform presenter
        -> concrete renderer widget
```

## Objects

### Feedback Intent

A feedback intent is a small product-level request:

- message text or localization-ready string
- display duration
- severity/category information

It is not a widget, LVGL object, GTK object, timer, or page-local toast.

### UI Feedback Runtime

The runtime owns:

- a stable API for feedback producers
- the `NoticeIntent` product request
- the `IFeedbackPresenter` bridge contract
- dispatching feedback without blocking the producer
- renderer scheduling policy
- async dispatch failure cleanup

It must not:

- know which page produced the feedback unless an explicit future policy needs
  that metadata
- block protocol/event dispatch while rendering
- require the producing page to remain alive
- expose concrete renderer widget classes to feature code

### Platform Presenter

The presenter translates feedback intents into the platform notification
surface.

Examples:

- ESP32/LVGL shell: top-level system notification widget
- Linux LVGL simulator shell: top-level system notification widget
- nRF52 mono shell: app-local feedback sink that renders a mono transient popup
- headless tests: capture/no-op presenter through `set_presenter`

The presenter is the only layer allowed to call concrete renderer widgets.

Non-LVGL targets must provide an equivalent feedback sink before they include
feedback in an active UI path. They must not reuse the LVGL
`SystemNotification` presenter or call concrete notification surfaces from
feature/page code. The nRF52 mono shell satisfies this through an app-local
event sink consumed from the mono UI tick, not through LVGL.

### SystemNotification

`SystemNotification` is an LVGL widget implementation detail.

It may:

- create and update LVGL objects
- animate the notification surface
- own LVGL timers for its own hide animation

It must not:

- be imported by page components
- be imported by protocol runtime code
- be treated as the product notification service
- decide feedback eligibility or delivery semantics

## Event-Driven Scheduling

Feedback display must be non-blocking with respect to producers.

On LVGL targets, a feedback request must schedule presentation through the UI
runtime/presenter and must not directly mutate LVGL objects from event
dispatch, protocol callbacks, or page teardown paths.

```text
EventBus dispatch
  -> feedback intent is posted
  -> event dispatch continues
  -> LVGL async/timer phase renders the notice
```

This prevents a feedback prompt from re-entering LVGL while a page is being
destroyed, while event dispatch is draining queued runtime events, or while the
display/shared-SPI presenter is in a sensitive refresh window.

If async scheduling fails, the runtime must release the request payload and
return failure to the producer. Producers may ignore that return value for
best-effort notices, but the runtime must not leak or retain dangling page
state.

On nRF52 mono targets, protocol/runtime producers publish lightweight runtime
events. `AppFacadeRuntime` updates core state and records a feedback intent;
the mono runtime consumes that intent during its UI tick and renders the
transient popup. Producers must not draw directly to the mono display.

## Page Responsibilities

Pages may:

- request feedback for local validation failures
- request feedback for unavailable actions
- close, navigate, or destroy themselves immediately after posting feedback

Pages must not:

- include concrete notification widget headers
- call `SystemNotification::show`, `hide`, or `init`
- own global feedback queueing or deduplication
- make feedback depend on the current page still being active

## Chat Delivery Feedback

Chat delivery feedback is a producer of feedback intents.

`ChatDeliveryFeedbackController` decides whether a send-result fact should
produce visible feedback. It then emits a stable feedback intent through
`IChatDeliveryFeedbackPort`.

The platform implementation of that port must post to the UI feedback runtime.
It must not call `SystemNotification` directly.

## Simulation Requirement

Feedback runtime behavior must be testable without concrete renderer widgets.
Tests must use a fake presenter/event drain to simulate:

- producer posts feedback and immediately returns
- page is destroyed before the feedback is rendered
- chat send success or failure arrives while another page is active
- renderer presenter is unavailable or rejects a notice
- duplicate notices are deduplicated or queued according to policy

These tests must assert that no feature/page/protocol code imports or calls
`SystemNotification` directly. `SystemNotification` remains only a platform
presenter implementation detail.

## Compatibility Burn-Down

The following are legacy coupling points and must not remain in active feature
or runtime code after this specification is implemented:

- direct page imports of `ui/widgets/system_notification.h`
- direct page calls to `SystemNotification::show`
- event-runtime calls to `SystemNotification::show`
- platform service calls to `SystemNotification::show`

The concrete widget implementation itself may remain, but only as the presenter
owned by the UI feedback runtime.
