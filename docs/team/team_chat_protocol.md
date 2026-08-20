# Team Chat protocol solution (Scheme C, v0.1)

This document describes the minimum implementable solution of the "Team Chat" protocol, which is used to carry structured messages:
Text / Location / Command, and is bound to the Team security domain to support map and situation linkage.

---

## 1. Goal and Scope

- Goal: Make team chat a "parseable and executable" structured message flow.
- Override media type:
 - Text: normal text.
 - Location: Location sharing, can be rendered on Chat/GPS map.
 - Command: command message, which can trigger team action prompts and map annotations.
- Security: only team members can decrypt, does not rely on the Meshtastic normal chat channel.

Prerequisites (v0.2 adjustments):
- **Team creation/joining no longer via LoRa or NFC**.
- Team formation only occurs in close-range scenarios within 5 meters, **Use ESP‑NOW to complete team building and key distribution**.
- LoRa is only used for daily communication within the team (Team Chat/Position/Track) and does not participate in the team formation process.

Non-target:
 - Does not replace existing normal chat/broadcast messages.
- Does not implement complex reliable transmission (v0.1 does best-effort first).

---

## 2. Port and encryption

 - New port:
 - `TEAM_CHAT_APP = 303` (defined in `modules/core_team/include/team/protocol/team_portnum.h`)
 - Encryption method:
 - Reuse `TeamEncrypted` envelope (`team_wire.h`).
 - Derive `team_chat` key from `team_psk`.
 - It is recommended to add:
    - `deriveKey(psk, "team_chat", keys.chat_key)`

This can achieve "team chat protocol layer isolation" and non-team members cannot decrypt it.

---

## 3. Payload structure (v0.1)

### 3.1 Universal header

Use concise TLV or fixed header + variable length payload. Recommended fixed header:

```
struct TeamChatHeader {
  uint8_t  version;    // =1
  uint8_t  type;       // 1=Text 2=Location 3=Command
 uint16_t flags; // Reserved
 uint32_t msg_id; // Locally generated, anti-replay/deduplication
 uint32_t ts; // Sending time (unix)
 uint32_t from; // Sender node_id
};
```

### 3.2 Text

```
struct TeamChatText {
  // UTF-8 text bytes
  bytes text;
};
```

### 3.3 Location

```
struct TeamChatLocation {
  int32_t lat_e7;
  int32_t lon_e7;
  int16_t alt_m;       // optional, 0=unknown
  uint16_t acc_m;      // optional, 0=unknown
  uint32_t ts;         // optional, 0=use header.ts
 uint8_t source; // Location semantic icon, see mapping below
  bytes    label;      // optional short label
};
```

`source` mapping (consistent with current firmware implementation):

- `0`: None / Ordinary position (no semantic icon)
- `1`:AreaCleared
- `2`:BaseCamp
- `3`:GoodFind
- `4`:Rally
- `5`:Sos

### 3.4 Command

v0.1 only does the minimum instruction set and does not do revocation/expiration semantics.

```
enum TeamCommandType : uint8_t {
 RallyTo = 1, // Rally to the target point
 MoveTo = 2, // Go to the target point
 Hold = 3 // Stay in place
};

struct TeamChatCommand {
  uint8_t  cmd_type;
 int32_t lat_e7; // Optional: used for Rally/Move
  int32_t  lon_e7;
 uint16_t radius_m; // Optional: collection radius
  uint8_t  priority;    // 0=normal 1=high
 bytes note; // Optional: short note
};
```

---

## 4. Sending and receiving process

### 4.1 Sending

- UI generates `Text/Location/Command`.
- Encoded as `TeamChatHeader + payload`.
 - Via `TeamService::sendTeamChat()`:
 - Use `keys.chat_key` encryption encapsulation as `TeamEncrypted`.
 - Sent via `TEAM_CHAT_APP` (`mesh_.sendAppData(...)`).

### 4.2 Receiving

- `TeamService::processIncoming()` Added `TEAM_CHAT_APP` branch:
 - Decrypt `TeamEncrypted`.
 - Parse `TeamChatHeader`.
 - Trigger `TeamChatEvent` (new EventBus type).
 - UI received `TeamChatEvent`:
 - Appended to `team_ui_chatlog` (new type field).
 - If it is Location/Command, update the map/GPS page annotation simultaneously.

---

## 5. Changes from existing code

Minimum change list (v0.1):

1. **Protocol and port**
 - `modules/core_team/include/team/protocol/team_portnum.h` added `TEAM_CHAT_APP = 303`
 - added `team_chat.h/.cpp` (encoding/decoding)

2. **Key derivation**
 - `TeamKeys` added `chat_key`
 - Derived from `"team_chat"` in `TeamService::setKeysFromPsk()`

3. **TeamService**
 - Added `sendTeamChat(...)`
 - `processIncoming()` handles `TEAM_CHAT_APP`

4. **Event Bus**
 - `sys/event_bus.h` adds `TeamChatEvent`

5. **UI/Storage**
 - `team_ui_chatlog_append()` is expanded to a structured message (type + payload)
 - `Contacts/Team` chat page uses the TeamChat data source to render cards

---

## 6. Compatibility and Transition

- Keep existing normal chat:
 - Primary/Secondary continue to use TEXT_MESSAGE_APP
- Team Chat as an independent channel:
 - Does not affect normal chat history and notifications
 - Team can be gradually replaced Page session source
- `Location.source` compatibility suggestions (PC/host computer):
 - If an unknown `source` (not in `0..5`) is received, render it as a normal `Location`, and the entire message must not be discarded
 - Continue to use coordinate and time fields, and `label` can be used for display when it exists
 - It is recommended to retain the original `source` Value for forward compatibility

---

## 7. v0.1 Fixed Matter
- ACK/Delivery: Not required (best effort).
- Command Revoke/Expire: Not supported.
- Map interaction: only a system notification pops up when a message is received; the pop-up window is triggered by selecting a map annotation in the Chat session and displays a cropped image of the tile map at that location.
- Team mode GPS: Render player positions from posring.log.

## 8. Version strategy

- `TeamChatHeader.version` = 1
-Subsequent extensions will be compatible through `flags` or new `type`

---

# Appendix A: ESP‑NOW team building and joining (v0.2)

> Goal: Reduce steps and unstable links, and quickly build a team at short distances (≤5m).

## A1. Roles and constraints
- Scenario: Everyone in the same room/range ≤5m.
- Medium: ESP‑NOW (2.4GHz), LoRa/NFC is no longer used.
- Result: The member receives **team_psk + team_id + key_id + epoch**, and then uses LoRa to send and receive the Team protocol.

## A2. Simplify the process

**Leader**
1) Create Team → Open the "Pairing (ESP‑NOW)" window (default 120s).
2) Receive member Join request (can be "Auto‑accept" or manually Accept).
3) Send KeyDist (ESP‑NOW) to the new member.

**Member**
1) Join Team → Nearby Teams (ESP‑NOW Scan) to select the target.
2) Send Join request and wait for KeyDist.
3) Enter the joined state after receiving the KeyDist.

## A3. ESP‑NOW messages (recommended)

```
PAIR_BEACON { team_id_short, team_id, epoch, key_id, leader_id, expires_at }
PAIR_JOIN   { team_id, member_id, member_pub, nonce }
PAIR_ACCEPT { team_id, member_id, ok, nonce }
PAIR_KEY    { team_id, key_id, epoch, team_psk_encrypted, nonce }
```

Description:
- `team_psk_encrypted` can be encrypted with a temporary session key (derived from `member_pub` + leader private key).
- `nonce` is used to correlate requests and prevent replay.
- All ESP‑NOW packets are processed only within the Pairing window.

## A4. Relationship with Team Chat
- ESP‑NOW is only responsible for "team building/enlisting/key distribution".
- Use LoRa's Team protocol (TEAM_CHAT / TEAM_POS / TEAM_TRACK / …) when done.
