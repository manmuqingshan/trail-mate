# Settings Architecture Refactor Specification

Status date: 2026-07-09

This document defines the next phase of retrofit specifications for Trail Mate settings. It is not a minor UI
 tidying up, but a re-straightening of "what the configuration is, which protocol it belongs to, how to display it, how to persist it, how to restore it from SD, and how it
 applies it to the runtime".

This specification will be documented first and the runtime code will not be changed. The actual implementation must be carried out in stages according to this article, and GitNexus impact analysis must be performed according to the warehouse rules before modifying any
functions, classes or methods.

## User Goal

The target user scenario is that Trail Mate can work without a mobile phone:

- After selecting Meshtastic, MeshCore or Reticulum, the settings only show projects that are truly relevant to that protocol.
- Meshtastic's MQTT only serves Meshtastic; MeshCore's MQTT only serves MeshCore; the two are not interoperable.
- MQTT only supports light-burden mode: no TLS is implemented, and no additional MQTT payload encryption layer is implemented.
- MQTT enablement is an explicit configuration; the runtime must stop MQTT when Wi-Fi is turned off, and MQTT
 configuration must not be automatically changed to enabled when Wi-Fi is on.
- If a protocol is configured with MQTT and the runtime is using the Wi-Fi MQTT transport, the protocol corresponding
 BLE phone dependency should be suppressed or turned off to avoid users mistakenly thinking that the phone must still be connected.
- All user configurations must be persistent and restoreable from SD backups.
- Backup restore cannot sacrifice ESP memory safety for "human readability"; the current whole package JSON/cJSON schema needs to be replaced.

## Distinctions

These concepts must be separated in code and UI, and cannot continue to be mixed in the two big baskets of `Chat` / `Network`.

| Concept | Meaning | Must not be confused with |
| --- | --- | --- |
| Protocol | Protocol semantics and node identity system of Meshtastic, MeshCore, Reticulum | transport, radio preset, UI page |
| Radio profile | LoRa frequency, bandwidth, spreading factor, coding rate, tx power, region/preset | channel name, PSK, broadcast/private |
| Channel / group | Group, slot, topic or destination configuration within the protocol | Air interface parameters |
| Transport | Bearing methods such as LoRa, BLE phone link, Wi-Fi MQTT, Reticulum TCP/UDP | protocol itself |
| Conversation | Broadcast session, private chat session, contact context in the UI | Meshtastic channel slot or MeshCore channel slot |
| Device settings | Screen, language, GPS, map, Wi-Fi, owner name, privacy and other cross-protocol settings | Profile of the current active protocol |
| Persistence | NVS/Preferences/IDF store/SD backup placement fact | UI widget state |
| Apply runtime | Apply configuration changes to radio, MQTT, BLE, GPS, privacy policy | Save configuration |

### Channel, Radio, Broadcast, Private

The air interface parameters determine "who can hear whom at the RF layer": frequency, bandwidth, spreading factor, coding rate, tx power, region or
preset must be compatible so that two devices can send and receive LoRa frames to each other.

Channel/group determines "which protocol group or key domain it belongs to after receiving the frame": Meshtastic uses channel
 slot/name/key/hash, MeshCore uses channel slot/name/key/public-channel fallback, and Reticulum
 uses destination, announce, interface and identity. They are not the same kind of objects, and you cannot make a generalized
`channel` and then have three protocols apply it.

Broadcast and private chat are addressing semantics: broadcast means sent to all visible nodes in the current protocol/channel/group; private chat means
sent to a node id, destination or peer, and may involve ack, route, retry, session status. Broadcast
or private chat does not change the air interface parameters, nor automatically create a channel.

MQTT is a transport, not a fourth protocol, nor a cross-protocol bridge. Meshtastic
packets from Meshtastic MQTT should go through the Meshtastic receiving path; MeshCore packets from MeshCore MQTT should go through the MeshCore receiving
 path. There are no interoperability requirements between the two.

## Current Code Inventory

This section records the current implementation facts as a baseline before transformation.

| Area | Current owner | Observed shape |
| --- | --- | --- |
| Global config aggregate | `modules/core_sys/include/app/app_config.h` | `AppConfig` also carries device, chat, GPS, map, privacy, Meshtastic, MeshCore, Reticulum, MQTT, legacy channel fields |
| Protocol config object | `modules/core_chat/include/chat/domain/chat_types.h` | `chat::MeshConfig` is reused by three protocols, and contains radio, Meshtastic channel, MeshCore channel, MQTT, Reticulum group/interface fields |
| Shared LVGL settings | `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp` | `kChatItems` / `kNetworkItems` mixed protocol items, hidden through `pref_key` string and `should_show_item` |
| Shared settings state | `modules/ui_shared/include/ui/screens/settings/settings_state.h` | A large UI state that also caches chat, network, MT MQTT, MC MQTT, Reticulum, device fields |
| Mono settings | `modules/ui_mono/src/runtime.cpp` | Keeps a lot of MT/MC settings processing code, but currently radio item list Only a few entrances are exposed, and capabilities are inconsistent with UI display |
| GTK settings | `apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_logic.cpp` | There is a prototype of stack/page switching by protocol, which can be used as a reference for the "protocol page" idea, but the GTK widget logic should not be copied directly |
| Arduino ESP persistence | `platform/esp/arduino_common/src/app_config_store.cpp` | Use Preferences to save by field, a large number of keys already exist, but field coverage relies on handwriting load/save to keep synchronized |
| IDF persistence | `apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp` | Put the entire `AppConfig` is packaged into raw blob, and the version is determined by `sizeof(AppConfig)`. Once the structure becomes old, the configuration will be rejected |
| SD settings backup | `platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp` | The current path is `/trailmate/settings-backup.json`, using cJSON Construct/parse the entire tree and read the entire file |
| Store API | `modules/core_sys/include/platform/ui/settings_store.h` | Also provides `get_blob(std::vector<uint8_t>&)` and `get_blob_into(...)`; new ESP paths should preferentially use the bounded buffer version |
| Apply facade | `modules/core_sys/include/app/app_facades.h` | UI can be modified directly with `getConfig()`, and then call `saveConfig()`, `applyMeshConfig()`, `applyUserInfo()` and other apply methods |

## Problems

### 1. Settings taxonomy is wrong

`Chat` and `Network` are not product concepts now, but historical containers. The result is:

- Meshtastic MQTT, MeshCore MQTT, and Reticulum Wi-Fi interface may all appear in the same type of page.
- The name `chat_psk` cannot express whether it is a Meshtastic channel key or a MeshCore channel key.
- After the user selects a protocol, they will still see the remnants of another protocol, or they must be hidden by string blacklist.
- When adding channel management, "Meshtastic channel slot" and "MeshCore channel slot" cannot be expressed naturally.

### 2. Config ownership is too broad

`AppConfig` and `MeshConfig` are currently runtime large objects. They can serve as transitional compatibility layers, but cannot continue to be
Long-term settings schema. Reason:

- The field ownership is unclear, causing UI, Preferences, SD backup, and protocol apply to each record a copy of the facts.
- ESP stack hygiene has listed `AppConfig` and `MeshConfig` as dangerous automatic local types.
- When adding a channel list or more protocol profiles, if you continue to stuff these two structs, the memory risk will continue to amplify.

### 3. Persistence is not schema-driven

Arduino Preferences is currently saved by field, which is more stable than raw blob, but each field requires hand-written load/save, default value, and
migration logic. When adding a settings field, it is easy to miss SD backup or a certain UI.

IDF raw blob uses `sizeof(AppConfig)` as the compatibility condition, which is very fragile to subsequent split structures. Any `AppConfig`
layout changes may cause the old configuration to fail to load.

### 4. SD JSON backup is too heavy

The advantage of the current JSON solution is that it is readable, but the cost is high on ESP:

- restore needs to read the entire file into memory.
- cJSON parse will construct the entire tree.
- backup print will generate the complete string.
 - A combination of `std::string`, `std::vector<uint8_t>`, whole-document parse/print exists in the old code.

This conflicts with the goal of "configuration can be completely dropped to SD and can be restored reliably". The recovery configuration should be a low-memory path, not the path that
easiest pushes the device to the heap/stack boundary.

### 5. Apply semantics are scattered

The UI directly changes `getConfig()`, and then manually calls different apply methods according to fields. This is difficult to guarantee:

- When modifying the protocol, the BLE/MQTT/LoRa/runtime status will switch according to the same set of rules.
- Turning off Wi-Fi only stops the MQTT runtime and does not quietly change the user configuration.
- When MQTT is successfully uplinked, the UI no longer only waits for LoRa to be successful before sending.
 - Nodes received from MQTT and nodes received from LoRa go into the same contact/nearby/chat projection.

These problems have been exposed in MQTT debugging. Settings refactor needs to use the runtime impact as part of the field metadata
 rather than scattered in the callback.

## Target Architecture

### Layer Shape

The target structure is as follows:

```text
Settings UI
  -> SettingsDescriptor tables
  -> SettingsEditSession / field-level draft
  -> SettingsTransaction
  -> SettingsValidator + normalizer
  -> SettingsPersistence
  -> RuntimeApplyDispatcher
  -> Protocol/runtime adapters
```

`AppConfig` continues to exist in the first stage, but should be downgraded to the compatibility backing store and no longer as settings
The single source of truth for schema.

### Domain Buckets

The long-term structure should divide the configuration into these owners:

| Owner | Examples |
| --- | --- |
| `DeviceSettings` | owner long/short name、locale、screen、time、battery/display policy |
| `ConnectivitySettings` | Wi-Fi credentials、Wi-Fi enable policy、network limits |
| `GpsMapSettings` | GPS power/publish policy、map tile/cache/source、tracker defaults |
| `PrivacySettings` | ignored nodes、contact alert policy、location visibility |
| `MeshtasticProfile` | radio preset、region、hops、node info、channels、Meshtastic MQTT |
| `MeshCoreProfile` | radio profile、channel slot/name/key、public-channel fallback、MeshCore MQTT |
| `ReticulumProfile` | identity、LoRa interface、Wi-Fi interface、LXMF/announce groups |
| `ChatPresentationSettings` | active conversation defaults、notification/presentation preferences |
| `BackupRestoreSettings` | backup version、restore policy、sensitive export policy |

These owners can be mapped to the existing `AppConfig` fields first, but the schema naming must be designed according to the owner first to avoid
 continuing to stuff the three protocols back into `Chat` / `Network` in the future.

### Settings Descriptor

Each displayable/persistent field must have a static descriptor. The descriptor should be a small `constexpr`
 table entry to avoid dynamic allocation and heavy callbacks.

It is recommended that the descriptor at least contain:

| Metadata | Purpose |
| --- | --- |
| stable field id | compile-time enum, no arbitrary string is used for business judgment |
| owner/profile | device, connectivity, mt, mc, reticulum, etc. |
| UI section | determine which protocol page or device page to display |
| type | bool、u8、i32、enum、bounded string、hex blob、secret |
| bounds | Maximum string length, value range, maximum blob length |
| protocol mask | Visibility of MT/MC/Reticulum/global |
| capability mask | Whether the board supports Wi-Fi, BLE, GPS, LoRa, SD |
| runtime impact | none、save-only、apply-mesh、apply-user、apply-gps、restart-mqtt、restart-ble |
| storage key | NVS key、SD key、legacy key |
| default provider | Default value according to protocol/region/board type |
| migration rule | How to write when restoring from old key, old blob, old JSON |
| sensitive flag | PSK, MQTT password, Wi-Fi password, etc. |

The UI layer only consumes descriptor and current protocol/capability, and can no longer write `if (pref_key == "...")` as the main
 visibility rule.

### Protocol-Specific UI

The top level of Settings is recommended to be split into:

- Device
- Connectivity
- Protocol
- Channels
- MQTT
- GPS & Map
- Privacy
- Backup & Restore
- Diagnostics

The contents of `Protocol`, `Channels`, and `MQTT` are determined by the active protocol.

The Meshtastic page should display:

- Meshtastic radio preset/region/modem preset/hops/tx power.
- Meshtastic channel slots. The first stage can continue with primary/secondary, and the schema must reserve a slot list.
- Meshtastic MQTT:enabled、preset、host、port、username、password、root topic、uplink/downlink.
- BLE phone link policy: BLE can be turned off or downgraded when the Meshtastic MQTT runtime is available.

The MeshCore page should display:

- MeshCore radio profile/region/channel slot/tx power.
- MeshCore channel name/key/public channel fallback.
- MeshCore MQTT:enabled、preset、host、port、username、password、root topic、uplink/downlink.
 - MeshCore contact/nearby projection node that must handle MQTT receive the same as LoRa receive.

The Reticulum page should show:

- Reticulum identity/status.
- LoRa interface parameters.
- Wi-Fi interface parameters.
- LXMF/announce groups.
 - MQTT is not shown because the current target does not contain Reticulum MQTT.

### MQTT Policy

MQTT is a protocol-scoped transport:

```text
MeshtasticProfile.mqtt -> Meshtastic MQTT runtime only
MeshCoreProfile.mqtt   -> MeshCore MQTT runtime only
```

Running state eligibility:

```text
configured = profile.mqtt.enabled && host not empty && port > 0
eligible = configured && wifi_runtime.connected && protocol == active_protocol
```

Constraints:

- MQTT runtime must be stopped when `wifi_runtime.connected == false`.
- When Wi-Fi becomes connected, only an enabled and fully configured MQTT runtime can become eligible;
 `profile.mqtt.enabled` cannot be automatically changed from false to true.
- Plain MQTT only: `tls=false` is the only supported form; the UI does not provide a TLS switch, and the code does not introduce a TLS client.
- MQTT username/password is supported as it is not TLS; but must be handled as secret field.
- MQTT receive must enter the same protocol as LoRa receive projection: chat message, delivery status,
  contacts/nearby、node info、position、notification.
- MQTT uplink success cannot be overwritten as failed by LoRa TX failure; delivery outcome should distinguish between transports.

### Default Presets

The default MQTT preset cannot use a personal broker as the default value. The default table should be part of the protocol owner:

| Protocol | Default preset intent |
| --- | --- |
| Meshtastic | mainstream Meshtastic public MQTT preset, plaintext transport, default root/topic/channel matching current community convention |
| MeshCore | mainstream MeshCore public/community preset if available; otherwise disabled with empty custom host until user selects preset |

The implementation does not hard-code a personal domain name as the default. Personal brokers can exist in custom presets or user configurations.

## SD Backup Format

### Decision

The new format does not use JSON by default.

Using line-oriented typed key-value format, the goal is:

- Manual inspection.
- Streamable.
- Only one bounded line buffer is needed at a time.
- No cJSON tree required.
- No need to read the entire file into `std::string`.
- No need for `std::vector<uint8_t>` to accept whole blob.

Recommended file name:

```text
/trailmate/settings-backup.tms
/trailmate/settings-backup.tmp
```

`.json` Old files can be used as legacy restore input during the migration period, but new backups must be written out using `.tms`.

### Format Sketch

```text
TMSET2
schema.version=u16:2
created.unix=u32:1783500000
device.owner.long=str:Trail Mate
device.owner.short=str:TM
protocol.active=enum:meshtastic

mt.radio.region=enum:CN
mt.radio.modem_preset=enum:LONG_FAST
mt.channel.0.name=str:LongFast
mt.channel.0.psk=hex:01020304...
mt.mqtt.enabled=bool:1
mt.mqtt.host=str:mqtt.meshtastic.org
mt.mqtt.port=u16:1883
mt.mqtt.root=str:msh/CN
mt.mqtt.username=str:meshdev
mt.mqtt.password=secret:large4cats

mc.channel.slot=u8:0
mc.channel.name=str:public
mc.channel.key=hex:
mc.mqtt.enabled=bool:0

checksum.crc32=hex:89ABCDEF
```

Rules:

- First line is magic: `TMSET2`.
- Max line length is fixed, initially 256 or 384 bytes. Any longer line is skipped with a diagnostic.
- Key is ASCII stable storage key.
- Type prefix is mandatory.
- Strings are bounded by descriptor metadata.
- Hex blob max length is bounded by descriptor metadata before decoding.
- Unknown keys are ignored but counted.
- Known key with invalid value is rejected and reported, not partially applied.
- Restore writes through `SettingsTransaction`; it must not directly mutate random globals.
- Backup write uses temp file + fsync/close + rename where backend supports it.
- CRC covers all lines before checksum.

### Why Not Binary TLV First

Binary TLV is smaller and faster, but it is harder to inspect and repair on SD. The line KV format is the
better first target because it keeps manual recovery possible without the cJSON memory cost. A future binary
TLV export can be added for factory/provisioning use, but it should not be the only user backup format.

### Sensitive Fields

In order to meet "complete recovery from SD", Wi-Fi password, MQTT password, channel PSK should be able to enter the backup. The UI must
mark these fields as sensitive and provide clear prompts in the manual export/restore interface.

In implementation, sensitive only affects UI rendering and log desensitization, but does not mean that it will not be deleted. When the user selects SD backup, the goal is
to restore the complete configuration of an offline device.

## Persistence Model

Each field must declare its persistence location through the same descriptor.

### Arduino Preferences

The existing Preferences key can be retained, but the schema must become an override list:

- Press descriptor to read the key during load, and apply default/migration.
- Press descriptor to write the key when saving.
- Use bounded buffer for blob/secret.
- Do a migration for the old key and do not write compatibility logic in the UI callback.

### IDF Store

raw `sizeof(AppConfig)` blob can only be used as legacy input. The new path must be a versioned field store:

 - Migrate to schema field store when reading old raw blobs.
- New saves no longer depend on `sizeof(AppConfig)`.
- If compact snapshot is retained for startup speed, there must also be independent schema version and field-level fallback.

### SD Backup

SD backup is a cross-store restore source, not the only store at runtime. NVS should not be overwritten from SD every time on boot.
Restore should be an explicit action:

```text
User chooses Restore
  -> parse .tms stream
  -> validate descriptors
  -> build transaction
  -> persist to primary store
  -> apply affected runtimes
  -> emit UI result
```

## Memory Budget Rules

Actual implementation must comply with:

 - Do not create `AppConfig`, `chat::MeshConfig`, protobuf frame, large byte array on ESP task stack.
- Settings UI edit session does not copy the entire `AppConfig`; only saves the field-level dirty value or active editor
  buffer.
- SD restore does not read the complete file, does not construct the tree, and does not use `cJSON_ParseWithLength` as the new path.
 - Do not introduce `std::deque` to ESP BLE/Meshtastic bridge headers.
- New schema table uses static/constexpr storage.
- The new channel list uses a fixed upper limit and explicit drop/error policy, and cannot grow without bounds.
- Large string formatting uses caller-provided buffer or small scratch owner, without placing temporary large objects on the callback stack.

## Packaged Delivery Plan

This refactor is delivered as one cohesive feature package, not as user-visible partial phases.
The steps below are an internal construction sequence only. The final deliverable must include
schema, protocol-aware UI, persistence, SD backup/restore, runtime apply behavior, tests and
verification together.

No intermediate state should be considered complete if it leaves settings half migrated, exposes
new protocol pages without matching persistence, or writes a new SD backup format without restore.

### Slice 0: Specification and Audit

Deliverables:

- This document.
- Current settings/persistence/apply code listing.
 - Confirm JSON backup replacement direction.

No runtime behavior change.

### Slice 1: Descriptor Read Model

Introduce descriptor tables and read accessors without changing existing UI behavior.

Deliverables:

- `SettingsFieldId` enum.
- protocol/global owner metadata.
- field descriptors for all currently visible settings.
- tests that every field has default, storage key, owner, visibility, runtime impact.

Compatibility:

- Existing `AppConfig` remains backing store.
- Existing LVGL settings can still use old state while descriptors are validated in tests.

### Slice 2: Transaction and Apply Dispatcher

Move settings mutation through a small transaction boundary.

Deliverables:

- field-level set/get APIs.
- validator/normalizer.
- runtime impact diff.
- dispatcher that calls `applyMeshConfig()`、`applyUserInfo()`、`applyPositionConfig()`、
  MQTT restart/stop and BLE policy in one place.

Compatibility:

- Existing UI callbacks can be converted incrementally field by field.

### Slice 3: Protocol-Aware UI Sections

Replace `kChatItems` / `kNetworkItems` as primary organization.

Deliverables:

- Device/Connectivity/Protocol/Channels/MQTT/GPS & Map/Privacy/Backup sections.
- Active protocol filter from descriptor metadata.
- Board capability filter from descriptor metadata.
- No business visibility based on `pref_key` string comparisons.

Acceptance:

- Selecting Meshtastic shows Meshtastic channel/MQTT/radio settings only.
- Selecting MeshCore shows MeshCore channel/MQTT/radio settings only.
- Selecting Reticulum shows Reticulum interface/group settings and hides MQTT.

### Slice 4: Lightweight SD Backup

Replace default JSON backup writer/reader with `.tms`.

Deliverables:

- streaming writer.
- streaming parser.
- fixed max line length.
- CRC.
- descriptor-backed export/restore coverage.
- legacy `.json` restore either removed or isolated behind explicit compatibility path with strict size cap.

Acceptance:

- Full settings backup/restore succeeds without whole-file allocation.
- Unknown future keys are ignored safely.
- Sensitive fields restore correctly and logs are redacted.

### Slice 5: Channel Management

Introduce protocol-specific channel/group management.

Deliverables:

- Meshtastic channel slot model.
- MeshCore channel slot model.
- Reticulum group/destination model remains separate.
- Create/join/share flow for supported protocols.
- QR/import/export payload generation on demand, using bounded scratch storage.

Acceptance:

- Creating a Meshtastic channel does not mutate MeshCore fields.
- Creating a MeshCore channel does not mutate Meshtastic fields.
- Broadcast/private conversation selection references protocol-specific channel identity explicitly.

### Slice 6: Retire Raw Struct Persistence

After migrations are covered by tests and field store is proven:

- Stop writing raw `AppConfig` blobs.
- Keep one-way read migration for a bounded release window.
- Remove legacy keys only after backup/restore and migration tests prove no supported user path is lost.

### Package Acceptance

The package is not done until all of these are true:

- Protocol selection changes visible settings, stored settings and runtime apply behavior together.
- Meshtastic, MeshCore and Reticulum each have their own settings surface; hidden fields are hidden by
  descriptor/capability metadata, not by ad hoc string checks.
- Meshtastic MQTT and MeshCore MQTT can be configured independently and are persisted/restored.
- Wi-Fi off stops MQTT runtime; Wi-Fi on does not auto-enable MQTT config.
- SD backup writes `.tms`, restore reads `.tms`, and all user settings covered by descriptors round trip.
- Legacy Preferences/IDF/raw config paths migrate into the new schema without losing existing user settings.
- Settings UI does not create new large ESP stack objects or whole-config drafts.
- Tests and stack hygiene checks pass for the touched areas.

## Verification Requirements

Before implementation PR/commit:

- Run GitNexus impact analysis before each edited symbol, and warn before HIGH/CRITICAL edits.
- Run unit tests for descriptor coverage, migration, transaction diff and backup restore parser.
- Run `python3 scripts/check_esp_stack_hygiene.py` when touching settings save/load, ESP BLE,
  Meshtastic bridge, or app config code.
- For PlatformIO build/upload/monitor, use background process + log polling as required by repo rules.
- Run `detect_changes()` before commit to verify affected symbols and flows.

Suggested tests:

| Test | Purpose |
| --- | --- |
| descriptor coverage snapshot | every field has owner, protocol visibility, storage key, default, impact |
| protocol visibility matrix | MT/MC/Reticulum show different settings |
| legacy Preferences migration | existing NVS keys map to schema fields |
| IDF raw blob migration | old raw config can migrate once |
| `.tms` round trip | export -> restore produces equivalent config |
| `.tms` malformed input | long line, bad type, bad hex, unknown key, bad CRC handled safely |
| MQTT policy matrix | Wi-Fi off stops runtime; Wi-Fi on does not enable config; protocol switch stops old runtime |
| contact projection parity | MQTT receive and LoRa receive update contacts/nearby through same app event path |

## Explicit Non-Goals

- Does not support Meshtastic and MeshCore MQTT interoperability.
 - Do not add TLS for MQTT.
- Do not disguise Reticulum as MQTT/channel page.
- Don't force a generic `Channel` type onto three protocols.
 - Don't use whole-document JSON in new backup paths.
- Discontinue using raw `sizeof(AppConfig)` as the new persistence format.
- Do not continue to expand the `pref_key` string blacklist for fast UI hiding.
- Don't stuff a lot of channel or QR/share payloads into `AppConfig`.

## Open Decisions

| Decision | Recommendation |
| --- | --- |
| `.tms` max line length | Start at 256 bytes; allow 384 only if current MQTT/password fields need it |
| Legacy JSON restore | Keep one release as explicit compatibility restore with strict size cap, then remove |
| MeshCore public MQTT preset | Verify upstream/community default before hardcoding; otherwise default disabled with preset picker |
| Meshtastic channel slot count | Implement current primary/secondary first, schema list-ready |
| IDF protocol support | Current IDF runtime appears Meshtastic-only; full protocol UI must either expose capability limits or implement MC/RT there first |
| Sensitive backup UX | Default to full restore capability, with explicit warning/redaction rather than silently omitting secrets |

## Implementation Guardrail

Even though this is a single packaged feature, implementation should still proceed in a safe internal
order. The first code change after this spec should not rewrite all settings UI at once. The safest opening
move is:

1. Add schema field IDs and descriptor coverage tests.
2. Map descriptors to existing `AppConfig` read paths.
3. Add protocol visibility tests for MT/MC/Reticulum.
4. Only then start moving UI sections and persistence writers.

This keeps the refactor observable during development while still packaging the final user-facing result as
one complete settings architecture change.
