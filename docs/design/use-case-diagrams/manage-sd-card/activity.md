# Activity Diagram：业务流程：管理 SD 卡

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
父级 Use Case：use-case:manage-sd-card
业务边界路径：Trail Mate 系统 / 设备管理
父级文档：docs/design/use-case-diagrams/manage-sd-card.md
Markdown 路径：docs/design/use-case-diagrams/manage-sd-card/activity.md
HTML 路径：docs/design/use-case-diagrams/manage-sd-card/activity.html

## 业务流程目标

管理存储卡以确保数据记录或日志空间

## 流程边界

从用户进入 SD 卡管理到操作完成

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

SD 卡挂载/卸载流程

- mainSuccessScenario[1]
- mainSuccessScenario[2]
- mainSuccessScenario[3]

## 决策点与分支

- checkStatus --> choice{当前状态?}

## 失败 / 补偿路径

- 无

## 流程业务规则

Board 提供 SDCard 相关方法

## Activity UML 读图说明

检查状态后允许挂载或卸载

## 实现范围锚点

- 模块：boards
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- SD 卡格式化
- 文件浏览器


## Mermaid 图

```mermaid
flowchart TD
  startNode[用户打开 SD 卡管理] --> checkStatus[检查 SD 卡状态]
  checkStatus --> choice{当前状态?}
  choice -- 未挂载 --> mount[挂载 SD 卡]
  mount --> endNode[SD 卡已挂载]
  choice -- 已挂载 --> unmount[卸载 SD 卡]
  unmount --> endNode
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | SD 卡管理函数 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | SD 卡操作相关方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「管理 SD 卡」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
