# Activity Diagram：业务流程：捕获屏幕截图

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
父级 Use Case：use-case:capture-screenshot
业务边界路径：Trail Mate 系统 / 诊断工具
父级文档：docs/design/use-case-diagrams/capture-screenshot.md
Markdown 路径：docs/design/use-case-diagrams/capture-screenshot/activity.md
HTML 路径：docs/design/use-case-diagrams/capture-screenshot/activity.html

## 业务流程目标

用于创建带截图的设备文档或报告

## 流程边界

从用户启动工具到 PNG 文件保存

## 参与泳道 / 阶段

- 未显式声明泳道；请结合节点标签和实现范围判断业务阶段。

## 主成功路径

屏幕截图获取流程

- mainSuccessScenario[1]
- mainSuccessScenario[2]

## 决策点与分支

- 无

## 失败 / 补偿路径

- 无

## 流程业务规则

独立工具程序，调用系统截图 API

## Activity UML 读图说明

工具模拟按键、稍后截取并保存

## 实现范围锚点

- 模块：apps/linux_cardputer_zero
- 关键文件：apps/linux_cardputer_zero/tools/cardputer_zero_screenshot_capture.cpp
- 代码锚点：apps/linux_cardputer_zero/tools/cardputer_zero_screenshot_capture.cpp#L1
- 不覆盖代码：底层函数调用链
- 不覆盖代码：DTO/Mapper/Repository 细节

## 不覆盖范围

- 屏幕区域选择
- 定时截屏


## Mermaid 图

```mermaid
flowchart TD
  startNode[用户启动截图工具] --> simKeys[模拟按键切换到目标界面]
  simKeys --> wait[等待界面渲染]
  wait --> capture[调用系统截图 API]
  capture --> savePNG[保存为 PNG 文件]
  savePNG --> endNode[截图完成]
```

## 证据上下文

| 来源 | 路径 | 行号 | 强度 | 摘要 |
| --- | --- | --- | --- | --- |
| 本地仓库扫描 | apps/linux_cardputer_zero/tools/cardputer_zero_screenshot_capture.cpp | 1-1 | strong | 截屏工具主函数和辅助函数 |
| 本地仓库扫描 | apps/linux_cardputer_zero/tools/cardputer_zero_screenshot_capture.cpp | 1-1 | strong | 截屏工具实现 |

## 变更记录

### 0.1.30-alpha - 2026-06-25T14:17:55.307Z

变更类型：DISCOVERY
版本决策：none - 本次渲染没有单独的 agent 版本决策


Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

摘要：
- 恢复或更新「捕获屏幕截图」的 Activity Diagram。

<!-- praxis:use-case-diagram:end -->
