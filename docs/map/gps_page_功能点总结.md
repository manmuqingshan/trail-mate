# GPS / Map 页面功能点总结（当前代码结构与职责）

这份文档只做“功能点与代码位置”的对照。ESP 侧旧的
`platform/esp/arduino_common/src/ui/screens/gps/*` GPS map 页面实现已经删除；
当前地图页不再直接操纵 `GPSPageState`、`TileContext`、`MapTile` 或
`update_map_tiles()`。

## 1) 页面入口

- 页面 shell：`modules/ui_shared/src/ui/screens/gps/gps_page_shell.cpp`
- 页面 runtime：`modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp`
- app 注册入口：`modules/ui_shared/src/ui/app_catalog_builder.cpp`

`gps::ui::shell` 只负责进入/退出页面；真正的页面对象树、按键处理、刷新节奏与
地图模型同步都在 shared `gps_page_runtime.cpp` 内完成。

## 2) 地图视口

- 共享视口组件：`modules/ui_shared/src/ui/widgets/map/map_viewport.cpp`
- 共享视口 API：`modules/ui_shared/include/ui/widgets/map/map_viewport.h`

GPS 页面通过 `ui::widgets::map::Runtime` 创建地图视口，并用
`ui::widgets::map::Model` 输入焦点、zoom、pan、底图源、等高线开关和坐标系。
页面不再直接持有 tile record vector、decoded image cache、tile path helper 或
LVGL tile image 数组。

## 3) ESP 瓦片后端

- ESP 后端实现：`platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp`
- ESP 后端接口：`platform/esp/arduino_common/include/ui/widgets/map/map_tiles.h`
- 共享 tile runtime：`modules/ui_map_runtime/include/ui_map_runtime/map_tiles/*`

ESP 侧只保留一份 platform map tile backend。它负责：

- SD/FATFS 路径解析与读取适配
- LVGL image/tile 对象创建
- decoded PNG cache 生命周期
- base layer 与 contour overlay 的实际渲染

页面层与 Node Info 都经由 shared `map_viewport` 调用这份后端。

## 4) 地图工作区模型

- runtime source/sink：`modules/ui_shared/src/ui/presentation_sources/runtime_map_workspace_source.cpp`
- presentation model：`modules/ui_presentation/include/ui_presentation/map/map_workspace_model.h`
- overlay snapshot：`ui_presentation/map/map_overlay_snapshot.h`

GPS map 页面用 `MapWorkspaceModel` 同步 viewport、layer、当前自身位置和 team overlay。
图层切换、等高线开关、缺图提示统一通过 `map_viewport` 的 layer API 处理。

## 5) 输入与交互

GPS map 的按键/按钮处理在 shared `gps_page_runtime.cpp` 内：

- `W/A/S/D` 或方向键：平移地图
- `+/-`：缩放
- `P` / `Pos`：回到自身位置
- `L`：切换底图
- `O` / `Contour`：切换等高线
- `F1`：显示帮助

输入只改变 shared map model / viewport model；不再直接重建瓦片或操作底层 tile 对象。

## 6) 路线、轨迹和 team overlay

- Tracker 页面仍负责 route/track 文件入口与配置写入：
  `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp`
- GPS map 页面显示 route/team 上下文按钮和 overlay summary。
- 旧的 ESP GPS route/tracker overlay 绘制文件已删除，地图语义 overlay 走
  `MapOverlaySnapshot` / `map_viewport`。

## 7) 最容易被改坏的边界

1. 页面层不要重新引入 `TileContext`、`MapTile`、`GPSPageState` 或 tile path helper。
2. ESP 平台层只允许保留 `map_tiles.cpp` 这一份瓦片后端。
3. Linux/uConsole GTK 与 CardputerZero 可以有产品差异，但 ESP Arduino 侧不应再出现第二套 GPS map 页面实现。
4. 地图缺图、图层切换、等高线开关要通过 shared `map_viewport` API，不要在页面里私自碰文件系统。
