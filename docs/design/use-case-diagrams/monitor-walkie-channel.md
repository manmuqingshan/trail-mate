# Use Case：监听模拟对讲频道

状态：**confirmed receive-only behavior**
业务边界：通信、媒体与投递

## 用户目标

在支持 walkie runtime 的设备上监听当前模拟语音频率，观察 RSSI/音频电平并控制 monitor；当前页面没有被文档夸大为完整 PTT 发射用例。

## 行为与规则

1. 进入页面前检查 `platform::ui::walkie::is_supported()`。
2. runtime 启动 receiver，页面读取频率、RSSI、squelch/monitor 和音量电平。
3. 用户开关 monitor；状态只有平台确认后才更新。
4. 离开页面停止 monitor 并释放音频/radio 资源。

失败时显示 runtime error，不保留“已监听”的假状态。

源码：`modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp`、`modules/core_sys/include/platform/ui/walkie_runtime.h`。

## 下钻

- [Activity](monitor-walkie-channel/activity.md)
- [Sequence](monitor-walkie-channel/sequences/sequence-monitor-walkie-channel.md)
