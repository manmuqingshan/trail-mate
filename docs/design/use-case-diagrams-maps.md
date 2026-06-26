# 用例图地图

<!-- praxis:use-case-diagrams-maps:start -->

## 元数据

项目版本：0.1.30-alpha
设计文档版本：0.1.30-alpha
Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty
Agent 版本决策：none - 本次渲染没有单独的 agent 版本决策
更新于：2026-06-25T14:17:55.307Z
来源：Agent 候选分析

## 版本策略

本项目的设计变更使用 agent 控制的语义化版本。

- agent 在识别真实需求或设计变更后决定版本 bump。
- 用户确认业务语义、兼容性和风险，但不手工选择版本号。
- 每一次版本变化都应对应一个边界清晰的原子化 git commit。
- MAJOR：参与者边界、系统边界、核心故事职责、公开 API 或数据契约发生不兼容变化。
- MINOR：向后兼容地新增用例、参与者、外部系统、流程或设计能力。
- PATCH：向后兼容的问题修复、澄清、证据补充、图布局或非行为性文档修正。
- NONE：纯讨论、被拒绝的输入，或不会写入持久项目/设计/代码/记忆的操作。

## 业务模块边界

- Trail Mate 系统（系统边界）：管理整个应用的用户目标和业务能力
  - 设备管理（业务模块）：提供设备状态的可见性和用户可控的硬件管理
  - 聊天通信（业务模块）：管理用户间的消息传递和无线电链路配置
  - 地图导航（业务模块）：提供基于地图的位置可视化和导航
  - 团队协作（业务模块）：支持多个用户组成团队并共享实时位置
  - 诊断工具（业务模块）：提供系统诊断和现场数据采集工具

## 用例图索引

| ID | 用例图文档 | 语义 HTML | 下钻 UML | 业务边界路径 | 状态 | 置信度 | 当前版本 | 最近变更 |
| --- | --- | --- | ---: | --- | --- | --- | --- | --- |
| use-case:view-device-overview | [查看设备概览](use-case-diagrams/view-device-overview.md) | [HTML](use-case-diagrams/view-device-overview.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:send-text-message | [发送文本消息](use-case-diagrams/send-text-message.md) | [HTML](use-case-diagrams/send-text-message.html) | 2 | Trail Mate 系统 / 聊天通信 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:view-map-navigate | [查看地图并导航](use-case-diagrams/view-map-navigate.md) | [HTML](use-case-diagrams/view-map-navigate.html) | 2 | Trail Mate 系统 / 地图导航 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:manage-team-sharing | [管理团队共享位置](use-case-diagrams/manage-team-sharing.md) | [HTML](use-case-diagrams/manage-team-sharing.html) | 2 | Trail Mate 系统 / 团队协作 | candidate | low | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:view-data-details | [查看数据详情](use-case-diagrams/view-data-details.md) | [HTML](use-case-diagrams/view-data-details.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:view-hardware-status | [查看硬件状态](use-case-diagrams/view-hardware-status.md) | [HTML](use-case-diagrams/view-hardware-status.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:view-system-logs | [查看系统日志](use-case-diagrams/view-system-logs.md) | [HTML](use-case-diagrams/view-system-logs.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:manage-device-settings | [管理设备设置](use-case-diagrams/manage-device-settings.md) | [HTML](use-case-diagrams/manage-device-settings.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:sync-gps-time | [同步 GPS 时间](use-case-diagrams/sync-gps-time.md) | [HTML](use-case-diagrams/sync-gps-time.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:control-device-notifications | [控制设备通知](use-case-diagrams/control-device-notifications.md) | [HTML](use-case-diagrams/control-device-notifications.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:power-off-device | [关闭设备电源](use-case-diagrams/power-off-device.md) | [HTML](use-case-diagrams/power-off-device.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:manage-sd-card | [管理 SD 卡](use-case-diagrams/manage-sd-card.md) | [HTML](use-case-diagrams/manage-sd-card.html) | 2 | Trail Mate 系统 / 设备管理 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:receive-text-message | [接收文本消息](use-case-diagrams/receive-text-message.md) | [HTML](use-case-diagrams/receive-text-message.html) | 3 | Trail Mate 系统 / 聊天通信 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:configure-radio-parameters | [配置无线电参数](use-case-diagrams/configure-radio-parameters.md) | [HTML](use-case-diagrams/configure-radio-parameters.html) | 2 | Trail Mate 系统 / 聊天通信 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |
| use-case:capture-screenshot | [捕获屏幕截图](use-case-diagrams/capture-screenshot.md) | [HTML](use-case-diagrams/capture-screenshot.html) | 2 | Trail Mate 系统 / 诊断工具 | candidate | medium | 0.1.30-alpha | 2026-06-25T14:17:55.307Z |

<!-- Interaction Model runtime snapshot is stored in the transition cache. This Markdown map stays human-readable and is not a hidden JSON carrier. -->

## 地图变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策
Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

- 更新用例图地图，并链接 15 个候选用例图独立文档。

<!-- praxis:use-case-diagrams-maps:end -->
