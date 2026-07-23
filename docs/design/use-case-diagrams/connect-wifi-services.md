# Use Case：连接 Wi-Fi 并仲裁联网能力

状态：**confirmed**

业务边界：网络、身份与目录 / 设备资源治理

主要参与者：设备用户
资源申请者：FirmwareUpdate、PackRepository、RouteStorage、MeshMqtt、ReticulumGateway

## 用户目标

连接已选择的 Wi-Fi，使更新、扩展包、MQTT、路线下载或 Reticulum gateway 可用，同时不让后台网络活动破坏实时通话、OTA、屏幕唤醒保护或 radio 响应。

## 成功场景

1. 用户启用 Wi-Fi、扫描网络、选择 SSID 并提交凭据。
2. Wi-Fi runtime 尝试连接并返回明确状态；凭据只有保存成功后才成为自动连接来源。
3. 需要网络的功能提交包含 `Client / AccessKind / Priority / allowConnect` 的 Request。
4. Wi-Fi access runtime 根据 ScreenPhase、OTA、通话独占、不可抢占活动与当前 owner 返回 Lease 或具体 Decision。
5. 获得 lease 的调用方执行有界操作，完成后 release；长连接按 traffic budget 读写。

## 失败与恢复

- 无凭据、Wi-Fi 禁用、断开、连接退避、屏幕保护期、OTA 独占、Call 独占和 Busy 必须可区分。
- lease 被撤销时调用方停止当前网络工作，不继续复用旧 generation。
- 通话从响铃软抢占进入 ActiveCall 时可升级为 exclusive，并使后台 HTTP/长连接让路。

## 源码证据

- `modules/core_sys/include/platform/ui/wifi_access_runtime.h`
- `platform/esp/arduino_common/src/platform_ui_wifi_access_runtime.cpp`
- `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp`

## 下钻

- [Activity：连接与资源请求](connect-wifi-services/activity.md)
- [Sequence：客户端取得和释放 Lease](connect-wifi-services/sequences/sequence-connect-wifi-services.md)
