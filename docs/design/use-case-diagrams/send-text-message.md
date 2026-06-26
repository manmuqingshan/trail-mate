# 用例图：发送文本消息

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
所属地图：../use-case-diagrams-maps.md
语义 HTML：send-text-message.html

## 身份信息

- ID：use-case:send-text-message
- 业务边界路径：Trail Mate 系统 / 聊天通信
- 当前边界类型：业务模块
- 当前边界职责：管理用户间的消息传递和无线电链路配置
- 状态：candidate
- 置信度：medium
- Markdown 路径：docs/design/use-case-diagrams/send-text-message.md
- HTML 路径：docs/design/use-case-diagrams/send-text-message.html
- 触发条件：用户在聊天界面输入消息并点击发送

## 故事摘要

用户输入文本，通过 Meshtastic 网络将消息发送给其他节点

## 参与者

- 用户（个人）

## 外部系统

- Meshtastic 网络

## UML 下钻地图

- Use Case Diagram：[发送文本消息](send-text-message.html)
  - Activity Diagram：[业务流程：发送文本消息](send-text-message/activity.html)
  - Sequence Diagram：[对象交互：发送文本消息主成功场景](send-text-message/sequences/sequence-send-text-message-sequence.html)

## Mermaid 用例图

```mermaid
flowchart LR
  actor_actor_user["&laquo;Actor&raquo;<br/>用户"]
  external_external_system_meshtastic_network["&laquo;External System&raquo;<br/>Meshtastic 网络"]
  subgraph system_system_trail_mate["Trail Mate 系统"]
    subgraph system_business_module_chat_communication["聊天通信"]
      useCase_use_case_send_text_message(["发送文本消息"])
    end
  end
  actor_actor_user --- useCase_use_case_send_text_message
  external_external_system_meshtastic_network --- useCase_use_case_send_text_message
```

## 设计指标索引

此章节是 Design Explorer 可解析的指标索引。每个数字都必须能回溯到这里的具体条目，避免 UI 只展示不可解释的计数。

| 指标 | 数量 | 内容边界 |
| --- | ---: | --- |
| 节点 | 5 | 参与者、外部系统、上下文和当前用例。 |
| 关系 | 10 | 当前用例与上下文、参与者、外部系统或其他用例之间的关系。 |
| 证据 | 3 | 支撑当前候选用例的文件、代码证据、规范或推断证据。 |
| 待裁决 | 0 | 仍需产品或业务负责人裁决的外部问题。 |

### 节点

- Trail Mate 系统（业务边界：系统边界） - 管理整个应用的用户目标和业务能力
- 聊天通信（业务边界：业务模块） - 管理用户间的消息传递和无线电链路配置
- 发送文本消息（用例） - 用户输入文本，通过 Meshtastic 网络将消息发送给其他节点
- 用户（参与者） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- Meshtastic 网络（外部系统） - 基于 LoRa 的 Mesh 通信网络，用于传输消息和位置信息

### 关系

- Trail Mate 系统 包含业务边界「聊天通信」（包含关系）
- 聊天通信 包含 Use Case「发送文本消息」（包含关系）
- 用户 作为主要参与者参与「发送文本消息」（参与关系） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- Meshtastic 网络 参与「发送文本消息」（外部协作） - 基于 LoRa 的 Mesh 通信网络，用于传输消息和位置信息
- 用户 -> 发送文本消息（参与关系） - 用户是发送消息的参与者
- Meshtastic 网络 -> 发送文本消息（参与关系） - Meshtastic 网络作为消息传输中介
- 用户 -> 发送文本消息（参与关系） - 用户参与发送消息
- Meshtastic 网络 -> 发送文本消息（参与关系） - Meshtastic 网络传输消息
- 用户 -> 发送文本消息（参与关系） - 用户参与发送消息
- Meshtastic 网络 -> 发送文本消息（参与关系） - Meshtastic 网络传输消息

### 证据

- 证据 1 · 本地仓库扫描 · apps/nrf52_node/src/nrf52_node_app_facade_runtime.cpp · 9-9（FACT:strong） - Nrf52 节点固件包含聊天服务

  ```text
  #include "chat/usecase/chat_service.h"
  ```
- 证据 2 · 本地仓库扫描 · apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp · 1-1（FACT:strong） - 聊天页面生命周期提供输入处理与发送逻辑

  ```text
  makeChatPageLifecycle
  ```
- 证据 3 · 本地仓库扫描 · apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_chat_logic.cpp · 1-1（FACT:strong） - 聊天逻辑处理消息发送流程

  ```text
  chat_logic.cpp
  ```

### 待裁决

- 无

## 主成功路径

1. 1. 用户输入文本并触发发送
2. 2. 系统通过 ChatService 将消息打包
3. 3. 通过 MeshAdapter 发送到网络
4. 4. 显示发送成功反馈
5. 用户在聊天页面输入消息并触发发送命令
6. 系统将消息打包并通过无线电发送
7. 系统将发出的消息添加到本地历史记录
8. 用户编写文本消息
9. 用户触发发送
10. 系统将消息封装并通过无线电发射

## 备选路径

1. 无

## 失败路径

1. 无线电通道忙碌或超时，向用户显示发送失败提示
2. 无线电发送失败，系统显示错误提示

## 证据

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
- 恢复或更新候选用例图「发送文本消息」。

<!-- praxis:use-case-diagram:end -->
