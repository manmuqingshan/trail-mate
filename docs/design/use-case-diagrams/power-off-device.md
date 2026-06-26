# 用例图：关闭设备电源

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
语义 HTML：power-off-device.html

## 身份信息

- ID：use-case:power-off-device
- 业务边界路径：Trail Mate 系统 / 设备管理
- 当前边界类型：业务模块
- 当前边界职责：提供设备状态的可见性和用户可控的硬件管理
- 状态：candidate
- 置信度：medium
- Markdown 路径：docs/design/use-case-diagrams/power-off-device.md
- HTML 路径：docs/design/use-case-diagrams/power-off-device.html
- 触发条件：用户选择关机或长按电源键

## 故事摘要

用户通过长按电源按钮或菜单选择关机，系统安全关闭硬件

## 参与者

- 用户（个人）

## 外部系统

- 无

## UML 下钻地图

- Use Case Diagram：[关闭设备电源](power-off-device.html)
  - Activity Diagram：[业务流程：关闭设备电源](power-off-device/activity.html)
  - Sequence Diagram：[对象交互：关机](power-off-device/sequences/sequence-power-off-device-sequence.html)

## Mermaid 用例图

```mermaid
flowchart LR
  actor_actor_user["&laquo;Actor&raquo;<br/>用户"]
  subgraph system_system_trail_mate["Trail Mate 系统"]
    subgraph system_business_module_device_management["设备管理"]
      useCase_use_case_power_off_device(["关闭设备电源"])
    end
  end
  actor_actor_user --- useCase_use_case_power_off_device
```

## 设计指标索引

此章节是 Design Explorer 可解析的指标索引。每个数字都必须能回溯到这里的具体条目，避免 UI 只展示不可解释的计数。

| 指标 | 数量 | 内容边界 |
| --- | ---: | --- |
| 节点 | 4 | 参与者、外部系统、上下文和当前用例。 |
| 关系 | 5 | 当前用例与上下文、参与者、外部系统或其他用例之间的关系。 |
| 证据 | 2 | 支撑当前候选用例的文件、代码证据、规范或推断证据。 |
| 待裁决 | 0 | 仍需产品或业务负责人裁决的外部问题。 |

### 节点

- Trail Mate 系统（业务边界：系统边界） - 管理整个应用的用户目标和业务能力
- 设备管理（业务边界：业务模块） - 提供设备状态的可见性和用户可控的硬件管理
- 关闭设备电源（用例） - 用户通过长按电源按钮或菜单选择关机，系统安全关闭硬件
- 用户（参与者） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具

### 关系

- Trail Mate 系统 包含业务边界「设备管理」（包含关系）
- 设备管理 包含 Use Case「关闭设备电源」（包含关系）
- 用户 作为主要参与者参与「关闭设备电源」（参与关系） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- 用户 -> 关闭设备电源（参与关系） - 用户关机
- 用户 -> 关闭设备电源（参与关系） - 用户关机

### 证据

- 证据 1 · 本地仓库扫描 · boards/tlora_pager/src/tlora_pager_board.cpp · 1-1（FACT:strong） - 关机逻辑

  ```text
  handlePowerButton, shutdown
  ```
- 证据 2 · 本地仓库扫描 · boards/tlora_pager/src/tlora_pager_board.cpp · 1-1（FACT:strong） - 关机相关方法

  ```text
  tlora_pager_board.cpp
  ```

### 待裁决

- 无

## 主成功路径

1. 用户触发关机指令
2. 系统执行关机流程：停止无线电、保存数据、卸载 SD 卡等
3. 系统关闭电源
4. 用户触发关机流程
5. 系统关闭所有外设和无线电
6. 系统切断电源

## 备选路径

1. 无

## 失败路径

1. 无

## 证据

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 关机逻辑 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 关机相关方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新候选用例图「关闭设备电源」。

<!-- praxis:use-case-diagram:end -->
