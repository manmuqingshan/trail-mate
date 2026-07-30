# Activity：HostLink Frame 处理
```mermaid
flowchart TD
  Connect --> Handshake{"handshake complete?"}
  Handshake -- 否 --> Wait
  Handshake -- 是 --> Frame["receive frame"]
  Frame --> Codec{"magic/length/type/sequence valid?"}
  Codec -- 否 --> Error["error frame / close session"]
  Codec -- 是 --> Route{"known command + capability?"}
  Route -- 否 --> Error
  Route -- 是 --> Handle["bounded status/GPS/config/app-data service"]
  Handle --> Result{"success?"}
  Result -- 是 --> Response["encode response/event"]
  Result -- 否 --> Error
  Response --> Frame
```

## 本图回答的问题

外部主机完成 HostLink 握手后，帧如何经过 codec、序号、命令路由和 capability 检查，调用有界应用服务并生成同一会话内的响应。

## 会话与 framing

握手建立协议版本、capability 和 session generation。每个帧验证 magic、长度、类型、序号和大小上限；无效长度不能继续等待任意字节，也不能分配声明大小的无界 buffer。

## 命令路由

Router 只把已知命令交给明确 handler。状态/GPS 查询、配置命令和 app-data 各有独立 schema、权限与副作用。未知命令返回稳定错误；capability 不允许时不能依赖 handler 自己拒绝。

## 序号与幂等

请求序号在 session 内关联 response。重复只读请求可重新响应；有副作用命令需要 request identity/commit result，避免主机重试造成两次配置或消息发送。新 handshake generation 使旧会话迟到帧失效。

## 错误策略

可归因的命令错误返回 error frame 并保持会话；破坏 framing、持续超限或版本不兼容可关闭会话。handler 超时要有界，并释放占用的应用资源。

## 测试

覆盖半帧、粘包、超长、未知类型、重复序号、旧 session 帧、capability 拒绝、handler timeout 和 response 编码失败。
