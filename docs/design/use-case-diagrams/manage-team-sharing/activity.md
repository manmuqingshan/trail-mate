# Activity：配对到成员状态
```mermaid
flowchart TD
  Role{"创建团队或加入?"}
  Role -- 创建 --> Keys["创建/恢复 TeamId + TeamKeys"]
  Keys --> Invite["Leader 发起 pairing"]
  Role -- 加入 --> Request["收到 pairing request"]
  Request --> Confirm{"用户确认?"}
  Confirm -- 否 --> Reject["拒绝/超时；不建成员"]
  Confirm -- 是 --> Exchange["交换 confirm + key material"]
  Invite --> Exchange
  Exchange --> Verify{"消息和密钥验证通过?"}
  Verify -- 否 --> Reject
  Verify -- 是 --> Persist["保存 keys / 更新本地 roster 投影"]
  Persist --> Active["Team mode active"]
  Active --> Admin{"kick / transfer / key request"}
  Admin --> Validate{"角色允许?"}
  Validate -- 否 --> RejectAction["拒绝管理动作"]
  Validate -- 是 --> Commit["发送 Team payload 并更新事件投影"]
```

## 本图回答的问题

设备如何创建或加入团队、交换用途分离的密钥，并执行 kick、leader transfer 和 key request 等管理动作。本文确认配对行为；完整 TeamMember 生命周期仍是 candidate。

## 身份、角色与密钥

TeamId、leader/member pairing role、pairing state 和不同用途的 TeamKeys 是当前明确事实。协议 NodeId 可以参与传输，但不能自动成为稳定 TeamMemberId。每种密钥用途和版本必须保留，禁止把一段共享 secret 同时解释为所有能力。

## 配对提交

用户确认、远端 confirm、消息验证、key material 验证和本地持久化缺一不可。收到 pairing request 不创建成员；发送 confirm 也不表示双方都已提交。只有持久化成功后才能进入 Team mode active。

## 管理动作授权

kick、leader transfer 和 key distribution 必须检查当前角色、目标成员、团队 revision/密钥版本和消息认证。UI 是否显示按钮不是授权来源。拒绝必须说明角色不足、目标不存在、密钥过期或传输不可用。

## 缺失设计

当前 roster 主要是 `vector<NodeId>`，没有成员资格来源、角色生命周期、revision、撤销证明或跨协议 IdentityLink。因此“成员已加入/已被移除”的冲突合并与重放规则尚未闭合。

## 测试

覆盖用户拒绝、confirm 超时、密钥验证失败、持久化失败、重复 pairing、非 leader 管理命令、leader transfer 竞争和旧 key request。
