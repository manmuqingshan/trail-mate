# A. Overview of all pages (complete coverage by status)

> Device: 2.33-inch horizontal screen, resolution 222 x 480
> Constraints: Fixed TopBar (Back / Title / Battery)
> Principle: Priority is given to `ESP-NOW` for close teaming, and does not rely on LoRa / NFC; pairing is only enabled on the corresponding page to avoid accidental touches and continuous power consumption.

---

## A0. Global UI skeleton (reuse)

```text
+------------------------------------------------+
| < Back          [ TITLE / CONTEXT ]        Bat |
+------------------------------------------------+
|                                                |
|                 CONTENT AREA                   |
|                                                |
+------------------------------------------------+
| [ Action 1 ]          [ Action 2 ]             |
+------------------------------------------------+
```

---

## A1. Team status entry page (not added)

**Title:`Team`**

- Main copy: You are not in a team
- Description:
  - No shared map
  - No team awareness
- Main action:
  - `Create (ESP-NOW)`
  - `Join (ESP-NOW)`

---

## A2. Team status entry page (joined)

**Title:`Team Status`**

- Display fields:
  - Team name / Team ID
  - Role
  - Members / Online
  - Security / Epoch
  - Sync / Last event
- Team Health:
  - Leader online
  - Last update age
  - stale member count
- Main action:
  - `View Team`
  - `Pair Member`
  - `Leave`

This is an overview page to determine "whether the team is healthy and synchronized" and does not assume complex management responsibilities.

---

## A3. Team Home (Members and Structure)

**Title: `Team / Leader` or `Team / Member`**

- Display fields:
  - Team / ID
  - Members / Online
  - Epoch
  - Sync status
- List content:
 - Member name
 - Online status
 - Last online time
- Main action:
  - `Pair Member`
 - `Manage` (visible to Leader)
  - `Leave`

Note: In outdoor scenes, users more often stay on the map page or chat page, so the key status cannot only rely on pop-up prompts.

---

## A3b. Pairing(ESP-NOW)

**Title:`Pairing`**

- Pairing window validity period: 120 seconds
- Leader side:
 - Broadcast joinable status
 - Accept joining request
 - Deliver keys and initial snapshots
- Member side:
 - Scan beacon
 - Send join request
 - Wait for Key Distribution
- UI status:
  - `Scanning`
  - `Join sent`
  - `Waiting for keys`
  - `Completed`
  - `Failed`
- Action:
  - `Cancel`
  - `Retry`

---

## A7. Members management page (Leader)

**Title:`Members`**

- List content:
  - You (Leader)
 - Ordinary members
 - Each member has `Select`
- Purpose: Enter the details of a single member, initiate a kick or transfer the leader.

---

## A8. Member Detail(Leader)

**Title:`Member: <name>`**

- Display:
  - Status
  - Role
  - Device
 - Capability (Position / Waypoint, etc.)
- Action:
  - `Kick`
  - `Transfer Leader`

---

## A9. Kick confirmation page

**Title:`Kick Member`**

- copywriting:
  - Remove `<member>` from team?
  - This will update the security round (`epoch`).
  - The removed member will no longer receive team updates.
- Action:
  - `Cancel`
  - `Confirm Kick`

---

## A9b. Leave confirmation page

**Title:`Leave team?`**

- copywriting:
  - This clears local keys.
- Action:
  - `Cancel`
  - `Leave`

`Leave` requires a second confirmation to avoid accidentally triggering the local key to be cleared.

---

## A10. Access Lost (Member: removed/out of sync/abnormal)

**Title:`Team`**

- Status: Access lost
- Reason:
  - `Revoked`
  - `Out-of-sync`
  - `Unknown`
- Description:
 - Revoked: Removed by Leader
 - Out-of-sync: The team has been updated and this machine needs to be synchronized
- Action:
  - `Try Sync`
  - `Join Another Team`
  - `OK`

Key point: It is necessary to clearly distinguish between "kicked out" and "epoch inconsistent" to reduce misjudgments.

---

# B. Page flow description (UI state machine)

## B1. Top-level flow

```text
[ Team Menu ]
     |
     v
[ Team Status ]
     |
     +--> (not in team) --> [ Create / Join ] -> [ Team Status (joined) ]
     |
     +--> (joined) -------> [ View Team ] -> [ Team Home ]
```

---

## B2. Member joining process (ESP-NOW)

```text
[ Team Status (not in team) ]
        |
        v
     [ Pairing ]
        |
        +-- scanning -> join sent -> waiting key -> [ Team Status (joined) ]
        |
        +-- timeout / cancel --------------------> [ Team Status (not in team) ]
```

---

## B3. Leader pairing process (ESP-NOW)

```text
[ Team Status (leader) ]
     |
     v
  [ Pairing ]
     |
     +-- member joins -> send keys -> [ Team Status ]
     |
     +-- timeout / cancel ----------> [ Team Status ]
```

---

## B4. Kicking process (Leader)

```text
[ Team Home ]
     |
     v
[ Members ]
     |
     v
[ Member Detail ]
     |
     v
[ Kick Confirm ]
     |
     +-- confirm --> epoch rotate --> [ Team Status ]
```

---

# C. Involved protocols (Pager Team Core v0.1)

## C1. Main message types

| Type | Description |
| --- | --- |
| `TEAM_KEY_DIST` | Distribute team keys via `ESP-NOW` |
| `TEAM_KICK` | Remove members |
| `TEAM_TRANSFER_LEADER` | Transfer captain |
| `TEAM_STATUS` | Team status broadcast |
| `TEAM_POS` | Member position synchronization |
| `TEAM_WAYPOINT` | Team waypoint |
| `TEAM_TRACK` | Track data |
| `TEAM_CHAT` | Team Chat |

`v0.2` will consider extending some messages to LoRa, and `v0.1` will first run through Join Handshake and local status closed loop.

---

## C2. Field naming convention: `epoch` / `event_seq` / `msg_id`

- `epoch`: key round, used to select the currently valid key.
- `event_seq`: key event sequence number, only used for key event synchronization, monotonically increasing.
- `msg_id`: Common message deduplication identifier, optional; `v0.1` can be omitted now.

---

## C3. `TeamEnvelope`

All Team messages are uniformly packaged in an outer structure:

```text
TeamEnvelope {
  team_id
  epoch
  type
  sender_id
  timestamp
  msg_id?      // optional
  auth         // AEAD tag or MAC
  payload
}
```

Explanation:
- `event_seq` only appears when key events or synchronization carry key events.
- Presence / Position messages do not require continuous `seq`.

---

## C4. Key Events (source of fact written to `events.log`)

`v0.1` Key events that must be recorded:

- `TeamCreated(event_seq=1)`
- `MemberAccepted(event_seq++)`
- `MemberKicked(event_seq++)`
- `LeaderTransferred(event_seq++)`
- `EpochRotated(event_seq++)`

`Key Events` are the basis for synchronization and recovery; Presence / Position / Chat are not key events.

---

# D. Correspondence between protocol and UI

## D1. Create(ESP-NOW)

- Locally generate `team_id`
- Initialize `epoch = 1`
- Write local snapshot
- Enter `Team Status`

## D2. Join(ESP-NOW)

- Scan Leader broadcast
- Send Join request
- Wait for `TEAM_KEY_DIST`
- Write initial snapshot and key
- Enter `Team Status`

## D3. Kick / Leave / Transfer Leader

- These are key events
- Must update `event_seq`
- Rotate `epoch` if necessary
- UI success status returns to `Team Status`
