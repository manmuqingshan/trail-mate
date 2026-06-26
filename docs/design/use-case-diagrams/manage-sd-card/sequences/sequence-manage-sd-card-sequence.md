# Sequence Diagram：对象交互：管理 SD 卡

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
父级 Use Case：use-case:manage-sd-card
业务边界路径：Trail Mate 系统 / 设备管理
父级文档：docs/design/use-case-diagrams/manage-sd-card.md
Markdown 路径：docs/design/use-case-diagrams/manage-sd-card/sequences/sequence-manage-sd-card-sequence.md
HTML 路径：docs/design/use-case-diagrams/manage-sd-card/sequences/sequence-manage-sd-card-sequence.html

## 交互场景

使外部存储可用或安全移除

## 参与者 / 生命线

- participant User as 用户
- participant UI as SDCardUI
- participant Board as TLoRaPagerBoard

## 消息时序

- User->>UI: 点击挂载 SD 卡
- UI->>Board: mountSdCard()
- Board-->>UI: 返回挂载结果
- UI-->>User: 显示成功/失败

## 同步 / 异步 / 回调

- Board-->>UI: 返回挂载结果
- UI-->>User: 显示成功/失败

## 返回 / 异常 / 补偿

- Board-->>UI: 返回挂载结果
- UI-->>User: 显示成功/失败

## 事务 / 幂等 / 重试边界

Board 作为 SD 卡适配器

- 无

## Sequence UML 读图说明

UI 发送命令，Board 操作硬件并返回状态

## 实现范围锚点

- 模块：boards
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：非当前场景的全部调用链
- 不覆盖代码：未被证据支持的异步/补偿路径

## 不覆盖场景

- 错误处理


## Mermaid 图

```mermaid
sequenceDiagram
  participant User as 用户
  participant UI as SDCardUI
  participant Board as TLoRaPagerBoard
  User->>UI: 点击挂载 SD 卡
  UI->>Board: mountSdCard()
  Board-->>UI: 返回挂载结果
  UI-->>User: 显示成功/失败
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | SD 卡管理函数 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | SD 卡操作相关方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「管理 SD 卡」的 Sequence Diagram。

<!-- praxis:use-case-diagram:end -->
