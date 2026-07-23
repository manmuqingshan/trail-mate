# Use Case：接收、验证并提交去中心化消息

状态：**confirmed**

业务边界：通信、媒体与投递

触发者：Radio IRQ / transport event / active protocol backend
主要受益者：设备用户

## 用户目标

只看到通过当前协议验证、没有重复、已经可靠提交到正确会话的消息，并能识别发送者、未读状态和必要回执。

## 成功场景

1. Radio/transport 把帧交给当前活动 backend；UI 不是接收链的发起者。
2. backend 先检查长度、目的、协议 framing，再执行解密、签名/密钥或 LXMF proof 验证。
3. `ReceivePacketService`/协议适配器建立协议身份和消息身份；去重键包含协议上下文。
4. 接收提交按有界顺序更新消息、peer/contact facts、会话元数据、未读与所需 ACK。
5. 只有提交成功或进入明确 deferred 恢复路径后才发布 UI 事件。

## 拒绝与恢复

- malformed、目的不匹配、认证失败在修改业务状态前拒绝。
- duplicate 不创建第二条消息；必要时仍可回应协议级 ACK。
- 存储 Busy/暂不可用返回 Deferred，放入有界 deferred slot；溢出必须计数并可诊断。
- Rejected 不能触发未读、联系人提升或“收到消息”提示。
- ESP 热路径使用 scratch/ring storage，不在 task stack 创建大型 protobuf/frame。

## 源码证据

- `modules/core_mesh/include/mesh/usecase/receive_packet_service.h`
- `modules/core_mesh/src/usecase/receive_packet_service.cpp`
- `modules/core_chat/include/chat/usecase/chat_message_ledger.h`
- `platform/esp/arduino_common/src/chat/infra/mesh_adapter_router.cpp`

## 下钻

- [Activity：帧验证、去重与提交](receive-text-message/activity.md)
- [Sequence：Radio 到 Ledger](receive-text-message/sequences/sequence-receive-text-message.md)
- [Sequence：Deferred 存储恢复](receive-text-message/sequences/sequence-receive-text-message-sequence.md)
- [Class Collaboration：接收职责边界](receive-text-message/realization/class-collaboration.md)
