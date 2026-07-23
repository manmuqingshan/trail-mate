# Use Case：扫描频段并选择低干扰频点

状态：**confirmed**
业务边界：地图、定位与现场感知 / Radio 工具

## 用户目标

依据当前区域和 radio 参数扫描允许频段，观察噪声底与热点，并在人工确认后把候选低干扰频点应用到 receiver。

## 主场景

1. 从区域 preset 或当前配置建立 start/end/bandwidth 和量化后的 sweep bins。
2. Energy Sweep 取得 radio runtime，逐 bin 配置 receive 并采样 RSSI。
3. 有界数组累积结果，计算 noise floor、hot bins、可视范围和 best frequency。
4. 用户可以停止/继续、移动 cursor 或选择 AUTO；AUTO 只配置 receiver，不暗中重写所有协议配置。
5. 离开页面恢复 radio owner。

失败：radio 不支持、配置失败或 ownership 不可得时停止扫描并显示原因；不能把未扫描 bin 当作低噪声。

源码：`modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp`、`modules/core_sys/include/platform/ui/lora_runtime.h`。

## 下钻

- [Activity](survey-radio-spectrum/activity.md)
- [Sequence](survey-radio-spectrum/sequences/sequence-survey-radio-spectrum.md)
