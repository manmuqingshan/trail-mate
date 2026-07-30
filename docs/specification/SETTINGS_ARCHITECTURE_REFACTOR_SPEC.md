# Settings Architecture Refactor Specification

Status date: 2026-07-09

本文档定义 Trail Mate settings 的下一阶段改造规格。它不是一次小修小补的 UI
整理，而是把“配置是什么、属于哪个协议、如何展示、如何持久化、如何从 SD 恢复、如何
应用到运行时”重新拉直。

本规格先落地文档，不改变运行时代码。实际实现必须按本文分阶段推进，并在修改任何
函数、类或方法前按仓库规则做 GitNexus impact analysis。

## User Goal

目标用户场景是 Trail Mate 可以脱离手机工作：

- 选择 Meshtastic、MeshCore 或 Reticulum 后，settings 只展示该协议真正相关的项目。
- Meshtastic 的 MQTT 只服务 Meshtastic；MeshCore 的 MQTT 只服务 MeshCore；两者不互通。
- MQTT 只支持轻负担模式：不实现 TLS，不实现额外 MQTT payload 加密层。
- MQTT 启用是显式配置；Wi-Fi 关闭时 runtime 必须停 MQTT，Wi-Fi 开启时不得自动把 MQTT
  配置改成启用。
- 如果某个协议配置了 MQTT 并且 runtime 正在使用 Wi-Fi MQTT transport，该协议对应的
  BLE phone dependency 应被压低或关闭，避免用户误以为还必须连手机。
- 所有用户配置都必须可持久化，并能从 SD 备份恢复。
- 备份恢复不能为了“人类可读”牺牲 ESP 内存安全；当前整包 JSON/cJSON 模式需要替换。

## Distinctions

这些概念必须在代码和 UI 中分开，不能继续混在 `Chat` / `Network` 两个大筐里。

| Concept | Meaning | Must not be confused with |
| --- | --- | --- |
| Protocol | Meshtastic、MeshCore、Reticulum 的协议语义和节点身份体系 | transport、radio preset、UI 页面 |
| Radio profile | LoRa 频点、带宽、扩频因子、编码率、tx power、region/preset | channel name、PSK、broadcast/private |
| Channel / group | 协议内的群组、slot、topic 或 destination 配置 | 空口参数 |
| Transport | LoRa、BLE phone link、Wi-Fi MQTT、Reticulum TCP/UDP 等承载方式 | protocol 本身 |
| Conversation | UI 里的广播会话、私聊会话、联系人上下文 | Meshtastic channel slot 或 MeshCore channel slot |
| Device settings | 屏幕、语言、GPS、地图、Wi-Fi、owner name、隐私等跨协议设置 | 当前 active protocol 的 profile |
| Persistence | NVS/Preferences/IDF store/SD backup 的落盘事实 | UI widget state |
| Apply runtime | 把配置变更应用到 radio、MQTT、BLE、GPS、privacy policy | 保存配置 |

### Channel, Radio, Broadcast, Private

空口参数决定“谁能在 RF 层听见谁”：频率、带宽、扩频因子、编码率、tx power、region 或
preset 必须兼容，两个设备才可能互相收发 LoRa frame。

Channel/group 决定“收到 frame 后属于哪个协议群组或密钥域”：Meshtastic 使用 channel
slot/name/key/hash，MeshCore 使用 channel slot/name/key/public-channel fallback，Reticulum
使用 destination、announce、interface 和 identity。它们不是同一种对象，不能做一个泛化的
`channel` 然后让三个协议硬套。

广播和私聊是寻址语义：广播表示发给当前协议/channel/group 中的所有可见节点；私聊表示
发给一个 node id、destination 或 peer，并可能涉及 ack、route、retry、session 状态。广播
或私聊不改变空口参数，也不自动创建 channel。

MQTT 是 transport，不是第四种协议，也不是跨协议桥。Meshtastic MQTT 下来的 Meshtastic
packet 应走 Meshtastic 接收路径；MeshCore MQTT 下来的 MeshCore packet 应走 MeshCore 接收
路径。二者没有互通要求。

## Current Code Inventory

本节记录当前实现事实，作为改造前的基线。

| Area | Current owner | Observed shape |
| --- | --- | --- |
| Global config aggregate | `modules/core_sys/include/app/app_config.h` | `AppConfig` 同时承载 device、chat、GPS、map、privacy、Meshtastic、MeshCore、Reticulum、MQTT、legacy channel 字段 |
| Protocol config object | `modules/core_chat/include/chat/domain/chat_types.h` | `chat::MeshConfig` 被三个协议复用，里面同时有 radio、Meshtastic channel、MeshCore channel、MQTT、Reticulum group/interface 字段 |
| Shared LVGL settings | `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp` | `kChatItems` / `kNetworkItems` 混合协议项，通过 `pref_key` 字符串和 `should_show_item` 做隐藏 |
| Shared settings state | `modules/ui_shared/include/ui/screens/settings/settings_state.h` | 一个大 UI state 同时缓存 chat、network、MT MQTT、MC MQTT、Reticulum、device 字段 |
| Mono settings | `modules/ui_mono/src/runtime.cpp` | 保留大量 MT/MC 设置处理代码，但当前 radio item list 只暴露少数入口，能力与 UI 展示不一致 |
| GTK settings | `apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_logic.cpp` | 已有按协议 stack/page 切换的雏形，可作为“协议页”思路参考，但不应直接复制 GTK widget 逻辑 |
| Arduino ESP persistence | `platform/esp/arduino_common/src/app_config_store.cpp` | 使用 Preferences 按字段保存，大量 key 已存在，但字段覆盖靠手写 load/save 保持同步 |
| IDF persistence | `apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp` | 把整个 `AppConfig` 包进 raw blob，按 `sizeof(AppConfig)` 判断版本，结构一变旧配置就会被拒绝 |
| SD settings backup | `platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp` | 当前 path 是 `/trailmate/settings-backup.json`，用 cJSON 构造/解析整棵树，并读入整文件 |
| Store API | `modules/core_sys/include/platform/ui/settings_store.h` | 同时提供 `get_blob(std::vector<uint8_t>&)` 和 `get_blob_into(...)`；新 ESP 路径应优先使用 bounded buffer 版本 |
| Apply facade | `modules/core_sys/include/app/app_facades.h` | UI 可直接拿 `getConfig()` 修改，再调用 `saveConfig()`、`applyMeshConfig()`、`applyUserInfo()` 等 apply 方法 |

## Problems

### 1. Settings taxonomy is wrong

`Chat` 与 `Network` 现在不是产品概念，而是历史容器。结果是：

- Meshtastic MQTT、MeshCore MQTT、Reticulum Wi-Fi interface 都可能出现在同一类页面里。
- `chat_psk` 这种名字无法表达它到底是 Meshtastic channel key 还是 MeshCore channel key。
- 用户选择协议后仍会看到另一个协议的残留项，或者必须靠 string blacklist 隐藏。
- 新增 channel management 时无法自然表达 “Meshtastic channel slot” 与 “MeshCore channel slot”。

### 2. Config ownership is too broad

`AppConfig` 和 `MeshConfig` 目前是运行期大对象。它们可以作为过渡兼容层，但不能继续作为
长期 settings schema。原因：

- 字段归属不清，导致 UI、Preferences、SD backup、协议 apply 都各自记一份事实。
- ESP stack hygiene 已把 `AppConfig`、`MeshConfig` 列为危险 automatic local 类型。
- 新增 channel 列表或更多协议 profile 时，如果继续塞进这两个 struct，会持续放大内存风险。

### 3. Persistence is not schema-driven

Arduino Preferences 当前按字段保存，比 raw blob 稳定，但每个字段需要手写 load/save、默认值、
迁移逻辑。新增一个 settings 字段时，很容易漏掉 SD backup 或某个 UI。

IDF raw blob 以 `sizeof(AppConfig)` 为兼容条件，这对后续拆分结构非常脆弱。任何 `AppConfig`
布局变化都可能导致旧配置无法加载。

### 4. SD JSON backup is too heavy

当前 JSON 方案的优点是可读，但在 ESP 上代价偏大：

- restore 需要把整个文件读进内存。
- cJSON parse 会构造整棵树。
- backup print 会生成完整字符串。
- 旧代码中存在 `std::string`、`std::vector<uint8_t>`、whole-document parse/print 的组合。

这与“配置可完整落 SD，并能可靠恢复”的目标相冲突。恢复配置应该是低内存路径，而不是
最容易把设备推到 heap/stack 边界的路径。

### 5. Apply semantics are scattered

UI 直接改 `getConfig()`，再按字段手动调用不同 apply 方法。这样很难保证：

- 修改协议时，BLE/MQTT/LoRa/runtime 状态都按同一套规则切换。
- Wi-Fi 关闭只停 MQTT runtime，不悄悄改用户配置。
- MQTT 成功上行时 UI 不再只等 LoRa 成功才算 sent。
- 从 MQTT 收到的节点和 LoRa 收到的节点进入同一 contact/nearby/chat projection。

这些问题已经在 MQTT 调试中暴露过，settings refactor 需要把 runtime impact 作为字段元数据
的一部分，而不是散落在回调里。

## Target Architecture

### Layer Shape

目标结构如下：

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

`AppConfig` 在第一阶段继续存在，但应降级为 compatibility backing store，不再作为 settings
schema 的唯一事实来源。

### Domain Buckets

长期结构应把配置分成这些 owner：

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

这些 owner 可以先映射到现有 `AppConfig` 字段，但 schema 命名必须先按 owner 设计，避免未来
继续把三种协议塞回 `Chat` / `Network`。

### Settings Descriptor

每个可展示/持久化的 field 必须有一条静态 descriptor。descriptor 应该是小的 `constexpr`
表项，避免动态分配和重型 callback。

建议 descriptor 至少包含：

| Metadata | Purpose |
| --- | --- |
| stable field id | 编译期 enum，不用任意字符串做业务判断 |
| owner/profile | device、connectivity、mt、mc、reticulum 等 |
| UI section | 决定显示在哪个协议页或设备页 |
| type | bool、u8、i32、enum、bounded string、hex blob、secret |
| bounds | 字符串最大长度、数值范围、blob 最大长度 |
| protocol mask | MT/MC/Reticulum/global 的可见性 |
| capability mask | 板子是否支持 Wi-Fi、BLE、GPS、LoRa、SD |
| runtime impact | none、save-only、apply-mesh、apply-user、apply-gps、restart-mqtt、restart-ble |
| storage key | NVS key、SD key、legacy key |
| default provider | 按协议/地区/板型给默认值 |
| migration rule | 从旧 key、旧 blob、旧 JSON 恢复时如何写入 |
| sensitive flag | PSK、MQTT password、Wi-Fi password 等 |

UI 层只消费 descriptor 和当前 protocol/capability，不能再写 `if (pref_key == "...")` 作为主要
可见性规则。

### Protocol-Specific UI

Settings 顶层建议拆为：

- Device
- Connectivity
- Protocol
- Channels
- MQTT
- GPS & Map
- Privacy
- Backup & Restore
- Diagnostics

其中 `Protocol`、`Channels`、`MQTT` 的内容由 active protocol 决定。

Meshtastic 页面应展示：

- Meshtastic radio preset/region/modem preset/hops/tx power。
- Meshtastic channel slots。第一阶段可继续 primary/secondary，schema 必须预留 slot list。
- Meshtastic MQTT：enabled、preset、host、port、username、password、root topic、uplink/downlink。
- BLE phone link policy：当 Meshtastic MQTT runtime 可用时，BLE 可被关闭或降级。

MeshCore 页面应展示：

- MeshCore radio profile/region/channel slot/tx power。
- MeshCore channel name/key/public channel fallback。
- MeshCore MQTT：enabled、preset、host、port、username、password、root topic、uplink/downlink。
- MeshCore contact/nearby projection 必须与 LoRa receive 一样处理 MQTT receive 的节点。

Reticulum 页面应展示：

- Reticulum identity/status。
- LoRa interface 参数。
- Wi-Fi interface 参数。
- LXMF/announce groups。
- 不展示 MQTT，因为当前目标不包含 Reticulum MQTT。

### MQTT Policy

MQTT 是 protocol-scoped transport：

```text
MeshtasticProfile.mqtt -> Meshtastic MQTT runtime only
MeshCoreProfile.mqtt   -> MeshCore MQTT runtime only
```

运行态 eligibility：

```text
configured = profile.mqtt.enabled && host not empty && port > 0
eligible = configured && wifi_runtime.connected && protocol == active_protocol
```

约束：

- `wifi_runtime.connected == false` 时必须 stop MQTT runtime。
- Wi-Fi 变为 connected 时，只能让已经启用且配置完整的 MQTT runtime 变为 eligible；不得自动把
  `profile.mqtt.enabled` 从 false 改为 true。
- Plain MQTT only：`tls=false` 是唯一支持形态；UI 不提供 TLS 开关，代码也不引入 TLS 客户端。
- MQTT username/password 可支持，因为它不是 TLS；但必须按 secret 字段处理。
- MQTT receive 必须进入与 LoRa receive 相同的协议 projection：chat message、delivery status、
  contacts/nearby、node info、position、notification。
- MQTT uplink 成功不能被 LoRa TX 失败覆盖成 failed；delivery outcome 应区分 transport。

### Default Presets

默认 MQTT preset 不能以个人 broker 作为默认值。默认表应是协议 owner 的一部分：

| Protocol | Default preset intent |
| --- | --- |
| Meshtastic | mainstream Meshtastic public MQTT preset, plaintext transport, default root/topic/channel matching current community convention |
| MeshCore | mainstream MeshCore public/community preset if available; otherwise disabled with empty custom host until用户选择 preset |

实现时不把某个个人域名硬编码成默认。个人 broker 可以存在于 custom preset 或用户配置里。

## SD Backup Format

### Decision

新格式默认不使用 JSON。

采用 line-oriented typed key-value 格式，目标是：

- 可人工检查。
- 可流式读取。
- 每次只需要一个 bounded line buffer。
- 不需要 cJSON tree。
- 不需要把整个文件读入 `std::string`。
- 不需要 `std::vector<uint8_t>` 承接 whole blob。

建议文件名：

```text
/trailmate/settings-backup.tms
/trailmate/settings-backup.tmp
```

`.json` 旧文件可以在迁移期作为 legacy restore input，但新备份写出必须使用 `.tms`。

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

为了满足“完全从 SD 恢复”，Wi-Fi password、MQTT password、channel PSK 应可进入备份。UI 必须
把这些字段标记为 sensitive，并在手动导出/恢复界面给出明确提示。

实现上 sensitive 只影响 UI 呈现和日志脱敏，不意味着不落盘。用户选择 SD backup 时，目标是
恢复一台离线设备的完整配置。

## Persistence Model

每个字段必须通过同一份 descriptor 声明其持久化位置。

### Arduino Preferences

现有 Preferences key 可以保留，但 schema 要成为覆盖清单：

- load 时按 descriptor 读 key，应用 default/migration。
- save 时按 descriptor 写 key。
- 对 blob/secret 使用 bounded buffer。
- 对旧 key 做一次 migration，不在 UI 回调里散写兼容逻辑。

### IDF Store

raw `sizeof(AppConfig)` blob 只能作为 legacy input。新路径必须是版本化字段 store：

- 读取旧 raw blob 时，迁移到 schema field store。
- 新保存不再依赖 `sizeof(AppConfig)`。
- 如果为了启动速度保留 compact snapshot，也必须有独立 schema version 和 field-level fallback。

### SD Backup

SD backup 是 cross-store restore source，不是运行时唯一 store。启动时不应每次从 SD 覆盖 NVS。
恢复应该是显式动作：

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

实际实现必须遵守：

- 不在 ESP task stack 上创建 `AppConfig`、`chat::MeshConfig`、protobuf frame、大 byte array。
- Settings UI edit session 不复制整份 `AppConfig`；只保存 field-level dirty value 或 active editor
  buffer。
- SD restore 不读完整文件，不构造树，不用 `cJSON_ParseWithLength` 作为新路径。
- 不引入 `std::deque` 到 ESP BLE/Meshtastic bridge headers。
- 新 schema table 使用 static/constexpr storage。
- 新 channel list 使用固定上限和显式 drop/error policy，不能无界增长。
- 大字符串格式化使用 caller-provided buffer 或小 scratch owner，不把临时大对象放在回调栈上。

## Packaged Delivery Plan

This refactor is delivered as one cohesive feature package, not as user-visible partial phases.
The steps below are an internal construction sequence only. The final deliverable must include
schema, protocol-aware UI, persistence, SD backup/restore, runtime apply behavior, tests and
verification together.

No intermediate state should be considered complete if it leaves settings half migrated, exposes
new protocol pages without matching persistence, or writes a new SD backup format without restore.

### Slice 0: Specification and Audit

Deliverables:

- 本文档。
- 当前 settings/persistence/apply 代码清单。
- 确认 JSON 备份替换方向。

No runtime behavior change.

### Slice 1: Descriptor Read Model

Introduce descriptor tables and read accessors without changing existing UI behavior.

Deliverables:

- `SettingsFieldId` enum。
- protocol/global owner metadata。
- field descriptors for all currently visible settings。
- tests that every field has default, storage key, owner, visibility, runtime impact。

Compatibility:

- Existing `AppConfig` remains backing store。
- Existing LVGL settings can still use old state while descriptors are validated in tests。

### Slice 2: Transaction and Apply Dispatcher

Move settings mutation through a small transaction boundary.

Deliverables:

- field-level set/get APIs。
- validator/normalizer。
- runtime impact diff。
- dispatcher that calls `applyMeshConfig()`、`applyUserInfo()`、`applyPositionConfig()`、
  MQTT restart/stop and BLE policy in one place。

Compatibility:

- Existing UI callbacks can be converted incrementally field by field。

### Slice 3: Protocol-Aware UI Sections

Replace `kChatItems` / `kNetworkItems` as primary organization.

Deliverables:

- Device/Connectivity/Protocol/Channels/MQTT/GPS & Map/Privacy/Backup sections。
- Active protocol filter from descriptor metadata。
- Board capability filter from descriptor metadata。
- No business visibility based on `pref_key` string comparisons。

Acceptance:

- Selecting Meshtastic shows Meshtastic channel/MQTT/radio settings only。
- Selecting MeshCore shows MeshCore channel/MQTT/radio settings only。
- Selecting Reticulum shows Reticulum interface/group settings and hides MQTT。

### Slice 4: Lightweight SD Backup

Replace default JSON backup writer/reader with `.tms`.

Deliverables:

- streaming writer。
- streaming parser。
- fixed max line length。
- CRC。
- descriptor-backed export/restore coverage。
- legacy `.json` restore either removed or isolated behind explicit compatibility path with strict size cap。

Acceptance:

- Full settings backup/restore succeeds without whole-file allocation。
- Unknown future keys are ignored safely。
- Sensitive fields restore correctly and logs are redacted。

### Slice 5: Channel Management

Introduce protocol-specific channel/group management.

Deliverables:

- Meshtastic channel slot model。
- MeshCore channel slot model。
- Reticulum group/destination model remains separate。
- Create/join/share flow for supported protocols。
- QR/import/export payload generation on demand, using bounded scratch storage。

Acceptance:

- Creating a Meshtastic channel does not mutate MeshCore fields。
- Creating a MeshCore channel does not mutate Meshtastic fields。
- Broadcast/private conversation selection references protocol-specific channel identity explicitly。

### Slice 6: Retire Raw Struct Persistence

After migrations are covered by tests and field store is proven:

- Stop writing raw `AppConfig` blobs。
- Keep one-way read migration for a bounded release window。
- Remove legacy keys only after backup/restore and migration tests prove no supported user path is lost。

### Package Acceptance

The package is not done until all of these are true:

- Protocol selection changes visible settings, stored settings and runtime apply behavior together。
- Meshtastic, MeshCore and Reticulum each have their own settings surface; hidden fields are hidden by
  descriptor/capability metadata, not by ad hoc string checks。
- Meshtastic MQTT and MeshCore MQTT can be configured independently and are persisted/restored。
- Wi-Fi off stops MQTT runtime; Wi-Fi on does not auto-enable MQTT config。
- SD backup writes `.tms`, restore reads `.tms`, and all user settings covered by descriptors round trip。
- Legacy Preferences/IDF/raw config paths migrate into the new schema without losing existing user settings。
- Settings UI does not create new large ESP stack objects or whole-config drafts。
- Tests and stack hygiene checks pass for the touched areas。

## Verification Requirements

Before implementation PR/commit:

- Run GitNexus impact analysis before each edited symbol, and warn before HIGH/CRITICAL edits。
- Run unit tests for descriptor coverage, migration, transaction diff and backup restore parser。
- Run `python3 scripts/check_esp_stack_hygiene.py` when touching settings save/load, ESP BLE,
  Meshtastic bridge, or app config code。
- For PlatformIO build/upload/monitor, use background process + log polling as required by repo rules。
- Run `detect_changes()` before commit to verify affected symbols and flows。

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

- 不做 Meshtastic 与 MeshCore MQTT 互通。
- 不为 MQTT 增加 TLS。
- 不把 Reticulum 伪装成 MQTT/channel 页面。
- 不把一个 generic `Channel` 类型强塞给三种协议。
- 不在新备份路径使用 whole-document JSON。
- 不继续用 raw `sizeof(AppConfig)` 作为新持久化格式。
- 不为了快速 UI 隐藏继续扩大 `pref_key` string blacklist。
- 不把大量 channel 或 QR/share payload 常驻塞进 `AppConfig`。

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
