# Meshtastic Protocol Policy Specification

本规格定义 `modules/core_chat/include/chat/runtime/meshtastic_protocol_policy.h`
的职责边界。它的目的很窄：把可由纯输入判断的 Meshtastic 协议业务规则集中在一个
共享 owner 中，防止 ESP32、nRF52、Linux 或 BLE bridge 分叉实现同一条规则。

## Position

`meshtastic_protocol_policy.h` 是 Meshtastic 的共享 policy table / pure decision
owner。它只拥有“给定事实后应如何决策”的规则，不拥有平台执行、protobuf 编解码、
BLE SDK 生命周期、radio IO、持久化或 UI 呈现。

一个规则应放入此文件，当且仅当它满足这些条件：

- 规则属于 Meshtastic 协议或 Meshtastic app-facing 行为；
- 规则可以表达为无副作用的输入到输出；
- ESP32、nRF52、Linux 或测试运行时都应该得到同一结果；
- 平台差异只影响是否能执行该结果，不改变规则本身。

平台代码可以拥有执行动作，例如重启 BLE、写 NVS、发送 LoRa frame、更新 advertising。
但平台代码不得重新决定 Meshtastic 业务条件。

## Owned Rules

当前此文件拥有以下共享规则：

| Rule | Public API | Ownership reason |
| --- | --- | --- |
| App-data send intent | `resolveMeshtasticAppDataSendPolicy(...)` | `dest == 0`、broadcast、`want_ack`、`want_response` 是 Meshtastic wire/app intent，不是平台策略。 |
| BLE visible-name change policy | `resolveMeshtasticBleVisibleNamePolicy(...)` | Meshtastic BLE 可见名由 Meshtastic node id 决定；owner long/short name 变化不能让某个平台自行决定断开 BLE。 |
| MQTT downlink accept/relay | `resolveMeshtasticMqttDownlinkPolicy(...)` | gateway echo/self/local/tx-disabled/relay 决策必须跨平台一致。 |
| NodeInfo reannounce gate | `resolveMeshtasticNodeInfoReannouncePolicy(...)` | peer NodeInfo 后是否重发 self NodeInfo 是协议行为，受 MQTT/self/invalid/suppress gate 约束。 |
| NodeInfo reply gate | `resolveMeshtasticNodeInfoReplyPolicy(...)` | `want_response`、addressing、suppression 是 Meshtastic request/reply policy。 |
| Position reply gate | `resolveMeshtasticPositionReplyPolicy(...)` | Position request/reply suppression 是协议行为，不属于板级 GPS 或 radio IO。 |
| TraceRoute reply gate | `resolveMeshtasticTraceRouteReplyPolicy(...)` | request/response、broadcast in-flight、addressing gate 是 Meshtastic TraceRoute 语义。 |

## Non-Owned Rules

这些内容不得塞进 `meshtastic_protocol_policy.h`：

- BLE service start/stop、GATT characteristic、CCCD、Bluefruit/NimBLE API 调用；
- nRF52/ESP32 是否要调用 `setEnabled(false)` 或重启 advertising 的执行细节；
- packet wire encode/decode，属于 `chat/infra/meshtastic`；
- self NodeInfo payload 构造，属于 `MeshtasticSelfAnnouncementCore`；
- Position payload 构造和 position availability，属于 `MeshtasticPositionCore`；
- TraceRoute/Position action lifecycle tracking，属于 `MeshtasticAppActionRuntime`；
- MeshCore 规则，必须进入 MeshCore runtime / policy；
- UI 是否展示动作，必须由 capability 和 UI/action specs 决定。

如果某个行为同时需要共享决策和平台执行，必须拆成两层：

1. 在 `meshtastic_protocol_policy.h` 里返回显式 policy / reason。
2. 在平台 adapter/runtime 中只消费 policy 结果并执行平台动作。

## BLE Visible Name Rule

Meshtastic BLE 可见名的协议侧事实是：

```text
Meshtastic_<compact node id>
```

因此：

- owner `long_name` 改变不改变 Meshtastic BLE 可见名；
- owner `short_name` 改变不改变 Meshtastic BLE 可见名；
- node id 改变会改变 Meshtastic BLE 可见名；
- 平台不得因为 `set_owner` / `applyUserInfo` 中 long/short name 变化而自行重启 BLE。

平台执行层只有在 `resolveMeshtasticBleVisibleNamePolicy(...)` 返回
`visible_name_changed == true` 时，才可以因 Meshtastic 可见名变化刷新 BLE service。

`chat/runtime/self_identity_policy.h` 可以负责跨协议的 identity projection 和字符串格式化，
但 Meshtastic 是否需要刷新 BLE 可见名的规则必须由本 policy 文件裁决。

## Admission Checklist

新增或修改 Meshtastic 行为时，先按以下顺序裁决：

1. 这是 Meshtastic 协议/app-facing 行为，还是平台 IO？
2. 如果是协议/app-facing 行为，能否表达为纯 policy？
3. 如果能，先把规则放入或扩展 `meshtastic_protocol_policy.h`。
4. 给 policy 增加共享测试，优先放入
   `modules/core_chat/tests/test_meshtastic_protocol_policy.cpp`。
5. 让 ESP32、nRF52、Linux 或 BLE bridge 只消费 policy 结果，不复制判断条件。
6. 如果行为改变跨平台 parity，更新
   `docs/specification/PROTOCOL_ADAPTER_PARITY_SPEC.md` 或 drift audit。

当没有合适 owner 时，可以创建新的共享 runtime/policy owner；不得因为当前修复发生在
nRF52 或 ESP32 日志中，就把业务规则写进平台分支。

## Review Rule

任何 PR 如果在 `platform/esp`、`platform/nrf52`、BLE bridge、board runtime 或 app facade
中新增 Meshtastic 判断条件，review 必须先问：

```text
为什么这条规则不在 meshtastic_protocol_policy.h？
```

只有当答案是“这是平台执行、资源约束或 SDK binding，不是 Meshtastic 业务规则”时，
平台侧改动才成立。
