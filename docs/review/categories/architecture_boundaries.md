# 尚未形成的领域模型

状态：**has_unresolved** · 4 findings

前三项不是 Model Explorer 漏掉了现有类，而是业务能力已经出现、核心 owner 尚未形成。第四项是源码里已有稳定状态语言、但仍需判定为独立 Model 还是既有 Model 的 Projection/Workflow。

- P1：[路线导航规则仍由 UI Runtime 持有](../issues/route-navigation-domain-model-missing.md)
- P1：[团队成员与团队生命周期没有领域 owner](../issues/team-membership-lifecycle-model-missing.md)
- P1：[协议身份到业务联系人的 IdentityLink 缺失](../issues/peer-identity-ownership-split.md)
- P2：[系统与媒体 Runtime 尚未完成 Model-or-Projection 分类](../issues/runtime-model-candidates-unclassified.md)

关闭准则不是“文档里补一个类名”，而是代码出现可验证的状态 owner、不变量、命令/事件和测试，并由作者确认边界；候选还必须明确裁决为独立模型、既有模型元素、应用工作流或集成投影。
