# Use Case：发送去中心化消息并跟踪投递

状态：**confirmed**

业务边界：通信、媒体与投递

主要参与者：设备用户
核心责任：Chat intent、协议 backend、radio send queue、ChatMessageLedger

## 用户目标

从频道、私聊、Reticulum destination/group 或 Team 会话发送文本，并看到与协议事实一致的 queued、sent、delivered 或 failed，而不是 UI 自己猜测成功。

## 成功场景

1. Compose 校验非空文本、当前会话、目标身份、协议分区和所需密钥。
2. 建立稳定本地消息身份和协议关联键，先提交 `Queued`/outbox，再请求活动 backend 编码。
3. backend 按 Meshtastic、MeshCore、LXMF 或 Team contract 生成帧；radio owner 有界排队并发射。
4. 本地发射完成只可提交 `Sent`；只有协议 ACK/receipt 才可提交 `Delivered`。
5. `ChatMessageLedger` 合并乱序/重复回执并保护终态，持久化结果再驱动 UI。

## 失败与恢复

- 目标/密钥不匹配：创建消息前拒绝。
- 队列满、radio unavailable：记录可解释失败或保留可重试 queued，不能丢失身份关联。
- timeout 不能覆盖已经到达的 delivered；迟到 ACK 也不能复活明确 failed 的不可重试消息，除非协议策略允许。
- `Durable / Deferred / Rejected` 是持久化提交结果，不是第二套 MessageStatus。

## 源码证据

- `modules/core_chat/include/chat/usecase/chat_message_ledger.h`
- `modules/core_mesh/include/mesh/usecase/send_message_service.h`
- `modules/core_mesh/src/usecase/send_message_service.cpp`
- `modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp`

## 下钻

- [Activity：消息提交与发送](send-text-message/activity.md)
- [Sequence：Compose 到 Ledger](send-text-message/sequences/sequence-send-text-message.md)
- [Sequence：ACK、超时与终态竞争](send-text-message/sequences/sequence-send-text-message-sequence.md)
