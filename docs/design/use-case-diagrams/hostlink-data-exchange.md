# Use Case：通过 HostLink 与外部主机交换应用数据

状态：**confirmed integration behavior**
业务边界：外部应用与主机集成

## 用户目标

让受支持的本地主机读取设备状态/GPS、提交允许的配置和应用数据，并收到明确响应或异步事件；坏帧、未知命令和断线不能留下半应用操作。

## 主场景

1. `SessionRuntime` 从 Stopped 进入 Waiting/Connected，完成握手后进入 Ready。
2. frame codec 检查 magic/length/type/sequence；router 只把合法 frame 映射到 status、GPS、configuration 或 app-data handler。
3. command handler 校验 capability 和 payload，调用 bounded service，形成 response/error frame。
4. session 管理 TX sequence、队列和节流；主机断线后清空 session-scoped pending 状态并回到 Waiting。

## 规则与失败

- HostLink 是本地集成协议，不等于 USB Mass Storage，也不等于 P4↔C6 内部 companion link。
- 未完成 handshake 不接受业务命令。
- 非法长度、未知 command、sequence/codec 错误返回明确 error 或关闭 session。
- 配置变更必须以服务返回结果为准，不能在 decode 后直接显示成功。

源码：`modules/core_hostlink/include/hostlink/session_runtime.h`、HostLink frame router/codecs、platform hostlink transports。

## 下钻

- [Activity](hostlink-data-exchange/activity.md)
- [Sequence](hostlink-data-exchange/sequences/sequence-hostlink-data-exchange.md)
- [State Machine](hostlink-data-exchange/state-machines/hostlink-session.md)
