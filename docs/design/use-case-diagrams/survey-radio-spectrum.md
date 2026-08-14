# Use Case：探测协议空口参数并确认可用性

状态：**confirmed**
业务边界：通信、媒体与投递 / Radio 工具

## 用户目标

从有限、可解释的完整 LoRa air profile 集合中，发现承载 MeshCore、
Meshtastic 或 Reticulum 协议包的参数，并在协议语义允许时完成主动确认。

## 主场景

1. 从当前配置的完整 profile 建立有限候选队列：Meshtastic 加入同一配置上下文的标准 modem profile，MeshCore 加入相同频率族的区域 preset，Reticulum 只保留当前 RNode profile；不创建全频段 25 kHz RSSI bins，也不伪造历史候选。
2. Protocol Probe 取得 radio runtime，按候选配置完整 receive profile，并收集 CRC 通过的 LoRa 帧。
3. 协议解析把证据分为 LoRa frame、protocol observed 和 active confirmation；只有后两级进入结果列表。
4. MeshCore 发送受限 Discover 并等携带本次 tag 的响应；Meshtastic 必须先解密并验证一个本地频道的数据包，再向该来源单播 `want_ack`；Reticulum 仅被动接受自洽的 Announce 或固定控制目的的 Path Request，不在暂时调谐的 profile 上运行 Path/Ping/Proof。
5. 用户选择观察到或确认过的 profile 后必须二次确认才可应用；离开页面恢复 radio owner。

失败：radio 不支持、配置失败或 ownership 不可得时停止探测并显示原因；不能把静默候选、未响应 ACK 或超时响应当作 profile 不存在。

源码：`modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp`、`modules/core_sys/include/platform/ui/lora_runtime.h`。

## 下钻

- [Activity](survey-radio-spectrum/activity.md)
- [Sequence](survey-radio-spectrum/sequences/sequence-survey-radio-spectrum.md)

本文件保留 `survey-radio-spectrum` 路径仅为文档链接兼容；其内容和
产品名称均已改为 Protocol Probe。
