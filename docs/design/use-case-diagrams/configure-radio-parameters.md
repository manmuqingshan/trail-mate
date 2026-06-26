# 用例图：配置无线电参数

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
语义 HTML：configure-radio-parameters.html

## 身份信息

- ID：use-case:configure-radio-parameters
- 业务边界路径：Trail Mate 系统 / 聊天通信
- 当前边界类型：业务模块
- 当前边界职责：管理用户间的消息传递和无线电链路配置
- 状态：candidate
- 置信度：medium
- Markdown 路径：docs/design/use-case-diagrams/configure-radio-parameters.md
- HTML 路径：docs/design/use-case-diagrams/configure-radio-parameters.html
- 触发条件：用户进入无线电设置页面

## 故事摘要

用户调整 LoRa 或 FSK 无线电调制、频率、功率、超时等参数

## 参与者

- 用户（个人）

## 外部系统

- Meshtastic 网络

## UML 下钻地图

- Use Case Diagram：[配置无线电参数](configure-radio-parameters.html)
  - Activity Diagram：[业务流程：配置无线电参数](configure-radio-parameters/activity.html)
  - Sequence Diagram：[对象交互：配置无线电](configure-radio-parameters/sequences/sequence-configure-radio-parameters.html)

## Mermaid 用例图

```mermaid
flowchart LR
  actor_actor_user["&laquo;Actor&raquo;<br/>用户"]
  external_external_system_meshtastic_network["&laquo;External System&raquo;<br/>Meshtastic 网络"]
  subgraph system_system_trail_mate["Trail Mate 系统"]
    subgraph system_business_module_chat_communication["聊天通信"]
      useCase_use_case_configure_radio_parameters(["配置无线电参数"])
    end
  end
  actor_actor_user --- useCase_use_case_configure_radio_parameters
  external_external_system_meshtastic_network --- useCase_use_case_configure_radio_parameters
```

## 设计指标索引

此章节是 Design Explorer 可解析的指标索引。每个数字都必须能回溯到这里的具体条目，避免 UI 只展示不可解释的计数。

| 指标 | 数量 | 内容边界 |
| --- | ---: | --- |
| 节点 | 5 | 参与者、外部系统、上下文和当前用例。 |
| 关系 | 6 | 当前用例与上下文、参与者、外部系统或其他用例之间的关系。 |
| 证据 | 2 | 支撑当前候选用例的文件、代码证据、规范或推断证据。 |
| 待裁决 | 0 | 仍需产品或业务负责人裁决的外部问题。 |

### 节点

- Trail Mate 系统（业务边界：系统边界） - 管理整个应用的用户目标和业务能力
- 聊天通信（业务边界：业务模块） - 管理用户间的消息传递和无线电链路配置
- 配置无线电参数（用例） - 用户调整 LoRa 或 FSK 无线电调制、频率、功率、超时等参数
- 用户（参与者） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- Meshtastic 网络（外部系统） - 基于 LoRa 的 Mesh 通信网络，用于传输消息和位置信息

### 关系

- Trail Mate 系统 包含业务边界「聊天通信」（包含关系）
- 聊天通信 包含 Use Case「配置无线电参数」（包含关系）
- 用户 作为主要参与者参与「配置无线电参数」（参与关系） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- Meshtastic 网络 参与「配置无线电参数」（外部协作） - 基于 LoRa 的 Mesh 通信网络，用于传输消息和位置信息
- 用户 -> 配置无线电参数（参与关系） - 用户配置无线电
- 用户 -> 配置无线电参数（参与关系） - 用户配置无线电参数

### 证据

- 证据 1 · 本地仓库扫描 · boards/tlora_pager/src/tlora_pager_board.cpp · 1-1（FACT:strong） - 无线电配置函数

  ```text
  configureLoraRadio, configureFskRadio, apply_tx_power
  ```
- 证据 2 · 本地仓库扫描 · boards/tlora_pager/src/tlora_pager_board.cpp · 1-1（FACT:strong） - 无线电配置相关方法

  ```text
  tlora_pager_board.cpp
  ```

### 待裁决

- 无

## 主成功路径

1. 用户修改无线电频率、调制方式或功率设置
2. 系统应用新的无线电配置
3. 系统开始使用新参数发送/接收数据
4. 用户修改无线电参数
5. 系统验证参数合法性
6. 系统将参数写入无线电模块并生效

## 备选路径

1. 无

## 失败路径

1. 设置无效时，回滚到之前有效配置或提示错误
2. 参数不合法，系统拒绝并提示

## 证据

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 无线电配置函数 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 无线电配置相关方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新候选用例图「配置无线电参数」。

<!-- praxis:use-case-diagram:end -->
