# Use Case：检查并安装设备固件更新

状态：**confirmed behavior / model classification pending**
业务边界：设备维护与数据所有权

## 用户目标

检查当前目标是否有兼容更新，在明确确认后安全下载、安装并重启；设备在网络、验证或写入失败时保持现有可启动固件。

## 主场景

1. Settings 显示 current version，用户触发 Check。
2. Firmware runtime 获取 Wi-Fi metadata lease，读取 release metadata 并比较 target/profile/version。
3. 有更新时显示 latest version 和 UpdateAvailable；没有时显示 UpToDate。
4. 用户触发 Install，runtime 取得 OTA download/exclusive ownership，下载并验证 image。
5. 写入 inactive OTA target，完成后标记 boot partition，进入 Rebooting。

## 失败与恢复

- unsupported target、无网络、metadata invalid、版本不兼容、下载中断、image 验证失败和 OTA write 失败进入 Error。
- 不能在验证完成前修改 boot target。
- 安装期间 Wi-Fi/flash 为不可抢占活动，实时通话和其他 HTTP 请求得到具体拒绝原因。

源码：`modules/core_sys/include/platform/ui/firmware_update_runtime.h`、`platform/esp/arduino_common/src/platform_ui_firmware_update_runtime.cpp`、Settings firmware actions。

## 下钻

- [Activity](update-device-firmware/activity.md)
- [Sequence](update-device-firmware/sequences/sequence-update-device-firmware.md)
- [State Machine](update-device-firmware/state-machines/firmware-update.md)
