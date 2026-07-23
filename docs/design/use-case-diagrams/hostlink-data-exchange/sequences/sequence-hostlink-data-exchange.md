# Sequence：Host、Session 与 Handler
```mermaid
sequenceDiagram
  actor Host as External Host
  participant Transport as HostLink Transport
  participant Session as SessionRuntime
  participant Codec as Frame Codec/Router
  participant Service as Status/GPS/Config/AppData Service
  Host->>Transport: connect + hello
  Transport->>Session: connected / handshake frame
  Session-->>Host: ready response
  Host->>Session: command frame(seq,payload)
  Session->>Codec: decode + route
  Codec->>Service: validated command
  Service-->>Codec: explicit result
  Codec->>Session: response/error frame
  Session-->>Host: ordered TX
  Host--xTransport: disconnect
  Transport->>Session: link lost; clear session pending
```

## 场景与责任

Transport 管理字节流和连接事件；SessionRuntime 管理 handshake、generation、pending request 与有序 TX；Codec/Router 验证帧并选择 handler；Service 执行有界应用命令。

## Handshake 与会话

ready response 只在版本/capability 协商完成后发送。每次连接创建新 generation；旧连接的迟到 frame、handler result 和 TX callback 不得进入新 session。

## 请求/响应关联

`seq` 在 session 内唯一或按窗口管理。Router 在调用 Service 前完成大小、类型和 capability 验证。副作用 handler 返回 committed result 后才编码 success；超时或 rejected 返回稳定 error code。

## TX 顺序与背压

Session 维护固定容量 ordered TX。异步 event 与 command response 的排序规则必须明确；队列满时不能无界分配。断连清理 pending，但业务已经 committed 的命令不能回滚为“未执行”。

## 测试

覆盖 handshake 失败、重复 seq、handler 迟到、TX 满、response 编码失败、断连时已提交命令和快速重连。
