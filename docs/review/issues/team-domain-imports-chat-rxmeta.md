# P2 · 【边界缺陷】Team 领域事件直接依赖 Chat 接收元数据

状态：**acknowledged**
类别：**依赖与耦合**

## 结论

Team 用例接口直接使用 `chat::RxMeta`，使团队模型依赖通信模块的传输元数据布局。真正属于 Team 的是“谁以何种已验证上下文发送了团队命令”，不是 Chat 当前怎样组织全部接收字段。

## 风险

- Chat 增删字段会迫使 Team 接口变化。
- Team 可能意外使用未经身份层验证的传输字段。
- 授权、去重和审计需要的最小契约无法被看见和测试。

## 目标契约

`TeamReceiveContext` 只保留：

- verified peer identity；
- team/protocol namespace；
- receive timestamp；
- message identity / replay protection 所需字段；
- 明确需要的 link quality（若业务规则确实使用）。

由 adapter 从 `chat::RxMeta` 显式映射。这样 Chat 可以演进自己的接收元数据，Team 只在业务契约变化时改变。

证据：`modules/core_team/include/team/usecase/team_service.h` 与 `modules/core_chat/include/chat/domain/chat_types.h`。
