# Protocol Runtime Design Specification

Status date: 2026-06-14

本文档定义 Trail Mate 在多平台、多协议环境下共享协议细节的设计。它补充
`PROTOCOL_ADAPTER_PARITY_SPEC.md`：后者说明“哪些协议行为必须一致”，本文说明“用什么
结构保证一致”。

本文采用 Mermaid 图表达 UML 语义。参考的表达习惯来自
`C:\Users\vicliu\Projects\etc-business\ui-layer\app-backend\docs` 中的时序图文档：先解释
图例，再将类、关系和时序放进 Markdown，便于和代码一起演进。

## Problem Statement

当前 ESP32 和 nRF 的协议 adapter 里混合了四类东西：

- 协议语义：NodeInfo 何时回复、TraceRoute 如何完成、PKI 缺钥如何 resync；
- 协议编解码：Meshtastic protobuf / wire packet，MeshCore frame / control payload；
- 平台执行：radio IO、BLE、storage、clock、queue、board power；
- 产品投影：UI 弹窗、聊天列表、节点列表、诊断日志。

这种混合导致 ESP32 和 nRF 会各自长出“相似但不完全相同”的协议实现。风险不是代码重复本身，
而是协议真相被分散到多个平台 adapter 里，后续任何 bugfix 都依赖人工同步。

目标结构必须让：

- 用户动作以 `ProtocolIntent` 表达；
- 协议真相由共享 `IProtocolRuntime` 解释；
- runtime 只输出 `ProtocolEffect`，不直接访问硬件；
- 平台差异由 `IProtocolEffectExecutor` 执行；
- UI / ChatService 不再知道 protocol portnum、payload type、request id、routing error 细节；
- ESP32 / nRF adapter 不再独立决定协议业务规则。

## UML Notation

| 图中写法 | UML 含义 | 本规格中的解释 |
| --- | --- | --- |
| `classDiagram` | 类图 | 表达接口、实现、组合、依赖和模式结构 |
| `<|..` | Realization | 类实现接口，例如 `MeshtasticRuntime` 实现 `IProtocolRuntime` |
| `*--` | Composition | 强拥有关系，例如 runtime 拥有内部状态机 |
| `o--` | Aggregation | 聚合或注入关系，例如 facade 持有 runtime 引用 |
| `..>` | Dependency | 依赖某个类型，例如 runtime 返回 effects |
| `flowchart` | 关系/管线图 | 表达 Intent -> Runtime -> Effects -> Executor 主轴 |
| `sequenceDiagram` | 时序图 | 表达一次用户动作或一次 incoming packet 的调用顺序 |
| `alt / opt / loop` | UML 组合片段 | 表达互斥分支、可选动作和重复 tick |

## Pattern Decision

本文只采用少数 GoF 23 设计模式作为主轴，避免“模式堆砌”。

### Design Verdict

本规格接受原始改造设想作为约束基线：Trail Mate 的协议层改造不是把若干 helper
抽到 shared 目录，而是建立一条清晰的用例入口到平台执行链路：

```text
UI / ChatService
  -> MeshProtocolFacade
  -> ProtocolIntent
  -> IProtocolRuntime strategy
  -> Protocol State
  -> ProtocolEffects
  -> IProtocolEffectExecutor bridge
  -> Platform adapters
```

其中 `Strategy + Command + State + Bridge + Adapter` 是主轴。如果这五个没有立住，
`Visitor / Chain of Responsibility / Facade / Factory / Builder` 都只能算局部工具，不能宣称
协议 runtime 改造完成。

`Facade` 虽然不是前五个主轴之一，但它是上层用例入口边界，不能只存在于图里。规格中出现
`MeshProtocolFacade` 时，表示必须有代码级对象或等价命名的 concrete boundary。该对象负责给
UI / ChatService 暴露稳定用例 API，并隐藏 runtime 选择、effect 执行、TX 失败回灌和 action
result 投影的编排细节。若未来决定不用 `MeshProtocolFacade` 这个名字，必须先修改本规格，
明确新的等价代码对象和验收条件。

### Language Baseline

目标方向是现代 C++，长期可以提升到 C++20；但当前 ESP32 / nRF52 的 PlatformIO
工具链不能同时稳定接受 `-std=gnu++20`：

- ESP32S3 当前 xtensa GCC 8.4 只支持早期 `gnu++2a`；
- nRF52 当前 arm-none-eabi GCC 7.2 不支持 `gnu++20`。

因此当前共享协议 runtime 的最低落地基线定为 **C++17**。构建系统必须去掉 Arduino
默认追加的 `-std=gnu++11`，确保 `std::variant`、`std::visit`、`if constexpr` 等 C++17
能力在 ESP32 和 nRF52 编译单元中都可用。等 nRF 工具链升级后，再将 baseline 提升到
C++20，并考虑引入 concepts / `std::span` / 更强类型约束。

### Primary Patterns

| Pattern | 用在何处 | 解决的问题 | 不变量 |
| --- | --- | --- | --- |
| Strategy | `IProtocolRuntime` 的 `MeshtasticRuntime` / `MeshCoreRuntime` | 同一个 Intent 在不同协议下有不同解释 | UI 和 platform adapter 不得用 `if protocol == ...` 自行解释协议 |
| Command | `ProtocolIntent` | UI / usecase 发出用户意图，而不是 portnum/payload | Intent 表示“要做什么”，不表示“无线包怎么长” |
| State | ACK、TraceRoute、Position、PKI resync 等 runtime 内部状态机 | 跨时间协议会话不再散落在 UI 和 adapter 的 if 分支中 | 状态迁移必须由 runtime 统一拥有 |
| Bridge | `IProtocolRuntime` 输出 `ProtocolEffect`，平台 `IProtocolEffectExecutor` 执行 | 协议语义和平台执行分离，避免 `protocol x platform` 爆炸 | runtime 不调用 radio/storage/BLE；executor 不决定协议语义 |
| Adapter | radio IO、storage、BLE、legacy `IMeshAdapter` 兼容层 | 对接现有平台 API 和 SDK | Adapter 只能适配技术接口，不能承载协议业务规则 |

### Supporting Patterns

| Pattern | 用在何处 | 使用边界 |
| --- | --- | --- |
| Visitor | `std::variant<ProtocolEffect...>` + `std::visit` 的 effect dispatch | 只用于 executor/test 处理 effects，不扩散到 UI |
| Chain of Responsibility | incoming packet 分类管线：decrypt -> routing -> nodeinfo -> position -> trace -> text/appdata | handler 顺序必须写进 runtime 或 spec，不能形成隐式吞包黑洞 |
| Facade | `MeshProtocolFacade` 给 UI/ChatService 的入口 | 必须落成真实代码边界；只做用例门面，不沉淀协议语义 |
| Abstract Factory / Factory Method | product composition 选择 facade + runtime + executor + codec | 用静态对象/引用也可以，不强制动态分配 |
| Builder | packet/control payload 构造 | 局部用于 codec/build request，不能替代 runtime |

### Patterns To Avoid As Main Axis

| Pattern | 不作为主轴的原因 |
| --- | --- |
| Singleton | 协议 runtime / executor 应通过组合注入，避免全局状态污染测试和多实例 |
| Template Method | 容易把平台差异塞回基类大泥球，不适合作为协议-平台分离主轴 |
| Mediator | 若做成“大协调器”，会替代 runtime 成为新的 God Object |
| Decorator / Proxy / Flyweight / Composite / Prototype | 当前问题不是对象包装、代理、共享小对象、树结构或原型复制 |
| Interpreter | 当前没有脚本化协议规则需求 |
| Memento | 仅在未来持久化 pending action / retransmit state 时考虑 |
| Iterator | 普通容器遍历足够 |
| Observer | 可继续用于事件通知，但它不能解决协议规则分裂 |

## Core Distinctions

### Intent

Intent 是用户或用例层想完成的动作：

- `SendTextIntent`
- `RequestNodeInfoIntent`
- `TraceRouteIntent`
- `ExchangePositionIntent`
- `StartKeyVerificationIntent`
- `SendSelfAnnouncementIntent`

Intent 不包含 Meshtastic portnum、MeshCore payload type、wire channel hash、protobuf bytes 或 request id
分配规则。

### Runtime

Runtime 是协议真相所在：

- `MeshtasticRuntime` 解释 Meshtastic Intent、incoming packet、routing result、tick；
- `MeshCoreRuntime` 解释 MeshCore Intent、incoming frame、trace path、NodeInfo control、tick；
- runtime 可以拥有 State，但不能拥有平台 IO。

### Facade

Facade 是 UI / ChatService 面对协议系统的稳定用例入口。它不是 protocol runtime 的别名，也不是
adapter 的包装名。

`MeshProtocolFacade` 必须拥有或聚合：

- 当前协议的 `IProtocolRuntime` strategy；
- 对应平台的 `IProtocolEffectExecutor` bridge；
- 构造 `RuntimeContext` 所需的 clock / self node / protocol facts provider；
- 将 `ProtocolEffects` 执行、记录、回灌 `TxResult`、并向上层返回 app-facing result 的编排逻辑。

Facade 必须暴露的是用例动作，而不是 wire 细节：

- `startTraceRoute(peer)`
- `exchangePosition(peer)`
- `requestNodeInfo(peer, wantResponse)`
- `sendText(channel, peer, text)`
- `sharePosition(...)`
- `shareWaypoint(...)`
- `handleIncoming(packet)`
- `handleTxResult(result)`
- `tick(nowMs)`

Facade 不得：

- 直接选择 Meshtastic portnum 或 MeshCore payload type；
- 编码 protobuf / MeshCore frame；
- 决定 PKI resync、NodeInfo reply、TraceRoute completion、ACK timeout 等协议语义；
- 执行 radio/storage/BLE 细节；
- 变成新的 God Object 或 Mediator。

Facade 的职责是隔离上层与协议编排。上层不应该直接拼 `ProtocolIntent` 后调用 runtime，
也不应该直接遍历 `ProtocolEffects` 后调用 executor。active UI / ChatService 入口必须迁入
`MeshProtocolFacade` 或本规格明确命名的等价边界。

`MeshProtocolFacade` 默认捕获 `EmitActionResultEffect`、`PublishIncomingTextEffect`、
`PublishIncomingDataEffect`、`PublishNodeInfoEffect` 等 app-facing projection，让 UI 可以从
`MeshProtocolFacadeResult` 读取结果而不把 projection 当平台 IO 执行。平台 adapter 需要把这些
projection 写入平台队列、路由表或 contact projection 时，必须显式选择
`ProtocolProjectionPolicy::ExecuteAppFacing`，这样 projection 处理策略是组合配置，而不是散落的
if 分支。

### Effect

Effect 是 runtime 要求外部世界发生的动作：

- `SendPacketEffect`
- `SendNodeInfoEffect`
- `SendRoutingErrorEffect`
- `SendTraceRouteEffect`
- `ForgetPeerKeyEffect`
- `RequestPeerNodeInfoEffect`
- `PublishIncomingTextEffect`
- `PublishIncomingDataEffect`
- `PublishNodeInfoEffect`
- `EmitActionResultEffect`
- `UpdatePeerRouteEffect`

Effect 是 runtime 和 executor 的桥。Effect 可以被记录、测试、重放或由不同平台执行。

### Executor

Executor 是平台执行者：

- ESP32 executor 调用 ESP radio/storage/BLE/event bus；
- nRF executor 调用 nRF radio/storage/mono UI app queues；
- test executor 记录 effects。

Executor 不得决定“该不该回复 NodeInfo”“该不该忘掉 PKI key”。它只执行 runtime 给出的 effects。

### Codec

Codec 只负责编解码：

- `MeshtasticCodec`：protobuf/wire packet/Data/Routing/RouteDiscovery；
- `MeshCoreCodec`：frame/header/direct data/group data/control/trace；
- codec 可以使用 Builder 风格的 build request，但不得保存业务状态。

### Product Composition / Factory

产品组合阶段负责把 `MeshProtocolFacade`、`IProtocolRuntime`、`IProtocolEffectExecutor`、codec 和平台
facts provider 接起来。这里使用 Abstract Factory / Factory Method 的意图是避免每个 UI 或 adapter
手动拼装协议对象。

嵌入式平台不要求动态分配。允许使用静态对象和返回引用的工厂：

```cpp
struct ProtocolRuntimeBundle
{
    MeshProtocol protocol;
    IProtocolRuntime* runtime;
    IProtocolEffectExecutor* executor;
    const IProtocolRuntimeContextProvider* contextProvider;

    MeshProtocolFacade createFacade(ProtocolProjectionPolicy policy);
};

ProtocolRuntimeBundle protocolRuntimeFor(MeshProtocol protocol,
                                         const ProtocolRuntimeSelection& selection,
                                         IProtocolEffectExecutor& executor,
                                         const IProtocolRuntimeContextProvider& contextProvider);
```

Factory 可以知道产品/平台能力，但不得决定协议语义。它选择“用哪套 runtime/executor”，不决定
“该不该回复 NodeInfo”或“TraceRoute 何时完成”。

### Incoming Handler Chain

Incoming packet 的分类可以局部使用 Chain of Responsibility，但顺序必须显式，不能形成隐式吞包。
handler 可以解密、分类、更新状态、产生 effects，但必须使用统一返回语义：

```cpp
enum class PacketHandling
{
    NotHandled,
    HandledContinue,
    HandledStop,
    DropWithEffects,
};
```

代码级边界是 `IProtocolRuntime::handleIncomingPacket(...) -> IncomingPacketHandlingResult`。
旧的 `handleIncoming(...) -> ProtocolEffects` 可以保留为兼容入口，但 active facade path 必须调用
`handleIncomingPacket(...)`，并且 shared tests 必须断言 `PacketHandling`。

Meshtastic incoming chain 的概念顺序必须是：

1. wire/Data 解码与 channel / PKI 解密事实建立；
2. routing app / routing error；
3. NodeInfo；
4. Position；
5. TraceRoute；
6. KeyVerification；
7. text / generic app-data projection。

MeshCore incoming chain 的概念顺序必须是：

1. frame/header/direct/group 解码与 peer identity facts 建立；
2. ACK；
3. control: discover / NodeInfo；
4. trace；
5. text / app-data projection。

这些 handler 可以先以函数存在，也可以之后拆成对象；但顺序和吞包语义必须属于 runtime/codec
边界，不能散落在平台 adapter 的任意 if 分支里。

## Primary Class Model

```mermaid
classDiagram
    class MeshProtocolFacade {
        +startTraceRoute(NodeId peer)
        +exchangePosition(NodeId peer)
        +requestNodeInfo(NodeId peer, bool wantResponse)
        +sendText(ChannelId channel, NodeId peer, string text)
        +sharePosition(PositionSnapshot position)
        +shareWaypoint(WaypointSnapshot waypoint)
        +handleIncoming(IncomingPacket packet)
        +handleTxResult(TxResult result)
        +tick(uint32_t nowMs)
    }

    class IProtocolRuntime {
        <<interface>>
        +prepareOutgoing(ProtocolIntent, RuntimeContext) ProtocolEffects
        +handleIncoming(IncomingPacket, RuntimeContext) ProtocolEffects
        +handleTxResult(TxResult, RuntimeContext) ProtocolEffects
        +tick(RuntimeContext) ProtocolEffects
    }

    class MeshtasticRuntime {
        -TraceRouteState traceRoute
        -PositionExchangeState positionExchange
        -PkiResyncState pkiResync
    }

    class MeshCoreRuntime {
        -MeshCoreTraceState trace
        -MeshCoreNodeInfoState nodeInfo
        -MeshCoreAckState ack
    }

    class ProtocolIntent {
        <<Command>>
    }

    class ProtocolEffect {
        <<Effect Variant>>
    }

    class IProtocolEffectExecutor {
        <<interface>>
        +execute(ProtocolEffect effect) bool
    }

    class EspProtocolExecutor
    class NrfProtocolExecutor
    class RecordingProtocolExecutor

    class ProtocolRuntimeFactory {
        +protocolRuntimeFor(MeshProtocol protocol, selection, executor, contextProvider) ProtocolRuntimeBundle
    }

    class MeshtasticCodec
    class MeshCoreCodec

    ProtocolRuntimeFactory ..> MeshProtocolFacade
    ProtocolRuntimeFactory ..> IProtocolRuntime
    ProtocolRuntimeFactory ..> IProtocolEffectExecutor
    MeshProtocolFacade o-- IProtocolRuntime
    MeshProtocolFacade o-- IProtocolEffectExecutor
    IProtocolRuntime <|.. MeshtasticRuntime
    IProtocolRuntime <|.. MeshCoreRuntime
    IProtocolEffectExecutor <|.. EspProtocolExecutor
    IProtocolEffectExecutor <|.. NrfProtocolExecutor
    IProtocolEffectExecutor <|.. RecordingProtocolExecutor
    MeshtasticRuntime ..> MeshtasticCodec
    MeshCoreRuntime ..> MeshCoreCodec
    IProtocolRuntime ..> ProtocolIntent
    IProtocolRuntime ..> ProtocolEffect
    IProtocolEffectExecutor ..> ProtocolEffect
```

## Pattern Relationship Map

```mermaid
flowchart LR
    UI[UI / ChatService] -->|Use-case command| Facade[MeshProtocolFacade]
    PlatformIn[Platform packet source] -->|IncomingPacket / TxResult / tick| Facade
    Facade -->|build ProtocolIntent| Intent[ProtocolIntent]
    Intent -->|Strategy dispatch| Runtime[IProtocolRuntime]
    Facade -->|direct incoming / tx / tick| Runtime
    Runtime --> MT[MeshtasticRuntime]
    Runtime --> MC[MeshCoreRuntime]
    MT -->|State| MTState[ACK / TraceRoute / Position / PKI states]
    MC -->|State| MCState[NodeInfo / Trace / ACK states]
    MT -->|Codec dependency| MTCodec[MeshtasticCodec]
    MC -->|Codec dependency| MCCodec[MeshCoreCodec]
    MT -->|Effects| Effects[ProtocolEffects]
    MC -->|Effects| Effects
    Effects -->|returned to Facade| Facade
    Facade -->|Visitor dispatch| Executor[IProtocolEffectExecutor]
    Executor -->|Bridge implementation| Esp[ESP executor]
    Executor -->|Bridge implementation| Nrf[nRF executor]
    Esp -->|Adapter| EspIO[ESP radio / storage / BLE]
    Nrf -->|Adapter| NrfIO[nRF radio / storage / queues]
```

## Runtime Call Flow

### Outgoing User Action

```mermaid
sequenceDiagram
    autonumber
    actor User as User/UI
    participant Facade as MeshProtocolFacade
    participant Runtime as IProtocolRuntime
    participant State as Protocol State
    participant Codec as Protocol Codec
    participant Executor as IProtocolEffectExecutor
    participant Platform as Platform IO

    User->>Facade: startTraceRoute(peer)
    Facade->>Facade: build TraceRouteIntent + RuntimeContext
    Facade->>Runtime: prepareOutgoing(TraceRouteIntent)
    Runtime->>State: allocate request id and enter pending state
    Runtime-->>Facade: ProtocolEffects(SendPacket/SendTraceRoute)
    loop for each effect
        Facade->>Executor: execute(effect)
        Executor->>Codec: build protocol packet/control payload
        Codec-->>Executor: EncodedPacket
        Executor->>Platform: radio/storage/event operation
        Platform-->>Executor: ok/fail
        Executor-->>Facade: EffectExecutionResult
    end
    opt tx failure / tx metadata
        Facade->>Runtime: handleTxResult(TxResult)
        Runtime-->>Facade: ProtocolEffects(EmitActionResultEffect...)
    end
    Facade-->>User: ActionHandle / app-facing result
```

### Incoming Packet

```mermaid
sequenceDiagram
    autonumber
    participant Platform as Platform IO
    participant Adapter as Platform Adapter
    participant Facade as MeshProtocolFacade
    participant Runtime as IProtocolRuntime
    participant Chain as Incoming Handler Chain
    participant State as Protocol State
    participant UI as UI/ChatService

    Platform->>Adapter: raw packet/frame
    Adapter->>Adapter: decode/decrypt enough to form IncomingPacket facts
    Adapter->>Facade: handleIncoming(IncomingPacket)
    Facade->>Runtime: handleIncomingPacket(IncomingPacket)
    Runtime->>Chain: decrypt/classify/route handlers
    alt NodeInfo request
        Chain->>State: apply NodeInfo rule
        State-->>Runtime: SendNodeInfoEffect
    else PKI unknown
        Chain->>State: apply PKI resync state
        State-->>Runtime: ForgetPeerKey/SendNodeInfo/SendRoutingError effects
    else Trace response
        Chain->>State: complete trace action
        State-->>Runtime: EmitActionResultEffect
    end
    Runtime-->>Facade: ProtocolEffects
    Facade->>Adapter: execute platform effects through executor
    Facade-->>UI: stable app-facing result
```

## Meshtastic Runtime Responsibilities

`MeshtasticRuntime` owns:

- app-data destination / ACK / response intent;
- NodeInfo request/reply/reannounce;
- Position request/reply/correlation;
- TraceRoute request/reply/result state;
- routing ACK/error interpretation;
- PKI unknown / stale-key / decrypt-fail resync state;
- duplicate-sensitive packet handling order.

It does not own:

- radio TX/RX calls;
- platform key-value storage;
- OLED/e-paper UI;
- BLE phone protocol transport;
- memory placement or ISR details.

Current C++17 migration state:

- Meshtastic TraceRoute and Position Exchange outgoing user actions can now enter
  `MeshtasticRuntime::prepareOutgoing(...)` as `TraceRouteIntent` / `ExchangePositionIntent`.
  The runtime chooses Meshtastic portnum, request id fallback, ACK/response flags, and protobuf payload shape,
  then emits `SendPacketEffect`.
- Meshtastic direct position sharing can enter the runtime as `SharePositionIntent`; the runtime delegates
  payload construction to `MeshtasticPositionCore`, selects `POSITION_APP`, and emits `SendPacketEffect`.
- nRF mono UI keeps a long-lived `MeshtasticRuntime`, executes `SendPacketEffect`s through
  `MeshAdapterProtocolEffectExecutor`, and feeds incoming packet / TX failure / tick events back into
  the runtime. TraceRoute and Position Exchange lifecycle state is owned by the runtime; UI only projects
  `EmitActionResultEffect` into logs/popups and no longer constructs `TRACEROUTE_APP` / `POSITION_APP`
  packets directly.
- Linux uConsole chat position sharing also uses `SharePositionIntent` and no longer constructs
  Meshtastic Position protobuf or portnum directly.
- Linux uConsole POI sharing uses `ShareWaypointIntent`; the runtime delegates payload construction to
  `MeshtasticWaypointCore`, selects `WAYPOINT_APP`, and emits `SendPacketEffect`.
- Linux uConsole also executes runtime packet effects through `MeshAdapterProtocolEffectExecutor`, so the
  workspace model no longer reads protocol portnum fields directly.
- Platform adapters still own physical radio send, local GPS source selection, BLE projection, queueing, and
  adapter-side incoming packet execution until those can be represented as runtime effects/state.

### Meshtastic PKI Resync State

PKI resync must be a State object, not scattered if branches.

```mermaid
stateDiagram-v2
    [*] --> Ready
    Ready --> LocalPkiNotReady: receive PKI packet while local PKI unavailable
    Ready --> PeerKeyMissing: receive PKI packet without peer key
    Ready --> PeerKeyStale: decrypt/auth failure with known peer key
    Ready --> PeerReportsUnknown: routing error PKI_UNKNOWN_PUBKEY
    Ready --> PeerReportsNoChannel: routing error NO_CHANNEL

    LocalPkiNotReady --> WaitingNodeInfo: SendNodeInfo + SendRoutingError(PKI_UNKNOWN)
    PeerKeyMissing --> WaitingNodeInfo: SendNodeInfo + SendRoutingError(PKI_UNKNOWN)
    PeerKeyStale --> WaitingNodeInfo: ForgetPeerKey + SendNodeInfo + SendRoutingError(PKI_UNKNOWN)
    PeerReportsUnknown --> WaitingNodeInfo: SendNodeInfo(want_response)
    PeerReportsNoChannel --> WaitingNodeInfo: SendNodeInfo(want_response)
    WaitingNodeInfo --> Ready: peer NodeInfo with key received
    WaitingNodeInfo --> Ready: timeout / user retry
```

Required effects:

| Input | Effects |
| --- | --- |
| Local PKI not ready | `SendNodeInfo(peer, wantResponse=true)`, `SendRoutingError(peer, request, PKI_UNKNOWN_PUBKEY)` |
| Peer key missing | `SendNodeInfo(peer, wantResponse=true)`, `SendRoutingError(peer, request, PKI_UNKNOWN_PUBKEY)` |
| Peer key stale/decrypt fail | `ForgetPeerKey(peer)`, `SendNodeInfo(peer, wantResponse=true)`, `SendRoutingError(peer, request, PKI_UNKNOWN_PUBKEY)` |
| Peer routing error `PKI_UNKNOWN_PUBKEY` | `SendNodeInfo(peer, wantResponse=true)` |
| Peer routing error `NO_CHANNEL` | `SendNodeInfo(peer, wantResponse=true)` |

The executor chooses how to perform those effects on ESP32/nRF. Runtime decides that they must happen.

## MeshCore Runtime Responsibilities

`MeshCoreRuntime` owns:

- MeshCore NodeInfo query/info control frame semantics;
- MeshCore discover request/response rules;
- MeshCore trace action lifecycle: pending -> delivered -> completed / failed / timed out;
- MeshCore app ACK lifecycle: pending -> completed / failed / timed out;
- route/identity policy when the platform declares support.

Current C++17 migration state:

- Direct-route send decision for ESP32 MeshCore direct text/app-data now uses shared
  `MeshCoreDirectRoutePolicy`: missing peer pubkey triggers discover/failure, selected routes use direct path,
  and route/channel fallback remains explicit without redefining direct-secret material.
- ESP32 MeshCore direct text/app-data no longer fires missing-key discover through an adapter-local send shortcut;
  it enters `DiscoverIntent -> MeshCoreRuntime -> SendDiscoverRequestEffect`, then the adapter executes the effect.
- ESP32 MeshCore receive-side missing-peer auto-discover now asks `MeshCoreRuntime` to apply the peer-hash
  validity and cooldown decision table. The adapter executes the emitted `SendDiscoverRequestEffect` and reports
  TX success back so runtime updates cooldown only after a successful discover request.
- Incoming MeshCore discover request/response control payloads now flow through `MeshCoreRuntime`: filter/since
  matching emits `SendDiscoverResponseEffect`, and discover responses emit `PublishNodeInfoEffect` plus
  `UpdatePeerRouteEffect`.
- MeshCore identity shared-secret expansion and nRF peer-key derivation now use shared
  `MeshCoreDirectSecretCore`; ESP32 still owns private-key storage and route pubkey lookup before delegating
  key expansion to the runtime helper.
- ESP32 no longer falls back to the historical group-secret-derived direct key; MeshCore direct secrets are
  identity/pubkey-derived only.
- Peer route storage, pubkey persistence, private identity storage, response scheduling, and frame transmission
  are still platform adapter responsibilities until they can be represented as runtime state/effects.

It does not pretend MeshCore is Meshtastic:

- no Meshtastic `TRACEROUTE_APP` for MeshCore trace;
- no Meshtastic `NODEINFO_APP` for MeshCore NodeInfo;
- no Meshtastic PKI assumptions for MeshCore identity/direct secrets.

### MeshCore NodeInfo Command Mapping

```mermaid
flowchart TD
    Intent[RequestNodeInfoIntent] --> Runtime[MeshCoreRuntime]
    Runtime --> Target{target?}
    Target -->|broadcast / 0 and want_response| BroadcastQuery[NodeInfo control query]
    Target -->|broadcast / 0 and no response| BroadcastInfo[NodeInfo control info]
    Target -->|unicast and want_response| DirectQuery[NodeInfo control query with request-reply]
    Target -->|unicast and no response| DirectInfo[NodeInfo control info]
    BroadcastQuery --> Effect1[SendNodeInfoEffect protocol=MeshCore peer=0 want_response=true]
    BroadcastInfo --> Effect2[SendNodeInfoEffect protocol=MeshCore peer=0 want_response=false]
    DirectQuery --> Effect3[SendNodeInfoEffect protocol=MeshCore peer=target want_response=true]
    DirectInfo --> Effect4[SendNodeInfoEffect protocol=MeshCore peer=target want_response=false]
```

nRF must not silently degrade `requestNodeInfo(dest, want_response)` into `sendAdvert(true)` once it claims
NodeInfo support. If nRF lacks a required route/identity capability, it must return unsupported or emit a
capability failure effect.

MeshCore NodeInfo control payload layout is shared codec territory, not platform adapter territory:

- portnum: `4`;
- prefix: `TM`;
- kind: `0x01`;
- query type: `0x01`, optional request-reply flag `0x01`;
- info type: `0x02`, followed by role, hops, node id, timestamp, 10-byte short name, 32-byte long name.

ESP32 and nRF may differ in how they route the resulting `SendNodeInfoEffect`, but they must use the same
codec and the same query/info decision table.

### MeshCore Trace

MeshCore trace must be represented as native MeshCore trace:

- outgoing: `TraceRouteIntent(peer)` -> MeshCore trace state -> `SendTraceRouteEffect`;
- executor resolves the platform route into MeshCore path-hash bytes and sends native `PAYLOAD_TYPE_TRACE`;
- incoming: trace packet handler accumulates path/SNR at the MeshCore frame layer, then reports terminal trace
  payloads back into runtime;
- UI consumes `EmitActionResultEffect`, not raw trace payloads.

Current C++17 migration state:

- trace base payload (`tag`, `auth`, `flags`) is built by shared MeshCore codec;
- target route hashes are appended after the 9-byte base payload. The frame `path` field is not the target route;
  it is the SNR/path trail accumulated while the trace packet is forwarded;
- trace payload decode decides `terminal`, `path_hash_size`, `offset`, `next_hash`, and trace hash slice in
  shared MeshCore codec;
- runtime decides whether the local pending trace is delivered, completed, failed, or timed out;
- ESP32 still owns the concrete relay scheduling and BLE `TraceData` event projection;
- nRF may use a minimal one-hop hash route when no route table exists, but it may not redefine completion policy.

### MeshCore App ACK

MeshCore app ACK has two different responsibilities:

- runtime responsibility: remember pending ACK signatures, bind an ACK signature to a local chat message id, match an
  incoming ACK, evict/timeout pending ACKs, and emit `EmitActionResultEffect`;
- adapter responsibility: compute the on-air packet signature from the encoded frame, transmit ACK frames, project
  completed ACKs into ESP BLE / EventBus events, and apply platform route penalties after timeout.

This split intentionally leaves direct ACK burst scheduling and multi-ACK frame construction in the adapter. Those are
radio execution details. The question "is this local send still pending, completed, failed, or timed out" belongs to
`MeshCoreRuntime`.

## Spec Conformance Criteria

不能只因为 runtime helper 被抽出来就宣称改造完成。满足本规格必须同时满足以下条件：

1. `IProtocolRuntime`、`MeshtasticRuntime`、`MeshCoreRuntime` 是真实代码对象，并有 shared tests 覆盖
   outgoing / incoming / tx result / tick。
2. `ProtocolIntent` 是 UI / usecase 进入协议系统的命令模型；active UI / ChatService 入口不得直接制造
   portnum、payload type、protobuf bytes 或 MeshCore control frame。
3. 跨时间协议会话必须由 runtime state 拥有。UI 和 adapter 不得独立维护 TraceRoute / Position
   exchange / PKI resync / ACK completion 的状态转移。
4. `IProtocolEffectExecutor` 或等价 executor bridge 是真实代码对象；平台只执行 `ProtocolEffect`，不决定
   协议语义。
5. `MeshProtocolFacade` 或明确命名的等价 facade 是真实代码对象。它必须是 UI / ChatService 面向协议系统的
   稳定用例入口，负责选择 runtime、构造 intent、执行 effects、回灌 tx result、返回 app-facing result。
6. Product factory / composition 不能散落在 UI 中。平台可以使用静态对象，但对象选择必须集中在产品组合层。
7. Incoming handler chain 的顺序和吞包语义必须写进 runtime/codec 边界，不能只靠平台 adapter 巨型 if
   分支维持。
8. 如果规格中的核心类名只存在于 Mermaid 图中，而代码里没有真实对象或等价声明，则该项未完成。

## Implementation Status

截至 2026-06-14，本规格中的核心 runtime architecture 已落成代码对象，并由 shared smoke /
platform build 覆盖：

1. `IProtocolRuntime`、`MeshtasticRuntime`、`MeshCoreRuntime` 是真实代码对象；
   `trailmate_meshtastic_runtime_smoke` 和 `trailmate_meshcore_runtime_smoke` 覆盖 outgoing /
   incoming / tx result / tick。
2. `MeshProtocolFacade` 是真实代码对象；`trailmate_mesh_protocol_facade_smoke` 覆盖 send text、
   trace、NodeInfo、position、incoming、tx result、tick，以及 UI capture / platform execute 两种
   projection policy。
3. `ProtocolRuntimeBundle`、`ProtocolRuntimeSelection`、`protocolRuntimeFor(...)` 是真实 product
   composition 边界；`trailmate_protocol_runtime_factory_smoke` 覆盖 protocol selection、
   invalid protocol、context provider 更新和 platform-style projection execution。
4. nRF mono UI、Linux uConsole 的 active protocol use-case path 已经通过 facade / factory 进入
   runtime；ESP32 与 nRF MeshCore adapter 的标准 NodeInfo、discover、incoming、tx result、tick path
   也通过 facade / factory 进入 runtime。
5. Incoming handler chain 已以 `PacketHandling` / `IncomingPacketHandlingResult` 显式化；
   `MeshtasticRuntime` 和 `MeshCoreRuntime` 都由 `handleIncomingPacket(...)` 组织 handler 顺序，
   shared tests 直接断言 `HandledStop` / `NotHandled`。
6. `MeshAdapterProtocolEffectExecutor` 和 platform adapters 只执行 `ProtocolEffect`。执行层可以选择
   projection policy，但不得重新决定协议语义。

### Accepted Runtime Extensions

以下保留项不是 legacy adapter 逻辑，而是目前尚未纳入通用 facade API 的 MeshCore runtime extension：

- MeshCore app ACK registration / binding / incoming ACK completion 仍通过
  `MeshCoreRuntime::trackAppAck(...)`、`bindAppAckToMessage(...)`、`handleAppAck(...)` 表达。ACK burst
  frame scheduling 和 multi-ACK frame construction 仍是 adapter IO。
- ESP32 receive-side missing-peer auto-discover 的 cooldown state 仍通过
  `prepareAutoDiscoverMissingPeer(...)`、`markAutoDiscoverMissingPeerTxResult(...)`、
  `resetAutoDiscoverState()` 表达。协议决策在 runtime，route cache 和 radio scheduling 仍在 adapter。
- ESP32 detailed discovery result (`MeshActionResult`) 仍使用 runtime effects 后由 adapter 映射成
  platform-specific detailed result；通用 `MeshProtocolFacadeResult` 目前不承诺替代该产品级结果类型。

这些 extension 可以在未来提升为 facade use-case API，但在提升前不得在 adapter 中复制协议决策表。

## Migration Rules

1. Add shared contracts first: Intent, Effect, Runtime, Executor.
2. Add a real Facade boundary before claiming UI / ChatService decoupling:
   - first create `MeshProtocolFacade` or update this spec with the exact equivalent code object;
   - facade must hide runtime selection and effect execution from active UI / ChatService paths;
   - add smoke tests that fail if the facade object disappears.
3. Add test-only `RecordingProtocolExecutor` to prove effects are inspectable.
4. Move one state machine at a time:
   - Meshtastic PKI resync;
   - Meshtastic TraceRoute/Position action lifecycle;
   - Meshtastic NodeInfo/Position reply packet construction;
   - MeshCore NodeInfo control payload build/parse;
   - MeshCore trace action lifecycle.
5. Replace adapter logic with `executeEffect(...)` calls only after the shared state test exists.
6. Update `MeshCapabilities` when a protocol action becomes truly shared and executable.
7. Keep existing helper functions only as codec/building utilities, not as orchestration owners.

## Review Checklist

Before changing protocol code:

1. Identify whether the change is Intent, Runtime, Effect, Executor, Codec, or UI projection.
2. Name the GoF pattern involved and why it is being used.
3. If changing a platform adapter, verify the change is only executor/adapter work.
4. If changing protocol semantics, put the rule in shared runtime and add a shared test.
5. If the spec names a core class or boundary, verify the code contains that object or an explicitly documented
   equivalent. Mermaid-only architecture does not count as implementation.
6. Update this spec when the pattern boundary changes.
7. Update `PROTOCOL_ADAPTER_DRIFT_AUDIT.md` when a drift item is resolved or accepted.
8. Run GitNexus impact analysis before edits and `detect-changes` before commit.

## Relationship To Other Specs

- `PROTOCOL_ADAPTER_PARITY_SPEC.md` defines required cross-platform protocol behavior.
- `PROTOCOL_ADAPTER_DRIFT_AUDIT.md` records current drift and migration status.
- `NODE_ACTION_PROTOCOL_SPEC.md` defines user-facing legality of node actions.
- This document defines the design-pattern architecture used to remove drift.
