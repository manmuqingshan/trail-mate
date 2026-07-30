# 规则一致性

状态：**has_unresolved** · 1 finding

- P2：[附近节点可见性注释与实际查询行为不一致](../issues/contact-visibility-policy-disabled.md)

接口说明和状态文本暗示六天新鲜度，但 `isNodeVisible()` 当前始终返回 true。规则确认之前，模型文档只描述真实行为，不替代码假装已经执行过期策略。
