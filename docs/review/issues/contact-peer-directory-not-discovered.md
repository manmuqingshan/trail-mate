# P1 · 【已有模型未发现】联系人、对端目录与本地信任未进入 Model Explorer

状态：**acknowledged**
类别：**发现缺陷 / 文档索引**

## 结论

联系人目录不是未来设想。`MeshPeerRecord`、`IMeshPeerDirectory`、`ContactService`、`INodeStore` 与 `IContactStore` 已经共同承担对端索引、协议事实、用户别名、联系人分类、ignored 和本机信任投影。此前八模型审计漏掉了这一边界。

这是“模型存在但工具/作者没有找到”，不是产品代码缺少模型。修复方式是把真实边界补进 Registry，而不是在 Review Queue 建议创建另一个空类。

## 直接证据

- `mesh_peer_directory.h` 定义有效目录身份、跨协议 facts、观察和用户 flags。
- `i_mesh_peer_directory.h` 定义 record/find/search/setUserFlags/remove/flush 契约。
- `ContactService` 管理协议分区、nickname、附近/忽略分类、人工验证和删除语义。
- `IContactStore` 与 `INodeStore` 将联系人 nickname 和节点观察分开持久化。
- 联系人、key verification、聊天投影和多个平台 runtime 都消费该服务。

## 已执行的文档修复

- 新增 `model:contact-peer-directory`。
- 新增 5 个真实 Element 和目录生命周期图。
- 新增 Mesh Identity → Directory → Conversation / Team 跨模型关系。
- 保留新旧目录表达并存、IdentityLink 缺失和可见性策略失效等未解决问题。

## 验收

Model Explorer 必须显示“联系人、对端目录与本地信任”，并允许下钻到 `MeshPeerIdentity`、`MeshPeerRecord`、`ContactService`、`NodeInfo / NodeUpdate` 和 directory/store ports。
