# Activity Diagram：业务流程：控制设备通知

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
父级 Use Case：use-case:control-device-notifications
业务边界路径：Trail Mate 系统 / 设备管理
父级文档：docs/design/use-case-diagrams/control-device-notifications.md
Markdown 路径：docs/design/use-case-diagrams/control-device-notifications/activity.md
HTML 路径：docs/design/use-case-diagrams/control-device-notifications/activity.html

## 业务流程目标

自定义消息到达时的提醒方式

## 流程边界

从用户修改参数到设置立即生效

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

调整通知参数

- mainSuccessScenario[1]
- mainSuccessScenario[2]

## 决策点与分支

- 无

## 失败 / 补偿路径

- 无

## 流程业务规则

设置值直接应用到硬件

## Activity UML 读图说明

滑块或数值输入直接传递到 Board 设置

## 实现范围锚点

- 模块：boards
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 通知开关
- 测试播放


## Mermaid 图

```mermaid
flowchart TD
  startNode[用户调整通知音量] --> updateVolume[更新音量数值]
  updateVolume --> applyVolume[调用 Board 设置音量]
  applyVolume --> endNode[音量已应用]
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 声音和振动控制函数 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 与通知控制相关的方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「控制设备通知」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
