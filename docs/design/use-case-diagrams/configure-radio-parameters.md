# Use Case：切换活动协议并提交无线配置

状态：**confirmed**

业务边界：网络、身份与目录

主要参与者：设备用户
支持系统：Target Capability、AppConfig、MeshAdapterRouter、协议分区存储、Radio owner

## 用户目标

在 Meshtastic、MeshCore 与 Reticulum 之间选择一个当前协议，使身份、频道/密钥、无线参数、backend 与 UI 使用同一份已提交配置。

## 前置条件与触发

- 目标 manifest 声明支持所选协议与所需 radio/bearer。
- 用户在 Settings 选择协议，或修改当前协议的 region、channel、PSK、LoRa preset、Reticulum bearer。
- 修改先进入编辑状态，不能在每个字段变化时假装整体配置已经成功应用。

## 成功场景

1. `AppConfig` 校验协议、区域、频率、带宽、SF、CR、发射功率、频道与密钥组合。
2. `MeshAdapterRouter` 停止旧 backend，清空属于旧协议的活动引用，但不删除其他协议分区的数据。
3. 从对应协议分区加载本机身份、peer facts、频道密钥和协议设置。
4. 创建并安装新 backend，应用有效用户信息和无线配置。
5. backend 启动成功后保存配置，更新 active protocol，并刷新 Chat/Contacts/Network 投影。

## 失败与恢复

- 目标不支持：在释放旧 backend 前拒绝。
- 新 backend 创建或 radio 配置失败：进入明确 stopped/error 状态；不得显示新协议已可用。
- 持久化失败：运行态变更与“已保存”必须区分，并提示用户重试。
- 切换协议不合并 NodeId、密钥或消息去重空间。

## 业务规则

- 同一时刻只有一个活动 mesh backend/radio owner。
- Meshtastic、MeshCore、Reticulum 的身份、寻址、频道与 ACK 语义保持隔离。
- RNode bridge 只是 Reticulum bearer，不等于本机 LXMF identity。

## 源码证据

- `modules/core_sys/include/app/app_config.h`
- `modules/core_chat/include/chat/infra/mesh_adapter_router.h`
- `apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp`
- `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp`

## 下钻

- [Activity：协议切换与提交](configure-radio-parameters/activity.md)
- [Sequence：Settings 到活动 backend](configure-radio-parameters/sequences/sequence-configure-radio-parameters.md)
