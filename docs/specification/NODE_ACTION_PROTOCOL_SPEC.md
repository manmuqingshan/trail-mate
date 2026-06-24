# Node Action Protocol Specification

本规格定义节点列表操作菜单中与协议相关的动作边界，重点约束
nRF mono UI 上的 Trace Route 与 Exchange Position 行为。它的目标是避免
把 Meshtastic 的 app port 语义误投射到 MeshCore，或把本地发送入队误描述成
远端动作成功。

## Source Baseline

- Meshtastic 官方代码：
  - `.tmp/firmware/src/modules/TraceRouteModule.cpp`
  - `.tmp/firmware/src/mesh/PhoneAPI.cpp`
  - `.tmp/firmware/src/modules/PositionModule.cpp`
  - `.tmp/firmware/src/mesh/MeshModule.cpp`
- MeshCore 官方代码：
  - `.tmp/MeshCore/src/Packet.h`
  - `.tmp/MeshCore/src/Mesh.cpp`
  - `.tmp/MeshCore/examples/companion_radio/MyMesh.cpp`
- Trail Mate 当前适配代码：
  - `modules/ui_mono/src/runtime.cpp`
  - `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp`
  - `platform/nrf52/arduino_common/src/chat/infra/meshcore/meshcore_radio_adapter.cpp`
  - `platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_adapter.cpp`

## Distinctions

### Local Send Admission

本地发送入队或交给 radio adapter 成功，只能说明请求被本机接受。
它不能说明远端已经收到、远端已经响应、TraceRoute 已经完成，或 Position
已经交换成功。

UI 文案必须使用等待语义，例如 `WAIT REPLY`。只有本地发送失败时才显示
`SEND FAILED`。

### Remote Response

远端响应必须由后续收到的协议报文、ACK/NAK、routing error、timeout 或节点
Position 更新来确认。没有这类后续事实时，不得显示 `SUCCESS`。

### Protocol Ownership

Meshtastic 节点动作只能使用 Meshtastic portnum 与 Meshtastic wire 语义。
MeshCore 节点动作只能使用 MeshCore payload type / command 语义。

跨协议发送 app-data 是非法行为，即使字段名看起来类似。

## Meshtastic Trace Route

Meshtastic Trace Route 请求必须满足：

- 目标是非广播、非本机的 Meshtastic 节点。
- `decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP`。
- payload 是 `meshtastic_RouteDiscovery`，内容可以为空编码。
- `decoded.want_response = true`。
- 对非广播目标必须设置 `want_ack = true`，与官方
  `TraceRouteModule::startTraceRoute()` 和 `PhoneAPI` 的可靠投递升级一致。

本地发送成功后的 UI 文案是 `WAIT REPLY`。最终结果应由后续
`TRACEROUTE_APP` response、routing error 或 timeout 驱动。

## Meshtastic Exchange Position

Meshtastic Exchange Position 请求必须满足：

- 目标是 Meshtastic 节点。
- `decoded.portnum = meshtastic_PortNum_POSITION_APP`。
- 请求可以为空 payload。
- `decoded.want_response = true`。

本地发送成功后的 UI 文案是 `WAIT REPLY`。远端可能因为没有新 GPS、策略限制、
或官方 `PositionModule::allocReply()` 的 3 分钟节流而不回复。最终成功应由后续
Position 报文更新节点位置来确认。

## MeshCore Trace And Position

MeshCore 官方协议确实有 `PAYLOAD_TYPE_TRACE`，但它不是 Meshtastic
`TRACEROUTE_APP`。官方 `Mesh::createTrace()` 创建 trace payload，
`Mesh::sendDirect()` 将目标 path 追加到 payload 尾部，`onTraceRecv()` 返回
path hash 与 SNR 数据。

Trail Mate 当前 MeshCore adapter 尚未提供等价的 Trace Route 操作接口；
`sendAppData()` 也不会承载 MeshCore 原生 trace 语义。因此 mono UI 不得在
MeshCore 节点菜单中显示或执行 `TRACE ROUTE`。

MeshCore 位置请求同理：当前 Trail Mate 没有实现一个与 Meshtastic
`POSITION_APP + want_response` 等价的 MeshCore 位置交换动作。MeshCore adapter
当前也不会处理 app-data 的 `want_response`。因此 mono UI 不得在 MeshCore
节点菜单中显示或执行 `EXCHANGE POSITION`。

在 MeshCore 节点菜单中，当前位置相关的合法动作是基于已持久化节点位置打开
`OPEN COMPASS`。它不是一次位置交换请求。

## Current Mono UI Rule

Meshtastic mode:

- `DETAIL`
- `REPLY`
- `ADD CONTACT`
- `IGNORE NODE` / `UNIGNORE NODE`
- `TRACE ROUTE`
- `EXCHANGE POSITION`
- `OPEN COMPASS`

MeshCore mode:

- `DETAIL`
- `REPLY`
- `ADD CONTACT`
- `IGNORE NODE` / `UNIGNORE NODE`
- `OPEN COMPASS`

未来如果实现 MeshCore 原生 trace 或原生位置请求，必须先在 adapter 层提供明确
接口，并按 MeshCore 官方 `PAYLOAD_TYPE_TRACE` 或相应位置/telemetry 语义建模，
不能复用 Meshtastic portnum。
