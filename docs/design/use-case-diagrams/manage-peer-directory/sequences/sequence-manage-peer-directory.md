# Sequence Diagram：协议观察、目录与 Contacts 投影

```mermaid
sequenceDiagram
  participant Backend as 活动协议 backend
  participant Directory as IMeshPeerDirectory
  participant Contact as ContactService
  participant Stores as NodeStore / ContactStore
  participant UI as Contacts / Node Info
  Backend->>Directory: record(protocol identity, observed facts)
  Directory->>Directory: preserve firstSeen; update lastSeen/facts
  Directory-->>Contact: directory record / node update
  Contact->>Stores: persist observation and local flags
  UI->>Contact: getContacts/getNearby/getIgnoredNodes
  Contact-->>UI: active-protocol projection
  alt 用户保存或重命名
    UI->>Contact: addContact/editContact(nodeId,nickname)
    Contact->>Stores: commit nickname relation
  else 用户忽略
    UI->>Contact: setNodeIgnored(nodeId,true)
    Contact->>Stores: commit ignored flag
  else 用户人工验证
    UI->>Contact: setNodeKeyManuallyVerified(nodeId,true)
    Contact->>Stores: commit only if node exists
  end
```

## 场景

该时序覆盖两条不同时间线：后台协议持续记录观察，前台按需查询并提交本地关系。两者可以并发发生，因此 ContactService 不能以一次 UI 快照覆盖更新中的协议事实。

## 参与者职责

- **Backend** 只提供经过协议解析的观察事实，不拥有 nickname、ignored 或人工验证。
- **Directory** 维护 protocol-scoped identity、first/last seen 和观察事实。
- **ContactService** 组合目录与本地关系，执行用户命令的业务前置条件。
- **Stores** 是持久化提交边界。
- **UI** 只消费活动协议投影并发送明确命令。

## 顺序与并发规则

观察必须先通过身份形状验证，再 upsert Directory。用户动作按稳定 NodeId/identity 定位，不按列表位置定位。后台更新 `lastSeen` 与用户重命名可并发；合并策略是字段所有权合并，而不是 last-write-wins 整条记录覆盖。

## 提交点

ContactService 只有在 Store 成功后才返回命令完成。UI 列表刷新来自重新投影或提交事件，不能把按钮点击当成已保存。人工验证在节点不存在时明确失败，不能为了满足命令偷偷创建无证据节点。

## 迟到与重复消息

重复 protocol observation 应幂等更新；旧 observation 不应倒退 `lastSeen` 或覆盖新事实。重复 `setNodeIgnored(true)`、相同 nickname 编辑和相同验证 flag 均应安全。`removeNode` 后迟到的旧缓存事件需要 revision/时间检查，否则会立即复活已删除记录。

## 验证重点

测试应交错执行 observation、rename、ignore 和 remove，证明本地字段不会被协议刷新覆盖，并验证每种协议 namespace 具有独立 key space。
