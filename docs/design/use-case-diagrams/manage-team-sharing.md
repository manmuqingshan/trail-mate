# Use Case：建立团队凭据与成员关系

状态：**candidate；配对行为确认，成员模型未闭合**
业务边界：团队协作

## 用户目标

创建一个受密钥保护的团队，邀请或加入对端，明确谁是 leader/member，并能请求/分发密钥、移除成员或转移 leader，而不会把未确认配对当成永久成员。

## 已实现行为

1. leader 创建/恢复 TeamId 与 TeamKeys，启动 pairing 并产生配对消息。
2. candidate member 接收请求，用户确认后交换确认/密钥材料。
3. key distribution 成功后 UI store 恢复 team mode；`TeamService` 记住 NodeId roster。
4. key request、kick、leader transfer 和 status 通过独立 Team payload 发送并由 reducer 更新页面状态。
5. kick self 会清理本地 team membership；leader transfer 改变后续权限投影。

## 不变量与失败

- 未确认或超时不建立永久成员。
- team key 解密/验证失败不改变 roster。
- leader-only action 在发送前校验角色。
- 当前 roster 主要是 `vector<NodeId>`，没有 membership revision、来源、撤销证明和稳定跨协议 IdentityLink；因此完整成员生命周期仍是 candidate。

源码：`modules/core_team/src/usecase/team_pairing_coordinator.cpp`、`modules/core_team/src/usecase/team_service.cpp`、`modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp`。

## 下钻

- [Activity：配对到成员状态](manage-team-sharing/activity.md)
- [Sequence：Leader 与 Candidate 配对](manage-team-sharing/sequences/sequence-manage-team-sharing.md)
- [State Machine：Pairing 与本地成员投影](manage-team-sharing/state-machines/team-membership.md)
