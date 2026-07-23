# Use Case：加载路线并判断偏航与恢复

状态：**candidate；行为存在，领域 owner 未形成**
业务边界：地图、定位与现场感知

## 用户目标

从本地加载一条路线，看到当前位置相对路线的进展，在偏离时收到稳定告警，回到路线后自动恢复，且定位跳变不会造成告警抖动。

## 当前实际行为

1. Route storage 加载 route points。
2. GPS page runtime 对当前位置与每个 route segment 计算最近距离。
3. `update_route_deviation_state` 使用 enter/exit 双阈值形成迟滞，避免临界点反复切换。
4. UI 投影 on-route/deviated 和距离；路线文件或 fix 不可用时停止判断。

## 设计缺口

`Route / NavigationSession / RouteProgress / DeviationPolicy` 没有核心 owner；路线进度、目标点推进、重入、路线版本变化和告警节流没有统一模型。因此本文确认用户目标和现有算法，不把完整导航生命周期标成 confirmed。

源码：`modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp`、`modules/core_sys/include/platform/ui/route_storage.h`。

## 下钻

- [Activity](follow-route/activity.md)
- [Sequence](follow-route/sequences/sequence-follow-route.md)
