# Sequence Diagram：对象交互：查看硬件状态

<!-- praxis:use-case-diagram:start -->

## 元数据

项目版本：0.1.30-alpha
设计文档版本：0.1.30-alpha
Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty
Agent 版本决策：none - 本次渲染没有单独的 agent 版本决策
更新于：2026-06-25T14:17:55.307Z
来源：Agent 候选分析
父级 Use Case：use-case:view-hardware-status
业务边界路径：Trail Mate 系统 / 设备管理
父级文档：docs/design/use-case-diagrams/view-hardware-status.md
Markdown 路径：docs/design/use-case-diagrams/view-hardware-status/sequences/sequence-view-hardware-status-sequence.md
HTML 路径：docs/design/use-case-diagrams/view-hardware-status/sequences/sequence-view-hardware-status-sequence.html

## 交互场景

诊断设备硬件状态

## 参与者 / 生命线

- participant User as 用户
- participant UI as HardwareUI
- participant Board as TLoRaPagerBoard

## 消息时序

- User->>UI: 打开硬件状态
- UI->>Board: isGpsReady()
- Board-->>UI: 返回是/否
- UI->>Board: isRadioReady()
- Board-->>UI: 返回是/否
- UI->>Board: isSdCardReady()
- Board-->>UI: 返回是/否
- UI-->>User: 显示各状态

## 同步 / 异步 / 回调

- Board-->>UI: 返回是/否
- Board-->>UI: 返回是/否
- Board-->>UI: 返回是/否
- UI-->>User: 显示各状态

## 返回 / 异常 / 补偿

- Board-->>UI: 返回是/否
- Board-->>UI: 返回是/否
- Board-->>UI: 返回是/否
- UI-->>User: 显示各状态

## 事务 / 幂等 / 重试边界

采用轮询模式，HardwareLayout 作为协调者

- 无

## Sequence UML 读图说明

UI 向 Board 发送多个查询，汇总后显示

## 实现范围锚点

- 模块：apps/linux_uconsole_gtk
- 模块：boards
- 关键文件：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_hardware_layout.cpp
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：非当前场景的全部调用链
- 不覆盖代码：未被证据支持的异步/补偿路径

## 不覆盖场景

- 连续监控
- 日志记录


## Mermaid 图

```mermaid
sequenceDiagram
  participant User as 用户
  participant UI as HardwareUI
  participant Board as TLoRaPagerBoard
  User->>UI: 打开硬件状态
  UI->>Board: isGpsReady()
  Board-->>UI: 返回是/否
  UI->>Board: isRadioReady()
  Board-->>UI: 返回是/否
  UI->>Board: isSdCardReady()
  Board-->>UI: 返回是/否
  UI-->>User: 显示各状态
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_hardware_logic.cpp | 1-1 | strong | 硬件页面逻辑负责调用各种硬件状态检查函数 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 硬件状态查询的实现 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「查看硬件状态」的 Sequence Diagram。

<!-- praxis:use-case-diagram:end -->
