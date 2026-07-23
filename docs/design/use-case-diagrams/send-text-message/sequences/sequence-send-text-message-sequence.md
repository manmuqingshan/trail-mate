# Sequence Diagram：ACK、超时与终态竞争

```mermaid
sequenceDiagram
  participant Backend as Protocol Backend
  participant Timer as Timeout Policy
  participant Ledger as ChatMessageLedger
  participant Store as Message Store
  Backend->>Ledger: Sent(messageKey)
  par protocol receipt
    Backend->>Ledger: Delivered(messageKey, receipt)
  and timeout
    Timer->>Ledger: Failed(messageKey, timeout)
  end
  Ledger->>Ledger: merge facts; protect terminal state
  Ledger->>Store: persist winning transition
  Store-->>Ledger: Durable / Deferred / Rejected
```

Ledger 决定最终可见状态；callback 到达顺序不能直接成为业务状态。

## 竞争场景

协议回执和 timeout 由不同执行上下文产生，可以任意顺序到达，也可能重复到达。二者都只提交事实，不能直接改 UI 或 Store 中的状态字段。

## 终态合并规则

Ledger 读取当前 revision，并按消息状态偏序裁决。有效 Delivered 对同一发送 attempt 优先于迟到 timeout；明确永久失败后是否接受迟到 receipt 必须由协议策略决定。任何 transition 都以 messageKey、attempt/generation 和 expected revision 定位。

## 持久化与发布

只有 winning transition 成功持久化后才发布投影。Store Deferred 时保留待提交事实；不能先通知 UI 再补写。Rejected 表示 transition 与当前状态不兼容或输入无效，应记录诊断而不是循环重试。

## 幂等

重复 Delivered、重复 timeout 和 retry pump 必须返回相同可见结果。旧 attempt 的 ACK 不得完成后来重新发送的 attempt。

## 测试矩阵

覆盖 ACK-first、timeout-first、同时到达、重复 ACK、Store Deferred、重发后旧 ACK，以及 Delivered 后应用重启的终态恢复。
