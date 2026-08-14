# Use Case：备份、恢复或重置设备设置

状态：**confirmed behavior；configuration model incomplete**
业务边界：设备维护与数据所有权

## 用户目标

在修改复杂协议/GNSS/显示/网络设置前建立本地备份；需要时验证并恢复；只有在明确确认后才执行删除消息、节点、mesh 设置或 factory reset。

## 行为与规则

1. Backup 检查 SD 可用性，聚合所有用户设置 owner：`AppConfig`、`settings_store` 偏好和 Reticulum 群组，写入临时文件后原子替换目标备份。`settings_store` 的每个受支持键都携带存在状态；源端未显式写入即记录为默认态。
2. Restore 先解析/验证备份；Reticulum 群组必须交给其 SD owner 完成实际落盘，不能只更新 `AppConfig` 的运行期镜像。只有该 SD 写入成功后才写 NVS 偏好并提交 `AppConfig`，因此 SD 失败不会改变这两个 owner。成功后按需要重启/重新初始化 owner。
3. Reset Mesh、Reset Nodes、Clear Messages 和 Factory Reset 是不同破坏范围，必须分别确认。
4. 当前 `AppConfig` 缺少统一 schema version、跨字段验证和跨 NVS/SD owner 的原子 ConfigurationService；文档不能承诺超出 runtime 实际支持的完整事务。设置备份格式当前为 schema v2：恢复 `present: false` 的偏好会清除目标 NVS 覆盖并回归默认态；v1 可导入且不会清空 v2 新增字段或其未列出的偏好。

源码：`modules/core_sys/include/platform/ui/settings_backup_runtime.h`、`platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp`、Settings reset actions。

## 下钻

- [Activity](backup-restore-settings/activity.md)
- [Sequence](backup-restore-settings/sequences/sequence-backup-restore-settings.md)
