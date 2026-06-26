# Activity Diagram：业务流程：查看硬件状态

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
Markdown 路径：docs/design/use-case-diagrams/view-hardware-status/activity.md
HTML 路径：docs/design/use-case-diagrams/view-hardware-status/activity.html

## 业务流程目标

用户验证设备各个部件的健康状态

## 流程边界

从用户选择硬件页面到显示所有模块状态

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

硬件状态查询流程

- mainSuccessScenario[1]
- mainSuccessScenario[2]

## 决策点与分支

- 无

## 失败 / 补偿路径

- 无

## 流程业务规则

HardwareLayout 遍历硬件清单，调用 Board 的方法检查

## Activity UML 读图说明

循环查询各硬件模块并展示

## 实现范围锚点

- 模块：apps/linux_uconsole_gtk
- 模块：boards
- 关键文件：apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_hardware_layout.cpp
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 动态更新状态
- 故障诊断


## Mermaid 图

```mermaid
flowchart TD
  startNode[用户打开硬件状态] --> checkGPS[检查 GPS 模块]
  checkGPS --> checkRadio[检查无线电]
  checkRadio --> checkSD[检查 SD 卡]
  checkSD --> checkBattery[检查电池]
  checkBattery --> updateDisplay[更新状态显示]
  updateDisplay --> endNode[硬件状态页面显示]
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
- 恢复或更新「查看硬件状态」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
