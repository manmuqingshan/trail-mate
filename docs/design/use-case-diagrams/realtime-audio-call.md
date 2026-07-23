# Use Case：发起或响应 Reticulum 实时通话

状态：**confirmed behavior / model classification pending**

业务边界：通信、媒体与投递
参与者：呼叫方、被叫方、LXST Call Session、Realtime Resource Hooks、Audio Engine

## 用户目标

从 Reticulum 对端建立低延迟语音通话，看到识别、响铃、接听、活动、结束或失败的真实阶段，并保证通话期间音频、Wi-Fi、radio 与休眠资源不互相破坏。

## 主场景

1. 呼叫方选择可解析 peer 后创建 outgoing call；或 adapter 以 link/destination/identity facts 建立 incoming identifying。
2. incoming 完成身份解析后进入 ringing soft-preempt；用户可接受或拒绝。
3. `accept()` 只有在当前 link 一致且 phase 合法时才取得 exclusive 资源；link active 与用户 accept 可乱序到达。
4. media engine 启动后进入 `Active / ActiveCall`，音频包通过固定大小队列流动。
5. hangup、remote close、link loss 或 media failure 进入 closing，释放 exclusive、wake/sleep lease 和媒体资源。

## 失败与恢复

- 未识别/不匹配 link 不得操作当前通话。
- 媒体不支持、exclusive 获取失败或 audio start 失败进入 Failed，并通知远端关闭。
- 关闭必须幂等；迟到包不能恢复已结束 session。
- Call exclusive 优先于后台 HTTP/长连接；结束后资源治理恢复正常。

## 源码证据

- `modules/core_sys/include/platform/ui/reticulum_call_runtime.h`
- `modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp`
- `platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter_call.cpp`
- `platform/esp/common/src/reticulum_call_audio_engine.cpp`

## 下钻

- [Activity：来电与去电汇合](realtime-audio-call/activity.md)
- [Sequence：接听与资源独占](realtime-audio-call/sequences/sequence-realtime-audio-call.md)
- [State Machine：Call State 与 RealtimePhase](realtime-audio-call/state-machines/call-session.md)
