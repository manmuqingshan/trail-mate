# Use Case：向手机应用提供协议兼容服务

状态：**confirmed integration behavior**
业务边界：外部应用与主机集成

## 用户目标

让 Meshtastic 或 MeshCore 手机应用通过其原生 BLE contract 读取节点/配置/GPS、交换消息并修改允许的设置，同时保持设备内部单一 App capability facade 和活动协议 owner。

## 主场景

1. Target capability 决定是否启用 phone BLE；活动 mesh protocol 选择 Meshtastic BLE core 或 MeshCore BLE service。
2. BLE service 完成连接和协议握手，解析 ToRadio/commands。
3. `IPhoneAppFacade / AppPhoneFacade` 提供用户信息、配置、节点、频道、GPS、消息和允许的 tuning operations。
4. facade 把请求映射到当前应用服务/active backend；响应由对应协议 core 编码为自己的 wire frame。
5. 断开时清理 BLE session/ring state，不改变已经提交的设备配置或消息。

## 边界规则

- Meshtastic protobuf 与 MeshCore BLE framing 不合并。
- phone app 是外部客户端，不拥有 radio、ContactService 或 message ledger。
- 大 frame/protobuf 使用成员 scratch 或固定环形槽，不放在 ESP task stack。
- “companion App BLE 已移除”指旧 companion 工作流，不等于这两个 phone protocol service 不存在。

源码：`modules/core_phone/include/phone/ports/i_phone_app_facade.h`、`platform/esp/arduino_common/include/ble/app_phone_facade.h`、Meshtastic/MeshCore BLE cores。

## 下钻

- [Activity](sync-phone-application/activity.md)
- [Sequence](sync-phone-application/sequences/sequence-sync-phone-application.md)
- [Composite Structure](sync-phone-application/composite-structures/phone-protocol-boundary.md)
