# 用例图：查看地图并导航

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
语义 HTML：view-map-navigate.html

## 身份信息

- ID：use-case:view-map-navigate
- 业务边界路径：Trail Mate 系统 / 地图导航
- 当前边界类型：业务模块
- 当前边界职责：提供基于地图的位置可视化和导航
- 状态：candidate
- 置信度：medium
- Markdown 路径：docs/design/use-case-diagrams/view-map-navigate.md
- HTML 路径：docs/design/use-case-diagrams/view-map-navigate.html
- 触发条件：用户从导航栏进入地图页面

## 故事摘要

用户在地图界面上查看自己的当前位置和历史轨迹

## 参与者

- 用户（个人）

## 外部系统

- 无

## UML 下钻地图

- Use Case Diagram：[查看地图并导航](view-map-navigate.html)
  - Activity Diagram：[业务流程：查看地图并导航](view-map-navigate/activity.html)
  - Sequence Diagram：[对象交互：查看地图导航主成功场景](view-map-navigate/sequences/sequence-view-map-navigate-sequence.html)

## Mermaid 用例图

```mermaid
flowchart LR
  actor_actor_user["&laquo;Actor&raquo;<br/>用户"]
  subgraph system_system_trail_mate["Trail Mate 系统"]
    subgraph system_business_module_map_navigation["地图导航"]
      useCase_use_case_view_map_navigate(["查看地图并导航"])
    end
  end
  actor_actor_user --- useCase_use_case_view_map_navigate
```

## 设计指标索引

此章节是 Design Explorer 可解析的指标索引。每个数字都必须能回溯到这里的具体条目，避免 UI 只展示不可解释的计数。

| 指标 | 数量 | 内容边界 |
| --- | ---: | --- |
| 节点 | 4 | 参与者、外部系统、上下文和当前用例。 |
| 关系 | 5 | 当前用例与上下文、参与者、外部系统或其他用例之间的关系。 |
| 证据 | 3 | 支撑当前候选用例的文件、代码证据、规范或推断证据。 |
| 待裁决 | 0 | 仍需产品或业务负责人裁决的外部问题。 |

### 节点

- Trail Mate 系统（业务边界：系统边界） - 管理整个应用的用户目标和业务能力
- 地图导航（业务边界：业务模块） - 提供基于地图的位置可视化和导航
- 查看地图并导航（用例） - 用户在地图界面上查看自己的当前位置和历史轨迹
- 用户（参与者） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具

### 关系

- Trail Mate 系统 包含业务边界「地图导航」（包含关系）
- 地图导航 包含 Use Case「查看地图并导航」（包含关系）
- 用户 作为主要参与者参与「查看地图并导航」（参与关系） - 使用 Trail Mate 设备的最终用户，可以查看状态、收发消息、配置设备、使用诊断工具
- 用户 -> 查看地图并导航（参与关系） - 用户参与导航
- 用户 -> 查看地图并导航（参与关系） - 用户参与查看地图导航

### 证据

- 证据 1 · 本地仓库扫描 · apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp · n/a（FACT:medium） - 存在地图页面逻辑实现
- 证据 2 · 本地仓库扫描 · apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp · 1-1（INFERENCE:medium） - 地图逻辑文件负责地图视图和位置更新

  ```text
  map lifecycle
  ```
- 证据 3 · 本地仓库扫描 · apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp · 1-1（FACT:medium） - 地图页面布局，包含地图显示入口

  ```text
  map_layout.cpp
  ```

### 待裁决

- 无

## 主成功路径

1. 1. 用户打开地图页面
2. 2. 系统获取当前 GPS 坐标
3. 3. 系统加载地图瓦片并渲染
4. 4. 系统显示位置标记和轨迹
5. 用户选择地图导航功能
6. 系统获取当前位置坐标
7. 系统在地图上渲染位置标记和路径
8. 用户打开地图页面
9. 系统加载地图瓦片
10. 系统获取当前 GPS 位置并显示

## 备选路径

1. 无

## 失败路径

1. GPS 未定位时，显示上次已知位置或提示用户等待定位
2. GPS 未锁定，地图显示默认位置

## 证据

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp | _n/a_ | medium | 存在地图页面逻辑实现 |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp | 1-1 | medium | 地图逻辑文件负责地图视图和位置更新 |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp | 1-1 | medium | 地图页面布局，包含地图显示入口 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新候选用例图「查看地图并导航」。

<!-- praxis:use-case-diagram:end -->
