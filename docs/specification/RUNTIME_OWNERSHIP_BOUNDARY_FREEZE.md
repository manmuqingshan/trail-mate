# Runtime Ownership Boundary Freeze

Status: normative

本文档冻结 Trail Mate runtime 关键机制的 ownership 边界。它不是阶段计划，也不是事故复盘。
后续修改必须先遵守这里的 owner 关系，再考虑局部实现。不能再用页面补丁、协议旁路、
storage 双写侥幸、或资源临时判断去绕开主路径。

如果本文档与旧的实现说明冲突，以本文档为准；旧文档必须被更新，而不是让代码继续选择
更方便的旁路。

## One Rule

一个事实只能有一个权威 owner。

其他模块只能提交 intent、消费 projection、或执行 owner 给出的 effect。任何模块只要同时
“读取事实、改写事实、解释失败、刷新 UI”，就已经越界。

## Scope

本文档冻结以下机制：

1. UI 与 runtime 的分工。
2. MT / MC / RT 三协议消息投递状态。
3. read / unread / badge 状态。
4. Reticulum direct / propagation 去重与确认。
5. Reticulum Sideband/LXST call 与 realtime resource lease。
6. MQTT downlink 到 LoRa 的空口预算与 UI 非阻塞关系。
7. 外部字体和语言包加载。
8. Contacts / Network 投影分类。
9. God file 拆解后的 owner 迁移规则。

## Non-Negotiable Boundaries

### UI

UI 只允许：

1. 发出用户 intent。
2. 展示 projection snapshot。
3. 展示 runtime 明确给出的 pending / failure / progress。
4. 管理页面本地选择、滚动位置、焦点和可见导航。

UI 不允许：

1. 直接解析协议包、announce、LXMF envelope、Meshtastic protobuf 或 MeshCore frame。
2. 直接改 read/unread、delivery、contact、path、link、call、font loaded 状态。
3. 为了解决显示问题私自加载字体或访问 SD 字体文件。
4. 为了 call、download、MQTT 或 LoRa 直接停止硬件资源。
5. 通过隐藏 badge、刷新列表、删除 item 等方式伪装业务状态已经改变。

### Settings

Settings 只提交 product intent。

Settings 不允许决定协议内部 wire profile、packet context、call fallback、resource lease 或
字体加载策略。Settings 可以选择 active protocol、Wi-Fi profile、Reticulum gateway、通知策略、
音量和 locale，但不能把这些选择实现成绕过 runtime owner 的私有分支。

### Protocol Adapters

MT / MC / RT adapter 可以拥有 wire codec、平台 IO 适配、队列接入和协议 runtime 组合。

它们不允许拥有通用业务状态。消息状态、read/unread、conversation badge、联系人投影、
发送重试、去重 ledger 必须进入共享 owner，再投影给 UI。

协议差异必须以 protocol-aware event 表达，而不是把 UI 或 ChatService 退回到 bare msg_id、
node id 或 packet id。

### Store And Index

index、conversation list、message list cache、header mirror 都是 projection 或 cache。它们可以
加速显示，但不能成为业务事实权威。

如果一个状态重启后应该保持一致，它必须有独立 owner 或 ledger。靠多个文件同时写成功来维持
状态，属于未收敛设计。

## Authoritative Owners

| Fact | Owner | Projection | Hard invariant |
| --- | --- | --- | --- |
| Outgoing/incoming message identity | `MessageLedger` | Chat message rows | MT/MC/RT 都必须带 protocol-aware identity，不得只靠裸 `msg_id` |
| Delivery state | `MessageLedger` + `ChatDeliveryEventProjector` | Message badge, feedback | `Delivered` 必须来自 ACK/proof/receipt 或协议等价事实 |
| Read/unread state | `ReadStateLedger` | Conversation badge, unread budget, app badge | Index/header 只能镜像，不能是权威 |
| Conversation list | `ConversationProjectionStore` | Chat workspace snapshot | 可重建，不得反向改 ledger |
| UI chat state | `ChatWorkspaceModel` | Renderer | 只保存 selection/offset，不保存业务状态 |
| Runtime events | `ChatPageRuntimeEventPump` | UI refresh sink | 事件泵路由事件，不渲染、不推断业务结果 |
| Reticulum destination | `DestinationRegistry` | Contacts/Network row | Full destination hash/aspect 是权威，projected node id 不是 |
| Reticulum path | `PathManager` | Path diagnostics, send eligibility | Freshness/replay/coalescing/expiry 只能在一个地方裁决 |
| Reticulum link | `LinkManager` | Call/link status | Link open/identify/keepalive/close 只有一个 lifecycle owner |
| Reticulum announce ingest | `AnnounceIngestor` | Contacts/Network/propagation metadata | 验签、identity/destination 关联、path observation 统一完成 |
| Reticulum packet routing | `ReticulumPacketRouter` | Domain events | Packet type/context 到 owner 的路由只有一个入口 |
| Propagation sync | `PropagationClient` + propagation seen/ack ledger | Chat projection | 重复 offer 不能产生重复消息或重复 unread |
| Reticulum call | `LxstTelephonyClient` | Call Page projection | 产品 call path 只支持 Sideband/LXST |
| Call resources | Call realtime leases + `WifiAccessRuntime` | Call Page progress/failure | UI 不直接抢占 Wi-Fi/LoRa/GPS/audio |
| Audio hardware | Platform audio adapter | Ring/call volume projection | ES8311/I2S/mic/speaker setup teardown 只有一个 owner |
| Notification policy | Notification policy runtime | Tone/vibration/notice intents | 消息提示、联系人提示、静音/震动/音量只消费业务 projection |
| Font loading | `FontRuntimeCoordinator` + `ResourcePackRegistry` | Loading page/modal + refreshed font chain | 缺字不得被 active locale 或 hot-path 永久拦掉 |
| MQTT downlink relay | Meshtastic runtime TX queue / air-time budget owner | Send/deferred/drop state | UI 不等待 LoRa TX，MQTT burst 不直接占满 UI tick |

## Runtime Overview Design

概要设计固定为四个 runtime 面向产品组合，而不是页面补丁组合：

```text
Product intent
  -> Protocol facade
  -> Domain owner
  -> Ledger / queue / lease
  -> Projection
  -> UI renderer
```

1. Reticulum call 由 `LxstTelephonyClient` 拥有协议事实，由 Call realtime leases
   拥有 Wi-Fi/LoRa/GPS/sleep/audio 资源事实，由 Call Page 展示 projection。
2. Notification 由 Notification runtime 拥有产品策略事实，platform audio adapter 拥有
   ES8311/I2S/扬声器/麦克风硬件事实。消息事件、联系人事件和 Settings 预览只能提交
   notification intent。
3. Contacts 只消费 person/contact projection。Network 消费 service/relay/web/unknown
   projection。二者都不解析 Reticulum announce。
4. LoRa TX 由协议 adapter 的 TX scheduler 拥有空口事实。业务层只能 enqueue。
   `sendAppData()` 成功表示进入 scheduler，不表示已经占用空口发射完成。
5. `LxmfAdapter` 只能作为 Reticulum facade/coordinator shell 存在。新增功能必须优先落在
   DestinationRegistry、PathManager、LinkManager、AnnounceIngestor、ReticulumPacketRouter、
   PropagationClient、PingService、NetworkPageClient 或 LxstTelephonyClient。
6. Adapter 内不允许重新引入独立的调度状态、RX 统计状态、deferred discovery queue
   或 MTU scratch 数组；这些事实分别属于 `RuntimeBudget`、`RawRxTelemetry`、
   `DeferredDiscoveryQueue` 和 `AdapterScratchBuffers`。

## Runtime Detailed Design

### Reticulum Call

详细设计：

1. 产品 call profile 固定为 Sideband-compatible `lxst.telephony`。
2. 用户主动拨出直接进入 hard preempt，因为用户已经明确提交通话 intent。
3. 来电 LinkRequest 可进入 identifying/ringing 资源阶段；接听前不报告已接通。
4. 接听必须先拿到 hard realtime lease，再启动 media session。任一步失败都进入明确失败，
   不自动接听。
5. 通话中只允许当前 `link_id` 的 LXST audio RX/TX。
6. 挂断/远端关闭/媒体失败/timeout 必须统一进入 Closing，再释放 lease。
7. MeshChat `call.audio` 只允许作为默认不注册的源代码兼容 adapter，不允许进入 product
   Settings、不允许自动 fallback、不允许主 LXST path 分支依赖它。

### Notification And Audio

详细设计：

1. `ChatNewMessageEvent`、`NodeInfoUpdateEvent`、Settings 音量预览都必须调用
   Notification runtime。
2. Notification runtime 读取 message alerts、contact alerts、vibration、tone volume 等
   product policy，输出 tone/vibration intent。
3. Notification runtime 不允许解析消息协议、不允许改 unread、不允许绕过 platform audio
   adapter。
4. Call ring 和 call media 仍由 Call realtime/audio owner 控制；Notification runtime
   不得在 `ActiveCall` 抢占通话音频。
5. Platform audio adapter 是唯一硬件 owner，负责 ES8311/I2S/mic/speaker session open、
   volume、gain、mute、teardown。

### Contacts / Network

详细设计：

1. Contacts 使用 `ReticulumContactProjectionPolicy`，只投影有效 LXMF address/person 记录：
   favorite/manual/import 为 Contact，runtime announce 为 Announced，ignored 为 Ignored。
2. Contacts 不显示 propagation、Nomad/web/service、unknown、gateway、interface 或 path hop。
3. Network 使用 `ReticulumNetworkProjectionPolicy`，投影非联系人 announce：
   `lxmf.propagation` 为 Message Relay，`nomadnetwork.node` 为 Web/Service，
   `lxst.telephony`/legacy `call.audio` 为 Telephony Service，unknown 为 Unknown Service。
4. PropagationClient 可以后台维护 relay metadata；UI 是否显示 relay 由 Network projection
   policy 决定，不能通过 Contacts 旁路显示。
5. Destination hash 和 projected node id 只是地址/搜索 metadata，不是联系人身份权威。
6. `PeerDirectoryService` 拥有 Reticulum peer directory 的读写、热加载和投影队列；
   adapter 只作为 `IPeerProjectionSink` 发布最终 NodeInfo/Protocol update event。

### Reticulum Runtime Owners

详细设计：

1. `RuntimeBudget` 是 call/nomad/sleep/saver/P4 screen 阶段的唯一调度策略输出。
   adapter 只能提供输入事实，不能复制阶段判定。
2. `AnnounceScheduler` 拥有本机 announce pending、retry、interval 和 rebroadcast
   节流状态。adapter 只执行签名、组包和实际 TX。
3. `DeferredDiscoveryQueue` 拥有 public discovery 的 bounded queue、drop-oldest 和
   packet-hash 去重。adapter 只判断是否 defer 和如何 replay。
4. `RawRxTelemetry` 拥有 RX summary counters、LoRa discovery detail 抑制和 LoRa
   ignored announce 抑制。adapter 不保存这些 counter。
5. `AdapterScratchBuffers` 是 MTU 级 packet scratch 的长期 owner。新增 MTU buffer
   不能以裸字段散落在 adapter。

### LoRa TX Scheduler

详细设计：

1. 所有会占用 LoRa 空口的发送都必须进入同一个 scheduler tick。
2. `sendText()`、`sendAppData()`、key verification、runtime protocol effects、MQTT
   downlink relay、ACK retry 都不能从 UI/event/RX path 直接同步阻塞 radio TX。
3. 每个 tick 持有 `kLoRaAirTxBudgetPerTick`。协议动作、ACK retry、普通消息、MQTT
   downlink 成功 enqueue radio TX 时都消耗这个预算。
4. `min_tx_interval_ms_` 是跨 TX owner 的共享节流，不是某个队列自己的局部判断。
5. MQTT downlink 保持官方 gateway relay 语义，但必须按 `from + id + channel` 去重，
   入队，按预算 drain；队列满必须产生 drop/deferred reason，而不是卡 UI。
6. UI 只能展示 Queued/Sending/Sent/Delivered/Failed 或 deferred/drop projection，不能等待
   LoRa TX 完成后才继续渲染。

## Notification Policy Contract

通知策略是 product policy，不是消息存储、协议 adapter 或音频驱动的副作用。

Settings 可以配置：

1. message alerts enabled/disabled。
2. contact alerts: none / contacts only / all discovered people，或等价用户可理解选项。
3. vibration enabled/disabled。
4. message tone volume。
5. call ring volume。

通知 runtime 只能消费：

1. message projection。
2. contact/person projection。
3. read/unread projection。
4. user notification policy。
5. active interruption/call state。

通知 runtime 可以输出：

1. play message tone intent。
2. start/stop call ring intent。
3. vibrate intent。
4. on-screen notice intent。

它不允许：

1. 自己判定消息 delivered。
2. 自己清 unread。
3. 直接解析 protocol packet。
4. 绕过 platform audio adapter 播放声音。
5. 在 call active/exclusive 时启动非通话音频。

消息提示音、来电铃声、Settings 音量预览、通话播放都必须经过同一个 platform audio owner。
如果音频 owner 不可用，通知 runtime 只能得到显式失败或 deferred 结果，不能静默吞掉声音。

## Message State Contract

消息状态是抽象业务状态，协议 adapter 只负责把协议事实映射进它。

允许的业务状态：

1. `Queued`: 已进入本地 outbox 或等待 runtime 机会。
2. `Sending`: 正在发送或等待协议收据。
3. `Sent`: 已发出但协议没有或不承诺端到端送达证明。
4. `Delivered`: 已收到 ACK、proof、receipt 或协议定义的等价送达事实。
5. `Failed`: 发送被拒绝、无线发送失败、ACK 超时、资源不可用或协议不支持。

规则：

1. MT direct 且需要 ACK：`Queued -> Sending -> Delivered/Failed`。
2. MT broadcast/group 或 ackless 成功：`Queued -> Sending -> Sent`。
3. MC app ACK 完成：进入 `Delivered`。
4. MC app ACK 超时：进入 `Failed(AckTimeout)`。
5. RT LXMF proof/receipt 完成：进入 `Delivered`。
6. RT propagation 本地接收成功不等于远端 delivered；它只证明本机 durable accepted。
7. 同一个裸 `msg_id` 出现在 MT/MC/RT 时，只能更新匹配 protocol 的 message ref。
8. UI badge 可以只显示简化文字，但状态来源必须是 ledger/projection。

禁止：

1. 继续发只有 `msg_id + bool` 的最终业务事件作为新路径。
2. 让 renderer 根据“发送函数返回 true”显示已送达。
3. 在 retry、delivery action、presentation lookup 中丢掉 protocol 字段。
4. 因为找不到消息就创建另一个同内容 outgoing item。

### Message Persistence And Publication

消息内容、发送状态和 UI/通知事件必须走同一条 ledger 主路径：

```text
decoded incoming
  -> MessageLedger durable append or bounded deferred queue
  -> incoming delivery commit
  -> ChatModel / EventBus / notification
  -> conversation projection

outgoing protocol acceptance
  -> MessageLedger durable append or bounded deferred queue
  -> protocol-aware delivery event
  -> conversation projection
```

硬约束：

1. incoming 只有在 authoritative message record、dedup identity 和 read-state commit
   成功后才能发布 `ChatNewMessageEvent`、通知、震动或声音。
2. SD/SPI 暂时不可用时，incoming 进入固定深度 deferred queue；重试成功后只提交和发布
   一次。队列满必须输出明确 rejected/drop reason，并向支持 two-phase commit 的协议返回失败。
3. outgoing record 或后续 status 写入失败时，由 `MessageLedger` 保留 bounded pending write；
   message page、conversation page 和 lookup 必须合并该 pending state，不能谎报 `stored`，
   也不能让 UI 另造临时消息。
4. pending write 每个 runtime tick 只允许执行有限预算。ESP chat store 必须调用 storage
   service 的非阻塞语义接口；设备暂时不可用时立即 deferred，不能在同一 tick 连续等待多个
   250ms SD 操作。
5. conversation index、header mirror 和 UI cache 仍然只是 projection。projection 写失败可以让
   ledger operation 保持 pending，但不得触发收发热路径中的同步全盘 `rebuildIndex()`。
6. message/status retry 必须幂等。已经写入 conversation log、但后续 ledger/projection 写失败的
   消息，重试时只能完成未完成的提交，不能追加第二条相同记录。
7. chat workspace 最新页固定为 10 条；翻页继续使用同一个 ledger page API。runtime event 对
   当前会话最多触发一次 snapshot reload，不允许辅助函数和调用者各做一次全量重建。

禁止：

1. `appendIncomingDurably(...) == false` 后直接 `continue` 并丢弃消息。
2. 调用返回 `void append(...)` 后无条件记录 `stored`。
3. UI 监听 raw MQTT/LoRa packet，或在持久化失败时自行合成 message bubble。
4. 为了修复通知而绕过 durable commit 直接调用 notification/audio。
5. index 写失败后在消息收发 tick 内同步扫描所有 conversation log。

## Read And Unread Contract

`ReadStateLedger` 是 read/unread 的唯一权威。

它必须表达：

1. protocol。
2. conversation identity。
3. last read durable cursor 或等价 read watermark。
4. commit 状态。
5. 必要时的 pending/failed mark-read 结果。

读取规则：

1. unread count 由 `MessageLedger + ReadStateLedger` 推导。
2. conversation index、SD header、app badge、screen badge 都是投影。
3. 重启后必须从 ledger 恢复同一个 unread 结果。
4. projection 可以落后，但不能与 ledger 长期冲突。

写入规则：

1. `ChatWorkspaceModel::markRead(...)` 只是 UI intent。
2. `IChatActionSink` 把 intent 交给 app/runtime service。
3. app/runtime service 提交 `ReadStateLedger`。
4. projection store 收到 committed 或 pending 事实后刷新 badge。
5. durable commit 失败时必须保留可解释失败或 pending，而不是 UI 假成功。

禁止：

1. 只改 index/header 却不改 ledger。
2. 只在 UI 隐藏 unread badge。
3. read 状态以某个页面是否打开作为权威。
4. Reticulum direct 和 propagation 两条路径各自增加 unread。

## Reticulum Client Contract

Trail Mate 是 Reticulum client，不是通用 transport node、propagation node、gateway 或 service host。

产品能力固定为：

1. LXMF direct delivery。
2. LXMF propagation retrieval。
3. client 所需的 path discovery、identity recall、link lifecycle、proof/receipt。
4. Sideband-compatible `lxst.telephony` call。
5. Nomad/Micron 服务发现和浏览，投影到 Network。

Reticulum 主路径必须遵守：

1. `ReticulumPacketRouter` 是唯一入口。
2. `AnnounceIngestor` 统一完成 announce 验签、identity/destination 关联和 path observation。
3. `DestinationRegistry` 拥有 destination truth。
4. `PathManager` 拥有 path truth。
5. `LinkManager` 拥有 link truth。
6. `MessageLedger` 拥有 LXMF idempotency。
7. `PropagationClient` 拥有 propagation offer/ack/seen。
8. `LxstTelephonyClient` 拥有 call truth。

禁止：

1. UI、notification、Settings 或 Contacts 解析 Reticulum wire bytes。
2. `LxmfAdapter` 再次拥有 path、link、message、propagation、call 主状态。
3. 在主 LXST call path 中加入 MeshChat `call.audio` fallback 分支。
4. 在 product Settings 中显示 call protocol selector。

MeshChat `call.audio` 可以保留为源代码兼容/协议研究 adapter，但默认不注册、不进入产品图、
不自动 fallback、不作为用户可选配置。

## Reticulum Propagation Contract

Propagation 的重复 offer 是 Reticulum/LXMF 网络行为的一部分；重复展示给用户不是可接受行为。

规则：

1. Direct 和 propagation 必须在 LXMF envelope validation 之前或之中汇合到同一 message ledger。
2. 完整 LXMF message hash 是跨重启、跨 direct/propagation 的 idempotency key。
3. 重复 offer 可以更新 transport metadata、last seen、source path，但不能创建新消息。
4. 本机只有在消息 durable accepted 后才发送 propagation acknowledgement。
5. ack/seen ledger 必须能跨重启阻止重复用户可见 delivery。

禁止：

1. propagation 每次拉取都 append 聊天记录。
2. ack 在 durable message commit 前发出。
3. 用 sender + timestamp + text 这种弱 key 替代 LXMF hash。
4. direct 和 propagation 各自维护重复检测。

## Call Realtime Contract

产品 call path 是 Sideband-compatible LXST。接听体验是 Call Page，不是 UI modal。

状态：

1. `Idle`
2. `IncomingIdentifying`
3. `IncomingRinging`
4. `PathResolving`
5. `LinkConnecting`
6. `ResourceAcquiring`
7. `MediaPreparing`
8. `Active`
9. `Closing`

资源规则：

1. Incoming identifying/ringing 拥有 Call Page，并 soft-preempt Wi-Fi。
2. Incoming ringing 暂停 LoRa 和 GPS。BLE 在 ESP 产品固件中不编译，不存在 runtime lease。
3. 用户主动拨出直接进入 hard preempt。
4. 用户接听后先获取 hard preempt 和 audio session，再报告接听成功。
5. Active/Closing 阶段只允许当前 call link 的音频流量。
6. 非可中断 critical operation 导致接听/拨出失败，不自动接听。
7. 同时来电或通话中来电必须快速失败，不能排队成另一个 UI call。
8. Closing 持有 exclusive lease，直到 LinkClose 发出/观察到或 bounded cleanup 完成。

UI 规则：

1. Call Page 展示 caller、identifying/connecting/active/closing/failure。
2. 接听、拒接、挂断、音量快捷键是页面 action。
3. 页面底部展示通话期间可用的快捷键。
4. Call Page 不直接停止 Wi-Fi、LoRa、GPS、audio 或 MQTT。

Audio 规则：

1. Platform audio adapter 拥有 ES8311/I2S/mic/speaker setup/teardown。
2. Ring tone、message tone、settings tone、call playback 都必须进入同一 audio owner。
3. 接听后默认 speaker volume 可提升到通话 profile 的最大安全音量。
4. RX decode/playback 与 TX capture/encode 不能互相阻塞。
5. 任何 echo suppression、gain、jitter buffer 改动必须属于 media session，不得散落在 UI。

## MQTT Downlink And LoRa Air-Time Contract

Meshtastic MQTT downlink 可以保持 gateway 语义，但必须经过统一空口预算。

规则：

1. MQTT downlink 先进入 projection/ingest，不直接在 MQTT callback 中同步 LoRa TX。
2. LoRa TX 进入统一 TX queue 和 air-time budget。
3. downlink relay 必须按 `from + packet id + channel` 强去重。
4. 每个 tick 限制 downlink drain 数量。
5. UI 只消费 projection，不等待 LoRa TX 完成。
6. 队列满或预算不足时，消息状态进入 queued/deferred/drop reason，而不是卡住 UI。
7. LoRa 空口长包、重复 burst、route flood 不能占用 display/input wake path。

禁止：

1. MQTT callback 直接循环发 LoRa。
2. 为了防卡死永久禁掉 downlink-to-LoRa 官方语义。
3. 让 UI tick 承担 relay flush。
4. 没有去重地把同一 downlink burst 多次打到空口。

## Network Page Cache Worker Contract

Nomad page cache 读取属于后台存储工作，Network 页面只提交请求和读取投影。

规则：

1. page body、完成状态和请求状态必须由 `PageCacheLoadState` 唯一持有；大块 state 必须放在
   PSRAM，不允许静默回退到 internal heap。
2. PageCache worker 的 stack/TCB 必须在进入 Network 前具有确定的内存所有权，不能在页面
   tick 中反复动态分配 task stack。
3. worker 只有 `not-started -> running` 或 `not-started -> unavailable` 两条启动路径；启动失败
   是可观察的终止状态，不能把 mutex/queue 已创建误认为 worker 可用。
4. `request_cached_page_load()` 和 `poll_cached_page_load()` 在 worker unavailable 时必须快速返回
   明确状态，不能继续排队、等待 SD，或在每帧重新创建 task。
5. cache read/write 必须经过 PageCache storage service；UI 不等待设备存储事务完成。

禁止：

1. `xTaskCreate()` 失败后保留一个看似可用、实际没有 consumer 的 queue。
2. 在 Network render/timer 路径逐帧重试 task 创建并刷日志。
3. 为解决 internal heap 碎片而把 PageCache state 或不适合的 task stack 随意回退到 PSRAM。
4. task 创建失败后退回 UI 线程同步读取 Nomad page cache。

## Font And Localization Runtime Contract

字体是 runtime resource，不是页面私有修复点。

核心规则：

1. CJK/Japanese/Korean/Arabic 等 content text 的字形需求不由 active display locale 决定。
2. `active_locale=en` 时，如果聊天、Network、Contacts 或 Nomad 页面出现中文内容，已安装且可用的
   `zh-hans-core` 等 content supplement 仍必须允许加载和加入 content font chain。
3. 缺字检测可以发生在内容路径，但加载决策必须交给 `FontRuntimeCoordinator` /
   `ResourcePackRegistry`。
4. 同步外部字体加载是允许的，但只能作为用户可见的 foreground operation：
   显示 loading/progress/busy 页面或 modal，flush 到屏幕，然后交给字体设备服务加载。
5. 普通 render/list/timer 路径不得无主静默阻塞 SD IO。
6. 总线忙、内存不足、文件损坏必须形成可解释诊断和重试/失败状态，不能被永久 hard skip。
7. 页面不得因为 `ui_hot_path`、`active_locale`、或 `content_supplement` 标签直接否决字体加载。

禁止：

1. 页面/widget 直接读取 `font.bin`。
2. 在 renderer 中用“非 ASCII 就切 CJK 字体”的旁路替代 font chain。
3. 以 active locale 不是中文为理由阻止中文 content font。
4. 以保护 UI 为理由让中文永久显示 tofu boxes。
5. 创建 LVGL modal 但未 flush 就进入 `lv_binfont_create()`。

## Contacts And Network Projection Contract

Contacts 按字面意思，只显示可通信的人或身份。

允许进入 Contacts：

1. verified LXMF person destination。
2. verified LXST telephony destination。
3. 能关联到同一个身份的人名、短名、地址和可通信 destination。

不得进入 Contacts：

1. propagation node。
2. gateway/interface/path hop。
3. Nomad/web/service。
4. unknown announce。
5. relay-only 或 message infrastructure。

Network 显示网络能力和服务：

1. Nomad/Micron service。
2. web/service destination。
3. propagation node 状态。
4. gateway/interface diagnostics。
5. path/interface health。

Contacts 和 Network 都只能消费 projection，不允许读取 raw announce 或直接维护 protocol truth。

## God File Burn-Down Contract

拆 God file 不是物理拆文件，而是 owner 迁移。

每迁移一个事实必须一次完成：

1. 建立 owner。
2. 迁移状态和不变量。
3. Adapter 调用 owner。
4. 删除 Adapter 中旧状态和旧 mutation。
5. 增加或更新合同测试。

完成前不得宣称 facade 化。`LxmfAdapter` 只有在以下条件满足时才算 facade：

1. 对外方法只转发 use-case。
2. path/link/destination/message/propagation/call state 不在 adapter 内直接写。
3. UI 和 Settings 不读 adapter 内部协议细节。
4. old mutation 已删除，而不是留作 fallback。
5. compatibility code 与 product graph 隔离。

## Protocol-Partitioned Storage V2 Contract

ESP Arduino 产品运行时的 Chat/Peer/Contact 持久化只允许使用：

```text
/data/v2/mt
/data/v2/mc
/data/v2/rt
```

权威规则：

1. message journal 是消息事实；catalog/read/status 是可重建 projection。
2. RT message journal 是 LXMF seen ledger 的重建来源。
3. `SdProtocolPeerRepository` 是 peer facts 和 contact user facts 的唯一 owner。
4. `INodeStore`、`IContactStore` 是 repository view，不是独立 store。
5. contact alias/favorite/ignored/trusted 只写 contact journal，不重复写 peer slot。
6. active protocol 必须在 application query 边界过滤，UI 不读取三协议全量后再过滤。
7. nearby 只能淘汰 unprotected peer；contact 和 conversation reference 永远受保护。
8. snapshot 只能通过 temp/backup/final 原子替换；掉电后只恢复 v2 backup。
9. 大 projection/peer/contact/pending buffer 优先使用唯一的 strict PSRAM allocator。
10. 启动阶段可以做有界 compaction；普通 UI tick 不得反复扫描或重写完整 projection。

ESP product graph 禁止重新注册：

```text
/chat/*
/nodes.bin
/contacts.dat
/mesh/peers.bin
```

禁止以“兼容”为名在 v2 失败后读取旧格式。完整规则见
`PROTOCOL_PARTITIONED_STORAGE_V2_SPEC.md`。

## Prohibited Patch Patterns

以下修改方式禁止进入主线：

1. 页面级特殊判断修复协议或存储问题。
2. `if (reticulum_call::realtime_mode_active()) return;` 这类散落资源判断。
3. 绕开 `ChatDeliveryEventProjector` 更新消息状态。
4. 绕开 `ReadStateLedger` 更新 unread。
5. 绕开 `MessageLedger` 接收 direct/propagation 消息。
6. 绕开 `FontRuntimeCoordinator` 加载或拒绝字体。
7. 绕开 `WifiAccessRuntime` 抢占 Wi-Fi。
8. 在 Settings 中暴露尚未形成产品闭环的兼容/实验协议分支。
9. 为了让当前 case 通过而让主路径永远不命中。
10. 新增一个 owner 但不删除旧 owner。

## Change Gate

修改实现前必须回答：

1. 这次改动的事实 owner 是谁？
2. intent 从哪里进入？
3. effect 由谁执行？
4. projection 从哪里产生？
5. durable state 在哪里提交？
6. 重启后如何恢复？
7. 协议字段是否保留 protocol-aware identity？
8. UI 是否只看 snapshot/projection？
9. 资源 lease 是否由 runtime owner 申请？
10. 是否存在旧旁路仍可命中？

如果任一问题没有答案，先补 owner/spec/test，再改实现。

## Required Regression Contracts

后续相关修改至少需要覆盖以下合同：

1. `active_locale=en` 时中文聊天、Network/Nomad 内容能触发受控字体加载并最终使用 content font。
2. 字体加载前用户能看到 loading/progress/busy 状态，且不是静默 SD 阻塞。
3. mark-read durable commit 后重启 unread 不复活。
4. mark-read commit 失败不会让 UI 假装成功。
5. direct 与 propagation 同一 LXMF hash 只产生一条消息和一次 unread transition。
6. MT/MC/RT 相同裸 id 只更新对应 protocol 的 delivery/read 状态。
7. ackless send 不会永久显示 Sending。
8. failed send 有 protocol-aware failure kind。
9. MQTT downlink burst 不阻塞 UI wake/render。
10. MQTT downlink relay 经 LoRa queue/air-time budget 和去重。
11. Incoming/active call resource lease 阶段与 UI Call Page 状态一致。
12. 通话中再次来电快速失败。
13. call ring、message tone、settings tone 和 call playback 都经过同一个 audio owner。
14. message alerts/contact alerts/vibration/audio volume 策略不改变 delivery 或 unread 事实。
15. Contacts 不出现 propagation、service、gateway/interface、unknown announce。
16. Network 能呈现服务和网络基础设施，不污染 Contacts。
17. MQTT/LoRa/RT durable message 不因 catalog projection 失败而不展示或不通知。
18. peer refresh 不会覆盖 contact alias/flags 或已验证 key。
19. nearby 达到容量时不会淘汰 contact 或已有 conversation peer。
20. RT seen journal 损坏后从 authoritative RT message journal 重建，历史消息不重复投影。

## Relationship To Existing Specs

本文档是总边界冻结文档。相关细节继续由以下文档维护：

1. `docs/specification/CHAT_DELIVERY_RUNTIME_SPEC.md`
2. `docs/specification/CHAT_WORKSPACE_MODEL_SPEC.md`
3. `docs/specification/CHAT_PRESENTATION_IDENTITY_SPEC.md`
4. `docs/specification/LOCALIZATION_SPEC.md`
5. `docs/specification/PROTOCOL_RUNTIME_DESIGN_SPEC.md`
6. `docs/reticulum_client_architecture.md`
7. `docs/wifi_access_resource_policy.md`
8. `docs/MULTI_PROTOCOL_SUPPORT.md`
9. `docs/specification/PROTOCOL_PARTITIONED_STORAGE_V2_SPEC.md`
10. `docs/design/PROTOCOL_PARTITIONED_STORAGE_V2_OVERVIEW.md`
11. `docs/design/PROTOCOL_PARTITIONED_STORAGE_V2_DETAILED_DESIGN.md`

当实现与本文档冲突时，不能通过局部代码补丁解决；必须回到 owner 边界，修正主路径。
