# Use Case：备份、恢复或重置设备设置

状态：**confirmed behavior；configuration model incomplete**
业务边界：设备维护与数据所有权

## 用户目标

在修改复杂协议/GNSS/显示/网络设置前建立本地备份；需要时验证并恢复；只有在明确确认后才执行删除消息、节点、mesh 设置或 factory reset。

## 行为与规则

1. Backup 检查 SD 可用性，读取受支持设置集合并写入临时文件，成功后原子替换目标备份。
2. Restore 先解析/验证备份，再写入设置；失败时保留当前配置，成功后按需要重启/重新初始化 owner。
3. Reset Mesh、Reset Nodes、Clear Messages 和 Factory Reset 是不同破坏范围，必须分别确认。
4. 当前 `AppConfig` 缺少统一 schema version、跨字段验证和原子 ConfigurationService；文档不能承诺超出 runtime 实际支持的完整事务。

源码：`modules/core_sys/include/platform/ui/settings_backup_runtime.h`、`platform/esp/arduino_common/src/platform_ui_settings_backup_runtime.cpp`、Settings reset actions。

## 下钻

- [Activity](backup-restore-settings/activity.md)
- [Sequence](backup-restore-settings/sequences/sequence-backup-restore-settings.md)
