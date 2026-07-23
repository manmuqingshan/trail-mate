# Sequence：Settings Backup Store
```mermaid
sequenceDiagram
  actor U as 用户
  participant UI as Settings
  participant Backup as Settings Backup Runtime
  participant Config as Config Stores
  participant SD as SD Filesystem
  U->>UI: Backup
  UI->>Backup: backup()
  Backup->>Config: read supported settings
  Backup->>SD: write temp + fsync/close + replace
  SD-->>UI: success / error
  U->>UI: Restore
  UI->>Backup: restore()
  Backup->>SD: read backup
  Backup->>Backup: parse + validate
  Backup->>Config: apply only after validation
  Config-->>UI: reinitialize/reboot required
```

## 场景与责任

Settings 收集用户命令；Backup Runtime 定义版本化格式和事务；Config Stores 提供受支持字段与原子应用；SD Filesystem 只负责文件语义。UI 不直接遍历或覆盖各配置文件。

## Backup 顺序

读取受支持设置形成不可变快照，写临时文件，flush/fsync/close 后原子 replace。任何阶段失败保留旧备份。UI 只有收到 replace 成功才显示新备份时间。

## Restore 顺序

先完整 read、parse、版本迁移和字段验证，再调用 Config apply。验证失败不得写任何 owner。Apply 返回每个 owner 的生效策略：立即 reinitialize、下次启动或必须 reboot。

## 一致性与敏感数据

跨多个 Config Store 的 apply 需要聚合 validation 和受控提交；否则中途失败会形成混合版本。备份格式明确标注敏感 key 是否包含，并避免在错误日志中输出值。

## 测试

覆盖 temp write/close/replace 失败、旧版本迁移、未知字段、跨 Store 验证失败、部分 apply 和 reboot-required 投影。
