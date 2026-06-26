# Activity Diagram：业务流程：查看地图并导航

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
父级 Use Case：use-case:view-map-navigate
业务边界路径：Trail Mate 系统 / 地图导航
父级文档：docs/design/use-case-diagrams/view-map-navigate.md
Markdown 路径：docs/design/use-case-diagrams/view-map-navigate/activity.md
HTML 路径：docs/design/use-case-diagrams/view-map-navigate/activity.html

## 业务流程目标

用户查看自己的位置和地图环境

## 流程边界

从用户打开地图到显示当前位置

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

地图导航主流程

- mainSuccessScenario[1]
- mainSuccessScenario[2]
- mainSuccessScenario[3]

## 决策点与分支

- 无

## 失败 / 补偿路径

- 无

## 流程业务规则

MapLayout 负责加载地图控件并初始化 GPS 监听

## Activity UML 读图说明

流程图，从打开页面到显示位置

## 实现范围锚点

- 模块：apps/linux_uconsole_gtk
- 关键文件：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp
- 代码锚点：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 离线地图缓存
- 路线规划


## Mermaid 图

```mermaid
flowchart TD
  startNode[用户打开地图页面] --> loadTiles[加载地图瓦片]
  loadTiles --> getGPS[获取 GPS 位置]
  getGPS --> displayPos[显示位置标记]
  displayPos --> endNode[地图显示就绪]
```

## 证据上下文

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
- 恢复或更新「查看地图并导航」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
