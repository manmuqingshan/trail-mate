# P1 · 【设计未形成】配置缺少版本、验证与原子提交 owner

状态：**acknowledged**
类别：**设计缺陷 / 配置与环境**

## 结论

配置数据存在，但没有形成负责版本、验证和原子变更的 Configuration aggregate。`MeshConfig` 位于 `chat_types.h`，`AppConfig` 汇集跨领域设置，默认值、兼容转换和持久化又分散在 `core_sys` 与平台 store。

## 当前职责混合

- 领域设置：协议、团队、定位、显示等业务含义。
- 产品组合：某目标是否允许某设置。
- 持久化 schema：字段版本、缺省和 migration。
- 平台机制：NVS / file / flash 的读写。

这些职责需要协作，但不能由一个巨大 struct 和多个平台 loader 共同“默认拥有”。

## 目标模型

- `ConfigurationSnapshot`：有 schema version 的不可变已提交快照。
- typed settings：`CommunicationSettings`、`TeamSettings`、`PositioningSettings` 等。
- `ConfigurationPolicy`：跨字段与 capability 约束。
- `ConfigurationService`：validate → migrate → atomically commit。
- `IConfigurationStore`：只保存/读取序列化快照。

## 不变量

1. 无效配置不能部分写入。
2. migration 必须显式、可重复并记录来源版本。
3. 目标不支持的 capability 不能被配置强制开启。
4. 平台 store 不定义业务默认值。

证据：`modules/core_chat/include/chat/domain/chat_types.h`、`modules/core_sys/include/app/app_config.h`、`platform/esp/arduino_common/src/app_config_store.cpp`。
