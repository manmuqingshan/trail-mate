# Protocol Adapter Parity Specification

本规格约束同一协议在不同平台 adapter 中的行为一致性。它不是 UI 规格，也不是
板级硬件能力表。它定义的是：当 Trail Mate 声称某个平台实现了 Meshtastic 或
MeshCore adapter 时，哪些协议语义必须由共享 core 统一拥有，哪些平台差异必须
显式暴露为 capability，哪些行为禁止静默降级。

本规格的核心立场是：协议业务规则只能有一个实现。ESP32 与 nRF adapter 不应各自
维护一套 NodeInfo / Position / TraceRoute / ACK / PKI 状态机。平台 adapter 只能拥有
radio IO、SDK binding、storage binding、clock/random、queue scheduling、buffer
placement 等平台问题。

## Core Distinctions

### Protocol Behavior

Protocol behavior 是协议层事实，例如：

- Meshtastic `NODEINFO_APP` 的 `want_response` reply；
- Meshtastic 收到 peer NodeInfo 后的 self NodeInfo reannounce；
- Meshtastic `TRACEROUTE_APP` 的 `want_response` 与 unicast `want_ack`；
- MeshCore `PAYLOAD_TYPE_TRACE` 与 direct path/SNR collection；
- MeshCore NodeInfo query/info control frame。

同一协议的 protocol behavior 默认必须跨平台一致。

Protocol behavior 属于 shared protocol core，不属于 `platform/esp` 或 `platform/nrf52`
adapter。若某个规则需要在两个平台文件中复制，默认视为架构漂移。

### Platform Capability

Platform capability 是某个 adapter 当前能否安全提供某个行为。资源、硬件或工程阶段
限制可以导致 capability 不同，但不得让调用方误以为行为已经存在。

如果某个平台缺少某个 protocol behavior，必须满足至少一个条件：

- `MeshCapabilities` 明确不声明该能力；
- 专用 capability 字段明确标识缺失；
- 调用接口返回失败，并且上层 UI/用例不把失败解释成协议成功。

Capability 只能描述共享 core 暴露的能力在某个平台是否可用，不能把一套平台私有
协议规则包装成“能力差异”。

### Adapter Boundary

Platform adapter may own:

- radio driver calls and IRQ/RX/TX lifecycle;
- SDK-specific BLE, serial, preferences, filesystem, and timer binding;
- memory placement decisions such as scratch buffers and queue sizes;
- board-specific TX gating and power/runtime hooks;
- conversion between shared core commands and physical packet IO.

Platform adapter must not own:

- when to answer a NodeInfo request;
- when to reannounce self NodeInfo after peer NodeInfo;
- how Meshtastic `want_response` is interpreted;
- how TraceRoute request/reply state is built;
- how Position request/reply throttling works;
- MeshCore NodeInfo query/info semantics;
- MeshCore trace path semantics;
- PKI resync business rules.

These belong in shared protocol core with platform adapters supplying IO ports.

### Product UX

Product UX 只能消费 protocol behavior 或 capability。UX 不得根据平台名字猜测协议
语义，也不得把 Meshtastic portnum 映射到 MeshCore payload type。

## Required Parity Rules

### R1 Single Shared Protocol Core

Meshtastic and MeshCore business/protocol rules must live in shared code under
`modules/core_chat`, `modules/core_mesh`, or another explicit shared protocol module.

ESP32 and nRF adapter files must not independently implement the same protocol state machine.
If equivalent logic appears in both platform adapters, it is a defect unless a spec names it as
platform IO glue.

### R2 Same Protocol, Same Semantics

ESP32 Meshtastic adapter 与 nRF Meshtastic adapter 必须调用同一 Meshtastic core 语义。
ESP32 MeshCore adapter 与 nRF MeshCore adapter 如果都声明某项 MeshCore capability，
也必须调用同一 MeshCore core 语义。

### R3 Capability Before UI

任何 UI 菜单、自动回复、诊断状态或 app-facing action，都必须由 capability 或协议
模式授权。不能因为某个平台 adapter 恰好接受某个函数调用，就显示对应动作。

### R4 No Silent Downgrade

接口参数不得被静默丢弃，除非规格明确允许。例如：

- `want_response` 被协议支持时必须进入 wire/data 层；
- 如果 adapter 不支持 `want_response`，必须通过 capability 或返回值暴露；
- request-style empty payload 必须按协议语义处理，不能被普通 payload 校验误杀。

### R5 Broadcast Semantics Are Protocol Semantics

Broadcast 是否允许 `want_response` 是协议语义，不是平台策略。Meshtastic 官方 Position
broadcast 可以携带 request-replies 语义。因此 adapter 不能在没有规格依据时清除
broadcast `want_response`。

### R6 Drift Requires A Ledger Entry

任何以下改动都必须更新 drift ledger 或 parity matrix：

- 新增或修改 `IMeshAdapter` 行为；
- 修改 ESP32 或 nRF 任一协议 adapter 的 NodeInfo / Position / TraceRoute / ACK /
  PKI / discovery / app-data 语义；
- 在 UI 中新增协议相关动作；
- 改变 `MeshCapabilities`；
- 将某个 protocol behavior 标为暂不支持。

## Meshtastic Required Behavior Matrix

| Behavior | Required cross-platform rule |
| --- | --- |
| App-data empty payload | Request-style ports such as `POSITION_APP` request and `TRACEROUTE_APP` must allow `len == 0` when payload is null or protobuf-empty. |
| `want_ack` | Unicast `want_ack` must set Meshtastic air ACK. Broadcast must not request air ACK. |
| `want_response` | Must be preserved for request-style packets, including supported broadcast cases. |
| NodeInfo request | `requestNodeInfo(dest, want_response)` must send `NODEINFO_APP`; unicast `want_response` requests a reply. |
| NodeInfo reply | Incoming `NODEINFO_APP` with `want_response` addressed to us or broadcast must send our NodeInfo reply, subject to 12h reply suppression. |
| NodeInfo peer reannounce | After decoding a valid peer NodeInfo, adapter should broadcast our NodeInfo once, subject to 60s reannounce suppression, and must skip MQTT/self/invalid node sources. |
| Position response | Incoming `POSITION_APP` with `want_response` addressed to us or broadcast should send own position if available, subject to 3m reply suppression. |
| TraceRoute request | Outgoing unicast TraceRoute must send `TRACEROUTE_APP`, set `want_response=true`, and set `want_ack=true`. |
| TraceRoute response | Incoming `TRACEROUTE_APP` request with `want_response` must send RouteDiscovery response when addressed to us or broadcast. |
| TraceRoute result | UI/app-facing status must not treat local enqueue as success. A TraceRoute action is delivered on `ROUTING_APP` ACK, failed on `ROUTING_APP` error, completed on matching `TRACEROUTE_APP` response, and timed out if no final result arrives. |
| Position exchange result | Position replies sent as responses to `POSITION_APP` `want_response` requests must preserve the original request packet id in `Data.request_id`; UI/app-facing status may complete only on a matching Position response or fail on timeout/routing error. |
| Routing errors | `NO_RESPONSE`, PKI, and channel errors must not be reported as local send success. |
| PKI resync | PKI unknown/missing-key paths should request or send NodeInfo consistently. |

Implementation ownership:

- packet encode/decode helpers already live in shared `chat/infra/meshtastic`;
- self NodeInfo packet building already lives in `MeshtasticSelfAnnouncementCore`;
- app-data destination/ACK/response intent, NodeInfo peer-reannounce gating,
  NodeInfo/Position request-reply suppression gating, and TraceRoute reply gating live in
  `chat/runtime/meshtastic_protocol_policy.h`;
- TraceRoute payload mutation lives in shared `chat/infra/meshtastic/mt_protocol_helpers`;
- TraceRoute and Position Exchange UI/app action lifecycle tracking live in
  `chat/runtime/meshtastic_app_action_runtime.h`;
- Position availability and payload construction live in `MeshtasticPositionCore`;
- remaining platform GPS source selection and low-level send/channel mechanics must stay explicit
  and be extracted only when a shared runtime decision can own them without taking platform IO.

## MeshCore Required Behavior Matrix

MeshCore is not Meshtastic with different port numbers. MeshCore behavior must be modeled with MeshCore
payload types, control frames, direct paths, and identity/key rules.

| Behavior | Rule |
| --- | --- |
| NodeInfo | If `supports_node_info=true`, adapter must implement MeshCore NodeInfo query/info control frame semantics, including request/reply intent. |
| Discovery | If `supports_discovery_actions=true`, actions must map to MeshCore discover/advert/control behavior, not generic app-data. |
| App-data ACK | If `supports_appdata_ack=true`, `want_ack` must create/track MeshCore ACK semantics. |
| App-data `want_response` | If no MeshCore app-level response semantic exists for a port, adapter must not claim that it supports the response. |
| Trace | MeshCore trace must use `PAYLOAD_TYPE_TRACE` and direct path/SNR semantics. Meshtastic `TRACEROUTE_APP` is forbidden. |
| Position exchange | No MeshCore position exchange action may be exposed until a MeshCore-native telemetry/location request-response is specified and implemented. |
| PKI/identity | Identity keys, group/channel keys, and direct peer secrets are MeshCore-specific and must not reuse Meshtastic PKI assumptions. |

Implementation ownership:

- MeshCore payload helpers and protocol strategy already exist in shared modules;
- NodeInfo query/reply, discover request/response decisions, trace, ACK tracking, route selection policy,
  and identity/key policy must live in shared MeshCore runtime core instead of parallel ESP32/nRF adapter
  copies. Platform adapters may still own radio scheduling, route-cache persistence, and hardware identity
  storage while they execute runtime effects.

## Capability Surface Requirements

`MeshCapabilities` keeps the coarse legacy flags for existing UI code, but protocol-sensitive callers should
prefer these fine-grained flags over generic app-data support:

- `supports_node_info_query`;
- `supports_node_info_reply`;
- `supports_node_info_reannounce`;
- `supports_position_request`;
- `supports_position_reply`;
- `supports_trace_route_request`;
- `supports_trace_route_reply`;
- `supports_protocol_app_response`;
- `supports_protocol_ack_tracking`;
- `supports_meshcore_direct_route_table`;
- `supports_meshcore_identity_keys`;
- `supports_meshcore_peer_secret_derivation`;
- `supports_meshcore_rich_trace_projection`.

When a fine-grained flag is false, UI must be conservative and protocol-specific specs, such as
`NODE_ACTION_PROTOCOL_SPEC.md`, remain authoritative.

## Extraction Requirement

When drift is found between ESP32 and nRF implementations, the preferred fix order is:

1. Extract the protocol rule into shared core.
2. Add a shared unit test for the rule.
3. Make ESP32 and nRF adapters call the shared core.
4. Only then adjust platform-specific IO, queues, or storage.

Directly copying logic from one platform adapter into the other is allowed only as a temporary stopgap
when hardware validation is urgent. The drift audit must then record the copied behavior as technical debt
with an extraction target.

## Review Checklist

Before merging protocol adapter changes:

1. Identify the protocol behavior being changed.
2. Identify the shared protocol core owner.
3. If no shared owner exists, create or extend one before adding more platform adapter logic.
4. Compare ESP32 and nRF adapters only as platform IO consumers of that shared core.
5. Decide whether any remaining difference is a real capability difference.
6. Update this spec or the drift audit if behavior is not equal.
7. Add or update tests where the behavior is reachable without hardware.
8. Verify UI does not expose unavailable protocol actions.
9. Run GitNexus impact analysis for edited symbols and `gitnexus detect-changes` before commit.

## Relationship To Other Specs

- `PROTOCOL_RUNTIME_DESIGN_SPEC.md` defines the Strategy / Command / State / Bridge /
  Adapter design used to enforce shared protocol ownership.
- `NODE_ACTION_PROTOCOL_SPEC.md` defines user-facing node action legality for TraceRoute,
  Exchange Position, and Compass.
- `NRF52_NODE_ID_AND_CHANNEL_KEY_SPEC.md` defines stable nRF identity and key terminology.
- This spec defines cross-platform protocol adapter parity and capability drift policy.
