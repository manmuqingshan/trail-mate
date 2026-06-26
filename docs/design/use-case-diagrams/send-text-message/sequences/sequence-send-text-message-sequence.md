# Sequence Diagram：对象交互：发送文本消息主成功场景

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
父级 Use Case：use-case:send-text-message
业务边界路径：Trail Mate 系统 / 聊天通信
父级文档：docs/design/use-case-diagrams/send-text-message.md
Markdown 路径：docs/design/use-case-diagrams/send-text-message/sequences/sequence-send-text-message-sequence.md
HTML 路径：docs/design/use-case-diagrams/send-text-message/sequences/sequence-send-text-message-sequence.html

## 交互场景

消息从用户输入到网络发射

## 参与者 / 生命线

- participant User as 用户
- participant UI as ChatUI
- participant Logic as ChatLogic
- participant Board as TLoRaPagerBoard

## 消息时序

- User->>UI: 输入消息并发送
- UI->>Logic: sendMessage(text)
- Logic->>Board: sendRawMessage(data)
- Board-->>Logic: 发射结果
- Logic-->>UI: 更新聊天记录
- UI-->>User: 显示消息已发送

## 同步 / 异步 / 回调

- Board-->>Logic: 发射结果
- Logic-->>UI: 更新聊天记录
- UI-->>User: 显示消息已发送

## 返回 / 异常 / 补偿

- Board-->>Logic: 发射结果
- Logic-->>UI: 更新聊天记录
- UI-->>User: 显示消息已发送

## 事务 / 幂等 / 重试边界

ChatLogic 作为应用服务，Board 作为端口适配器

- 无

## Sequence UML 读图说明

用户、UI、ChatLogic、Board 的顺序交互

## 实现范围锚点

- 模块：apps/linux_uconsole_gtk
- 模块：boards
- 关键文件：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp#L1
- 不覆盖代码：非当前场景的全部调用链
- 不覆盖代码：未被证据支持的异步/补偿路径

## 不覆盖场景

- 发射失败回调
- 消息确认


## Mermaid 图

```mermaid
sequenceDiagram
  participant User as 用户
  participant UI as ChatUI
  participant Logic as ChatLogic
  participant Board as TLoRaPagerBoard
  User->>UI: 输入消息并发送
  UI->>Logic: sendMessage(text)
  Logic->>Board: sendRawMessage(data)
  Board-->>Logic: 发射结果
  Logic-->>UI: 更新聊天记录
  UI-->>User: 显示消息已发送
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | apps/nrf52_node/src/nrf52_node_app_facade_runtime.cpp | 9-9 | strong | Nrf52 节点固件包含聊天服务 |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp | 1-1 | strong | 聊天页面生命周期提供输入处理与发送逻辑 |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp | 1-1 | strong | 聊天逻辑处理消息发送流程 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「发送文本消息」的 Sequence Diagram。

<!-- praxis:use-case-diagram:end -->
