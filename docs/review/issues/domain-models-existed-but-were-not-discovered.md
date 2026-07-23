# P1 · 【已修复的发现缺陷】固定三模板曾遮蔽真实模型

状态：**resolved**
类别：**发现 / 文档索引缺陷**

## 结论

Trail Mate 不是只有三个 Model。旧 Registry 把“组织与过程、软件结构、部署与制品”硬编码成顶层类型，再把真实领域事实塞进这些容器。当前作者 Registry 已移除该限制，并基于源码列出九个模型边界。

## 直接证据

- `ChatMessageLedger` 拥有消息投递状态与幂等合并。
- `PeerIdentityService` 拥有本机身份建立与对端公钥保护规则。
- `TeamPairingCoordinator` 拥有团队配对流程。
- `LocationService` 拥有 fix 有效性和时间权威。
- `TrackStateMachine` 拥有完整轨迹记录命令与状态。
- `TargetManifestView`、`CapabilityStatus`、`AuthorityBinding` 形成设备能力语言。
- `SessionRuntime`、`LinkState` 与 frame router 形成跨处理器会话边界。
- `IPhoneAppFacade` 与两个 protocol core 形成手机互操作边界。

## 为什么是 P1

Model Explorer 是架构评审入口。把模型发现失败呈现成“项目没有模型”，会直接扭曲设计判断，并诱导维护者在错误边界上继续设计。

## 修复准则

1. Registry 接受作者声明的任意模型数量和类型。
2. Model 必须列出 owner、不变量、源码证据和跨模型关系。
3. 发现失败作为 finding 展示，不自动产生空 Model。
4. Design、Engineering 和 C4 只作为 projection。

当前作者 Registry 已按这些准则列出九个模型。以后每一个具体漏识别模型必须单独形成 finding；不能再用本历史问题代替完整性审计。
