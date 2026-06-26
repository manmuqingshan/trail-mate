# Activity Diagram：业务流程：同步 GPS 时间

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
父级 Use Case：use-case:sync-gps-time
业务边界路径：Trail Mate 系统 / 设备管理
父级文档：docs/design/use-case-diagrams/sync-gps-time.md
Markdown 路径：docs/design/use-case-diagrams/sync-gps-time/activity.md
HTML 路径：docs/design/use-case-diagrams/sync-gps-time/activity.html

## 业务流程目标

确保设备时间准确

## 流程边界

从用户触发同步到 RTC 时间更新

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

时间同步的主流程

- mainSuccessScenario[1]
- mainSuccessScenario[2]
- mainSuccessScenario[3]

## 决策点与分支

- startNode[用户点击同步时间] --> checkGPS{是否有 GPS 锁定?}

## 失败 / 补偿路径

- 无

## 流程业务规则

Board 直接操作 RTC 和 GPS 驱动

## Activity UML 读图说明

触发同步，等待 GPS 有效时间，写入 RTC

## 实现范围锚点

- 模块：boards
- 关键文件：boards/tlora_pager/src/tlora_pager_board.cpp
- 代码锚点：boards/tlora_pager/src/tlora_pager_board.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 时间偏移调整
- NTP 后备


## Mermaid 图

```mermaid
flowchart TD
  startNode[用户点击同步时间] --> checkGPS{是否有 GPS 锁定?}
  checkGPS -- 是 --> getTime[从 GPS 读取 UTC 时间]
  getTime --> writeRTC[写入 RTC]
  writeRTC --> successNode[时间已同步]
  checkGPS -- 否 --> showFail[提示 GPS 未锁定]
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 时间同步相关函数实现在 TLoRaPagerBoard 中 |
| 本地仓库扫描 | boards/tlora_pager/src/tlora_pager_board.cpp | 1-1 | strong | 与 RTC 同步相关的方法 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「同步 GPS 时间」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
