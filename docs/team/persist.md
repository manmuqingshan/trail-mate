﻿﻿﻿# Pager Team Core｜SD-only persistence solution (full version)

## 0. Goals and three principles

### Goal

* **Simple**: fewer files, less formats, less logic
* **Trusted**: recoverable after power outage, logs will not damage the overall situation
* **Low resource consumption**: avoid high-frequency random writes; control log growth

### Core principles

* **SD-only** (no flash wear)
* **append-only** (log only appends, no writing back, no seek)
* **No CRC** for each record**: rely on `magic + len` to determine complete records; **Incomplete tails are discarded**
* **Separation of "intention vs fact"**: UI trigger **Intent**; write to disk **Committed Fact**

---

## 1. Directory structure (finalized)

```text
/team/
 current.txt # Current team directory name, one line of text
 current.tmp # Atomic writing of temporary files
 T_A7K3/ # Shortcode/hash of team_id (stable directory name)
 keys.bin # Current team_psk (optional, separate persistence)
 snapshot.bin # Current world snapshot (low-frequency atomic writing)
 snapshot.tmp # Atomic writing temporary file
 events.log # Key event log (append writing, sync basis)
 posring.log # Position ring (fixed size, covering the oldest)
 chatlog.log # Chat log (rolling upper limit)
```

### current.txt format

* Content example: `T_A7K3\n`
* Atomic write: write `current.tmp` → flush → rename overwrite `current.txt`

---

## 2. Data model layering (boundaries that must be adhered to)

### 2.1 Key Events

**The fact of changing the team structure / epoch / waypoint**, must:

* Write to `events.log`
* With `event_seq` (monotonically increasing)
* as the basis for SYNC

**The key event type that must be written**:

* `TeamCreated`
* `MemberAccepted`
* `MemberKicked`
* `LeaderTransferred`
* `EpochRotated`
 (Waypoint is optional, the same key event will be added later)

### 2.2 High-frequency situation (Non-key)

* Presence: heartbeat/online status (not dropped)
* Position: dropped `posring.log` (ring + Throttle)
* Chat: drop `chatlog.log` (rolling upper limit)

> Key Event = "World Rules Change"; Location/Chat = "Live Noise and Recording".

---

## 3. event_seq: Authoritative source and placement rules (Q&A merged)

### 3.1 Who maintains event_seq?

✅ **Leader maintains and allocates event_seq** (recommended to be maintained at the TeamCore/Usecase layer; TeamService maintains IO responsibilities)

* When Leader generates key event: `event_seq = last_event_seq + 1`
* Member only accepts keys with seq event, and verify continuity

### 3.2 Is the UI triggered or placed after receiving?

✅ **Both are available, but the order can only be written as "Committed Fact"**

* The UI triggers the **Intent** (requests for kicking/transfer/rotation, etc.)
* It is only written when the Leader / Member **confirms the submission** `events.log`

Specific:

* **Leader**: UI Intent → Verify permissions/status → Commit (allocate seq) → append events.log → apply memory status → send message (failure is repaired by subsequent SYNC)
* **Member**: Receive the leader's key event → Verify team_id/epoch/permission/seq continuity → append events.log → apply

> Avoid the split of "UI clicked but the network did not send out/No one acknowledged it, but the log was written to death".

---

## 4. File format (team_id is unified 8 bytes, no CRC)

General convention: Little-endian, compact writing, does not rely on struct padding.

---

### 4.1 TeamId

* `team_id`:`uint64_t`(8 bytes)
* Directory name suggestion: `T_` + base32(team_id) (or short hash) to ensure FAT compatibility

---

### 4.2 snapshot.bin (current world snapshot)

#### Header (fixed)

```c
SnapshotHeaderV1 {
  char   magic[4] = "TMS1";
  uint8  version  = 1;
  uint8  flags;                 // bit0: in_team
  uint16 reserved;

  uint32 updated_ts;            // Write snapshot time (seconds)
  uint64 team_id;               // 8B
  uint32 epoch;                 // Current epoch
  uint32 last_event_seq;        // The last seq applied to it

  uint32 self_node_id;
  uint32 leader_node_id;

  uint8  self_role;             // 0 None, 1 Member, 2 Leader
  uint8  reserved2[3];

  uint16 member_count;
  uint16 reserved3;
}
```

#### member table (repeat member_count times, variable length)

```c
MemberRecV1 {
  uint32 node_id;
  uint8  role;                  // 1 Member, 2 Leader
  uint8  flags;                 // bit0: has_name
  uint16 name_len;              // Recommendation <= 24
  char   name[name_len];        // UTF-8
}
```

#### Atomic write

Write `snapshot.tmp` → flush → rename overwrite `snapshot.bin`

#### Write throttling (low consumption)

* Write once every **10** key events or **60 seconds** (whichever comes first)
* epoch rotate / kick / transfer / join success / leave the team: **Force write once**

---

### 4.3 events.log (key event log: truth source + SYNC basis)

#### Record header (fixed)

```c
EventRecHeaderV1 {
  char   magic[2] = "EV";
  uint8  version  = 1;
  uint8  type;                  // KeyEventType

  uint32 event_seq;             // Monotonically increasing
  uint32 ts;                    // seconds

  uint16 payload_len;
  uint16 reserved;
}
payload[payload_len]
```

#### Trusted rule without CRC

When scanning, if:

* magic/version does not match → stop (simplest strategy)
* Remaining length of file < header + payload_len → Stop (the last half is discarded)

Because only append is performed, the data before the stop point is credible.

#### KeyEventType

* `1 TeamCreated`
* `2 MemberAccepted`
* `3 MemberKicked`
* `4 LeaderTransferred`
* `5 EpochRotated`

#### Payload definition

**TeamCreated**

```c
uint64 team_id
uint32 leader_node_id
uint32 epoch          // Initial 1
```

**MemberAccepted**

```c
uint32 member_node_id
uint8  role           // Default 1 Member
uint8  reserved[3]
```

**MemberKicked**

```c
uint32 member_node_id
```

**LeaderTransferred**

```c
uint32 new_leader_node_id
```

**EpochRotated**

```c
uint32 new_epoch
```

> Payload should try to have fixed fields and fewer strings: more space and lower IO.

---

### 4.3.1 keys.bin (team_psk is persisted independently to avoid keys not being ready after restart)

> The snapshot does not save the key, and the key is saved separately.
> Only save the currently effective team_psk + key_id (epoch), without making history.

#### Format (fixed)

```c
TeamKeysFileV1 {
  char   magic[4] = "TMK1";
  uint8  version  = 1;
  uint8  psk_len;              // 16
  uint16 reserved;

  uint64 team_id;
  uint32 key_id;               // epoch/key_id
  uint8  psk[16];              // team_psk
}
```

#### Atomic write

Write `keys.tmp` → flush → rename overwrite `keys.bin`

---

### 4.4 posring.log (position ring, high frequency but throttling)

#### Header (fixed)

```c
PosRingHeaderV1 {
  char   magic[4] = "PSR1";
  uint8  version  = 1;
  uint8  reserved1[3];

  uint32 data_capacity;         // Data area size (fixed)
  uint32 write_offset;          // Next write position
  uint32 rec_size;              // Fixed = sizeof(PosRecV1)
  uint32 reserved2;
}
data[data_capacity]
```

#### Record (fixed)

```c
PosRecV1 {
  uint16 magic = 0x5053;        // 'PS'
  uint8  ver   = 1;
  uint8  flags;

  uint32 ts;
  uint32 member_id;
  int32  lat_e7;
  int32  lon_e7;

  int16  alt_m;
  uint16 speed_dmps;
}
```

#### Write throttling (required)

* Each member can write at most one message every 15-30 seconds, or the displacement is > 20m.
* When the ring is full, it covers the oldest one, and the file size is constant

---

### 4.5 chatlog.log (chat append writing + Upper limit rolling)

#### Record (variable length)

```c
ChatRecHeaderV1 {
  char   magic[2] = "CH";
  uint8  version  = 1;
  uint8  flags;                 // bit0 incoming/outgoing

  uint32 ts;
  uint32 peer_id;

  uint16 text_len;
  uint16 reserved;
}
text[text_len]                  // UTF-8
```

#### Team chat record format (V2, corresponding to `TEAM_CHAT_APP`)

After receiving the `TeamChat` payload and completing decoding, additionally write `chatlog.log` according to the V2 structure.
Compared with V1, V2 saves additional message types in the recording header to facilitate unified playback of text, location and instruction messages.

```c
ChatRecHeaderV2 {
  char   magic[2] = "CH";
  uint8  version  = 2;
  uint8  flags;                 // bit0 incoming/outgoing

  uint32 ts;
  uint32 peer_id;               // sender node_id

  uint8  msg_type;              // 1=Text 2=Location 3=Command
  uint8  reserved1[3];

  uint16 payload_len;
  uint16 reserved2;
}
payload[payload_len]            // decoded TeamChat payload
```

#### Upper limit policy (simple)

* Upper limit: **256KB** or **1000** (choose one)
* Over limit:

 * rename `chatlog.log` → `chatlog.old` (optional)
 * Create a new empty `chatlog.log`

---

## 5. Start the recovery process (with current.txt, the fastest)

1. Read `/team/current.txt`

* Does not exist/empty → There is currently no team

2. Open `/team/<dir>/snapshot.bin`

* If it does not exist → start from the empty state (but usually it will be written once after join)

3. Incremental playback `events.log`

* Start sequential scanning application from `snapshot.last_event_seq + 1`
* Encounter incomplete tail → stop

4. UI Ready

---

## 6. SYNC process (depends on event_seq)

* Presence (not placed) carries: `team_id, epoch, last_event_seq`
* Found the other party `last_event_seq > my_last_event_seq`:

 * Send `SYNC_REQ(from_seq = my_last_event_seq + 1)`
* The other party reads the seq range (up to N items) from `events.log` back to `SYNC_RSP`
* Local apply + append (can skip existing seq), update snapshot

---

## 7. event_seq + commit process (ASCII status/timing diagram)

### 7.1 Leader:UI Intent → Commit → Log → Broadcast

```text
UI                         TeamCore/Usecase                 TeamStore(SD)              TeamService(IO)           Mesh
 |   intentKick(target)          |                              |                           |                    |
 |------------------------------>|  check(role==Leader)         |                           |                    |
 |                               |  build KeyEvent(Kicked)      |                           |                    |
 |                               |  seq = last_seq + 1          |                           |                    |
 |                               |----------------------------->| append events.log(EV,seq)|                    |
 |                               |<-----------------------------| ok                        |                    |
 |                               |  apply to in-mem state        |                           |                    |
 |                               |  maybe snapshot throttle      |                           |                    |
 |                               |------------------------------>| save snapshot.tmp->bin    |                    |
 |                               |<-----------------------------| ok                        |                    |
 |                               |----------------------------------------------------------->| sendKick(seq,...)  |
 |                               |                                                            |------------------->|
 |                               |                                                            |   (may fail)       |
 |                               |  NOTE: even if send fails, state is committed;             |                    |
 |                               |        later Status/SYNC will repair delivery              |                    |
```

### 7.2 Member: RX KeyEvent → Verify seq → Log → Apply (missing seq triggers SYNC)

```text
Mesh                 TeamService(IO)             TeamCore/Usecase             TeamStore(SD)
 |  RX Kick(seq,...)       |                           |                         |
 |------------------------>| decode/decrypt            |                         |
 |                         | sink_.onTeamKick(...) ---->| verify team_id/epoch   |
 |                         |                           | verify seq == last+1 ? |
 |                         |                           |    NO -> request SYNC  |
 |                         |                           |    YES -> commit apply |
 |                         |                           |------------------------> append events.log
 |                         |                           |<------------------------ ok
 |                         |                           | apply in-mem state
 |                         |                           | maybe snapshot throttle
 |                         |                           |------------------------> save snapshot atomic
 |                         |                           |<------------------------ ok
```

### 7.3 When seq is discontinuous (Member triggers SYNC)

```text
Member Core                          Mesh                         Leader Core
    |  see seq gap (expected=41 got=45)                             |
    |---------------- SYNC_REQ(from=41) --------------------------->|
    |                                                               | read events.log seq>=41
    |<---------------- SYNC_RSP(events 41..45) ---------------------|
    | apply + append + snapshot                                     |
```

---

## 8. Engineering access point (aligned with TeamService / ITeamEventSink)

You now `TeamService::processIncoming()` will unpack the message and throw it to `sink_.onTeamXxx(event)`.

Recommended minimum changes:

* The implementation of `sink_` (TeamCore/Usecase) is responsible for:

 * Permission judgment
 * seq allocation (leader)
 * seq continuity check (member)
 * Call `TeamStore.append_key_event(...)` to write `events.log`
 * Update memory status
 * Trigger snapshot (throttling)
* `TeamService` only do IO: decode/encode/send, do not write SD directly

---

## 9. PosRing / ChatLog access point (Q&A merged into specification)

### 9.1 posring.log: Write from receive or send?
In team mode, GPS renders the position of team members (including yourself) from posring.log.

✅ Final draft: **Write both sides, and use the same throttle**

* **Receive TeamPosition (teammate)**: write to `posring.log` (teammate position source)
* **Native GPS fix (self)**: also write `posring.log` (you can still see your last position/trajectory fragment immediately after restarting)

Reason:

* Write-only receive → own history is empty
* Write-only send → teammate status will be empty after restart
* posring is a "fact stream cache" and does not participate in consistency: no event_seq, no strict deduplication
 Just do: per member throttling + ts latest coverage UI

**Recommended access (cleanest)**: Only write posring in **TeamCore (sink implementation)**

* `sink_.onTeamPosition(event)`:decode → `posring_append_throttled(from_id, pos, ts)`
* Local GPS update location: `TeamCore::onLocalPositionFix(fix)` → `posring_append_throttled(self_id, pos, ts)` → Then decide whether `TeamService.sendPosition(...)`

### 9.2 chatlog.log: Log Mesh chat (Team channel) or only Team protocol?
Decision (v0.1): Team chat uses TEAM_CHAT_APP; TeamChat messages are written to chatlog.log according to V2.
Map interaction: When receiving a message, only a system notification pops up; the pop-up window is triggered by selecting a map annotation in the Chat session and displays a cropped image of the tile map at that location.
Do not log TEAM_MGMT_APP to chatlog.log.

First distinguish three message domains:

1. **Meshtastic ordinary chat** (your existing chat module)
2. **Team management message (TEAM_MGMT_APP)**: Join/Kick/Rotate/Status... (not chat)
3. **Team chat** (can reuse Meshtastic text, or customize Team chat port in the future)

✅ Final draft: **chatlog.log Only record "chat text" visible to users, not management messages**

* ❌ Do not record `TEAM_MGMT_APP` to chatlog (they belong to `events.log` / diagnostics)
* ✅ Record "team chat text"

 * It is recommended to take the **route A**: Record **Meshtastic text belonging to the current Team channel News**

Route A (recommended): Record Meshtastic text chat (Team channel)

* Advantages: Immediately compatible with the existing ecosystem (Android/ATAK/other nodes)
* Disadvantages: Semantic decoupling of chat and Team (acceptable)

Route B (subsequent version): Team-specific chat protocol (TEAM_CHAT_APP/ TEAM_MGMT.Chat)

* Advantages: Chat is bound to team_id/epoch, members are controllable
* Disadvantages: New protocols and compatibility processing are required

**Chatlog access point (project)**

* Because TeamService does not handle text chat, chatlog should be accessed in the **chat module**:

 * ChatService/ChatUsecase when receiving/sending text:

 * If `channel_id == current_team_channel` → `chatlog_append(...)`
* TeamCore only needs to provide "current team channel_id"

---

## 10. Gap filling summary (current constraints)

* team_id:✅ 8 bytes (uint64)
* Key events must be placed: ✅ events.log + type + payload complete definition
* event_seq: ✅ leader authoritative maintenance; snapshot record last_seq; member check continuity; gap walking SYNC
* Intent vs Fact: ✅ UI trigger Intent; write only Committed Fact
* posring/chatlog: ✅ The access point and record range are clear (pos is double-written, chat is recorded as team channel text)

---

# Team Position (aligned with Meshtastic POSITION_APP)

## 1) Design goals

* **Align Meshtastic**: payload directly uses `meshtastic_Position` protobuf
* **Ecological compatibility**: consistent with Meshtastic POSITION_APP decoding
* **Extensibility**: retain Meshtastic Field's natural expansion space

---

## 2) Wire Payload

* **Type**: `meshtastic_Position` (protobuf)
* **Port**: `meshtastic_PortNum_POSITION_APP`
* **Encoding**: nanopb/pb_encode

> Description: Custom layout is no longer used. Position payload is exactly the same as Meshtastic.

---

## 3) Value rules (minimum set)

* `latitude_i` / `longitude_i`:E7(int32)
* `location_source`: Default `LOC_INTERNAL`
* `sats_in_view`: Fill in if any
* `timestamp`: Fill in if there are valid epoch seconds

The remaining fields are filled in as needed, keeping consistent with Meshtastic semantics.

---

# 5) Encoding/decoding access point (aligned with your existing TeamService + sink)

## 5.1 Sender (native GPS update)

**Recommended path** (TeamCore layer doing things):

1. `TeamCore::onLocalPositionFix(fix)`
2. Generate `meshtastic_Position` payload (protobuf)
3. **First** `posring_append_throttled(self_id, decoded_pos)` (same point placement)
4. Call `TeamService.sendPosition(payload, team_channel)` again

> You asked whether "posring should be written by sending or receiving": here it is "writing also by sending".

## 5.2 Receiver (TEAM_POSITION_APP)

Your current process already has:

* `TeamService::processIncoming()` decryption → `sink_.onTeamPosition(event{ctx, plain})`

In `sink` In the implementation (TeamCore):

1. decode `meshtastic_Position` → Get standardized fields
2. `posring_append_throttled(from_id, pos)` (teammate points drop)
3. Update memory `last_seen` (presence/online status)

---

# 6) posring.log Recommendations for alignment of placement fields

Your existing `PosRecV1` is sufficient (ts, member_id, lat/lon, alt, speed).
`course` and `vbat`:

* **It is not necessary to drop the disk** (Most UIs use trajectory direction estimation as enough; vbat does not necessarily need to display teammates in the topbar)
* `PosRecV2` will be released when needed (add 2 bytes of course and 2 bytes of vbat, not difficult)

---

# 7) Version and compatibility rules

* The payload must be decoded as `meshtastic_Position`, otherwise discard (or statistics error)
* Do not check the custom version field (subject to the Meshtastic protocol)
