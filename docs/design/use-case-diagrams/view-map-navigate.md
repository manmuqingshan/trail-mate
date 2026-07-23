# Use Case：使用离线地图建立现场态势

状态：**confirmed**
业务边界：地图、定位与现场感知

## 用户目标

在没有公网时查看当前位置、离线底图、联系人/附近节点、团队位置、航点、路线和轨迹，并能判断每个叠加对象来自哪里、何时更新、是否陈旧。

## 主场景

1. Map workspace 恢复上次视口；若有可信 fix，可由用户选择居中而不是自动抢走手势视口。
2. Tile source 从本地存储读取当前 zoom/viewport 所需瓦片；缺瓦片仍保留坐标网格和已有叠加层。
3. LocationService、ContactService、Team events、Route/Track storage 分别提供带来源与时间的投影。
4. Map model 把协议节点、联系人、团队成员、航点、路线、轨迹作为不同类型渲染；点击对象进入对应详情，不改变 source owner。
5. 新 revision 到达时增量刷新；过期位置降低可信度，不冒充实时。

## 失败与恢复

- 无 fix：显示 last-known 或 unknown，不生成 `(0,0)`。
- 无瓦片：保留 overlay 和坐标语义。
- SD/storage busy：UI 不阻塞 radio；显示底图/路线/轨迹暂不可用。
- 跨协议 NodeId 不直接合并为同一地图对象。

源码：`modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp`、`platform/linux/uconsole/src/uconsole_map_workspace_model.cpp`、`modules/core_gps/src/usecase/location_service.cpp`。

## 下钻

- [Activity：离线态势组装](view-map-navigate/activity.md)
- [Sequence：数据源到 Map workspace](view-map-navigate/sequences/sequence-view-map-navigate.md)
