# 联系人、对端目录与本地信任模型

模型状态：**confirmed / boundary split**
权威代码：`core_chat` 的 `MeshPeerRecord`、`IMeshPeerDirectory`、`ContactService` 及其 stores

## 这个模型回答什么

Trail Mate 从无线协议观察到一个节点之后，必须决定四件不同的事：它在协议中是谁、系统知道它哪些事实、用户是否把它保存为联系人，以及本机是否忽略或信任它。

这些决策已经形成可运行的模型，不能继续被埋在“聊天”或“Mesh 身份”的附属类型中。它和消息模型的区别是：消息模型拥有会话与投递状态；本模型拥有本地目录、用户别名、可见性、忽略与信任投影。

## 核心语言

| 概念 | 代码表达 | 含义 | 当前 owner |
| --- | --- | --- | --- |
| 对端身份 | `MeshPeerIdentity` | 由 protocol 与 NodeId、公钥或 Reticulum destination 组成的目录键 | `MeshPeerRecord` / directory |
| 协议事实 | `MeshtasticPeerFacts`、`MeshCorePeerFacts`、`ReticulumPeerFacts` | 协议观察到的名称、路由、公钥与能力 | `MeshPeerRecord` |
| 观察事实 | `MeshPeerObservations` | SNR、RSSI、设备指标和最后位置 | `MeshPeerRecord` |
| 用户事实 | `MeshPeerUserFlags`、`user_alias` | favorite、ignored、trusted 与用户别名 | local directory |
| 节点投影 | `NodeInfo` / `NodeEntry` | 面向旧联系人服务和 UI 的节点表示 | `INodeStore` |
| 联系人 | nickname attached to a node id | 用户主动保存并命名的节点 | `IContactStore` |

`PeerPublicKey` 属于 Mesh 密钥身份模型；`MeshPeerIdentity` 是目录索引。二者相关，但不能因为名字相似就当成同一个实体。

## 对端进入目录的路径

1. Meshtastic、MeshCore 或 Reticulum adapter 产生协议观察。
2. 观察被归一化为 `MeshPeerIdentity` 与协议 facts。
3. `IMeshPeerDirectory::record` 按稳定身份插入或合并 `MeshPeerRecord`。
4. 旧路径通过 `ContactService::applyNodeUpdate` 更新 `INodeStore` 中的 `NodeEntry`。
5. 联系人页面根据当前 active protocol 读取联系人、附近节点和被忽略节点。
6. 用户可以设置 nickname、ignored 或 manually verified；这些是本机事实，不应被后续无线观察静默覆盖。

## 已经存在的规则

### 身份必须有效

`meshPeerIdentityIsValid` 对三类目录键分别检查：NodeId 不能为零，公钥必须有非零有界字节，Reticulum destination 必须有效且 hash 非零。没有有效身份的记录不能成为目录事实。

### 协议分区是查询边界

`ContactService::setActiveProtocol` 同时切换 node store 与 contact store，并清空缓存。RNode 被归一化到 Reticulum。联系人查询不应该把不同协议的同值 NodeId 直接视为同一个人。

### 联系人与节点不是同一个生命周期

- `removeContact` 只删除 nickname，节点观察仍可保留。
- `removeNode` 同时尝试删除 nickname 和节点记录。
- `addContact` / `editContact` 会先确保 node entry 存在；因此联系人动作依赖目录记录，但联系人状态不是无线观察本身。

### 本机信任只能修改已存在节点

`setNodeIgnored` 与 `setNodeKeyManuallyVerified` 在 node entry 不存在时返回失败。它们通过 `NodeUpdate` 写回 store，不凭空创建可信身份。

### 显示名称有确定的降级次序

普通协议优先 nickname，再使用节点名称；Reticulum 路径优先协议 announce/long name，在缺少协议名称时再使用 nickname。该差异是现有行为，不应由 UI 各自重新猜测。

## 当前没有闭合的地方

### 两套目录表达并存

`MeshPeerRecord / IMeshPeerDirectory` 已经能表达协议身份、来源、首次/最后观察、用户 flags 和跨协议 facts；`NodeInfo / NodeEntry / ContactService` 仍拥有另一套位置、指标、ignored 和 verified 字段。平台 adapter 需要在两套结构之间投影，容易产生双写和覆盖顺序不一致。

### “附近节点”没有实际新鲜度门槛

`ContactService` 的接口注释写着附近节点只保留六天可见性，但当前 `isNodeVisible` 无条件返回 `true`。`formatTimeStatus` 虽然计算 Online/Seen/Offline，却没有成为查询不变量。因此文档不能宣称代码已经执行六天过期策略。

### trusted、manually verified 与 Mesh verified key 尚未统一

目录中同时有 `MeshPeerUserFlags::trusted`、Meshtastic `key_manually_verified`、MeshCore `public_key_verified`，Mesh 密钥模型还有 `PeerPublicKey::verified`。它们的证明来源、撤销和优先级没有单一 owner。

### 缺少业务身份链接

协议节点、目录记录、联系人和未来的团队成员仍通过 NodeId、destination hash 或临时映射关联。代码没有显式、可撤销的 `IdentityLink`。这个缺口留在 Review Queue，不在本文虚构一个已经存在的类。

## 与其他模型的关系

- Mesh 身份模型提供经过验证的密钥事实；本模型决定如何在本地目录中索引、命名和投影对端。
- 通信模型引用联系人显示名称，但不拥有联系人生命周期。
- Team 模型目前用 NodeId 维护 roster；未来成员身份应通过明确链接引用本模型，而不是复制联系人结构。
- Positioning 提供本机 fix；本模型保存的是对端报告的位置观察，两者的可信度和新鲜度不能混用。

## 源码证据

| 证据 | 说明 |
| --- | --- |
| `modules/core_chat/include/chat/domain/mesh_peer_directory.h` | 新目录的身份、记录、协议 facts、观察与用户 flags |
| `modules/core_chat/include/chat/ports/i_mesh_peer_directory.h` | record/find/search/flags/remove/flush 契约 |
| `modules/core_chat/include/chat/domain/contact_types.h` | 旧节点与联系人查询投影 |
| `modules/core_chat/include/chat/usecase/contact_service.h` | 联系人和节点目录用例入口 |
| `modules/core_chat/src/usecase/contact_service.cpp` | 协议分区、nickname、ignored、verified 和删除语义 |
| `modules/core_chat/tests/test_mesh_peer_directory_contract.cpp` | first-seen 合并与 verified key 防覆盖契约 |

## 下钻

- [对端观察、联系人提升与本地信任](peer-directory-lifecycle.md)
- Review Queue：IdentityLink 缺失、目录双重表达、团队成员身份缺失
