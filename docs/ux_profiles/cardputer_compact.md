# UX Profile: cardputer_compact

## Screen Class

Compact 320 x 170 landscape keyboard device.

## Input Model

Keyboard-first input. Pointer, touch, and trackball are absent in the current
Cardputer Zero board facts.

On the Linux device route, text composition is owned by the user-session Fcitx5
stack. Trail Mate's compact UX accepts committed text from the normal Linux
input frontend path; the Cardputer Zero IME panel socket is display-only and is
not a Trail Mate text submission API.

## Feature Set

Compact chat/status workflow with contacts, compact map, Sky Plot/team actions,
tracking, walkie-talkie, extensions, and settings.

## Screen Set

Dashboard, Chat, Contacts, Compact Map, Sky Plot, Team, Tracker, Walkie Talkie,
Extensions, Settings.

The Cardputer Zero product menu intentionally excludes PC Link, SSTV, Protocol
Probe, and SD Storage / USB Disk. SD Storage is the card-access/USB
mass-storage entry, not Extensions.

This screen set must stay aligned with both:

- the executable `CardputerCompactUxPack::buildScreens()` list
- the `cardputer_compact_manifest` route manifest

The main menu visual baseline is the Pager compact family. In shared LVGL UI,
`make_cardputer_zero_profile()` starts from `make_pager_profile()` and then
shrinks geometry for the 320 x 170 Cardputer Zero display. Cardputer Zero page
adaptation must preserve that Pager-derived visual language unless a new UX
profile decision explicitly changes it.

## Map Mode

Compact 320 x 170 landscape map with offline tiles and current-position focus.
The Cardputer Zero projection keeps the ESP32 map actions but names them for
the keyboard UI: arrows move the map, `+` / `-` zoom, `P` / `Pos` centers the
current position, `L` cycles the base layer, `O` / `Contour` toggles the
contour overlay, and `F1` opens the map help overlay. Route and Team controls
are contextual and appear only when those ESP32 map business states are active.

## Chat Mode

Quick message chat list and compose optimized for keyboard entry.

## Team Action Mode

Quick location plus compact command/status actions.

## Sky Plot Mode

The UX pack exposes a distinct Sky Plot route. The Linux/Cardputer Zero
shared-LVGL implementation renders this route through the GNSS sky-plot shell
rather than reusing the Map workspace or exposing a separate GPS-status product
page.

## Modal/Picker Strategy

Keyboard-first compact modals and list pickers.

## Renderer Family

Executable UX pack `cardputer_compact`; the shared LVGL menu profile already
has a Cardputer Zero target profile derived from the Pager profile. The current
Linux device-shell build slice validates target identity, UX selection, page
manifest alignment, notification/Fcitx5 boundaries, and the Pager-derived menu
profile.

Shared-LVGL 320 x 170 screenshot evidence exists for Dashboard, Chat, Contacts,
Map, Sky Plot, Team, Tracker, Walkie Talkie, Extensions, and Settings in
`docs/targets/cardputerzero-screenshots.md`. This evidence is from the
Cardputer Zero compact shell path and is not a simulator-only or ad hoc drawing
path. Real framebuffer rendering still remains pending until the Cardputer Zero
device renderer is wired and validated.

## Deferred Decisions

Real-device keyboard sampling, framebuffer handoff, notification daemon session
validation, Fcitx5 session validation, launch, and packaging remain deferred.

Map and Sky Plot route ids are distinct and use distinct projections: Map owns
the runtime map workspace, while Sky Plot owns the runtime GNSS sky-plot view.
Cardputer Zero product runtime must not seed Map with demo/default coordinates;
without a live GPS source or an explicit test environment override, Map should
show the no-fix/no-position state.

Board describes. Target chooses. UX Pack presents. Renderer draws.
