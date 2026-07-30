# Activity Diagram：帧验证、去重与提交

```mermaid
flowchart TD
  Frame["Radio / transport frame"] --> Active{"来自活动协议?"}
  Active -- 否 --> Ignore["拒绝跨协议污染"]
  Active -- 是 --> Decode{"framing / destination / length 有效?"}
  Decode -- 否 --> Reject["Rejected；不改业务状态"]
  Decode -- 是 --> Auth{"解密、签名或密钥验证通过?"}
  Auth -- 否 --> Reject
  Auth -- 是 --> Dedup{"protocol-scoped identity 已见?"}
  Dedup -- 是 --> AckOnly["不重复建消息；按协议处理 ACK"]
  Dedup -- 否 --> Commit{"消息/peer/会话提交结果"}
  Commit -- Durable --> Publish["发布消息与未读事件"]
  Commit -- Deferred --> Buffer{"进入有界 deferred slot"]
  Commit -- Rejected --> Reject
  Buffer --> Retry{"资源恢复?"}
  Retry -- 是 --> Commit
  Retry -- 溢出 --> Drop["计数并暴露诊断"]
```

## 本图回答的问题

一个异步无线帧满足什么条件才能变成用户可见消息。接收不是用户命令：入口是 radio/transport event，出口是 Durable 提交、受控 Deferred，或带诊断的拒绝。

## 验证层次

1. **协议归属**：只接受活动 backend 的事件，防止不同协议 identity 和 dedup 空间互相污染。
2. **帧结构**：长度、目标、版本和 framing 必须完整。
3. **真实性**：按协议执行解密、签名、密钥或身份验证；“可解码”不等于可信。
4. **去重**：使用 protocol-scoped message identity，而不是文本内容或到达时间。

## 提交结果

Durable 表示消息、peer 关联和会话状态已进入持久化边界，此后才能发布未读事件。Deferred 表示业务事实尚未提交，帧被放入固定容量 slot；UI 不得先显示消息。Rejected 不修改业务状态，必要的协议级 ACK/NACK 由 backend 单独处理。

## 重复、乱序与容量

重复帧不创建第二条消息，但可能需要重发协议 ACK。乱序消息按协议序号或消息时间投影，不改变存储提交顺序。有界 deferred slot 满时必须增加可观察 drop 计数，不能覆盖尚未处理的帧，也不能在 ESP 任务栈上复制大 frame。

## 恢复规则

资源恢复后按原 protocol identity 重试提交；重复重试必须幂等。重启恢复只处理已明确持久化的 inbox/outbox 或受支持的 durable deferred 数据，不能假设 RAM slot 会存活。

## 源码证据与测试

活动跨越 backend decode、认证/去重、消息接收服务和 Ledger/Store。测试需注入跨协议帧、无效签名、重复、乱序、存储 Deferred、slot 溢出及发布事件失败。
