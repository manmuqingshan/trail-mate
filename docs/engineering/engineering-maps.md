# Engineering Maps

<!-- praxis:engineering-maps:start -->

## 定位

软件结构模型的根索引。这里仅负责导航、聚合和版本时间线；具体 UML 与工程解释必须落在各图种目录和具体 diagram 文档中。

## 元数据

项目版本：0.1.30-alpha
Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty
更新于：2026-06-25T09:19:20.669Z

## 图种索引

| 图种 | 数量 | Maps | 说明 |
| --- | ---: | --- | --- |
| Package Diagrams | 18 | [docs/engineering/package-diagrams/package-diagrams-maps.md](docs/engineering/package-diagrams/package-diagrams-maps.md) | 进入包/模块边界看板，按工程目录查看 18 个模块如何承担入口、契约、适配、共享能力、运行装配或基础设施职责，并继续下钻到组件、结构、sequence、部署节点和技术热点。 |
| Component Diagrams | 22 | [docs/engineering/component-diagrams/component-diagrams-maps.md](docs/engineering/component-diagrams/component-diagrams-maps.md) | 进入组件看板，查看 22 个关键类、函数、React 组件、命令入口或接口对象的职责、被引用/调用关系、对外依赖/调用关系、所属模块和可下钻风险。 |
| Deployment Diagrams | 0 | [docs/engineering/deployment-diagrams/deployment-diagrams-maps.md](docs/engineering/deployment-diagrams/deployment-diagrams-maps.md) | 进入运行/部署节点看板，查看构建配置、桌面壳、CLI、包管理、CI 或本地运行入口如何影响工程的启动、打包和交付路径。 |
| Class / Structural Diagrams | 10 | [docs/engineering/class-structural-diagrams/class-structural-diagrams-maps.md](docs/engineering/class-structural-diagrams/class-structural-diagrams-maps.md) | 进入结构协作看板，按可命名业务/技术语境查看类、接口和值对象如何形成领域模型、策略、端口、适配或共享支撑；它不是按顶层 layer 或关系数量生成的类名列表。 |
| Sequence Diagrams | 18 | [docs/engineering/sequence-diagrams/sequence-diagrams-maps.md](docs/engineering/sequence-diagrams/sequence-diagrams-maps.md) | 进入 Sequence 看板，查看 18 条由本地仓库证据观察到的真实调用片段，用动态协作视角补足静态结构图。静态 import/reference 不会被当作时序图。 |
| State Machine Diagrams | 0 | [docs/engineering/state-machine-diagrams/state-machine-diagrams-maps.md](docs/engineering/state-machine-diagrams/state-machine-diagrams-maps.md) | 进入状态机看板；只有证据表明确实存在关键状态字段、枚举或迁移语义时才会生成，避免为凑 UML 虚构状态机。 |
| Technical Hotspots | 16 | [docs/engineering/technical-hotspots/technical-hotspots-maps.md](docs/engineering/technical-hotspots/technical-hotspots-maps.md) | 进入技术热点看板，查看大文件、被广泛复用对象、外部协作对象、依赖簇或扫描告警为什么会增加阅读、修改、测试和回归成本。 |

## UML 下钻层级规则

### package

- 可下钻图种：component、class_structural、sequence、deployment、technical_hotspot
- 原因：Package Diagram 是技术复杂度的顶层工程边界；它可以下钻到该边界内的关键组件、结构协作、运行链路、运行配置和复杂度热点。

### class_structural

- 可下钻图种：component、sequence、technical_hotspot
- 原因：Class / Structural Diagram 解释一个可命名业务/技术语境中的结构协作；它可以继续下钻到具体组件、动态交互片段和结构风险。

### component

- 可下钻图种：sequence、class_structural、technical_hotspot
- 原因：Component Diagram 是关键技术对象视角；它可以下钻到该对象参与的调用片段、所在结构切片和相关热点。

### sequence

- 可下钻图种：component、class_structural、technical_hotspot
- 原因：Sequence Diagram 是动态协作视角；它可以反向定位参与组件、所在结构边界和可能的运行风险。

### deployment

- 可下钻图种：package、component、technical_hotspot
- 原因：Deployment Diagram 是运行/构建节点视角；它可以下钻到对应包、入口组件和运行配置热点。

### technical_hotspot

- 可下钻图种：package、component、class_structural、sequence
- 原因：Technical Hotspot 是风险视角；它需要反向链接到产生复杂度的边界、组件、结构或调用片段。

## 目录树

### Package Diagrams

- [模块边界：managed_components](docs/engineering/package-diagrams/managed_components/package-diagram.md) - 打开 managed_components 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 4 个 Technical Hotspot。
- [模块边界：build.t_display_p4_tft](docs/engineering/package-diagrams/build-t_display_p4_tft/package-diagram.md) - 打开 build.t_display_p4_tft 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 1 个 Technical Hotspot。
- [模块边界：build.tdisplayp4_tft](docs/engineering/package-diagrams/build-tdisplayp4_tft/package-diagram.md) - 打开 build.tdisplayp4_tft 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 1 个 Technical Hotspot。
- [模块边界：boards](docs/engineering/package-diagrams/boards/package-diagram.md) - 打开 boards 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 2 个 Component Diagram、5 个 Class / Structural Diagram、2 个 Technical Hotspot。
- [模块边界：build.tdisplayp4_amoled](docs/engineering/package-diagrams/build-tdisplayp4_amoled/package-diagram.md) - 打开 build.tdisplayp4_amoled 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 1 个 Technical Hotspot。
- [模块边界：build.c6_companion](docs/engineering/package-diagrams/build-c6_companion/package-diagram.md) - 打开 build.c6_companion 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 1 个 Technical Hotspot。
- [模块边界：apps/linux_uconsole_gtk](docs/engineering/package-diagrams/apps-linux_uconsole_gtk/package-diagram.md) - 打开 apps/linux_uconsole_gtk 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 8 个 Component Diagram、3 个 Technical Hotspot。
- [模块边界：apps/esp32_lvgl](docs/engineering/package-diagrams/apps-esp32_lvgl/package-diagram.md) - 打开 apps/esp32_lvgl 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 3 个 Component Diagram、6 个 Sequence Diagram。
- [模块边界：firmware](docs/engineering/package-diagrams/firmware/package-diagram.md) - 打开 firmware 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 2 个 Component Diagram。
- [模块边界：apps/nrf52_node](docs/engineering/package-diagrams/apps-nrf52_node/package-diagram.md) - 打开 apps/nrf52_node 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：apps/linux_cardputer_zero](docs/engineering/package-diagrams/apps-linux_cardputer_zero/package-diagram.md) - 打开 apps/linux_cardputer_zero 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。 可继续下钻到 4 个 Component Diagram、2 个 Technical Hotspot。
- [模块边界：apps/linux_sim_shell](docs/engineering/package-diagrams/apps-linux_sim_shell/package-diagram.md) - 打开 apps/linux_sim_shell 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：builds](docs/engineering/package-diagrams/builds/package-diagram.md) - 打开 builds 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：images](docs/engineering/package-diagrams/images/package-diagram.md) - 打开 images 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：cmake](docs/engineering/package-diagrams/cmake/package-diagram.md) - 打开 cmake 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：apps/README.md](docs/engineering/package-diagrams/apps-readme-md/package-diagram.md) - 打开 apps/README.md 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：COPYRIGHT](docs/engineering/package-diagrams/copyright/package-diagram.md) - 打开 COPYRIGHT 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。
- [模块边界：LICENSE](docs/engineering/package-diagrams/license/package-diagram.md) - 打开 LICENSE 的包级图，先确认它是什么工程边界，再用文件规模、符号规模、跨模块引用方向和下钻图判断它是入口模块、共享能力、runtime 支撑还是基础设施边界。

### Component Diagrams

- [函数节点：main](docs/engineering/component-diagrams/apps-linux_cardputer_zero-main/component-diagram.md) - 打开 函数节点：main 的组件图，查看它位于 apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 2 个 Technical Hotspot。
- [函数节点：contains](docs/engineering/component-diagrams/apps-linux_cardputer_zero-contains/component-diagram.md) - 打开 函数节点：contains 的组件图，查看它位于 apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 2 个 Technical Hotspot。
- [函数节点：launchSettingsLayout](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-launchsettingslayout/component-diagram.md) - 打开 函数节点：launchSettingsLayout 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：makeLabel](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-makelabel/component-diagram.md) - 打开 函数节点：makeLabel 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_widgets.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：makeSettingsRow](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-makesettingsrow/component-diagram.md) - 打开 函数节点：makeSettingsRow 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：main](docs/engineering/component-diagrams/apps-esp32_lvgl-main/component-diagram.md) - 打开 函数节点：main 的组件图，查看它位于 apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 5 个 Sequence Diagram。
- [函数节点：not_contains](docs/engineering/component-diagrams/apps-linux_cardputer_zero-not_contains/component-diagram.md) - 打开 函数节点：not_contains 的组件图，查看它位于 apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 2 个 Technical Hotspot。
- [服务对象：main](docs/engineering/component-diagrams/firmware-main/component-diagram.md) - 打开 服务对象：main 的组件图，查看它位于 firmware/c6_companion/tests/test_tm_services_functional.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。
- [函数节点：contains](docs/engineering/component-diagrams/apps-esp32_lvgl-contains/component-diagram.md) - 打开 函数节点：contains 的组件图，查看它位于 apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 5 个 Sequence Diagram。
- [函数节点：makeBoardProfile](docs/engineering/component-diagrams/boards-makeboardprofile/component-diagram.md) - 打开 函数节点：makeBoardProfile 的组件图，查看它位于 boards/t_echo_lite/include/boards/t_echo_lite/board_profile.h 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 2 个 Class / Structural Diagram、2 个 Technical Hotspot。
- [函数节点：pinNum](docs/engineering/component-diagrams/boards-pinnum/component-diagram.md) - 打开 函数节点：pinNum 的组件图，查看它位于 boards/t_echo_lite/include/boards/t_echo_lite/board_profile.h 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 2 个 Class / Structural Diagram、2 个 Technical Hotspot。
- [函数节点：refreshUi](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-refreshui/component-diagram.md) - 打开 函数节点：refreshUi 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_shell.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：refreshMap](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-refreshmap/component-diagram.md) - 打开 函数节点：refreshMap 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：read_file](docs/engineering/component-diagrams/apps-linux_cardputer_zero-read_file/component-diagram.md) - 打开 函数节点：read_file 的组件图，查看它位于 apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 2 个 Technical Hotspot。
- [界面组件：tm_services_record_error](docs/engineering/component-diagrams/firmware-tm_services_record_error/component-diagram.md) - 打开 界面组件：tm_services_record_error 的组件图，查看它位于 firmware/c6_companion/components/tm_services/tm_services.c 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。
- [函数节点：main](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-main/component-diagram.md) - 打开 函数节点：main 的组件图，查看它位于 apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：launchMapLayout](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-launchmaplayout/component-diagram.md) - 打开 函数节点：launchMapLayout 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：companion_enter](docs/engineering/component-diagrams/apps-esp32_lvgl-companion_enter/component-diagram.md) - 打开 函数节点：companion_enter 的组件图，查看它位于 apps/esp32_lvgl/src/esp32_lvgl_idf_app_registry.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 5 个 Sequence Diagram。
- [函数节点：expect](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-expect/component-diagram.md) - 打开 函数节点：expect 的组件图，查看它位于 apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：makeSwitch](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-makeswitch/component-diagram.md) - 打开 函数节点：makeSwitch 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：setLabel](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-setlabel/component-diagram.md) - 打开 函数节点：setLabel 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_widgets.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。
- [函数节点：makeSpin](docs/engineering/component-diagrams/apps-linux_uconsole_gtk-makespin/component-diagram.md) - 打开 函数节点：makeSpin 的组件图，查看它位于 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp 的哪类技术对象、承担什么职责、被引用/调用关系和对外依赖/调用关系是否形成变更压力，以及它与结构图、sequence 和热点的关系。 可继续下钻到 3 个 Technical Hotspot。

### Deployment Diagrams

- 当前没有基于证据生成的具体图。

### Class / Structural Diagrams

- [结构协作：结构切片 boards · gat562_mesh_evb_pro/include/boards](docs/engineering/class-structural-diagrams/boards-gat562_mesh_evb_pro-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · gat562_mesh_evb_pro/include/boards，查看 boards/gat562_mesh_evb_pro/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 boards · t_echo_lite/include/boards](docs/engineering/class-structural-diagrams/boards-t_echo_lite-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · t_echo_lite/include/boards，查看 boards/t_echo_lite/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 2 个 Component Diagram、1 个 Technical Hotspot。
- [结构协作：结构切片 boards · tab5/include/boards](docs/engineering/class-structural-diagrams/boards-tab5-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · tab5/include/boards，查看 boards/tab5/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 boards · t_display_p4/include/boards](docs/engineering/class-structural-diagrams/boards-t_display_p4-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · t_display_p4/include/boards，查看 boards/t_display_p4/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 boards · tlora_pager/include/boards](docs/engineering/class-structural-diagrams/boards-tlora_pager-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · tlora_pager/include/boards，查看 boards/tlora_pager/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 boards · tdeck/include/boards](docs/engineering/class-structural-diagrams/boards-tdeck-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · tdeck/include/boards，查看 boards/tdeck/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 boards · tdeck_pro/include/boards](docs/engineering/class-structural-diagrams/boards-tdeck_pro-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · tdeck_pro/include/boards，查看 boards/tdeck_pro/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 boards · twatchs3/include/boards](docs/engineering/class-structural-diagrams/boards-twatchs3-include-boards/class-structural-diagram.md) - 打开 结构协作：结构切片 boards · twatchs3/include/boards，查看 boards/twatchs3/include/boards 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 gat562_mesh_evb_pro · gat562_mesh_evb_pro](docs/engineering/class-structural-diagrams/boards-gat562_mesh_evb_pro/class-structural-diagram.md) - 打开 结构协作：结构切片 gat562_mesh_evb_pro · gat562_mesh_evb_pro，查看 boards/gat562_mesh_evb_pro 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 1 个 Technical Hotspot。
- [结构协作：结构切片 t_echo_lite · t_echo_lite](docs/engineering/class-structural-diagrams/boards-t_echo_lite/class-structural-diagram.md) - 打开 结构协作：结构切片 t_echo_lite · t_echo_lite，查看 boards/t_echo_lite 中类、接口和值对象是否共同解释一个业务能力、共享支撑机制或适配边界；如果不能解释共同语境，这张图就应该拆分或降级。 可继续下钻到 2 个 Component Diagram、1 个 Technical Hotspot。

### Sequence Diagrams

- [动态协作：tick 调用 log_loop_interval](docs/engineering/sequence-diagrams/tick-calls-log_loop_interval/sequence-diagram.md) - 打开 tick 到 log_loop_interval 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：add_status_line 调用 add_label](docs/engineering/sequence-diagrams/add_status_line-calls-add_label/sequence-diagram.md) - 打开 add_status_line 到 add_label 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：add_u32_line 调用 add_label](docs/engineering/sequence-diagrams/add_u32_line-calls-add_label/sequence-diagram.md) - 打开 add_u32_line 到 add_label 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：add_hex_line 调用 add_status_line](docs/engineering/sequence-diagrams/add_hex_line-calls-add_status_line/sequence-diagram.md) - 打开 add_hex_line 到 add_status_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：companion_enter 调用 add_label](docs/engineering/sequence-diagrams/companion_enter-calls-add_label/sequence-diagram.md) - 打开 companion_enter 到 add_label 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：companion_enter 调用 add_status_line](docs/engineering/sequence-diagrams/companion_enter-calls-add_status_line/sequence-diagram.md) - 打开 companion_enter 到 add_status_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：companion_enter 调用 add_u32_line](docs/engineering/sequence-diagrams/companion_enter-calls-add_u32_line/sequence-diagram.md) - 打开 companion_enter 到 add_u32_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：companion_enter 调用 add_hex_line](docs/engineering/sequence-diagrams/companion_enter-calls-add_hex_line/sequence-diagram.md) - 打开 companion_enter 到 add_hex_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。
- [动态协作：startEsp32LvglLoopRuntime 调用 canStartEsp32LvglLoopRuntime](docs/engineering/sequence-diagrams/startesp32lvglloopruntime-calls-canstartesp32lvglloopruntime/sequence-diagram.md) - 打开 startEsp32LvglLoopRuntime 到 canStartEsp32LvglLoopRuntime 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：hasEsp32LvglRuntimeTargetProfile 调用 esp32LvglRuntimeTargetProfile](docs/engineering/sequence-diagrams/hasesp32lvglruntimetargetprofile-calls-esp32lvglruntimetargetprofile/sequence-diagram.md) - 打开 hasEsp32LvglRuntimeTargetProfile 到 esp32LvglRuntimeTargetProfile 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：showBootUi 调用 lockUi](docs/engineering/sequence-diagrams/showbootui-calls-lockui/sequence-diagram.md) - 打开 showBootUi 到 lockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：showBootUi 调用 unlockUi](docs/engineering/sequence-diagrams/showbootui-calls-unlockui/sequence-diagram.md) - 打开 showBootUi 到 unlockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：setBootLog 调用 lockUi](docs/engineering/sequence-diagrams/setbootlog-calls-lockui/sequence-diagram.md) - 打开 setBootLog 到 lockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：setBootLog 调用 unlockUi](docs/engineering/sequence-diagrams/setbootlog-calls-unlockui/sequence-diagram.md) - 打开 setBootLog 到 unlockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：canRunEsp32LvglStartupRuntime 调用 canStartEsp32LvglLoopRuntime](docs/engineering/sequence-diagrams/canrunesp32lvglstartupruntime-calls-canstartesp32lvglloopruntime/sequence-diagram.md) - 打开 canRunEsp32LvglStartupRuntime 到 canStartEsp32LvglLoopRuntime 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：runEsp32LvglStartupRuntime 调用 canRunEsp32LvglStartupRuntime](docs/engineering/sequence-diagrams/runesp32lvglstartupruntime-calls-canrunesp32lvglstartupruntime/sequence-diagram.md) - 打开 runEsp32LvglStartupRuntime 到 canRunEsp32LvglStartupRuntime 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：runEsp32LvglStartupRuntime 调用 showBootUi](docs/engineering/sequence-diagrams/runesp32lvglstartupruntime-calls-showbootui/sequence-diagram.md) - 打开 runEsp32LvglStartupRuntime 到 showBootUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。
- [动态协作：runEsp32LvglStartupRuntime 调用 setBootLog](docs/engineering/sequence-diagrams/runesp32lvglstartupruntime-calls-setbootlog/sequence-diagram.md) - 打开 runEsp32LvglStartupRuntime 到 setBootLog 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。

### State Machine Diagrams

- 当前没有基于证据生成的具体图。

### Technical Hotspots

- [大文件：build.t_display_p4_tft/bootloader/config/kconfig_menus.json 技术热点](docs/engineering/technical-hotspots/large-file--build-t_display_p4_tft-bootloader-config-kconfig_menus-json/technical-hotspot.md) - 打开 大文件：build.t_display_p4_tft/bootloader/config/kconfig_menus.json 技术热点 的热点图，查看 build.t_display_p4_tft/bootloader/config/kconfig_menus.json 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：build.tdisplayp4_amoled/bootloader/config/kconfig_menus.json 技术热点](docs/engineering/technical-hotspots/large-file--build-tdisplayp4_amoled-bootloader-config-kconfig_menus-json/technical-hotspot.md) - 打开 大文件：build.tdisplayp4_amoled/bootloader/config/kconfig_menus.json 技术热点 的热点图，查看 build.tdisplayp4_amoled/bootloader/config/kconfig_menus.json 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：build.tdisplayp4_tft/bootloader/config/kconfig_menus.json 技术热点](docs/engineering/technical-hotspots/large-file--build-tdisplayp4_tft-bootloader-config-kconfig_menus-json/technical-hotspot.md) - 打开 大文件：build.tdisplayp4_tft/bootloader/config/kconfig_menus.json 技术热点 的热点图，查看 build.tdisplayp4_tft/bootloader/config/kconfig_menus.json 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_48.c 技术热点](docs/engineering/technical-hotspots/large-file--managed_components-lvgl__lvgl-src-font-lv_font_montserrat_48-c/technical-hotspot.md) - 打开 大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_48.c 技术热点 的热点图，查看 managed_components/lvgl__lvgl/src/font/lv_font_montserrat_48.c 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：build.c6_companion/bootloader/config/kconfig_menus.json 技术热点](docs/engineering/technical-hotspots/large-file--build-c6_companion-bootloader-config-kconfig_menus-json/technical-hotspot.md) - 打开 大文件：build.c6_companion/bootloader/config/kconfig_menus.json 技术热点 的热点图，查看 build.c6_companion/bootloader/config/kconfig_menus.json 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_46.c 技术热点](docs/engineering/technical-hotspots/large-file--managed_components-lvgl__lvgl-src-font-lv_font_montserrat_46-c/technical-hotspot.md) - 打开 大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_46.c 技术热点 的热点图，查看 managed_components/lvgl__lvgl/src/font/lv_font_montserrat_46.c 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_44.c 技术热点](docs/engineering/technical-hotspots/large-file--managed_components-lvgl__lvgl-src-font-lv_font_montserrat_44-c/technical-hotspot.md) - 打开 大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_44.c 技术热点 的热点图，查看 managed_components/lvgl__lvgl/src/font/lv_font_montserrat_44.c 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_42.c 技术热点](docs/engineering/technical-hotspots/large-file--managed_components-lvgl__lvgl-src-font-lv_font_montserrat_42-c/technical-hotspot.md) - 打开 大文件：managed_components/lvgl__lvgl/src/font/lv_font_montserrat_42.c 技术热点 的热点图，查看 managed_components/lvgl__lvgl/src/font/lv_font_montserrat_42.c 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [承担外部协作的候选对象：main 技术热点](docs/engineering/technical-hotspots/collaboration-pressure--main/technical-hotspot.md) - 打开 承担外部协作的候选对象：main 技术热点 的热点图，查看 apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、4 个 Component Diagram。
- [被广泛复用的候选对象：contains 技术热点](docs/engineering/technical-hotspots/reuse-pressure--contains/technical-hotspot.md) - 打开 被广泛复用的候选对象：contains 技术热点 的热点图，查看 apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、4 个 Component Diagram。
- [承担外部协作的候选对象：launchSettingsLayout 技术热点](docs/engineering/technical-hotspots/collaboration-pressure--launchsettingslayout/technical-hotspot.md) - 打开 承担外部协作的候选对象：launchSettingsLayout 技术热点 的热点图，查看 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、4 个 Component Diagram。
- [被广泛复用的候选对象：makeLabel 技术热点](docs/engineering/technical-hotspots/reuse-pressure--makelabel/technical-hotspot.md) - 打开 被广泛复用的候选对象：makeLabel 技术热点 的热点图，查看 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_widgets.cpp 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、2 个 Component Diagram。
- [被广泛复用的候选对象：../../lv_examples.h 技术热点](docs/engineering/technical-hotspots/reuse-pressure--lv_examples-h/technical-hotspot.md) - 打开 被广泛复用的候选对象：../../lv_examples.h 技术热点 的热点图，查看 managed_components/lvgl__lvgl/examples/layouts/flex/lv_example_flex_1.c 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram。
- [依赖簇：boards 技术热点](docs/engineering/technical-hotspots/dependency-cluster--boards/technical-hotspot.md) - 打开 依赖簇：boards 技术热点 的热点图，查看 boards 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、2 个 Class / Structural Diagram。
- [承担外部协作的候选对象：boards/tlora_pager/src/tlora_pager_board.cpp 技术热点](docs/engineering/technical-hotspots/collaboration-pressure--boards-tlora_pager-src-tlora_pager_board-cpp/technical-hotspot.md) - 打开 承担外部协作的候选对象：boards/tlora_pager/src/tlora_pager_board.cpp 技术热点 的热点图，查看 boards/tlora_pager/src/tlora_pager_board.cpp 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、2 个 Class / Structural Diagram。
- [被广泛复用的候选对象：makeSettingsRow 技术热点](docs/engineering/technical-hotspots/reuse-pressure--makesettingsrow/technical-hotspot.md) - 打开 被广泛复用的候选对象：makeSettingsRow 技术热点 的热点图，查看 apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp 为什么可能抬高阅读、修改、测试或回归成本，并反向定位相关 package、component、structure 或 sequence。 可继续下钻到 1 个 Package Diagram、4 个 Component Diagram。

## 地图变更记录

### 0.1.30-alpha - 2026-06-25T09:19:20.669Z

变更类型：DISCOVERY
Git 分支：main
Git 提交：34aad0bffa2f6450192f655f248a94b6c3cbd767
Git 工作区状态：dirty

- 更新软件结构模型根索引，并拆分 84 个具体工程图文档。

<!-- praxis:engineering-maps:end -->
