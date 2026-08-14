# 240x320 Monochrome Screen Contract

## Scope

This document defines the reusable text-first projection for portrait,
monochrome `240x320` displays. It is selected by an app catalogue through the
existing `AppScreen` lifecycle and is not tied to T-Deck Pro, a modem model,
an EPD controller, or a particular input board.

The current T-Deck Pro target supplies a menu shell, bitmap font and display
driver. Other `240x320` monochrome targets can select the same pages by
providing an input/visual host plus the capability ports described below.

## Screen ownership

| Layer | Owns | Must not own |
| --- | --- | --- |
| Catalogue / `AppScreen` | stable app identity and active-screen lifecycle | 240x320 page geometry or device service implementation |
| `screen_240x320/screen_app.cpp` | LVGL root, common text widgets, focus and page dispatch | capability-specific rendering, state or commands |
| One `*_page.cpp` per page | its text projection, initial actions and page-local transient state | board/display/modem/radio/storage implementation |
| Generic page port | compact capability snapshot and typed user intent | target-specific drivers, AT commands, power rails or refresh policy |
| ESP/runtime adapter | projection from existing runtime services into generic ports | LVGL widgets, menu focus or page layout |
| Board/display host | panel transfer, refresh policy, fonts, keyboard/touch mapping | page state or domain actions |

`screen_240x320` contains no board macro, board header, modem model, EPD
refresh request, display-driver call, or hardware random source. Missing
capability ports are valid and render an explicit unavailable state.

## Geometry and input

All coordinates are physical `240x320` portrait pixels.

- Margin: `8px`.
- Header rule: `y=32`; body begins at `y=42`.
- Ten text lines are spaced `17px`.
- Actions begin at `y=232`; footer rule is `y=278`; footer starts at `y=280`.
- Buttons are rectangular with inverse-video focus. The presentation uses
  black ink on white paper only.
- The hosting menu supplies `W/S` focus movement, Enter activation and the
  physical Backspace return key (`Esc` is a desktop compatibility alias)
  through the existing `ui_request_exit_to_menu` lifecycle.
- Pages render on entry and after explicit user actions only. They do not own
  an idle redraw timer or request a full-panel waveform.

## Page coverage

The catalogue maps the following stable capability entries to this projection:

- Map: a dedicated `240x240` shared-tile viewport at physical origin
  `(0,0)`, with the remaining `80px` reserved for keyboard-focusable actions,
  a single status line and shortcut help. The map uses the existing tile
  runtime and draws the current-position overlay only when the workspace
  snapshot has a valid fix. Center, Zoom and Base Layer actions update the
  existing `MapWorkspace` intent boundary; touch drag is an optional viewport
  pan and commits its resulting center only when the drag ends.
- Sky Plot: GPS receiver/fix projection and settings-backed GPS toggle.
- Network: device and mesh status projection.
- Settings: use the same canonical filter catalogue and complete workflow as
  the other UI targets: **Profile → Mesh → Radio → Wi-Fi → Location → Device
  → Maintenance**. It retains the explicit **Filter → Option List → Option
  Detail → Text Editor** routes, including Wi-Fi scan/SSID/password/connect
  flow and the existing maintenance confirmations. On a 240x320 monochrome
  screen this is a two-pane presentation with directly tappable filters and
  setting rows; keyboard navigation is a parallel input method, not a required
  intermediary. Toggle, choice, and bounded-number values apply through the
  established settings runtime; text values use its focused editor.

  The page intentionally distinguishes three kinds of setting:

  1. **Direct settings** — bounded booleans, choices and numbers such as GPS
     strategy, map source, radio TX state, brightness and time zone. They may
     be changed in the compact UI and immediately update their runtime owner.
  2. **Text and secret settings** — node names, APN/MQTT endpoints and
     credentials. They are never represented as a toggle; a focused editor is
     required, and existing secret values are not echoed into the editor.
  3. **Maintenance or destructive actions** — firmware update, erase/reset,
     backup/restore and modem recovery. They must keep their dedicated full
     settings workflow and confirmation model; the compact projection does
     not turn them into one-tap commands.
- Tracker, Walkie, SSTV and USB Storage: `RuntimeFeaturePort` snapshots and
  Start/Stop/monitor intents. The ESP adapter delegates to existing services.
- Chat: **Conversation List → Conversation → Compose**. Compose is an
  independent LVGL page with a focussed multi-line input, preserved draft on
  Cancel, and an explicit Send return to the conversation. Contacts
  enter this route through a typed conversation id rather than constructing a
  composer themselves.
- Team: **Status → Members → Member Detail**, **Status → Team Chat →
  Compose**, and **Status → Leave Confirmation**. Team chat owns a different
  snapshot, selection and draft from ordinary Chat; neither flow is allowed to
  alter the other's state.
- Contacts: **Contact List → Contact Detail → Chat Conversation**. The
  detail page is the boundary that validates the selected peer and protocol
  before routing to Chat.
- Extensions: **Package List → Package Detail → Install/Remove Confirmation**.
  Installation and removal are no longer immediate commands from a list row.
- Protocol Probe: **Overview → Candidate Detail → Apply Confirmation**.
  Applying a radio profile is an explicit confirmation page, not a hidden
  "press the same button twice" condition. The `ProtocolProbePort` still owns
  the radio scan worker.
- Cellular: `CellularPort` state and call/SMS/email/settings intents. A
  target-specific adapter may bridge it to any modem; a missing port is
  supported. Its concrete screens retain an explicit fixed-depth return stack:
  **Phone → SMS / Email / Radio Settings → Mail Settings**. `BACK` and the
  physical **Backspace** key (with `Esc` retained as desktop compatibility)
  unwind this stack and leave the application only at the Phone root. While a
  text field is being edited, Backspace remains text deletion; its visible
  Cancel/Back control exits editing without navigation.

## Capability ports

The `CellularPort`, `RuntimeFeaturePort`, and `ProtocolProbePort` headers are
the presentation boundary. Port structs are fixed-size snapshots intended to
be copied by the adapter without placing large protocol/config buffers on an
ESP task stack. Implementations must be installed during runtime bootstrap,
not by page code.

The generic pages use existing presentation sources and action sinks for map,
settings, network, chat, team, contacts and extensions. They use the three
ports above where the old UI previously reached platform capability services
directly.

## Verification conditions

1. Every listed catalogue entry selects a `screen_240x320` page; no legacy
   T-Deck-only page adapter owns an application page.
2. A state that has an independent entry focus, Back target or destructive
   confirmation is represented as a named route. Chat Compose and Chat
   Conversation each own a source file; the remaining compact routes own
   separate renderer/action branches within their domain page module.
   `screen_app.cpp` remains the common LVGL shell only.
3. A static scan of the generic directory finds no board macro/name, modem
   model, EPD refresh call, hardware random source, or direct tracker/walkie/
   SSTV/USB/probe runtime call.
4. A target build compiles the generic page library and target adapters, then
   links a firmware image successfully.

## Navigation invariants

- **Backspace** is offered to the active non-text route first (`Esc` remains a
  desktop compatibility alias). It exits to the menu only from an entry/root
  route.
- Compose input receives focus when the Compose page is entered. The visible
  `CANCEL` control returns to the parent conversation and preserves the draft;
  `SEND` queues the message and returns to that conversation. Backspace inside
  the editor deletes text rather than discarding a draft.
- Confirmation routes never perform the state-changing command until
  `CONFIRM` is selected. `CANCEL`/`BACK` returns to the immediate parent route.
- Two capability domains never share route booleans or draft buffers. This is
  particularly important for direct Chat and Team Chat, which can be visited
  in either order during one firmware session.
- The Map viewport owns the tile loader only while the Map app is active. Its
  `Runtime` is destroyed before the generic LVGL root, so the tile timer and
  map children cannot outlive the app page.
