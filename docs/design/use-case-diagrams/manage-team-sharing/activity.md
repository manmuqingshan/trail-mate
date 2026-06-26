# Activity Diagram：业务流程：管理团队共享

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
父级 Use Case：use-case:manage-team-sharing
业务边界路径：Trail Mate 系统 / 团队协作
父级文档：docs/design/use-case-diagrams/manage-team-sharing.md
Markdown 路径：docs/design/use-case-diagrams/manage-team-sharing/activity.md
HTML 路径：docs/design/use-case-diagrams/manage-team-sharing/activity.html

## 业务流程目标

团队位置共享

## 流程边界

从团队管理到位置共享

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

主成功场景

- mainSuccessScenario

## 决策点与分支

- 无

## 失败 / 补偿路径

- 无

## 流程业务规则

发布-订阅

## Activity UML 读图说明

顺序

## 实现范围锚点

- 模块：apps/linux_uconsole_gtk
- 模块：boards
- 入口：team page
- 关键文件：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_team_logic.cpp
- 代码锚点：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_team_logic.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 完整代码调用链
- 仓库级全局流程
- 未被证据支持的业务分支


## Mermaid 图

```mermaid
flowchart TD
  startNode([开始])
  createOrJoin[创建或加入团队]
  shareLocation[共享自身位置]
  showMembers[在地图上显示成员]
  endNode([结束])
  startNode --> createOrJoin
  createOrJoin --> shareLocation
  shareLocation --> showMembers
  showMembers --> endNode
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp | 524-524 | medium | ESP32 固件中存在 TeamController |
| 本地仓库扫描 | apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_team_logic.cpp | 1-1 | medium | 团队管理逻辑 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「管理团队共享位置」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
