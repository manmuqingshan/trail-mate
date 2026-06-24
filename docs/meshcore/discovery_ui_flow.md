# MeshCore Discovery UI Flow

## Goal

Add an active discovery entry in Contacts page for MeshCore mode, so users can:

- discover neighbors proactively (`scan local`)
- expose local identity on demand (`send id local`)
- expose identity network-wide (`send id broadcast`)

Current issue: device can respond to discovery but cannot initiate it from UI.

## Shared Use Case Contract

All UI entry points must call:

- `ChatService::triggerDiscoveryActionDetailed(MeshDiscoveryAction::ScanLocal)`
- `ChatService::triggerDiscoveryActionDetailed(MeshDiscoveryAction::SendIdLocal)`
- `ChatService::triggerDiscoveryActionDetailed(MeshDiscoveryAction::SendIdBroadcast)`

The UI must not build MeshCore discover packets, choose route types, or duplicate
the discover decision table. Those rules belong to the shared protocol runtime:

`UI -> ChatService -> IMeshAdapter -> MeshProtocolFacade::discover -> MeshCoreRuntime -> ProtocolEffect`

Platform adapters only execute the effects returned by the runtime and publish
the results into their local presentation/storage ports.

## Entry Points

### ESP32 / LVGL Contacts

Location: Contacts page, `Filter Panel` (left column).

New button:

- label: `Discover`
- visible only when active protocol is `MeshCore`
- hidden in `Meshtastic` mode
- behavior: acts as a filter mode button (same interaction model as Contacts/Nearby/Broadcast/Team)

Rationale:

- user requested placement in Filter Panel
- unify with existing left-column mode buttons
- avoid dual-state confusion between `CHECKED`(mode) and `FOCUSED`(cursor)

### nRF / Mono Root Menu

Location: root menu, immediately above `SETTINGS`.

Menu item:

- label: `DISCOVER`
- visible only when active protocol is `MeshCore`
- hidden in `Meshtastic` mode
- opens a mono action page with the same command set:
  1. `SCAN LOCAL`
  2. `ID LOCAL`
  3. `ID BROADCAST`
  4. `CANCEL`

Rationale:

- nRF mono UI does not use the LVGL Contacts filter panel
- root menu placement keeps the discover use case reachable without creating a
  second protocol implementation
- action execution remains delegated to `ChatService` and the shared runtime

## Interaction Flow

### 1) Enter Discover Mode

On LVGL Contacts, when user focuses/clicks `Discover`, second column (`List Container`) shows four action items:

1. `Scan Local`
2. `Send ID Local`
3. `Send ID Broadcast`
4. `Cancel`

Focus/encoder flow remains identical to other modes: left column selects mode, right column executes row action.

On nRF mono, selecting root menu `DISCOVER` opens the Discover action page. `Back`
or `CANCEL` returns to the root menu.

### 2) Scan Local

Definition:

- send one `DISCOVER_REQ` control packet in zero-hop direct mode
- wait for `DISCOVER_RESP` matching request tag
- collect responses for a short window

UI behavior:

1. keep Discover mode page visible
2. show non-blocking toast: `Scanning 5s...`
3. update nearby list as responses arrive (background data update)
4. after timeout, show summary toast with gained/total nearby count

nRF mono behavior:

1. keep Discover action page visible
2. show non-blocking popup: `SCANNING 5S`
3. update `NODES` through `PublishNodeInfoEffect -> ContactService`

Protocol mapping:

- route type: `DIRECT`, `path_len=0`
- payload: `DISCOVER_REQ`
- type filter default: all supported advertise types
- `prefix_only`: false by default
- `since`: 0

### 3) Send ID Local

Definition:

- publish one identity advert for local neighbors only

UI behavior:

1. stay on Discover mode page
2. send command
3. toast result:
   - success: `ID sent (local)`
   - failure: `ID send failed (local)`

nRF mono behavior:

1. stay on Discover action page
2. show non-blocking popup: `ID SENT LOCAL` or a mapped `MeshActionResult` failure

Protocol mapping:

- route type: `DIRECT`, `path_len=0`
- payload type: `ADVERT`
- payload content: signed advert using local Ed25519 identity

### 4) Send ID Broadcast

Definition:

- publish identity advert intended for mesh-wide propagation

UI behavior:

1. stay on Discover mode page
2. send command
3. toast result:
   - success: `ID sent (broadcast)`
   - failure: `ID send failed (broadcast)`

nRF mono behavior:

1. stay on Discover action page
2. show non-blocking popup: `ID SENT BROADCAST` or a mapped `MeshActionResult` failure

Protocol mapping:

- route type: `FLOOD`, `path_len=0`
- payload type: `ADVERT`
- payload content: signed advert using local Ed25519 identity

## Focus Rules

- Discover is a normal mode button in the filter column.
- Enter from filter column moves focus to the list column.
- `Cancel` action exits Discover mode and returns to previous non-Discover mode in filter column.

## Data and List Behavior

On discovery response:

- create/update node entry with protocol = `MeshCore`
- update `Nearby` list immediately
- do not auto-promote to named contact

On nRF mono, the same `PublishNodeInfoEffect` must update `ContactService`, so
the root `NODES` page sees discovered MeshCore nodes without an nRF-specific
discover table.

Broadcast list remains channel-centric and protocol-tagged (already separated by MT/MC labels).

## Backend Contract (for implementation)

To avoid UI -> concrete adapter coupling, add capability and command API in `IMeshAdapter`.

Suggested shape:

- `MeshCapabilities::supports_discovery_actions`
- `enum class DiscoveryAction { ScanLocal, SendIdLocal, SendIdBroadcast }`
- `bool triggerDiscoveryAction(DiscoveryAction action)` in `IMeshAdapter`
- passthrough in `ChatService`

MeshCore adapter implements all three actions.
Meshtastic adapter returns false.

## Logging (required)

Add explicit TX logs:

- `[MESHCORE] TX DISCOVER_REQ mode=local tag=... filter=... prefix=...`
- `[MESHCORE] TX ADVERT mode=local ...`
- `[MESHCORE] TX ADVERT mode=broadcast ...`

Reason: field debugging currently depends on serial logs.

## Acceptance Checklist

1. In MeshCore mode, Contacts Filter Panel shows `Discover`; Meshtastic mode hides it.
2. Selecting `Discover` switches to Discover mode and shows exactly 4 action rows in list column.
3. `Scan Local` sends discover request and summary count is shown after timeout.
4. `Send ID Local` and `Send ID Broadcast` both produce clear success/failure feedback.
5. New nodes from discover responses appear in Nearby list under MeshCore protocol.
6. No regression to existing Contacts/Nearby/Broadcast/Team flows.
