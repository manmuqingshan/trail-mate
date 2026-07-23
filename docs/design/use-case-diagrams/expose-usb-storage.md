# Use Case：把设备存储安全交给 USB 主机

状态：**confirmed**
业务边界：设备维护与数据所有权

## 用户目标

让 PC 以 USB Mass Storage 访问设备 SD 卡，同时避免设备自身 GPS、track、radio 或文件 worker 与主机并发写同一介质；退出后恢复设备使用。

## 主场景

1. 只有 USB support 和 SD ready 的目标显示入口。
2. `prepare_mass_storage_mode` 请求相关 worker 停止/flush，暂停 GPS/radio tasks、screen sleep 等会触碰共享资源的活动。
3. device unmount/deinit application SD owner，USB backend 接管介质并报告 Active。
4. 用户退出或主机断开时 stop backend；重新 mount application SD，恢复 tasks 和 screen policy。

## 失败与恢复

- 任一 owner 未能停止或 SD 无法卸载时，不启动 USB backend。
- USB 启动失败必须恢复 application mount 和暂停的 tasks。
- 退出是异步过程；页面在 restore 完成前显示 stopping，不能提前返回让应用访问 SD。

源码：`modules/core_sys/include/platform/ui/usb_support_runtime.h`、`platform/esp/arduino_common/src/platform_ui_usb_support_runtime.cpp`、`modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp`。

## 下钻

- [Activity](expose-usb-storage/activity.md)
- [Sequence](expose-usb-storage/sequences/sequence-expose-usb-storage.md)
- [State Machine](expose-usb-storage/state-machines/usb-storage-ownership.md)
