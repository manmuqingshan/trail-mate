# Activity Diagram：业务流程：配置无线电参数

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
父级 Use Case：use-case:configure-radio-parameters
业务边界路径：Trail Mate 系统 / 聊天通信
父级文档：docs/design/use-case-diagrams/configure-radio-parameters.md
Markdown 路径：docs/design/use-case-diagrams/configure-radio-parameters/activity.md
HTML 路径：docs/design/use-case-diagrams/configure-radio-parameters/activity.html

## 业务流程目标

无线电参数自定义

## 流程边界

从设置到应用

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

主成功场景

- mainSuccessScenario

## 决策点与分支

- pickMode -->|LoRa| loraConf
- pickMode -->|FSK| fskConf

## 失败 / 补偿路径

- 无

## 流程业务规则

策略模式

## Activity UML 读图说明

分支选择

## 实现范围锚点

- 模块：boards
- 入口：configureLoraRadio
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
  startNode([开始])
  pickMode[选择调制模式]
  loraConf[配置 LoRa 参数]
  fskConf[配置 FSK 参数]
  applyPower[应用发射功率]
  testTX[测试发射]
  endNode([结束])
  startNode --> pickMode
  pickMode -->|LoRa| loraConf
  pickMode -->|FSK| fskConf
  loraConf --> applyPower
  fskConf --> applyPower
  applyPower --> testTX
  testTX --> endNode
```

## 证据上下文

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
- 恢复或更新「配置无线电参数」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
