# Sequence：Protocol Probe 与 Radio Owner
```mermaid
sequenceDiagram
  actor U as 用户
  participant UI as Protocol Probe
  participant Plan as Candidate Profile Queue
  participant Radio as LoRa Runtime
  participant Adapter as Protocol Adapter
  UI->>Plan: build(protocol-specific finite profiles)
  UI->>Radio: acquire + configure first full profile
  loop each candidate profile
    UI->>Radio: configure_receive(profile)
    Radio-->>UI: CRC-passing raw frame or timeout
    UI->>Adapter: parse(protocol, raw frame)
    alt MeshCore evidence
      UI->>Radio: rate-limited Discover TX
      Radio-->>UI: response or ACK
    else Meshtastic evidence with context
      UI->>Radio: targeted want_ack unicast TX
      Radio-->>UI: correlated ROUTING ACK or timeout
    else Reticulum public control traffic
      UI->>Adapter: validate Announce or fixed-control Path Request
    end
    UI->>UI: retain OBSERVED; upgrade only positive response to CONFIRMED
  end
  U->>UI: select observed/confirmed profile
  UI->>UI: show apply confirmation
  UI->>Radio: release temporary lease before config apply
  UI->>Radio: release on exit
```

## 场景与责任

Candidate Profile Queue 负责有限、可解释的完整 PHY 假设，Protocol Probe 编排证据和协议专属验证，Radio Runtime 拥有硬件配置和接收。用户选择 profile 后的应用是独立提交。

## 获取与恢复

`acquire` 返回 lease 和进入前配置快照；未取得 lease 不调用 configure。退出、取消、错误或应用前先停止探测，再恢复需要保留的 radio 配置并 release。

## 采样顺序

每项先 configure，等待硬件 settle，再进入 RX。原始帧必须属于当前 candidate generation，迟到帧不能计入下一 profile。主动包发送后，scheduler 必须停留在同一 profile 完整接收响应；未返回的 ACK 不能当作否定证据。RT 没有主动 Probe，也没有此页中的 Proof 等待窗口。

## 应用选择

不存在 AUTO/noise/hot 选择。只有 E2/E3 profile 可触发 Set，且必须通过确认 dialog。目标协议的持久化映射成功后才更新配置投影；失败保留旧配置。

## 测试

覆盖 settle、迟到帧、协议解析失败、MC/MT 的正/负验证、RT 被动观察、响应窗口、partial result、无可应用 profile、apply 失败、资源抢占和 release 后配置。
