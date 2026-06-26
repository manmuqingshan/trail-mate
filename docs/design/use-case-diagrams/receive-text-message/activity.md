# Activity Diagram：业务流程：接收文本消息

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
父级 Use Case：use-case:receive-text-message
业务边界路径：Trail Mate 系统 / 聊天通信
父级文档：docs/design/use-case-diagrams/receive-text-message.md
Markdown 路径：docs/design/use-case-diagrams/receive-text-message/activity.md
HTML 路径：docs/design/use-case-diagrams/receive-text-message/activity.html

## 业务流程目标

接收外部消息

## 流程边界

从无线电中断到 UI 更新

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

主成功场景

- mainSuccessScenario

## 决策点与分支

- isMsg{是文本消息?}
- isMsg -->|是| display
- isMsg -->|否| discard

## 失败 / 补偿路径

- 无

## 流程业务规则

事件驱动

## Activity UML 读图说明

分支判断

## 实现范围锚点

- 模块：boards
- 模块：apps/linux_uconsole_gtk
- 入口：startRadioReceive
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 完整代码调用链
- 仓库级全局流程
- 未被证据支持的业务分支


## Mermaid 图

```mermaid
flowchart TD
  startNode([开始: 无线电中断])
  read[readRadioData]
  parse[解析数据包]
  isMsg{是文本消息?}
  display[显示在聊天窗口]
  notify[触发通知]
  discard[丢弃]
  endNode([结束])
  startNode --> read
  read --> parse
  parse --> isMsg
  isMsg -->|是| display
  isMsg -->|否| discard
  display --> notify
  notify --> endNode
  discard --> endNode
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 接收消息函数 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 消息接收和无线电解码相关方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「接收文本消息」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
