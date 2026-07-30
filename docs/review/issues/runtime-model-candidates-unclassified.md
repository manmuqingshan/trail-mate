# P2 · 【候选待裁决】系统与媒体 Runtime 尚未完成 Model-or-Projection 分类

状态：**acknowledged**
类别：**架构边界 / 模型完整性**

## 结论

当前九个 Registry 模型不是“已经足够”的结论。源码中至少还有四组具有稳定状态语言和生命周期的候选边界，但它们主要位于 `platform/ui` 或 UI runtime，尚不能仅凭类型数量断言为独立领域模型：

1. Reticulum 实时通话：`State`、`RealtimePhase`、`Peer`、`Snapshot`、接听/拒绝/挂断与音频队列。
2. 内容包安装：`PackageRecord`、`InstalledPackageRecord`、`PackageInstallPhase`、安装/卸载与兼容性判断。
3. 固件更新：`Phase`、`Status`、检查、下载、安装和重启生命周期。
4. Wi-Fi 资源仲裁：`Request`、`Lease`、`Decision`、`ExclusiveOwner`、抢占阶段与流量预算。

这些候选不能继续在完整性评审中隐身；也不能为了增加 Explorer 数量，直接把 runtime 数据结构包装成领域模型。当前正确状态是“候选待裁决”。

## 为什么还不能直接登记为四个模型

- 实时通话同时包含业务会话、协议互操作、设备媒体和资源抢占；聚合边界尚未明确。
- 包安装与固件更新有生命周期，但可能属于 Device/Capability 的 application service，也可能形成独立的更新/内容管理模型。
- Wi-Fi lease 有清晰策略，却更像跨能力资源调度模型；其 owner 目前仍是平台 runtime。
- 四组 API 都以全局 runtime 状态和自由函数为主，缺少与 UI 解耦的端口、领域测试和明确持久化边界。

## 源码证据

- `modules/core_sys/include/platform/ui/reticulum_call_runtime.h`
- `modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp`
- `modules/core_sys/include/platform/ui/pack_repository_runtime.h`
- `platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp`
- `modules/core_sys/include/platform/ui/firmware_update_runtime.h`
- `modules/core_sys/include/platform/ui/wifi_access_runtime.h`
- `platform/esp/arduino_common/src/platform_ui_wifi_access_runtime.cpp`

## 需要作者裁决的问题

1. 实时通话的业务会话是否跨 Reticulum 与未来其他传输协议，还是仅为 Phone/Reticulum 集成投影？
2. Package 与 Firmware 是否共享一个“设备内容与升级”模型，还是两个独立 application workflow？
3. Wi-Fi lease 的优先级、独占和抢占规则是否属于产品级资源治理，应由 Device/Capability 模型拥有？
4. 哪些状态需要持久化、审计或跨重启恢复？只存在于一次 UI 会话的状态不应自动升级为领域实体。

## 关闭准则

逐项给出 `independent model`、`element of existing model`、`application workflow`、`integration projection` 四选一结论，并记录 owner、不变量、端口和测试证据。只有被裁决为独立模型且实现边界已经形成的候选，才进入 Model Explorer。
