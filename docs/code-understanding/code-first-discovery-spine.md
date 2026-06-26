# Code-First Discovery Spine
生成于：2026-06-25T14:17:52.885Z
事实来源：code_facts / codegraph
本地仓库证据：2026-06-25T09:13:57.407Z
## 定位
这份 spine 是 Design / Engineering / Architecture 三个 Explorer 的共享代码事实脊柱。它不是需求文档，也不是产品意图；它只描述当前代码事实中可观察的入口、结构、运行边界、证据断言和覆盖缺口。
## 摘要
- 文件：305
- 代码节点：4091
- 代码关系：10149
- 行为切片：420
- 结构聚类：17
- 运行/构建边界：0
- 证据断言：177
- 覆盖账本项：14545
- 未知缺口：2062
## 行为切片
| ID | 触发 | 入口 | 模块 | 文件 | 关系 | 置信度 |
| --- | --- | --- | --- | ---: | ---: | --- |
| behavior-slice:api-route-codegraph-method-64e00b9e34a8c17deba799357b3d8dfa | api_route | IdfAppFacadeRuntime::getTeamController | apps/esp32_lvgl | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-ff1693e9463e593245dc219bc001d332 | ui_route | makeChatPageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-27d0bf7aad63c554f6e83d1eb43f6244 | ui_route | refreshDataPage | apps/linux_uconsole_gtk | 2 | 27 | high |
| behavior-slice:ui-route-codegraph-function-537edf895d25fceddf733ab6ed611047 | ui_route | refreshHardwarePage | apps/linux_uconsole_gtk | 2 | 13 | high |
| behavior-slice:ui-route-codegraph-function-4a858539c72f0272c7d20353824bdc64 | ui_route | makeHardwarePageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-eff7442c19c5f3aadbffd864930c85e3 | ui_route | refreshLogsPage | apps/linux_uconsole_gtk | 2 | 9 | high |
| behavior-slice:ui-route-codegraph-function-5012e49a183e80a2f8940a8ca6469262 | ui_route | makeMapPageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-351797dda2e803d423fb1950bd7fca4c | ui_route | makeOverviewPageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-26e98b9a136cf454acaf45547c16e72a | ui_route | refreshSettingsPage | apps/linux_uconsole_gtk | 2 | 7 | high |
| behavior-slice:ui-route-codegraph-function-991cd1fd6dc709193e7a7db23c635fcb | ui_route | showSettingsPage | apps/linux_uconsole_gtk | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-848f0c88cbd268d7f19882a555165c81 | ui_route | showPage | apps/linux_uconsole_gtk | 2 | 27 | high |
| behavior-slice:api-route-codegraph-function-16f36cb0f809018e5bbfdab6ab4afb81 | api_route | AppFacadeRuntime::getTeamController | apps/nrf52_node | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-b4895dedcd03b2a66b2563a5eb513b2b | ui_route | thread | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-function-f0bd41ddb9dbf231b4124293ad021da6 | ui_route | wait_until_capture_ready | apps/linux_cardputer_zero | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-ec89be1f3d23bfaef2aea96f1a9bbff9 | ui_route | open_focused_app | apps/linux_cardputer_zero | 1 | 9 | high |
| behavior-slice:ui-route-codegraph-method-7c4070813c06e3561ce9dd81a40cb89b | ui_route | TLoRaPagerBoard::isHardwareOnline | boards | 1 | 19 | high |
| behavior-slice:ui-route-codegraph-method-9511b3acf19571a94eb3cec1a2f41fcc | ui_route | TLoRaPagerBoard::isGPSReady | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-method-8be2ffe9134de83dccb8cbfd53e0fa4f | ui_route | TLoRaPagerBoard::hasGPSHardware | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-method-a3a9766eaebc40192b484b0ea0fafc52 | ui_route | TLoRaPagerBoard::hasSstvAudioInput | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-method-864ba7b8b3cd5a0414802ba25409a3f2 | ui_route | TLoRaPagerBoard::isSensorReady | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-method-72b332f2d9877c1b0dc2ea5337bfb103 | ui_route | TLoRaPagerBoard::isSDReady | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-method-6ff7ccfd5f7d5bed424d8cd0d9493726 | ui_route | TLoRaPagerBoard::isRTCReady | boards | 2 | 9 | high |
| behavior-slice:ui-route-codegraph-method-eed6523b0cd1970d87b89f601d2aaa2c | ui_route | TLoRaPagerBoard::isHapticReady | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-method-faeda81b79606182bc47e59e4d2258b9 | ui_route | TLoRaPagerBoard::isPMUReady | boards | 2 | 7 | high |
| behavior-slice:ui-route-codegraph-method-e8dfd51fbc3c850c1e4d3a7fc4231436 | ui_route | TLoRaPagerBoard::isGaugeReady | boards | 2 | 7 | high |
| behavior-slice:ui-route-codegraph-function-c6e025f57e17433d5f6a8587b35b348d | ui_route | TLoRaPagerBoard::ensureSDReady | boards | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-9c6cb5f9b48c98912fe45d08cdf7baf1 | ui_route | TLoRaPagerBoard::isCardReady | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-d0ee2fed3865c94500bf978be388785a | ui_route | TLoRaPagerBoard::playMessageTone | boards | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-struct-9594b1a57241ad1df7eb3a3f6b575f44 | ui_route | TLoRaPagerBoard::playMessageTone::ToneStep | boards | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-function-c7243f3864e523639290c7c5fe308ffd | ui_route | TLoRaPagerBoard::setMessageToneVolume | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-9af278c3635bd9cfcd5634d3f96e791c | ui_route | TLoRaPagerBoard::getMessageToneVolume | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-42551bf3d6eaa52e7c0833e50e947f0d | ui_route | TLoRaPagerBoard::startRadioReceive | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-eb13294de5dd0a53322edbb177c5a744 | ui_route | TLoRaPagerBoard::readRadioData | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-7b4b87cfce61c9bff6d498b8dd6ca305 | ui_route | TLoRaPagerBoard::configureFskRadio | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-d9fef41c34ce43002aa6a4b172ef8c53 | ui_route | TLoRaPagerBoard::configureLoraRadio | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-63e4496f3bd5c13c98b0764dbebc100c | ui_route | TLoRaPagerBoard::getBatteryLevel | boards | 2 | 5 | high |
| behavior-slice:ui-route-codegraph-function-6705e41ce815947778de3572561da9ac | ui_route | TLoRaPagerBoard::getBatteryTemperatureC_bestEffort | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-a63b358228a40b712892c3d4c4eceb4e | ui_route | TLoRaPagerBoard::reloadGaugeCapacityFromPrefs | boards | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-805a3f11773a33c7c904b78ad9f5bf22 | ui_route | TLoRaPagerBoard::readADC | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-0004c59538ca9d1465ca04b58f773463 | ui_route | TLoRaPagerBoard::readADCVoltage | boards | 1 | 1 | medium |
| behavior-slice:ui-route-code-import-c-c-thread | ui_route | thread | apps/linux_cardputer_zero | 2 | 1 | medium |
| behavior-slice:ui-route-codegraph-variable-77598514febdf2dcaa2ba293f7a7bcff | ui_route | rotaryHandler | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-struct-8c220a0e73da5e58f5f019863c25c5a4 | ui_route | GtkUConsolePageLifecycle | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-3f6ca9d1d824dcb621dc816c98eacf89 | ui_route | makeDataPageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-538a0b96ee27455d2edc422fd50a6935 | ui_route | makeLogsPageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-1efc4e4e325ff94fbf42d1ee48d8d412 | ui_route | setSettingsStackPageVisible | apps/linux_uconsole_gtk | 1 | 7 | high |
| behavior-slice:ui-route-codegraph-function-7cb2c660fbfc61e1888e9d1547af3518 | ui_route | makeSettingsPageLifecycle | apps/linux_uconsole_gtk | 2 | 3 | high |
| behavior-slice:ui-route-codegraph-function-c6c0b6fa6480ba0986efd3c78a59b528 | ui_route | findPage | apps/linux_uconsole_gtk | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-03689f593fc68626d03443565a4b6e98 | ui_route | findPage | apps/linux_uconsole_gtk | 1 | 5 | high |
| behavior-slice:api-route-codegraph-function-c5106b68dff879d110dc0dda50f17813 | api_route | reset_touch_controller | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-4c29e5bbcc1dfd3fd9ed6406115e8068 | ui_route | initializeBoard | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-d2ce39f73cb79c5c5c3237578d0f3aaa | ui_route | initializeDisplay | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-29f6ff810e9d97dddd19e7e9dabbcfcf | ui_route | initializeStorage | boards | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-ec1987507f515b7f553a4486f5c6939a | ui_route | tryResolveAppContextInitHandles | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-f650bc9e2a6e7f858bc3f06eef107063 | ui_route | resolveAppContextInitHandles | boards | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-import-52683ac846286efe5d93dd9a3ab70023 | ui_route | chrono | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-import-7069db658a7ee0bb54513713a043e6a6 | ui_route | cstdio | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-import-72bdbfb3d5dfede2eef162d82ae74753 | ui_route | cstdlib | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-import-01cb1267f5ff78d3807b0befda2f4138 | ui_route | filesystem | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-import-fc75dbca2adffc8c643d5c844e1319f9 | ui_route | fstream | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-import-dda7fd77ad949bbd4e8c93cef335153c | ui_route | stdexcept | apps/linux_cardputer_zero | 1 | 2 | high |
| behavior-slice:ui-route-codegraph-struct-4726429d14c84a62c95d9c1e5e43bd62 | ui_route | CaptureTarget | apps/linux_cardputer_zero | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-function-dd11541a2e32021b05c89e4a88ce9b3e | ui_route | tick_for | apps/linux_cardputer_zero | 1 | 9 | high |
| behavior-slice:ui-route-codegraph-function-59a375abaebde59dd26bf563e20b3b79 | ui_route | render_now | apps/linux_cardputer_zero | 1 | 5 | high |
| behavior-slice:ui-route-codegraph-function-7865285c77463a36ffa03dde06ae79fd | ui_route | press | apps/linux_cardputer_zero | 1 | 21 | high |
| behavior-slice:ui-route-codegraph-function-d3fbdc3ca2493901b5ea5bcd2c36fea8 | ui_route | press_char | apps/linux_cardputer_zero | 1 | 13 | high |
| behavior-slice:ui-route-codegraph-function-8dbcec45a8635f96625b737b56fedb46 | ui_route | press_action | apps/linux_cardputer_zero | 1 | 33 | high |
| behavior-slice:ui-route-codegraph-function-453843ec7b86091efe0e4d653345cac2 | ui_route | split_actions | apps/linux_cardputer_zero | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-15b7c73131f63bc099164713691f82e3 | ui_route | save_png | apps/linux_cardputer_zero | 1 | 3 | high |
| behavior-slice:ui-route-codegraph-function-008950cc7ef355aa40b7446d8c491cbe | ui_route | capture_current | apps/linux_cardputer_zero | 1 | 5 | high |
| behavior-slice:ui-route-codegraph-function-3dd044711e309137b6c9d808e44f9c52 | ui_route | return_to_menu | apps/linux_cardputer_zero | 1 | 5 | high |
| behavior-slice:ui-route-codegraph-function-1dea722d901f4620e78d99857585cedd | ui_route | focus_next_menu_item | apps/linux_cardputer_zero | 1 | 7 | high |
| behavior-slice:ui-route-codegraph-function-bfab17678cf839beb8503dc5870cdc5e | ui_route | main | apps/linux_cardputer_zero | 1 | 13 | high |
| behavior-slice:ui-route-codegraph-import-cfd704e72f6ff9921f193b4037763446 | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-0e4a40b4e89c9cb2be4d288ca77dc2fb | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-5fe9f3dbfb09e1357f4ec34dc3ae5fbf | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-894396a4f30cbcc96f73cc1ffa66a528 | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-f5b411fd332a76c52fb01c7525623c3e | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-0f31bedbedce00fa7cc1906ff63d0d47 | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| behavior-slice:ui-route-codegraph-import-1b40d86a4b19619de848ff55c105434a | ui_route | platform/gtk/gtk_uconsole_pages.h | apps/linux_uconsole_gtk | 1 | 1 | medium |
| _截断_ | 还剩 340 个行为切片 | | | | | |
## 结构聚类
| ID | 模块 | 文件 | 节点 | 关系 | 行为切片 | 外部依赖 | 置信度 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| structural-cluster:boards | boards | 92 | 240 | 240 | 236 | 6 | high |
| structural-cluster:apps-linux-uconsole-gtk | apps/linux_uconsole_gtk | 53 | 240 | 240 | 61 | 3 | high |
| structural-cluster:apps-esp32-lvgl | apps/esp32_lvgl | 31 | 240 | 240 | 41 | 2 | high |
| structural-cluster:firmware | firmware | 33 | 240 | 240 | 1 | 3 | high |
| structural-cluster:apps-nrf52-node | apps/nrf52_node | 23 | 240 | 240 | 42 | 4 | high |
| structural-cluster:apps-linux-cardputer-zero | apps/linux_cardputer_zero | 23 | 229 | 240 | 32 | 2 | high |
| structural-cluster:apps-linux-sim-shell | apps/linux_sim_shell | 15 | 102 | 138 | 7 | 1 | high |
| structural-cluster:builds | builds | 25 | 37 | 22 | 0 | 2 | high |
| structural-cluster:cmake | cmake | 2 | 2 | 0 | 0 | 0 | low |
| structural-cluster:agents-md | AGENTS.md | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:apps-readme-md | apps/README.md | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:changelog-md | CHANGELOG.md | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:claude-md | CLAUDE.md | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:cmakelists-txt | CMakeLists.txt | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:compile-commands-json | compile_commands.json | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:copyright | COPYRIGHT | 1 | 1 | 0 | 0 | 0 | low |
| structural-cluster:license | LICENSE | 1 | 1 | 0 | 0 | 0 | low |
## 运行与构建边界
| ID | 类型 | 文件 | 模块 | 置信度 |
| --- | --- | --- | --- | --- |
## 覆盖账本摘要
- classified_structural_cluster: 3023
- test_only: 275
- classified_entrypoint: 420
- unknown_gap: 2062
- internal_detail: 8765
## 未知缺口样本
| 类型 | 目标 | 原因 |
| --- | --- | --- |
| symbol | & status() | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：& status() |
| symbol | esp32_lvgl_runtime_config.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_runtime_config.h |
| symbol | RuntimeStatus | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：RuntimeStatus |
| symbol | esp32_lvgl_loop_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_loop_runtime.h |
| symbol | esp32_lvgl_idf_app_runtime_access.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_idf_app_runtime_access.h |
| symbol | esp_log.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp_log.h |
| symbol | esp_timer.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp_timer.h |
| symbol | freertos/FreeRTOS.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：freertos/FreeRTOS.h |
| symbol | freertos/task.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：freertos/task.h |
| symbol | platform/esp/idf_common/wireless_companion/c6_companion.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/esp/idf_common/wireless_companion/c6_companion.h |
| symbol | ui/loop_shell.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：ui/loop_shell.h |
| symbol | LoopState | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：LoopState |
| symbol | loopTask | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：loopTask |
| symbol | canStartEsp32LvglLoopRuntime | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：canStartEsp32LvglLoopRuntime |
| symbol | startEsp32LvglLoopRuntime | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：startEsp32LvglLoopRuntime |
| symbol | esp32_lvgl_runtime_config.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_runtime_config.h |
| symbol | esp32_lvgl_runtime_config.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_runtime_config.h |
| symbol | product_composition/target_profile.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：product_composition/target_profile.h |
| symbol | sdkconfig.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：sdkconfig.h |
| symbol | & esp32LvglRuntimeConfig() | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：& esp32LvglRuntimeConfig() |
| symbol | esp32LvglRuntimeTargetProfile | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32LvglRuntimeTargetProfile |
| symbol | hasEsp32LvglRuntimeTargetProfile | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：hasEsp32LvglRuntimeTargetProfile |
| symbol | cstdint | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：cstdint |
| symbol | Esp32LvglRuntimeConfig | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：Esp32LvglRuntimeConfig |
| symbol | esp32_lvgl_startup_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_startup_runtime.h |
| symbol | esp32_lvgl_idf_app_runtime_access.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_idf_app_runtime_access.h |
| symbol | esp32_lvgl_loop_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_loop_runtime.h |
| symbol | app/app_config.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：app/app_config.h |
| symbol | app/app_facade_access.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：app/app_facade_access.h |
| symbol | board/BoardBase.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：board/BoardBase.h |
| symbol | esp_log.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp_log.h |
| symbol | platform/esp/boards/board_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/esp/boards/board_runtime.h |
| symbol | platform/esp/idf_common/bsp_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/esp/idf_common/bsp_runtime.h |
| symbol | platform/esp/idf_common/debug/sd_coredump_export.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/esp/idf_common/debug/sd_coredump_export.h |
| symbol | platform/esp/idf_common/startup_support.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/esp/idf_common/startup_support.h |
| symbol | platform/esp/idf_common/wireless_companion/c6_companion.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/esp/idf_common/wireless_companion/c6_companion.h |
| symbol | platform/ui/device_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/ui/device_runtime.h |
| symbol | platform/ui/gps_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/ui/gps_runtime.h |
| symbol | platform/ui/settings_store.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/ui/settings_store.h |
| symbol | ui/app_registry.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：ui/app_registry.h |
| symbol | ui/app_runtime.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：ui/app_runtime.h |
| symbol | ui/startup_shell.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：ui/startup_shell.h |
| symbol | lockUi | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：lockUi |
| symbol | unlockUi | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：unlockUi |
| symbol | applyPlatformRuntimeConfig | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：applyPlatformRuntimeConfig |
| symbol | buildShellHooks | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：buildShellHooks |
| symbol | setBootLog | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：setBootLog |
| symbol | trail_mate_idf_note_user_activity | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：trail_mate_idf_note_user_activity |
| symbol | canRunEsp32LvglStartupRuntime | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：canRunEsp32LvglStartupRuntime |
| symbol | runEsp32LvglStartupRuntime | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：runEsp32LvglStartupRuntime |
| symbol | esp32_lvgl_runtime_config.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：esp32_lvgl_runtime_config.h |
| symbol | refreshChat | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：refreshChat |
| symbol | refreshChatLogic | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：refreshChatLogic |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | launchDataLayout | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：launchDataLayout |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | string | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：string |
| symbol | refreshDataLogic | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：refreshDataLogic |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | launchHardwareLayout | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：launchHardwareLayout |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | cstddef | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：cstddef |
| symbol | buildHardwareCard | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：buildHardwareCard |
| symbol | refreshHardwareLogic | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：refreshHardwareLogic |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | launchLogsLayout | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：launchLogsLayout |
| symbol | platform/gtk/gtk_uconsole_shell.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_shell.h |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | ctime | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：ctime |
| symbol | string | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：string |
| symbol | formatLogTimestamp | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：formatLogTimestamp |
| symbol | onLogsSourceGpsClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onLogsSourceGpsClicked |
| symbol | onLogsSourceLoraClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onLogsSourceLoraClicked |
| symbol | onLogsSourceMqttClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onLogsSourceMqttClicked |
| symbol | buildPacketLogEntry | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：buildPacketLogEntry |
| symbol | refreshLogsLogic | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：refreshLogsLogic |
| symbol | platform/gtk/gtk_uconsole_layout_spec.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_layout_spec.h |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | buildMapSourceButton | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：buildMapSourceButton |
| symbol | buildMapContextPopover | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：buildMapContextPopover |
| symbol | makeMapRailViewport | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：makeMapRailViewport |
| symbol | launchMapLayout | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：launchMapLayout |
| symbol | platform/gtk/gtk_uconsole_layout_spec.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_layout_spec.h |
| symbol | platform/gtk/gtk_uconsole_mqtt_settings.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_mqtt_settings.h |
| symbol | platform/gtk/gtk_uconsole_shell.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_shell.h |
| symbol | platform/gtk/gtk_uconsole_widgets.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：platform/gtk/gtk_uconsole_widgets.h |
| symbol | sys/clock.h | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：sys/clock.h |
| symbol | algorithm | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：algorithm |
| symbol | chrono | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：chrono |
| symbol | cmath | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：cmath |
| symbol | cstdio | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：cstdio |
| symbol | exception | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：exception |
| symbol | future | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：future |
| symbol | sstream | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：sstream |
| symbol | string | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：string |
| symbol | utility | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：utility |
| symbol | vector | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：vector |
| symbol | tileKey | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：tileKey |
| symbol | mapFetchRetryDelaySeconds | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：mapFetchRetryDelaySeconds |
| symbol | mapGridSignature | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：mapGridSignature |
| symbol | missingContourTiles | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：missingContourTiles |
| symbol | onMapSourceClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapSourceClicked |
| symbol | onMapZoomInClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapZoomInClicked |
| symbol | onMapZoomOutClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapZoomOutClicked |
| symbol | onMapRecenterClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapRecenterClicked |
| symbol | formatMapCoordinate | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：formatMapCoordinate |
| symbol | formatMapDecimal | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：formatMapDecimal |
| symbol | mapDegToRad | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：mapDegToRad |
| symbol | mapDistanceMeters | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：mapDistanceMeters |
| symbol | formatMapDistance | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：formatMapDistance |
| symbol | formatMapNodeAge | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：formatMapNodeAge |
| symbol | updateMapMeasureStatus | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：updateMapMeasureStatus |
| symbol | setMapMeasurePoint | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：setMapMeasurePoint |
| symbol | refreshAfterMapContextAction | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：refreshAfterMapContextAction |
| symbol | onMapContextCenterClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapContextCenterClicked |
| symbol | onMapContextZoomInClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapContextZoomInClicked |
| symbol | onMapContextZoomOutClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapContextZoomOutClicked |
| symbol | onMapContextMeasureStartClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapContextMeasureStartClicked |
| symbol | onMapContextMeasureEndClicked | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：onMapContextMeasureEndClicked |
| symbol | coordinateAtPointer | 该代码节点尚未被 Design/Engineering/Architecture 投影解释：coordinateAtPointer |
## 机器模型
完整 JSON：docs/code-understanding/code-first-discovery-spine.json