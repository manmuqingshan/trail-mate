# 用例图：查看硬件状态

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
语义 HTML：view-hardware-status.html

## 身份信息

- ID：use-case:view-hardware-status
- 业务边界路径：Trail Mate 系统 / 设备管理
- 当前边界类型：业务模块
- 当前边界职责：提供设备状态的可见性和用户可控的硬件管理
- 状态：candidate
- 置信度：medium
- Markdown 路径：docs/design/use-case-diagrams/view-hardware-status.md
- HTML 路径：docs/design/use-case-diagrams/view-hardware-status.html
- 触发条件：用户进入硬件状态页面

## 故事摘要

用户查看所有硬件模块的就绪状态，包括 GPS、无线电、传感器、SD 卡、电池、充电等

## 参与者

- 用户（个人）

## 外部系统

- 无

## UML 下钻地图

- Use Case Diagram：[查看硬件状态](view-hardware-status.html)
  - Activity Diagram：[业务流程：查看硬件状态](view-hardware-status/activity.html)
  - Sequence Diagram：[对象交互：查看硬件状态](view-hardware-status/sequences/sequence-view-hardware-status-sequence.html)

## Mermaid 用例图

```mermaid
flowchart LR
  actor_actor_user["&laquo;Actor&raquo;<br/>用户"]
  subgraph system_system_trail_mate["Trail Mate 系统"]
    subgraph system_business_module_device_management["设备管理"]
      useCase_use_case_view_hardware_status(["查看硬件状态"])
    end
  end
  actor_actor_user --- useCase_use_case_view_hardware_status
```

## 设计指标索引

此章节是 Design Explorer 可解析的指标索引。每个数字都必须能回溯到这里的具体条目，避免 UI 只展示不可解释的计数。

| 指标 | 数量 | 内容边界 |
| --- | ---: | --- |
| 节点 | 4 | 参与者、外部系统、上下文和当前用例。 |
| 关系 | 9 | 当前用例与上下文、参与者、外部系统或其他用例之间的关系。 |
| 证据 | 2 | 支撑当前候选用例的文件、代码证据、规范或推断证据。 |
| 待裁决 | 0 | 仍需产品或业务负责人裁决的外部问题。 |

### 节点

- Trail Mate 系统（业务边界：系统边界） - 管理整个应用的用户目标和业务能力
- 设备管理（业务边界：业务模块） - 提供设备状态的可见性和用户可控的硬件管理
- 查看硬件状态（用例） - 用户查看所有硬件模块的就绪状态，包括 GPS、无线电、传感器、SD 卡、电池、充电等
- 用户（参与者） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具

### 关系

- Trail Mate 系统 包含业务边界「设备管理」（包含关系）
- 设备管理 包含 Use Case「查看硬件状态」（包含关系）
- 用户 作为主要参与者参与「查看硬件状态」（参与关系） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- 用户 -> 查看硬件状态（参与关系） - 用户查看硬件
- 查看硬件状态 -> 查看设备概览（includes） - 概览页包含部分硬件状态
- 查看数据详情 -> 查看硬件状态（includes） - 数据页依赖硬件传感器
- 用户 -> 查看硬件状态（参与关系） - 用户查看硬件状态
- 查看硬件状态 -> 查看设备概览（includes） - 概览页面包含部分硬件状态
- 查看数据详情 -> 查看硬件状态（includes） - 数据页依赖硬件传感器

### 证据

- 证据 1 · 本地仓库扫描 · apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_hardware_logic.cpp · 1-1（FACT:strong） - 硬件页面逻辑负责调用各种硬件状态检查函数

  ```text
  refreshHardwarePage, makeHardwarePageLifecycle
  ```
- 证据 2 · 本地仓库扫描 · boards/tlora_pager/src/tlora_pager_board.cpp · 1-1（FACT:strong） - 硬件状态查询的实现

  ```text
  tlora_pager_board.cpp
  ```

### 待裁决

- 无

## 主成功路径

1. 用户选择查看硬件状态
2. 系统依次检查 GPS、无线电、传感器、SD 卡、电池等
3. 系统显示每个硬件的状态（就绪/未就绪/错误）及电池电量、温度、充电状态
4. 用户选择硬件状态页面
5. 系统逐一查询各个硬件模块的状态并显示

## 备选路径

1. 无

## 失败路径

1. 某硬件不可用时显示为未就绪，不影响其他硬件检查
2. 某个硬件模块无响应，显示未就绪

## 证据

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
- 恢复或更新候选用例图「查看硬件状态」。

<!-- praxis:use-case-diagram:end -->
