# What exactly is your TEAM business doing (I can now summarize it to the extent of "writable TeamEvent")

## 1) Definition of Team (one sentence at the product level)

**Team = an outdoor action unit that is temporarily formed, goal-driven, and disbanded at any time. **

It is not a "group chat" or "ordinary location sharing", but allows a small group of people to continuously answer three things in a non-network environment:

1. **Are we still together** (whether the team is scattered/left behind/astray)
2. **What to do next** ("action consensus" such as gathering, waiting, asking for help, etc.)
3. **Is anyone having problems** (stationary, lost contact, low battery, SOS)

---

## 2) Team's core capabilities (which you have determined)

### A. Temporary team formation (Create / Join / Disband)

* Leader creates a team with one click
* Members pass short code/QR Get **TeamKey** and join
* Team can be disbanded at any time (dissolution means the key is destroyed)

**You have made this set into a state machine: Create → Active → Rotate → Disband**

---

### B. Strong privacy location sharing (your key to differentiation)

You clearly care about:

> **After the location is broadcast, only Team members can parse it successfully; even if the Team channel is configured to be transmitted, it cannot be understood. **

So you use two layers of protection:

* L1: Meshtastic **Channel** encryption (CH-ENC)
* L2: Application layer **E2EE** (TeamKey-derived PosKey/WpKey, TeamEncrypted Envelope)

And you tend to add your own PortNum:

* `TEAM_POSITION_APP`
* `TEAM_WAYPOINT_APP`
* `TEAM_TRACK_APP`
* Management class `TEAM_MGMT_APP`

---

### C. "Action command" within Team (Preset / Command)

The difference you proposed:

* Send **default commands** to teammates (gather, wait, ask for help...)
* The command carries necessary parameters (such as the meeting point waypoint)
* The command is an "event" and does not require typing to chat

> Note: You did not ask me to make it a complete agreement, but you have made it clear that this is an important business capability of Team.

---

### D. Team life cycle review (what you want to do now)

You want to review the entire team process, including the implementation of all events in the life cycle.

So we introduce:

* SD log (append-only)
* LogRecord(AEAD + CRC)
* snapshot acceleration (optional)
* LogKey (independent of TeamKey, configurable whether to retain)

---

## 3) "What events will occur" in the Team life cycle (this directly determines TeamEvent)

In order to write `TeamEvent` protobuf, you need to admit first:
 **TeamEvent is not equal to "all wireless packets"**, but **what happened in the business layer**, which can be used to review and explain behavior.

Your current business can be summarized into 5 types of events:

### 1) Lifecycle event (Lifecycle)

* Team is created (Leader local)
* Broadcast discoverable (ADVERTISE)
* Someone requests to join (JOIN_REQUEST)
* Leader accepts and issues the configuration (JOIN_ACCEPT)
* Member confirmation to join the team (JOIN_CONFIRM)
* Team status synchronization (TEAM_STATUS)
* Key rotation (KEY_ROTATE)
* Dismiss/leave (DISBAND / LEAVE)

These events are used to answer:

* "When does the team start/end"
* "Who is in the team and when did they join/leave"
* "When did the key change and why?"

---

### 2) Position and trajectory events (Telemetry)

* Receive a member's position (POSITION_RX)
* Receive waypoint/rendezvous point (WAYPOINT_RX)
* Receive track batch points (TRACK_RX)

These events are used for review:

* When and where people have been
* Whether the team dispersed and when they reunited

> Location events will have a downsampling strategy, otherwise the log will explode.

---

### 3) Command event (Command)

* Collection command (including location waypoint)
* Wait/Pause
* Ask for help (SOS, including location/severity level)

These events are used for review:

* "Who issued the action consensus"
* "How did the team respond"

---

### 4) Connection and visibility events (Presence/Health)

You clearly need this type in the product goal, but we have not refined it into protocol fields before, now we summarize it:

* A member is "temporarily invisible" (timeout and no location received)
* Restore visibility
* Low battery (if you decide to report it)
* Staying still for too long (if you decide to do native inference events)

These events are used for review:

* "Why was there no one on the map at that time?"
* "Is it because the device was out of power/lost contact?"

> This type of event can be "local inference" and does not necessarily come from wireless packets.

---

### 5) Security/Diagnostics

If you want to make a strong privacy commitment, you must be able to review the "reasons for failure", otherwise users will only feel bugs.

* Decryption failed (DECRYPT_FAIL)
* key_id mismatch (KEY_MISMATCH)
* Replay discard (REPLAY_DROP)
* Packet format error (DECODE_FAIL)

These events are used for review:

* Reason for "the package was received but not displayed"

---

## 4) I now summarize the boundaries of your TEAM business (I am sure vs I won't assume without permission)

### What I can confirm (from what you clearly stated and repeatedly confirmed)

* Team is a temporary action unit with Create/Join/Disband
* Location sharing within Team + waypoint
* Strong privacy: channel isolation + application layer E2EE
* The entire life cycle events need to be recorded and can be reviewed
* Are you willing to add a custom PortNum (TEAM_*_APP)

### I will not make up for it without authorization (you are not frozen)

* What are the types of commands and what parameters are included in each command (I can make suggestions, but you need to confirm)
* Whether member health fields need to be reported (battery, ability, role)
* Whether to require "unanimous review of the whole team" (you are currently more like a local review/Leader review)

---

# Team establishment/joining message interaction process (strong privacy version)

## Participants

```
L = Leader / team builder device
M = Member / team member device
O = Others / spectator device (non-Team member)
```

## Channel

```
CH0 = Default public channel (Primary Channel)
CHT = Team private channel (new/assigned channel) index)
```

## Security layer

```
CH-ENC = Meshtastic per-Channel link layer encryption
E2EE = TeamKey-derived application layer end-to-end encryption
         (Envelope: TeamEncrypted)
```

---

## Overview: Timing line diagram (ASCII)

```
Time ↓

L (Leader)                  M (Member)                   O (Others)
────────────────────────────────────────────────────────────────────────

(0) Local team building (no message)
L: Generate TeamKey
L: TeamId = Trunc(Hash(TeamKey))
L: Select/Create CHT
L: ChannelPSK = KDF(TeamKey, "channel-psk")
L: MgmtKey    = KDF(TeamKey, "mgmt")
L: PosKey     = KDF(TeamKey, "pos")
L: WpKey      = KDF(TeamKey, "wp")

────────────────────────────────────────────────────────────────────────

(1) Team can be "discovered" (without leaking secrets)
CH0
L  ─────── TEAM_ADVERTISE ───────▶  * 
      { team_id, join_hint?, channel_index?, nonce/ts }

 O: Can only know "there is a Team nearby"
 Unable to obtain any key or location

────────────────────────────────────────────────────────────────────────

(2) Team members request to join (without key)
CH0
M  ───── TEAM_JOIN_REQUEST ─────▶  L
      { team_id, member_pub?, nonce/ts }

────────────────────────────────────────────────────────────────────────

(3) Leader accepts joining and issues Team configuration
CH0
L  ───── TEAM_JOIN_ACCEPT ─────▶  M
      {
        team_id,
        payload = E2EE(MgmtKey, {
                     channel_index = CHT,
                     channel_psk   = ChannelPSK,
                     key_id        = current_key_id,
                     team_params?  (freq / precision / timeout)
                   }),
        nonce/ts
      }

Note:
- At this time M **must have obtained TeamKey** through UI**
 (short code / QR / short range method)
- Otherwise, MgmtKey / payload cannot be decrypted

────────────────────────────────────────────────────────────────────────

(4) Players switch to Team locally
M: Save TeamKey
M: Derive MgmtKey / PosKey / WpKey
M: Save CHT + ChannelPSK
M: Switch to CHT

────────────────────────────────────────────────────────────────────────

(5) Team confirmation (recommended)
CHT (CH-ENC)
M  ───── TEAM_JOIN_CONFIRM ─────▶  L
      {
        team_id,
        payload = E2EE(MgmtKey, {
                     ok,
                     capabilities?,
                     battery?
                   }),
        nonce/seq
      }

────────────────────────────────────────────────────────────────────────

(6) Team Status synchronization (optional)
CHT (CH-ENC)
L  ─────── TEAM_STATUS ───────▶  Team
      {
        team_id,
        payload = E2EE(MgmtKey, {
                     member_list_hash,
                     key_id,
                     team_params
                   }),
        nonce/seq
      }

────────────────────────────────────────────────────────────────────────

(7) Location sharing within Team (core)
CHT (CH-ENC)
Each Member ── TEAM_POSITION_APP ──▶ Team
      payload = TeamEncrypted {
                  team_id,
                  key_id,
                  nonce,
                  ciphertext = AEAD(
                      PosKey,
                      protobuf(meshtastic_Position),
                      aad = header
                  )
               }

External members of Team:
- Even if you get CHT + ChannelPSK
- You can only decrypt CH-ENC
- No TeamKey / PosKey → Unable to decrypt the location
```

---

## Key semantic explanation (very important)

### 1️⃣ Why **TEAM_ADVERTISE** does not contain a key

This is **"discover" not "join"**:

* Lets people know "there is a Team nearby"
* but does not grant any abilities
* Obtain permissions to prevent passive monitoring

---

### 2️⃣ Why **TeamKey does not spread through wireless clear text**

This is the core of your "Strong Privacy Commitment":

* TeamKey **Only propagates via UI side channels** (short code / QR / Proximity)
* Only **TeamKey derivatives** propagated over the air
* TeamKey cannot be reversed even if the channel is configured to outbound

---

### 3️⃣ Why divide **ChannelPSK / MgmtKey / PosKey**

This is an "evolvable design":

* ChannelPSK: Just care about "who can receive this package"
* MgmtKey: Manage member management/parameters
* PosKey/WpKey: Manage location/waypoint

👉 In the future you can do:

* Change the location key ≠ Kick someone
* Change management key ≠ Affect historical data

---

### 4️⃣ Why **TEAM_JOIN_CONFIRM**

Not for security, but for **product perceptibility**:

* Leader knows who has really successfully joined the team
* UI can display "Members are ready"
* Follow-up Team Only the state can be trusted

---

## MVP and enhancement item division (helping you control complexity)

### MVP is required (should be included in the first version)

* (0) Local team building + TeamKey derivation
* (1) TEAM_ADVERTISE
* TeamKey input/scan code on UI side
* (3) TEAM_JOIN_ACCEPT
* (4) Switch to CHT
* (7) TEAM_POSITION_APP(E2EE)

### Optional enhancements (to be added later)

* member_pub + stronger key exchange
* TEAM_JOIN_CONFIRM
* TEAM_STATUS
* key rotation / key_id update
* Anti-replay window optimization

---

# Team Management Protocol

## Message Definitions (Protobuf-Level)

---

## Public convention (applicable to all Team messages)

### Team Identity

* **TeamKey**

 * High entropy random generation
 * Only distributed through UI side (short code/QR/close range)
 * **MUST NOT** Transmission via wireless clear text

* **team_id**

 * Definition: `Trunc(Hash(TeamKey))`
 * Purpose: to identify Team without leaking TeamKey
 * **MUST** Appear in all Team-related packets

---

### Encryption level

* **CH-ENC**: Meshtastic Channel Encryption
* **E2EE**: Application Layer Team Encryption (AEAD)

---

### TeamEncrypted Envelope (generic)

> for all E2EE payloads (location/admin/status)

| Field | Type | Level | Description |
| ---------- | ------ | ---- | ------------ |
| version | uint32 | MUST | Envelope version |
| team_id    | bytes  | MUST | Team Identification |
| key_id | uint32 | MUST | Current key version |
| nonce | bytes | MUST | unique per package, for AEAD |
| ciphertext | bytes | MUST | AEAD encrypted data |
| aad_flags | uint32 | MAY | AAD type identifier |

---

## 1. TEAM_ADVERTISE

**PURPOSE**: Makes Team "discoverable", does not grant any abilities

* **Channel**: CH0
* **Encryption**: Can be plain text or CH0 encrypted
* **PortNum**:`TEAM_MGMT_APP`

### Field table

| Field | Type | Level | Description |
| ------------- | ------ | ---- | ---------------- |
| team_id | bytes | MUST | Team identification |
| join_hint | uint32 | MAY | Join prompt (requires confirmation/validity period, etc.) |
| channel_index | uint32 | MAY | Team Channel index |
| expires_at | uint64 | MAY | Ad expiration time |
| nonce | bytes | MUST | Anti-replay |

### Semantic rules

* **MUST NOT** Contain any key material
* **MUST NOT** Disclose location or membership information
* **MAY** Periodic broadcast

---

## 2. TEAM_JOIN_REQUEST

**Purpose**: Team members request to join Team (no key)

* **Channel**: CH0
* **PortNum**:`TEAM_MGMT_APP`

### Field table

| Field | Type | Level | Description |
| ------------ | ------ | ---- | ----------- |
| team_id | bytes | MUST | Target Team |
| member_pub | bytes | MAY | Public key (for enhanced key exchange) |
| capabilities | uint32 | MAY | Capability identifier |
| nonce | bytes | MUST | Anti-replay |

### Semantic rules

* **MUST NOT** Contains TeamKey
* **MAY** Rejected by Leader (no response)

---

## 3. TEAM_JOIN_ACCEPT

**Purpose**: Leader accepts members and delivers Team configuration

* **Channel**: CH0
* **PortNum**:`TEAM_MGMT_APP`
* **Encryption**: E2EE (MgmtKey)

### Payload (decrypted structure)

| Field | Type | Level | Description |
| ------------- | ---------- | ---- | ------------ |
| channel_index | uint32     | MUST | Team Channel |
| channel_psk   | bytes      | MUST | Channel PSK  |
| key_id | uint32 | MUST | Current key version |
| team_params | TeamParams | MAY | Behavior parameters |

### Outer field table

| Field | Type | Level | Description |
| ------- | ------------- | ---- | ------- |
| team_id | bytes | MUST | Team logo |
| payload | TeamEncrypted | MUST | E2EE packaging |
| nonce | bytes | MUST | Anti-replay |

### Semantic rules

* Recipient **MUST** already has TeamKey
* Decryption failed **MUST** Discard
* **MUST NOT** Apply configuration before TeamKey is confirmed

---

## 4. TEAM_JOIN_CONFIRM

**Purpose**: Member confirmation has successfully joined the team

* **Channel**: CHT
* **PortNum**:`TEAM_MGMT_APP`
* **Encryption**: CH-ENC + E2EE (MgmtKey)

### Payload (after decryption)

| Field | Type | Level | Description |
| ------------ | ------ | ---- | ----- |
| ok | bool | MUST | Joined successfully |
| capabilities | uint32 | MAY | capability statement |
| battery | uint32 | MAY | battery percentage |

---

## 5. TEAM_STATUS

**Purpose**: Synchronize the current status of Team

* **Channel**: CHT
* **PortNum**:`TEAM_MGMT_APP`
* **Encryption**: CH-ENC + E2EE (MgmtKey)

### Payload (after decryption)

| Fields | Type | Level | Description |
| ---------------- | ---------- | ---- | ------ |
| member_list_hash | bytes | MUST | Member list summary |
| key_id | uint32 | MUST | Current key version |
| team_params | TeamParams | MAY | Current parameters |

---

## 6. TEAM_POSITION_APP

**Purpose**: Position sharing within Team (strong privacy)

* **Channel**: CHT
* **PortNum**:`TEAM_POSITION_APP`
* **Encryption**: CH-ENC + E2EE (PosKey)

### Payload (E2EE plain text structure)

| Field | Type | Level | Description |
| --------------- | ------------------- | ---- | ---- |
| position | meshtastic_Position | MUST | Location Data |
| precision_level | uint32 | MAY | precision level |
| timestamp | uint64 | MUST | location time |

### Outer layer (TeamEncrypted)

| Field | Type | Level | Description |
| ---------- | ------ | ---- | ------- |
| team_id | bytes | MUST | Team logo |
| key_id | uint32 | MUST | location key version |
| nonce | bytes | MUST | unique per package |
| ciphertext | bytes | MUST | AEAD ciphertext |

---

## 7. TEAM_WAYPOINT_APP

**Purpose**: Team internal waypoint sharing

* Exactly the same as `TEAM_POSITION_APP`
* Use `WpKey` Decryption
* The plaintext structure is `meshtastic_Waypoint`

---

## 8. TEAM_TRACK_APP

**Purpose**: Track batch point sharing within Team (fixed interval)

* Exactly the same as `TEAM_POSITION_APP` (E2EE PosKey)
* The plain text structure is `TeamTrackMessage` (custom lightweight encoding)
* Track points do not have a single point timestamp, and the time is determined by `start_ts + i * interval_s` derivation
* `valid_mask` is used to mark whether each point is valid (it can be set to 0 when there is no fix)
* A single package can have up to 20 points
* **Sender suggestion**: Start a 10-minute sampling window after the team keys are ready; sample every 30 seconds, a total of 20 points; if all points in the window are invalid, no packet will be sent
* **Receiver placement**: `/team/<team_dir>/tracks/<member_id>.gpx` (GPX 1.1, incremental addition `<trkpt>`)

### Payload (E2EE plain text structure)

| Field | Type | Level | Description |
| ---------- | -------------------------------- | ---- | ---- |
| Version | uint8 | MUST | Version |
| start_ts | uint32 | MUST | Start time (epoch seconds) |
| interval_s | uint16 | MUST | Sampling interval (seconds) |
| count | uint8 | MUST | Points (<= 20) |
| valid_mask | uint32 | MUST | Point validity bitmap (bit i corresponds to the i-th point) |
| lon_e7 int32) * N | MUST | Latitude and longitude (E7) |

---

## 9. MUST / SHOULD / MAY Summary (Conformance Checklist)

### MUST

* TeamKey **must not** be transmitted in clear text over wireless
* All Team data **MUST** contain team_id
* All E2EE payloads **MUST** use AEAD
* nonce **MUST** be unique (anti-replay)
* When disbanding a Team **MUST** discard all derived keys

### SHOULD

* Use a separate PosKey / WpKey
* supports key_id rotation
* JOIN_CONFIRM / TEAM_STATUS for UI synchronization

### MAY

* Use member_pub for stronger key exchange
* Support permission classification
* Unify payload length to reduce association risk

---

## One-sentence protocol definition (can be written in the file header)

> **The Team protocol defines an ad hoc, goal-driven encrypted collaboration unit with a security perimeter centered on TeamKey rather than a wireless channel. **

---


## Goal: What do you want to promise to users

**Privacy commitment (you can write it in PRD / README):**

1. **Only Team members can decipher Team position and waypoint** (even if others get the channel configuration/PSK, they cannot decrypt it)
2. **Team Content cannot be recovered after disbandment** (key discarded)
3. **Outsiders cannot infer member locations from the package content** (ciphertext + randomization + anti-replay)

---

Okay, in this step we draw a clear picture of the **Team's life**.
 It is not a "functional flow chart", but a **state machine** - **when is it in what state, what event occurs due to the transition, and what must be done during the transition**.

You can put the **ASCII state machine diagram** below directly into the protocol document or PRD.

---

# Team life cycle state machine (State Machine)

```
                           ┌─────────────────────────┐
                           │         (Idle)          │
                           │     No Active Team      │
                           └───────────┬─────────────┘
                                       │
                          Create Team  │  (UI action)
                                       │
                                       ▼
┌─────────────────────────────────────────────────────────────┐
│                         CREATE                              │
│                                                             │
│  Entry actions (local only):                                │
│   - Generate TeamKey (high entropy)                         │
│   - Derive team_id = Trunc(Hash(TeamKey))                   │
│   - Allocate / select Team Channel (CHT)                    │
│   - Derive keys:                                            │
│       ChannelPSK = KDF(TeamKey, "channel-psk")              │
│       MgmtKey    = KDF(TeamKey, "mgmt")                     │
│       PosKey     = KDF(TeamKey, "pos")                      │
│       WpKey      = KDF(TeamKey, "wp")                       │
│                                                             │
│  Exit condition:                                            │
│   - Leader confirms creation                                │
└───────────┬─────────────────────────────────────────────────┘
            │
            │  TEAM_ADVERTISE (CH0)
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                         ACTIVE                              │
│                                                             │
│  Team exists and is operational                             │
│                                                             │
│  Ongoing behaviors:                                         │
│   - Broadcast TEAM_ADVERTISE (CH0, optional)                │
│   - Accept TEAM_JOIN_REQUEST (CH0)                          │
│   - Send TEAM_JOIN_ACCEPT (CH0, E2EE MgmtKey)               │
│   - Operate on Team Channel (CHT):                          │
│       * TEAM_JOIN_CONFIRM                                   │
│       * TEAM_STATUS                                         │
│       * TEAM_POSITION_APP (E2EE PosKey)                     │
│       * TEAM_WAYPOINT_APP (E2EE WpKey)                      │
│       * TEAM_TRACK_APP (E2EE PosKey)                        │
│                                                             │
│  Valid transitions:                                         │
│   - Rotate keys                                             │
│   - Disband team                                            │
└───────────┬───────────────────────┬─────────────────────────┘
            │                       │
            │ Rotate Team Key       │ Disband Team
            │ (Leader action)       │ (Leader or policy)
            │                       │
            ▼                       ▼
┌─────────────────────────────────────────────────────────────┐
│                         ROTATE                              │
│                                                             │
│  Purpose:                                                    │
│   - Mitigate key leakage                                    │
│   - Exclude lost / untrusted members                        │
│                                                             │
│  Entry actions (Leader):                                    │
│   - Increment key_id                                        │
│   - Generate new subkeys:                                   │
│       MgmtKey', PosKey', WpKey'                              │
│                                                             │
│  Protocol actions:                                          │
│   - Broadcast key update via TEAM_STATUS (E2EE MgmtKey)     │
│   - Optionally re-issue TEAM_JOIN_ACCEPT to valid members   │
│                                                             │
│  Member behavior:                                           │
│   - Switch to new key_id                                    │
│   - Drop packets with old key_id                            │
│                                                             │
│  Exit condition:                                            │
│   - All active members synced OR timeout                    │
└───────────┬─────────────────────────────────────────────────┘
            │
            │ Rotation complete
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                         ACTIVE                              │
│                   (with new key_id)                         │
└─────────────────────────────────────────────────────────────┘


            (from ACTIVE or ROTATE)
            │
            │ Disband Team
            │
            ▼
┌─────────────────────────────────────────────────────────────┐
│                        DISBAND                              │
│                                                             │
│  Entry actions (Leader):                                    │
│   - Broadcast TEAM_END / final TEAM_STATUS (optional)       │
│                                                             │
│  Mandatory local actions (ALL members):                     │
│   - Immediately discard:                                    │
│       * TeamKey                                             │
│       * ChannelPSK                                          │
│       * MgmtKey / PosKey / WpKey                             │
│   - Stop all Team broadcasts                                │
│   - Leave Team Channel (CHT)                                │
│                                                             │
│  Exit condition:                                            │
│   - Return to Idle                                          │
└───────────┬─────────────────────────────────────────────────┘
            │
            │
            ▼
┌─────────────────────────┐
│         (Idle)          │
│     No Active Team      │
└─────────────────────────┘
```

---

## State semantic description (this is more important than the diagram itself)

### 1️⃣ Idle

* The device** does not hold any TeamKey**
* Does not broadcast the Team location
* The UI displays "Not Team Created/Not Joined"

---

### 2️⃣ Create

* **Purely local state**
* No wireless security risks
* All "roots of trust" are generated at this moment

> This is the birth point of **Team security boundary**

---

### 3️⃣ Active

* Team's **Main life cycle**
* All Team functions (maps, locations, commands) are only legal in this state
* **TeamKey is the only source of trust**

---

### 4️⃣ Rotate

* **It is not an abnormal state, it is an active defense state**
* Used to fulfill your words to users:

 > "Even if external transmission is configured, the loss can be stopped immediately"

Key points:

* Rotate **Do not change team_id**
* Only change `key_id` + derived key
* The old key becomes invalid immediately

---

### 5️⃣ Disband

* **Safe termination state**
* All keys must be destroyed
* "Restore", "Review" and "Reconnect" are not allowed

> Team End ≠ Pause
> Team End = **Cryptographic Death**

---

## You can directly write the MUST rules of the protocol (from the state machine)

* **MUST** Only send Team data in the `ACTIVE` / `ROTATE` state
* **MUST** Destroy all Team keys on `DISBAND` entry
* **MUST** Drop packets with mismatched key_id
* **MUST NOT** Return from `DISBAND` to `ACTIVE` (the team must be re-established)

---

## Why this state machine "stands"

* **Product layer**: Each state can be mapped to a clear UI
* **Protocol layer**: Each transition has a clear message or local action
* **Security layer**: The key life cycle is completely consistent with the state life cycle

---

Very good. This step is actually to align "what the user clicked" and "what happened in the protocol" one by one**.
 Below I will write strictly according to UI behavior → status change → message trigger** without introducing new concepts or ambiguity.

You can understand this content as:

> **UI is the control panel of the state machine, and protocol messages are the side effects of state transitions. **

---

# UI behavior ↔ Team state machine ↔ Message trigger precise mapping

---

## 1. UI overview: three "user-perceivable states" of the Team menu

From a UI perspective, Team actually has only three states (one-to-one correspondence with the protocol state):

| UI display status | Agreement status | What users can do |
| -------- | ------- | ------------ |
| Team has not been established | Idle | Create team / join |
| Team in progress | Active | View / share / disband |
| Disbanding | Disband | None (system action) |

> Rotate is **Active The sub-actions** within are usually Leader-only, not the normal user main process.

---

## 2. UI behavior 1: Click "Create Team"

### UI behavior

```
Menu → Team → [Create Team]
```

---

### Corresponding state machine transition

```
Idle ──(Create Team)──▶ Create ──▶ Active
```

---

### Message and action mapping (step by step)

#### Step 1: The user clicks "Create Team" (no message)

**UI Behavior**

* User confirms "I want to build a Team"

**Local Action (MUST)**

* Generate `TeamKey`
* Calculate `team_id = Trunc(Hash(TeamKey))`
* Assign/Select `CHT`
* Derived key:

  * `ChannelPSK`
  * `MgmtKey`
  * `PosKey`
  * `WpKey`

📌 **There is no wireless message at this time**
📌 **This is the starting point of the Team security boundary**

---

#### Step 2: UI displays "Team has been created/shared code"

**UI Behavior**

* Displays Team Code / QR
* Displays "Number of members: 1"

**Protocol action**

* **Enter Active state**
* Start periodic (or one-time) broadcast:

```
CH0 → TEAM_ADVERTISE
```

```text
TEAM_ADVERTISE {
  team_id,
  join_hint?,
  channel_index?,
  nonce
}
```

📌 This is the only protocol basis for **UI to display "Team already exists"**

---

#### Step 3: UI returns to Team main page (Active)

**UI Behavior**

* Display:

  * Team Active
  * Members: 1
  * [View Team Map]
  * [Share Team]
  * [Disband Team]

**Protocol Behavior**

* Allow and process the following messages:

  * `TEAM_JOIN_REQUEST`
  * `TEAM_JOIN_ACCEPT`
  * `TEAM_POSITION_APP`

---

## 3. UI Behavior 2: Others join Team (Join Team)

### UI Behavior (Team Team)

```
Team → Join Team → Enter the short code / scan the code → Confirm
```

---

### Message and status mapping

#### Step 1: Team members enter short code/scan code (no message)

**UI Behavior**

* User confirms joining

**Local Action (MUST)**

* Parse/obtain `TeamKey`
* Precompute `team_id`
* Derive `MgmtKey / PosKey / WpKey`

📌 **Without TeamKey, any subsequent JOIN messages are meaningless**

---

#### Step 2: Team members request to join

```
CH0 → TEAM_JOIN_REQUEST
```

```text
TEAM_JOIN_REQUEST {
  team_id,
  nonce
}
```

---

#### Step 3: "Join request" appears in Leader UI

**UI behavior (Leader)**

* Display "New member requesting to join"
* [Accept] / [Ignore]

📌 UI event **Direct binding** Whether to send the next message

---

#### Step 4: Leader clicks "Accept"

```
CH0 → TEAM_JOIN_ACCEPT
```

```text
TEAM_JOIN_ACCEPT {
  team_id,
  payload = E2EE(MgmtKey, {
    channel_index,
    channel_psk,
    key_id,
    team_params?
  }),
  nonce
}
```

---

#### Step 5: Team UI switches to "Team Active"

**UI Behavior**

* Display Team Main page
* Members ≥ 2

**Local action**

* Save CHT + ChannelPSK
* Switch to CHT

**Protocol action (recommended)**

```
CHT → TEAM_JOIN_CONFIRM
```

---

## 4. UI behavior 3: Team in progress (no button, but continuous behavior)

### UI behavior

```
Team Active → View Team Map
```

---

### Protocol behavior (persistent)

* Each member periodically sends:

```
CHT → TEAM_POSITION_APP
```

* UI map refresh **Only relies on successfully decrypted payload**
* Decrypted failed packets:

 * MUST be discarded
 * MUST NOT be updated UI

---

## 5. UI Behavior 4: Click "Disband Team" [Leader]

### UI behavior

```
Team Active → [Disband Team] → Confirm
```

---

### Corresponding state machine transition

```
Active ──(Disband)──▶ Disband ──▶ Idle
```

---

### Message and action mapping

#### Step 1: Leader clicks "Disband"

**UI Behavior**

* Display a strong confirmation pop-up window

---

#### Step 2: Leader confirms disbandment

**Protocol action (optional but recommended)**

```
CHT → TEAM_STATUS / TEAM_END
```

```text
TEAM_STATUS {
  team_id,
  payload = E2EE(MgmtKey, {
    state = DISBAND
  })
}
```

📌 This is **notification nature**, not security critical
📌 Even if it is not received, it does not affect security

---

#### Step 3: All devices enter Disband

**Local action (MUST, all members)**

* Destroy immediately:

  * `TeamKey`
  * `ChannelPSK`
  * `MgmtKey / PosKey / WpKey`
* Stop all Team messages
* Leave CHT

---

#### Step 4: UI returns to the unestablished team state

```
Team → You are not in a Team
[Create Team] [Join Team]
```

📌 **UI It is the "visual confirmation" that the key destruction is completed**

---

## 6. UI Behavior 5: Ordinary members "leave Team (Leave)" [non-disbanded]

> This is a typical example of **UI behavior ≠ protocol status change**

### UI behavior

```
Team Active → [Leave Team]
```

---

### Local action (MUST)

* Destroy all TeamKey / derived keys
* Leave CHT

### Protocol action (MAY)

```
CHT → TEAM_LEAVE_NOTICE
```

> Only used for UI synchronization, does not affect Team survival

📌 **Team status is still Active (for others)**

---

## 7. Key design principles (can be written into PRD)

1. **UI Behavior always precedes messages**
 Messages are just "side effects of user intentions"

2. **No TeamKey → UI is not allowed to enter Active**
 Security failure must be expressed as "operation unreachable"

3. **Dismissal is a key behavior, not a notification behavior**
 Message failure ≠ Dissolution failure

---

## Summarize this mapping relationship in one sentence

> **UI Determine status,
> The status determines the key,
> The key determines which messages are "meaningful". **

---

### Next step (very natural)

If you are willing, I can help you do one of them directly in the next step:

1. **Write this set of mappings as a "UI × Protocol comparison table"** (convenient for testing and PR)
2. **Help you design the LVGL component tree of the Team page in different states**
3. **Convert these UI behaviors into automated test case descriptions**

At this step, you are already doing **system-level product design**, not "functional assembly".


Okay, we will do this step completely from the perspective of "embedded UI engineering".
The goal is not to "look good", but to have clear status, stable components, and not evolve out of control**.

What I give you below is the **LVGL Component Tree**, not the layout sketch;
You can directly disassemble `.cpp / .h` accordingly, or reconstruct it based on your current UI architecture.

---

# Team page · LVGL component tree design

(Clear branches by **status**, rather than piled together if/else conditions)

---

## Overall design principles (give you "architectural constraints" first)

### 1️⃣ Team page = State container (not functional container)

```text
TeamPage
└── TeamStateContainer // Only responsible for: Idle / Active / Disband
```

* **Always only mount one subtree**
* State switching = delete old subtree + create new subtree
* Not in the same tree hide/show

👉 This is the key to prevent the LVGL page from "turning into a mountain of shit"

---

### 2️⃣ The page is divided into three mutually exclusive trees

```text
TeamState = { IDLE, ACTIVE, DISBAND }
```

* `IdleTree`
* `ActiveTree`
* `DisbandTree` (transient, usually very short)

---

## 1. Team page root node (common to all states)

```text
TeamPage (lv_obj_t*)
├── TopBar
│   ├── BackButton
│   └── TitleLabel ("Team")
│
└── TeamStateContainer (lv_obj_t*)
    └── <StateSpecificTree>
```

Description:

* `TopBar` exists forever
* `TeamStateContainer` is the only variable area**

---

## 2. State 1: Unestablished Team (IDLE)

### User Mind

> "I am not in any Team now" "

---

### Component tree: IdleTree

```text
IdleTree (lv_obj_t*)
├── CenterContainer
│ ├── StatusIcon // Simple icon: empty team
│   ├── StatusLabel       // "You are not in a Team"
│   └── Spacer
│
├── ActionContainer
│ ├── CreateTeamButton // Primary button
│ └── JoinTeamButton // Secondary button
```

---

### Component Responsibility Description

* `CreateTeamButton`

 * Click → **Trigger UI behavior: Create Team**
 * Subsequent status switching is determined by the controller

* `JoinTeamButton`

 * Jump to the Join process page (enter code/scan code)
 * Switch to `ACTIVE` after success

📌 **IdleTree does not care about any protocol or key**

---

## 3. State 2: Team In progress (ACTIVE)

This is the **longest staying and most important state**.

### User Mind

> "I am in a Team, I can see the status and make decisions."

---

### Component tree: ActiveTree (complete)

```text
ActiveTree (lv_obj_t*)
├── SummaryCard
│   ├── TeamStatusLabel     // "Team Active"
│   ├── MemberCountLabel   // "Members: N"
│   └── PrivacyBadge       // "Strong Privacy / E2EE"
│
├── Divider
│
├── ActionList
│   ├── ViewMapItem        // → Team Map
│ ├── ShareTeamItem // → Display Team Code / QR
│   └── (optional) RotateKeyItem  // Leader only
│
├── Divider
│
└── DangerZone
    └── DisbandButton / LeaveButton
```

---

### Key: Leader / Member Branch (not different pages)

#### Ordinary members (Member)

```text
DangerZone
└── LeaveTeamButton
```

#### Leader (Team Builder)

```text
DangerZone
└── DisbandTeamButton
```

> ⚠️ **Do not expose the "Leader" concept at the UI layer**
> Only reflected by the presence or absence of buttons

---

### "Protocol trigger points" of each component

#### `ViewMapItem`

* Just do one thing: jump to Team Map Page
* Team Map page only consumes:

  * `TEAM_POSITION_APP`
  * `TEAM_WAYPOINT_APP`
  * `TEAM_TRACK_APP`

#### `ShareTeamItem`

* Enter **Share Subpage**
* Read-only data:

  * Team Code
  * QR
* ❌ Do not trigger any message

#### `RotateKeyItem`(Leader-only)

* Click → Enter the confirmation page
* After confirmation:

 * Trigger **Rotate status**
 * Send `TEAM_STATUS (new key_id)`

#### `DisbandTeamButton`

* Click → Enter **DisbandTree**

---

## 4. State 3: Disbanding (DISBAND)

This is a **transient state**, but very important.

### User Mind

> "This matter is irreversible."

---

### Component tree: DisbandTree

```text
DisbandTree (lv_obj_t*)
├── WarningIcon
├── WarningLabel
│   └── "Disbanding Team..."
│
└── ProgressIndicator
```

---

### Behavior binding (key)

* **While entering DisbandTree**

 * Local destruction immediately:

    * TeamKey
    * ChannelPSK
 * All derived keys
 * Stop Team messages

* **Do not wait for network results**

* UI dwell time is extremely short (e.g. 300–500ms)

---

### After Disband is completed

```text
DISBAND → IDLE
```

* Destroy `DisbandTree`
* Create `IdleTree`

---

## 5. State switching = component tree switching (recommended implementation mode)

### Recommended control logic (pseudocode)

```cpp
void TeamPage::setState(TeamState newState) {
    if (currentTree) {
        lv_obj_del(currentTree);
        currentTree = nullptr;
    }

    switch (newState) {
    case IDLE:
        currentTree = buildIdleTree(container);
        break;
    case ACTIVE:
        currentTree = buildActiveTree(container);
        break;
    case DISBAND:
        currentTree = buildDisbandTree(container);
        break;
    }
}
```

📌 **No show/hide**
📌 **No if (isLeader) scattered everywhere**
📌 **All state boundaries in one place**

---

## 6. Three "hard advantages" of this component tree design

1. **State and UI are completely isomorphic**
 State machine = UI structure, not comments

2. **Protocol failure will not pollute the UI**

 * Dismissal does not depend on message success
 * UI only reflects local key status

3. **Future expansion will not explode**

 * Add Team Settings
 * Adding Key Rotation
 * Adding Debug Info
 is just adding one to `ActiveTree`

---Your "pull back" action is very correct.
We are now **clearly doing only one thing**:

> **Revolving around the "team building state machine + key exchange process", what data must be persisted? Which ones must never be persisted? Which ones are status? **

Don't talk about UI, don't talk about maps, don't talk about future expansion, just talk about **data and life cycle**.

---

# Team building & key exchange phase

## Data persistence organization (strictly according to state machine)

---

## 1. First give the conclusion (general table)

> **Not everything involved in team building should be persistent**
> The only criterion for persistence is:
> 👉 *"Can the device continue to be in the same Team state after restarting?"*

### The data is divided into three categories:

| Category | Whether to persist | Reason |
| ---------- | ----- | ------------- |
| Team identity and key | ✅ Required | Otherwise, restart = automatic exit |
| Channel configuration | ✅ Required | Otherwise, Team packets cannot be received |
| Protocol transient state | ❌ Should not be | Can be reconstructed through messages |
| UI / Interactive state | ❌ Should not | Completely presentation layer |

---

## 2. State-by-state analysis by state machine

---

## State 0: Idle (not team established)

### Persistent data

**None. **

```text
(no Team-related persistent data)
```

### Principles

* In the Idle state **there is no trace of Team on the device**
* This is the starting point of the security and psychological boundaries

---

## State 1: Create (local team building, not yet Active)

> This is a **very short-lived state**
> only exists once in the UI In operation

### Is persistence required?

❌ **Not required**

### Reason

* Create is an **atomic operation**
* Either successfully enter Active
* or fail and return to Idle

👉 **Create phase failure = no trace left**

---

## State 2: Active (Team in progress)

This is the only core state that needs to be persisted.

---

### 1️⃣Team identity class (must be persisted)

```text
TeamIdentity
├── team_id          (bytes)
├── team_role        (enum: LEADER / MEMBER)
├── key_id           (uint32)
```

#### Description

* `team_id`: Index key for all Team data
* `team_role`: Influence UI and allowed actions
* `key_id`: Current key version (used for decryption judgment)

✅ **MUST persist**

---

### 2️⃣ Team key class (must be persisted and stored securely)

```text
TeamSecrets
├── team_key (bytes) // Root key
├── mgmt_key         (bytes)
├── pos_key          (bytes)
├── wp_key           (bytes)
```

#### Description

* Derived key **can be recalculated**, but not recommended
* Recalculation relies on KDF version consistency, high risk
* In practice, direct storage of derived results is more stable

✅ **MUST persist**
⚠️ **Safe storage (NVS / encrypted storage) must be used**

---

### 3️⃣ Channel configuration class (must be persisted)

```text
TeamChannelConfig
├── channel_index    (uint8 / uint32)
├── channel_psk      (bytes)
```

#### Description

* You must be able to re-listen to CHT after restarting
* Otherwise, "logically still exists" Team, physically unable to hear"

✅ **MUST persist**

---

### 4️⃣ Team behavior parameters (persistence recommended)

```text
TeamParams
├── position_interval_ms
├── position_precision_level
├── advertise_enabled
├── join_policy
```

#### Description

* These parameters are **determined by the Leader**
* The member side is just executed

🟡 **SHOULD persist**
 (it can run without saving, but the experience is inconsistent)

---

### 5️⃣ Things that should not be persisted (very important)

#### ❌ Protocol Transient Status

```text
DO NOT persist:
- JOIN_REQUEST sent but not acknowledged
- JOIN_CONFIRM status
-Last TEAM_STATUS content
- Member list (can be reconstructed via broadcast)
```

Reason:

* All are **soft state**
* Natural recovery after power outage/restart

---

#### ❌ UI status

```text
DO NOT persist:
- Whether you are currently on the Team page
- Whether to expand a submenu
- Whether the QR has just been displayed
```

---

## State 3: Rotate (key rotating)

> Rotate is a sub-state of **Active**

### Persistence strategy: **Two-phase commit**

#### Temporary state (not persisted)

```text
RotateContext (RAM only)
├── new_key_id
├── new_mgmt_key
├── new_pos_key
├── new_wp_key
```

#### The moment the switch is successful (persistence)

```text
TeamSecrets (overwrite)
├── key_id = new_key_id
├── mgmt_key = new_mgmt_key
├── pos_key  = new_pos_key
├── wp_key   = new_wp_key
```

📌 **Do not persist "in rotation"**
📌 **Either overwrite successfully or keep the old key**

This is the key to avoid "half-rotation" caused by power outage/abnormality**.

---

## State 4: Disband (disband)

### Behavior rules (mandatory)

> **Disband = data destruction**

### Persistence actions that must be performed

```text
DELETE persistent:
- TeamIdentity
- TeamSecrets
- TeamChannelConfig
- TeamParams
```

### Disallowed Behavior

* ❌ Not allowed to mark "disbanded but recoverable"
* ❌ Not allowed to retain team_id
* ❌ Not allowed to cache old keys

📌 **Disband is cryptographically dead**

---

## 3. Persistence timing in the Join process (accurate)

### Member device (Member)

| Time | Whether to persist | Content |
| -------------------- | ----- | -------------------------------- |
| Enter short code | ❌ | (RAM only) |
| Send JOIN_REQUEST | ❌ | |
| Receive JOIN_ACCEPT and decrypt successfully | ✅ | TeamIdentity + Secrets + Channel |
| Switched to CHT successfully | ✅ | Same as above |
| JOIN_CONFIRM         | ❌     |                                  |

---

### Leader

| Time | Whether to persist | Content |
| ----------- | ----- | ---------- |
| Create Team | ✅ | Full Team data |
| Accept new members | ❌ | |
| Rotate successful | ✅ | Overwrite key       |
| Disband | ❌(delete) | |

---

## 4. A one-sentence rule that you can write directly into the protocol/implementation document

> **Only "data that defines Team identity continuity" is allowed to be persisted;
> All message-level and interaction-level states must be discardable. **

---

## 5. A very practical engineering suggestion (important)

**Make Team persistence into a "single object"**

```text
PersistentTeamState
├── identity
├── secrets
├── channel
├── params
```

* Have this object → Team = Active
* There is no such object → Team = Idle

📌 **Do not split into multiple KV**
📌 **Do not use flag to indicate "whether you are in Team"**

---

# 1️⃣ Team building process (Leader)

## Persistent read and write sequence diagram

```
Time ↓

Leader Device
──────────────────────────────────────────────────────────────

[UI] Click "Create Team"
        |
        v
📦 RAM: generate TeamKey
📦 RAM: derive team_id, ChannelPSK, MgmtKey, PosKey, WpKey
📦 RAM: select Team Channel (CHT)

        |
        |  (still NOT persistent)
        |
        v
[UI] Confirm Create
        |
        v
💾 PERSIST: write PersistentTeamState
    ├── identity.team_id
    ├── identity.role = LEADER
    ├── identity.key_id = 0
    ├── secrets.team_key
    ├── secrets.mgmt_key
    ├── secrets.pos_key
    ├── secrets.wp_key
    ├── channel.channel_index = CHT
    ├── channel.channel_psk
    └── params (optional)

        |
        v
[STATE] Team = ACTIVE
        |
        v
[PROTO] start TEAM_ADVERTISE (CH0)
[PROTO] accept JOIN_REQUEST
[PROTO] send JOIN_ACCEPT
```

### Key rules (must be followed)

* ❌ **Never write Flash before UI Confirm**
* ✅ **First persistence = Team officially exists**
* 💡 Power off before Confirm → Automatically return to Idle (no residue)

---

# 2️⃣ Join the process (Member)

## Persistent read and write sequence diagram (the most likely place for bugs)

```
Time ↓

Member Device
──────────────────────────────────────────────────────────────

[UI] Click "Join Team"
        |
        v
[UI] Input Code / Scan QR
        |
        v
📦 RAM: obtain TeamKey
📦 RAM: derive team_id, MgmtKey, PosKey, WpKey

        |
        |  (still NOT persistent)
        |
        v
[PROTO] send TEAM_JOIN_REQUEST (CH0)
        |
        v
[PROTO] receive TEAM_JOIN_ACCEPT (CH0)

        |
        v
[SEC] try decrypt JOIN_ACCEPT payload
        |
        |-- decryption FAIL --> ❌ abort (NO write)
        |
        v
📦 RAM: extract channel_index, channel_psk, key_id

        |
        |  (still NOT persistent)
        |
        v
[PROTO] switch to Team Channel (CHT)
        |
        |-- switch FAIL --> ❌ abort (NO write)
        |
        v
💾 PERSIST: write PersistentTeamState
    ├── identity.team_id
    ├── identity.role = MEMBER
    ├── identity.key_id
    ├── secrets.team_key
    ├── secrets.mgmt_key
    ├── secrets.pos_key
    ├── secrets.wp_key
    ├── channel.channel_index
    ├── channel.channel_psk
    └── params (optional)

        |
        v
[STATE] Team = ACTIVE
        |
        v
[PROTO] send TEAM_JOIN_CONFIRM (CHT)
[PROTO] start TEAM_POSITION_APP
```

### Key rules (this is the key point)

* ❌ **JOIN_ACCEPT received ≠ can be written to Flash**
* ❌ **Decryption successful ≠ can be written to Flash**
* ✅ **Only after "Successful switch to Persistence is only allowed after CHT"**

> This is to prevent:
> **"Team is recorded in Flash, but the wireless layer cannot enter the Team Channel at all"**

---

# 3️⃣ Disband process (Disband)

## Persistent deletion sequence diagram (the safest one)

### 3A. Leader disbands Team

```
Time ↓

Leader Device
──────────────────────────────────────────────────────────────

[UI] Click "Disband Team"
        |
        v
[UI] Confirm Disband
        |
        v
❌ DELETE: erase PersistentTeamState
    ├── identity
    ├── secrets
    ├── channel
    └── params

        |
        v
📦 RAM: clear all Team-related state
        |
        v
[STATE] Team = IDLE
        |
        v
[PROTO] (optional) send TEAM_STATUS / TEAM_END
```

### 3B. Ordinary members leave Team (Leave)

```
Time ↓

Member Device
──────────────────────────────────────────────────────────────

[UI] Click "Leave Team"
        |
        v
❌ DELETE: erase PersistentTeamState
        |
        v
📦 RAM: clear Team state
        |
        v
[STATE] Team = IDLE
        |
        v
[PROTO] (optional) send LEAVE_NOTICE
```

### Key rules (must be unified)

* ❌ **Don't send messages first and then delete data**
* ❌ **Don't wait for network ACK**
* ✅ **Delete persistence = Team will no longer exist immediately**

> Messages are "polite",
> **Deleting the key is the "fact". **

---

# 4️⃣ Rotate key (Rotate) supplement: persistence atomicity

```
Leader Device
──────────────────────────────────────────────────────────────

[UI] Click "Rotate Key"
        |
        v
📦 RAM: generate new subkeys
📦 RAM: new_key_id

        |
        v
[PROTO] broadcast TEAM_STATUS(new_key_id)

        |
        |-- timeout / abort --> ❌ discard RAM only
        |
        v
💾 PERSIST (ATOMIC overwrite):
    ├── identity.key_id = new_key_id
    ├── secrets.mgmt_key = new
    ├── secrets.pos_key  = new
    └── secrets.wp_key   = new

        |
        v
[STATE] Team still ACTIVE (new key)
```

### Key rules

* ❌ ** "Half-write" is not allowed**
* ❌ **Do not persist Rotate in-progress status**
* ✅ **Either replace it all or not**

---

# 5️⃣ An iron rule that can be written into the implementation specification

> **Persistence only occurs "after the state transition is completed",
> Deletion only occurs "when the state transition begins". **

---

# 6️⃣ Final project checklist (you can use it directly)

* [ ] There is no Flash writing before Create Confirm
* [ ] There is no Flash writing before Join successfully switches CHT
* [ ] Disband / Leave The first action is to delete persistence
* [ ] Rotate has only one atomic write point
* [ ] Is PersistentTeamState a "single object"

---

# Team persistent storage design

## NVS / Flash Layout & Wear Strategy

> Design goals:
>
> * Power-off safety
> * Atomic updates
> * Minimal erase
> * Clarify the criteria for "Team existence/non-existence"

---

## 1. General principles (establish the iron law first)

### 1️⃣ Team persistence = **Single object**

> **There is only one source of truth for "Does Team exist" in Flash**

```text
PersistentTeamState
```

* With it → Team = Active
* Without it → Team = Idle

❌ Do not use multiple flags
❌ Do not split in different namespaces

---

### 2️⃣ The number of writes is very small, it is "life cycle level"

| Operation | Writing Flash? |
| ----------- | -------- |
| Team establishment successful | ✅ Once |
| Member joining successfully | ✅ Once |
| Rotate Key | ✅ Once |
| Normal use (location/chat) | ❌ |
| Dismiss/leave | ✅ Delete |

> **Not high-frequency writing scenes**
> So the focus is **correctness > extreme performance**

---

## 2. Recommended storage method (ESP32 practice)

### ✅ Recommended: NVS (Non-Volatile Storage)

Reason:

* Comes with wear leveling
* Supports blob
* Support namespace
* Clear atomic semantics

> Your current needs **No need to customize raw flash**

---

## 3. NVS Namespace design

```text
NVS Namespace: "team"
```

**Only this namespace stores Team. **

---

## 4. Key layout (minimalist but complete)

### Core Key List

| Key | Type | Description |
| ------------ | ---- | ---------------------- |
| `version` | u32 | Structure version |
| `team_state` | blob | Entire PersistentTeamState |

> ❗**Do not split into dozens key**
> If it is dismantled, it will be difficult to ensure consistency and atomicity

---

## 5. PersistentTeamState structure (the only truth in Flash)

### Logical structure (consistent with your previous analysis)

```text
PersistentTeamState
├── header
│   ├── magic
│   ├── version
│   ├── length
│   └── crc32
│
├── identity
│   ├── team_id
│   ├── role          (LEADER / MEMBER)
│   └── key_id
│
├── secrets
│   ├── team_key
│   ├── mgmt_key
│   ├── pos_key
│   └── wp_key
│
├── channel
│   ├── channel_index
│   └── channel_psk
│
└── params (optional)
    ├── position_interval_ms
    ├── precision_level
    └── flags
```

---

### Header design (very important)

| Field | Function |
| --------- | ----------- |
| `magic` | Determine whether Team exists |
| `version` | Structure version |
| `length` | Anti-truncation |
| `crc32` | Power failure / half-write detection |

📌 **header is your last line of defense against power failure and damage**

---

## 6. Writing strategy (when to write, how to write)

### 1️⃣ Writing only occurs "after successful transition"

The rules you set before now become storage rules:

| Scenario | Write content |
| --------------- | ----------------------- |
| Create Confirm | Write complete PersistentTeamState |
| Join successfully switched CHT | Write complete PersistentTeamState |
| Rotate successfully | **Overwrite write** |
| Disband / Leave | Delete key |

---

### 2️⃣ Writing method: overall coverage (not incremental)

```text
nvs_set_blob("team", "team_state", &state, sizeof(state))
nvs_commit()
```

* Do not modify subfields
* Do not patch
* Do not maintain "rotating" status

📌 **Write overall + CRC = most stable**

---

## 7. Erase/Delete Strategy (Key)

### Disband/Leave Team

```text
nvs_erase_key("team", "team_state")
nvs_commit()
```

* **Deletion is a fact**
* Does not retain "Historical Team"
* Does not retain a recoverable path

---

### Why not use flag to indicate "disbanded"

Because:

* The flag itself is also a state
* flag needs to maintain consistency
* flag will introduce "Ghost Team"

**Does not exist = cleanest**

---

## 8. Power failure and damage recovery strategy

### Judgment logic at startup (the only entry)

```text
if (nvs_has_key("team", "team_state")) {
    read blob
    if (magic ok && crc ok) {
        Team = ACTIVE
    } else {
        erase key
        Team = IDLE
    }
} else {
    Team = IDLE
}
```

📌 **Any exception → Delete → Idle**

---

## 9. Key Rotation's atomicity guarantee

### Correct approach (you have already decided before)

1. New key **Only exists in RAM**
2. Broadcast Rotate
3. Synchronization completed
4. **One-time overwrite PersistentTeamState**

```text
old_state → new_state
nvs_set_blob(...)
nvs_commit()
```

❌ Do not write intermediate states
❌ Do not write `rotating = true`

---

## 10. Flash wear analysis (gives you confidence)

### Worst case estimation

| Operations | Times |
| ------ | ------- |
| Team building | 1 |
| Rotate | Assume 20 times |
| Disband | 1 |

**< 50 writes/Team life cycle**

NVS default wear leveling is enough to support **hundreds of thousands of times**.

👉 **You can rest assured**

---

## 11. A very important safety suggestion (strong)

### TeamSecrets storage

* If the platform supports:

 * Use **NVS encrypted partition**
* or:

 * Use device-unique key to wrap it again before writing

You don't need military grade, just:

> **Flash is dumped, TeamKey cannot be read directly**

---

## 12. Final "Implementation Checklist" (you can use it directly)

* [ ] Team only occupies one NVS key
* [ ] PersistentTeamState has magic + crc
* [ ] All writes are integral blob
* [ ] dissolve = erase key
* [ ] Startup exception = erase key
* [ ] Rotate has only one commit point

---

## Summarize this design in one sentence

> **Flash does not save "process",
> only saves "facts". **

---

## 1) Three levels of review (define product boundaries first)

You want to "implement all events throughout the life cycle", but the implementation can have different intensities:

### A. Local review (recommended default)

* **Each device only records the events it sees/sends**
* No need for all employees to report
* Minimum protocol changes
* The review may not be 100% complete (it depends on whether you received the package at that time)

### B. Complete review of the team (optional enhancement)

* The leader device tries to collect the events of the entire team (still "as much as possible", the wireless environment cannot guarantee 100%)
* Closer to an "action recorder"

### C. Consistent review of the whole team (the most important, not recommended at the beginning)

* Requires ACK/complement/reconciliation from the entire team
* Immediately increases system complexity

> I suggest: **Do A** first, then B (Leader mode), and leave C alone for now.

---

## 2) What do you want to record: event model (Event Sourcing style)

Understand the life of Team as: **event sequence + state reconstruction**.

### Meta information that events must contain (common to all events)

* `event_id`: unique (available `sender_id + seq`)
* `team_id`
* `ts`: event occurrence time (device local time + optional mesh time)
* `sender_id`: node ID
* `event_type`
* `payload`: carry fields by type
* `key_id`: the key version used at the time (very important, easy for re-display and decryption)

### Event type suggestions (covering your current system)

**Lifecycle**

* `TEAM_CREATED` (Leader local)
* `TEAM_ADVERTISE_SENT/RECEIVED`
* `TEAM_JOIN_REQUEST_SENT/RECEIVED`
* `TEAM_JOIN_ACCEPT_SENT/RECEIVED`
* `TEAM_JOIN_CONFIRM_SENT/RECEIVED`
* `TEAM_STATUS_SENT/RECEIVED`
* `TEAM_KEY_ROTATED` (including old/new key_id)
* `TEAM_DISBANDED` / `TEAM_LEFT`

**Action**

* `TEAM_POSITION_RX` (it is recommended to only save the "sampled" position points)
* `TEAM_WAYPOINT_RX`
* `TEAM_COMMAND_RX` (your future collection/help, etc. instructions)

**Exception**

* `DECRYPT_FAIL` (Important: can explain "why it was not displayed at the time" during review)
* `REPLAY_DROP`
* `MEMBER_LOST` / `MEMBER_RECOVERED`

---

## 3) Log storage location: A realistic choice between Flash vs SD

### If you have an SD card (strongly recommended)

* Use SD files to do **append log** (Flash wear and tear problem will be reduced directly)
* The file system is easy to use (export, view, synchronize)

### Only Flash (NVS/custom partition)

* can also be done, but you need to use **ring buffer (ring buffer)**
* Write amplification and wear must be carefully controlled
* Not suitable for storing "high-frequency position points"

> Conclusion: **Position trajectory review is almost bound to be SD**, otherwise you must extremely downsample.

---

## 4) The most important thing: How is the review compatible with "dissolution and destruction of keys"?

Your previous security model was: Disband Destroy TeamKey immediately → History is unreadable.
The review requirement: you can still read history after disbandment.

So you need to introduce an independent log key:

### New key: `LogKey`

* `LogKey = KDF(TeamKey, "log")` **Yes**, but if Disband will destroy the TeamKey, the LogKey will also be lost.
* More reasonable: **Generate an independent random LogKey** when creating a Team, and set the "whether to keep" policy.

### Two strategies (corresponding to product switches)

1. **Default: Disband destroys LogKey**

 * Cannot be restored
 * Strongest privacy commitment (default recommendation)

2. **Enable restoration: Disband retains LogKey (only local machine)**

 * Log files are still encrypted
 * Only this device can be restored
 * You can still commit: **TeamKey Destroy, network permission disappears; but the machine retains action records**

This is actually in line with the real needs of the outdoors:

* **Communication permission** ends when the team ends
* **Action records** can be retained by the individual/team leader

---

## 5) Writing strategy: How to be complete without exploding storage?

### The position point should not be "dropped in every package"

The position is the highest frequency data and must be downsampled:

* Rule 1: **Sampling by time** (for example, record once every 10s/30s)
* Rule 2: **Sampling by distance** (recorded only when moving more than 20m)
* Rule 3: Exception priority (temporary upscaling during SOS/assembly)

### Recommended structure of event log: Append-only + Checkpoint

For review speed and stability:

* `events.log`: additional writing (one record for each event)
* `snapshot.bin`: Occasionally write a "current Team status snapshot" (such as every 5 minutes or every 200 events)

 * During review: play back events from the latest snapshot, fast

---

## 6) Storage format suggestions (don't worry, choose one that can run)

You are already in protobuf In the system, the most smooth:

* **protobuf event** (an event message)
* The outer layer is sealed with a layer of `LogRecord`:

  * `len`(u16/u32)
  * `record_type`(event/snapshot)
  * `ciphertext`
 * `crc32` (optional)

 Log content is recommended to be encrypted with **AEAD(LogKey)** to ensure:

* Even if the landing file is copied, it will not be readable
* File tampering can be detected

---

## 7) What is the output of the review?

### Minimum available replication (MVP)

* Timeline: Join/Leave/Assemble/Ask for help/Disconnect/Restore
* Map playback: Display the trajectory according to time (no animation required, do "jump by time" first)

### Leader review enhancement

* Statistics: The maximum distance between the team, the number of members disconnected, and the assembly time
* Abnormalities: Who is stationary for a long time, who has low battery, when packet loss is serious

---

## 8) The most important implementation decision: Do you want to "replay the switch"?

I strongly recommend deciding when building a team:

* `Record mode: Off / Local / Leader`
* `Retention: 1 day / 7 days / manual delete`
* `Privacy: Keep logs encrypted (default)`

And when disbanding:

* If record off → delete log + destroy LogKey
* If record on → write `TEAM_DISBANDED` event + close logging, but keep LogKey (native)

---

## 9) You asked to "organize it": New data that needs to be persisted

On the basis of your previous `PersistentTeamState`, it is enough to add these items:

**When Record=ON:**

* `log_enabled`(bool)
* `log_mode`(enum: LOCAL / LEADER)
* `log_key` (bytes)✅ (independent of TeamKey)
* `log_file_id` / `log_path` (if SD)
* `log_seq` (for event increment numbering)
* `last_snapshot_offset` (optional)

**When Record=OFF:**

* Do not persist any log related content

---

# 1) SD file layout

## 1.1 Directory structure (bucketed by Team session)

It is recommended that each Team life cycle (Create→Disband) generate a **session_id** (random 64-bit or timestamp + random) as a session directory.

```
/trail-mate/
  /team_logs/
    /YYYYMMDD/
      /T_<session_id>/
        meta.json
        keys.bin
        events.log
        snapshot.idx
        snapshots.bin
```

### File Responsibilities

* `meta.json` (plain text, readable)

 * Only store non-sensitive information: creation time, device name, version number, recording mode, etc.
* `keys.bin` (encryption/protection)

 * Store the package of LogKey (see 3 for details)
* `events.log` (core appended write log)

 * Event stream (encrypted record)
* `snapshot.idx` (small index, convenient for quick positioning)

 * Each snapshot corresponds to the offset of `events.log` + the offset of `snapshots.bin`
* `snapshots.bin` (snapshot stream, append writing)

 * State snapshot for recovery acceleration (encrypted record)

> **MUST**: `events.log` and `snapshots.bin` They are all *append-only* (only append, no rewrite, no truncation).
> **SHOULD**: Divide into directories every day to facilitate cleaning/archiving.

---

# 2) LogRecord binary format (universal recording container)

`events.log` and `snapshots.bin` use the same record container: `LogRecord`.
Each record is: **fixed header + variable length ciphertext + optional CRC**.

## 2.1 LogRecord structure (Little-endian)

```
+-------------------------------+
| magic      (4)  "TLOG"        |
| version    (1)  = 1           |
| type       (1)  1=EVENT 2=SNAP|
| flags      (2)                |
| header_len (2)  bytes         |
| body_len   (4)  bytes         |
| session_id (8)                |
| seq         (8) monotonic     |
| ts_ms       (8) unix ms       |
| key_id      (4)               |
| nonce      (12)               |
| aad_crc32   (4)               |  (optional but recommended)
|--------------------------------
| AAD (header extension...)      |  (header_len - fixed_header bytes)
|--------------------------------
| ciphertext (body_len bytes)    |  AEAD output (may include tag)
|--------------------------------
| trailer_crc32 (4) optional     |
+-------------------------------+
```

### Fixed header field description

* `magic`: used for fast scanning and recovery
* `version`: format version
* `type`:EVENT / SNAP
* `flags`: bit flags (whether there is trailer_crc, compression, etc.)
* `header_len`: total header length (to facilitate future expansion of AAD)
* `body_len`: ciphertext length
* `session_id`: session binding (preventing mixed writing)
* `seq`: Monotonically increasing sequence number (power failure recovery, deduplication, alignment)
* `ts_ms`: Record writing time (or event occurrence time, depending on your definition)
* `key_id`: LogKey rotation version (optional but highly recommended)
* `nonce(12)`: AEAD nonce (unique for each record)
* `aad_crc32`: Calculate CRC for AAD (used to quickly determine whether the header is damaged)

> **MUST**: `seq` Monotonically increasing and not repeated (within the same session).
> **MUST**: `nonce` cannot be repeated under the same `key_id`.
> **SHOULD**: `header_len` allows carrying extended AAD (such as clear text fields of sender_id, event_type, etc.).

---

## 2.2 flags bit definition (recommended)

* bit0: `HAS_TRAILER_CRC` (with `trailer_crc32` at the end of the record)
* bit1: `BODY_COMPRESSED` (the plaintext inside the ciphertext is compressed before encryption)
* bit2: `TS_IS_EVENT_TIME` (ts_ms represents the event occurrence time, otherwise it represents the writing time)
* bit3: `RESERVED`

> It is recommended to turn on `HAS_TRAILER_CRC` by default. Even though AEAD can verify ciphertext integrity, CRC is still valuable for **quickly locating corruption/truncation**.

---

# 3) Encryption and key storage (LogKey)

## 3.1 Positioning of LogKey

* TeamKey is used for Team communication (may be destroyed during Disband)
* **LogKey is used for log encryption**, whether to retain it is determined by "Record Mode/Retention Policy"

> If you want to be "recoverable", you must **still obtain it after Disband LogKey** (at least on this machine).

## 3.2 keys.bin (recommended structure)

`keys.bin` Do not put LogKey in plain text. It is recommended to wrap it with a device unique key (such as ESP32 NVS encrypted partition / eFuse key / or your own device key).

`keys.bin` content suggestion is also a small `KeyRecord`:

```
magic "TKEY" (4)
version (1)
flags   (1)
reserved(2)
session_id (8)
key_id (4)
nonce (12)
ciphertext (var) = AEAD(DeviceKey, LogKeyMaterial)
crc32 (4)
```

 Among them `LogKeyMaterial` contains at least:

* `log_key` (32 bytes)
* `kdf_info`/algo id (optional)
* `created_ts` (optional)

> **MUST**: The device is not unblocked When `keys.bin` has the ability, it is not allowed to enter the review (to avoid false display).

---

# 4) What is the plain text in events.log (EVENT content)

It is recommended to use protobuf (or your own custom TLV) for the plain text of EVENT, and then encrypt it with AEAD.

### Plain text suggestion structure: `TeamEvent` (protobuf)

Contains:

* `event_type`
* `sender_id`
* `team_id`
* `payload` (by type oneof)
* `mesh_ts` (optional)
* `rx_rssi/snr` (optional)
* `seq_in_mesh` (optional)

> **SHOULD**: Put `event_type` in the AAD extension of LogRecord (plain text), so that you can do quick filtering/statistics without decryption; but it will leak the "event category" - if you care about privacy very much, don't do this, put everything in cipher text.

---

# 5) CRC strategy (the key to power-off and damage recovery)

You have three levels of integrity:

1. AEAD tag: ensure that the ciphertext cannot be tampered with (but cannot determine the "file truncation" position)
2. `aad_crc32`: quickly determine whether the header is damaged
3. `trailer_crc32`: Do CRC on the entire record (from magic to ciphertext) to facilitate recovery scanning

### trailer_crc32 calculation range (recommended)

Do CRC32 on `LogRecord` starting from `magic` to the end of `ciphertext` (excluding trailer_crc32 itself).

> **MUST**: If the CRC does not match when reading, the data after the record can be regarded as untrustworthy and the recovery scan mode will be entered.

---

# 6) Append writing strategy (append-only)

## 6.1 Writing process (each record)

1. Assemble plaintext event
2. Generate nonce
3. AEAD encryption to get ciphertext
4. Write LogRecord header + ciphertext
5. Write trailer_crc32 (if enabled)
6. `fsync/flush` (depends on your platform: at least during critical events/periodic flush)

### Flush strategy (SD practical suggestions)

* Ordinary events: flush once every N items or every T seconds (for example, 2s)
* Key events (TEAM_CREATED / DISBANDED / ROTATED / SOS): **flush immediately**

> **SHOULD**: implement a `LogWriter` has a small internal buffer; but it should not be so large that it will suffer too much loss due to power failure.

---

# 7) Snapshot strategy (snapshot + index)

If the review only relies on replay events, it will become slower and slower. Snapshots are meant to "start playback from a certain point."

## 7.1 Snapshot content

Snapshot text is recommended to be saved:

* Current `key_id`
* Member table (most recently visible member, last position)
* Current waypoint/assemble point
* Key parameters (sampling strategy)
* `last_event_seq` (the last event sequence number covered by the snapshot)

Then AEAD(LogKey) encrypts and writes to `snapshots.bin` (same as LogRecord format, type=SNAP).

## 7.2 When to write snapshots (recommendations)

* Every **N events** (for example, 200)
* or every **T seconds** (for example, 60s)
* or after key events (rotate/disband)

> **MUST**: Snapshot writes should not affect real-time communication and must be discardable (failure does not affect main functionality).

## 7.3 snapshot.idx (small index, plain text)

The index is a fixed-length record, appended to write, to quickly locate the latest snapshot:

```
IdxEntry (fixed 32 bytes):
- magic "SIDX" (4)  (optional per file header)
- version (1)
- reserved(3)
- session_id (8)
- snap_seq (8)          // snapshot record seq
- events_offset (8)     // events.log offset at snapshot time
- snaps_offset (8)      // snapshots.bin offset for this snapshot
```

> **SHOULD**: `snapshot.idx` can be plain text, because it only exposes offset; if you think "whether there is a snapshot/frequency" is also sensitive, you can put idx Also encrypted, but the engineering complexity will increase.

---

# 8) Recovery and replication process (self-healing after power failure)

## 8.1 Opening a session's replication steps

1. Read `meta.json` (optional)
2. Unpack `keys.bin` to get LogKey
3. Read `snapshot.idx` to find the last available snapshot (check snap record CRC/AEAD)
4. Restore state from snapshot
5. From Starting from `events_offset` of `events.log`, read records sequentially:

 * magic check
 * header/aad_crc32 check
 * trailer_crc32 (if enabled)
 * AEAD decryption
 * parse event → playback update status

## 8.2 Recovery scan (file corruption/truncation)

When CRC found No match or half record read:

* Enter scan mode: slide by byte to find the next `"TLOG"` magic
* After finding it, try to parse header_len/body_len to see if it is reasonable
* If the CRC passes, continue

> **MUST**: The scan must have an upper limit (to avoid endless loops on damaged files).

---

# 9) File rolling and retention strategy (preventing unlimited growth)

## 9.1 Single session file rolling

It is recommended that `events.log` be split when it reaches the threshold:

* `events_0001.log`
* `events_0002.log`

Similar to snapshots.

Threshold recommendation:

* Choose one of 4MB / 16MB / 64MB (see SD/requirements)
* If there are many locations, it is recommended to be smaller for easy export and repair

## 9.2 Retention (retention period)

* Record retention policy in `meta.json`
* Regularly clean the `YYYYMMDD` directory

> **MUST**: Users should be able to "delete an action record with one click" (delete the entire `T_<session_id>` directory).

---

# 10) Minimum implementation version (you can implement this first)

If you want to run quickly first, I recommend MVP:

* Only three files:

  * `meta.json`
  * `keys.bin`
  * `events.log`
* `events.log`:LogRecord + AEAD + trailer_crc32
* No snapshots, no idx
* Forced downsampling of location events (for example, one every 10s)

Added later:

* `snapshots.bin` + `snapshot.idx`

---
