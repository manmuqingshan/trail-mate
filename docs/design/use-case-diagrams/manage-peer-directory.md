# Use Case：管理联系人、附近节点与本地信任

状态：**confirmed；identity linking partial**

业务边界：网络、身份与目录

主要参与者：设备用户
事件触发者：活动协议 backend、Reticulum directory

## 用户目标

理解设备发现了谁，把值得保留的对端保存为联系人，设置本地名称、忽略噪声节点、检查协议身份，并在有证据时标记人工验证。

## 进入目录

1. Meshtastic/MeshCore 节点信息或 Reticulum LXMF address 先形成协议观察。
2. 目录以 `protocol + protocol identity` 区分记录，保留 first seen，更新 last seen、显示名、位置、能力和公钥 facts。
3. Contacts 页面按活动协议投影 Contacts、Nearby、Reticulum Groups 与 Ignored，而不是把不同协议的同号 NodeId 合并。

## 用户动作

- 保存为联系人并设置 nickname。
- 编辑 nickname；删除联系人只删除用户关系，不等于删除协议节点观察。
- 忽略/取消忽略节点。
- 删除节点记录。
- 查看 key/hash；仅在节点记录存在时设置 manually verified。
- 从联系人或节点详情进入会话；Reticulum group destination 使用独立持久化状态。

## 失败与恢复

- 持久化失败不能更新为“已保存”。
- 未知协议、空身份和全零 key 不进入可验证身份。
- 跨协议相似名称不能自动建立同一联系人关系。
- `isNodeVisible()` 当前不执行文档声明的六天过滤；UI 不应把“附近”解释成已兑现的 retention policy。

## 仍未形成的设计

协议身份到业务联系人的可撤销 `IdentityLink` 不存在；人工验证、Reticulum trusted 与 Mesh verified key 也不是同一个证明状态。

## 源码证据

- `modules/core_chat/include/chat/domain/mesh_peer_directory.h`
- `modules/core_chat/include/chat/usecase/contact_service.h`
- `modules/ui_shared/src/ui/screens/contacts/contacts_page_runtime.cpp`
- `modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp`

## 下钻

- [Activity：观察到联系人](manage-peer-directory/activity.md)
- [Sequence：协议观察、目录与 Contacts 投影](manage-peer-directory/sequences/sequence-manage-peer-directory.md)
- [State Machine：目录条目的本地关系状态](manage-peer-directory/state-machines/peer-local-relationship.md)
