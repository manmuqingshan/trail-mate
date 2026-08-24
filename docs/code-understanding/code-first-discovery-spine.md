# Code-First Discovery Spine
Generated at: 2026-06-25T14:17:52.885Z
Source of facts: code_facts/codegraph
Local warehouse evidence: 2026-06-25T09:13:57.407Z
## Positioning
This spine It is the shared code de facto spine of the three Explorers Design / Engineering / Architecture. It is not a requirements document, nor is it a product intent; it only describes observable entries, structures, run boundaries, evidence assertions, and coverage gaps in the current code facts.
## Summary
- Files: 305
- Code nodes: 4091
- Code relationships: 10149
- Behavior slices: 420
- Structural clusters: 17
- Run/build boundaries: 0
- Evidence assertion: 177
- Overwritten ledger entry: 14545
- Unknown gap: 2062
## Behavior Slice
| ID | Trigger | Entry | Module | File | Relationship | Confidence |
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
| _Truncation_ | 340 behavioral slices left | | | | | |
## Structural clustering
| ID | Module | File | Node | Relationship | Behavior slice | External dependency | Confidence |
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
## Run and build boundaries
| ID | Type | File | Module | Confidence |
| --- | --- | --- | --- | --- |
## Coverage ledger summary
- classified_structural_cluster: 3023
- test_only: 275
- classified_entrypoint: 420
- unknown_gap: 2062
- internal_detail: 8765
## Unknown gap sample
| Type | Target | Reason |
| --- | --- | --- |
| symbol | & status() | This code node has not been interpreted by the Design/Engineering/Architecture projection: & status() |
| symbol | esp32_lvgl_runtime_config.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_runtime_config.h |
| symbol | RuntimeStatus | This code node has not been interpreted by the Design/Engineering/Architecture projection: RuntimeStatus |
| symbol | esp32_lvgl_loop_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_loop_runtime.h |
| symbol | esp32_lvgl_idf_app_runtime_access.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_idf_app_runtime_access.h |
| symbol | esp_log.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp_log.h |
 | symbol | esp_timer.h | This code node has not yet been interpreted by the Design/Engineering/Architecture projection: esp_timer.h |
| symbol | freertos/FreeRTOS.h | This code node has not yet been interpreted by the Design/Engineering/Architecture projection: Design/Engineering/Architecture projection interpretation: freertos/FreeRTOS.h |
| symbol | freertos/task.h | This code node has not been used yet Design/Engineering/Architecture projection interpretation: freertos/task.h |
| symbol | platform/esp/idf_common/wireless_companion/c6_companion.h | This code node has not been used yet Design/Engineering/Architecture projection interpretation: platform/esp/idf_common/wireless_companion/c6_companion.h |
| symbol | ui/loop_shell.h | This code node has not been interpreted by Design/Engineering/Architecture projection: ui/loop_shell.h |
| symbol | LoopState | This code node has not been interpreted by the Design/Engineering/Architecture projection: LoopState |
| symbol | loopTask | This code node has not been interpreted by the Design/Engineering/Architecture projection: loopTask |
| symbol | canStartEsp32LvglLoopRuntime | This code node has not been interpreted by the Design/Engineering/Architecture projection Projection explanation: canStartEsp32LvglLoopRuntime |
| symbol | startEsp32LvglLoopRuntime | This code node has not been Design/Engineering/Architecture Projection explanation: startEsp32LvglLoopRuntime |
| symbol | esp32_lvgl_runtime_config.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_runtime_config.h |
| symbol | esp32_lvgl_runtime_config.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_runtime_config.h |
| symbol | product_composition/target_profile.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: product_composition/target_profile.h |
| symbol | sdkconfig.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: sdkconfig.h |
| symbol | & esp32LvglRuntimeConfig() | This code node has not been interpreted by the Design/Engineering/Architecture projection: & esp32LvglRuntimeConfig() |
| symbol | esp32LvglRuntimeTargetProfile | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32LvglRuntimeTargetProfile |
| symbol | hasEsp32LvglRuntimeTargetProfile | This code node has not been interpreted by the Design/Engineering/Architecture projection: hasEsp32LvglRuntimeTargetProfile |
| symbol | cstdint | This code node has not been interpreted by the Design/Engineering/Architecture projection: cstdint |
| symbol | Esp32LvglRuntimeConfig | This code node has not been interpreted by the Design/Engineering/Architecture projection: Esp32LvglRuntimeConfig |
| symbol | esp32_lvgl_startup_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_startup_runtime.h |
| symbol | esp32_lvgl_idf_app_runtime_access.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_idf_app_runtime_access.h |
| symbol | esp32_lvgl_loop_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_loop_runtime.h |
| symbol | app/app_config.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: app/app_config.h |
| symbol | app/app_facade_access.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: app/app_facade_access.h |
| symbol | board/BoardBase.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: board/BoardBase.h |
| symbol | esp_log.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp_log.h |
| symbol | platform/esp/boards/board_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/esp/boards/board_runtime.h |
| symbol | platform/esp/idf_common/bsp_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/esp/idf_common/bsp_runtime.h |
| symbol | platform/esp/idf_common/debug/sd_coredump_export.h | This code node has not been interpreted by the Design/Engineering/Architecture Projection interpretation: platform/esp/idf_common/debug/sd_coredump_export.h |
| symbol | platform/esp/idf_common/startup_support.h | This code node has not been interpreted by Design/Engineering/Architecture projection: platform/esp/idf_common/startup_support.h |
| symbol | platform/esp/idf_common/wireless_companion/c6_companion.h | This code node has not been used yet Design/Engineering/Architecture projection interpretation: platform/esp/idf_common/wireless_companion/c6_companion.h |
| symbol | platform/ui/device_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/ui/device_runtime.h |
| symbol | platform/ui/gps_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/ui/gps_runtime.h |
| symbol | platform/ui/settings_store.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/ui/settings_store.h |
| symbol | ui/app_registry.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: ui/app_registry.h |
| symbol | ui/app_runtime.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: ui/app_runtime.h |
| symbol | ui/startup_shell.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: ui/startup_shell.h |
| symbol | lockUi | This code node has not been interpreted by the Design/Engineering/Architecture projection: lockUi |
| symbol | unlockUi | This code node has not been interpreted by the Design/Engineering/Architecture projection Projection interpretation: unlockUi |
| symbol | applyPlatformRuntimeConfig | This code node has not been interpreted by Design/Engineering/Architecture projection: applyPlatformRuntimeConfig |
| symbol | buildShellHooks | This code node has not been interpreted by Design/Engineering/Architecture projection: buildShellHooks |
| symbol | setBootLog | This code node has not been interpreted by the Design/Engineering/Architecture projection: setBootLog |
| symbol | trail_mate_idf_note_user_activity | This code node has not been interpreted by the Design/Engineering/Architecture projection: trail_mate_idf_note_user_activity |
| symbol | canRunEsp32LvglStartupRuntime | This code node has not been interpreted by the Design/Engineering/Architecture projection: canRunEsp32LvglStartupRuntime |
| symbol | runEsp32LvglStartupRuntime | This code node has not been interpreted by the Design/Engineering/Architecture projection: runEsp32LvglStartupRuntime |
| symbol | esp32_lvgl_runtime_config.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: esp32_lvgl_runtime_config.h |
| symbol | refreshChat | This code node has not been interpreted by the Design/Engineering/Architecture projection: refreshChat |
| symbol | refreshChatLogic | This code node has not been interpreted by the Design/Engineering/Architecture projection: refreshChatLogic |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | launchDataLayout | This code node has not been interpreted by the Design/Engineering/Architecture projection: launchDataLayout |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | string | This code node has not been interpreted by the Design/Engineering/Architecture projection: string |
| symbol | refreshDataLogic | This code node has not been interpreted by the Design/Engineering/Architecture projection: refreshDataLogic |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | launchHardwareLayout | This code node has not been interpreted by the Design/Engineering/Architecture projection: launchHardwareLayout |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | cstddef | This code node has not been interpreted by the Design/Engineering/Architecture projection: cstddef |
| symbol | buildHardwareCard | This code node has not been interpreted by Design/Engineering/Architecture Projection explanation: buildHardwareCard |
| symbol | refreshHardwareLogic | This code node has not been Design/Engineering/Architecture Projection explanation: refreshHardwareLogic |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | launchLogsLayout | This code node has not been interpreted by the Design/Engineering/Architecture projection: launchLogsLayout |
| symbol | platform/gtk/gtk_uconsole_shell.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_shell.h |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | ctime | This code node has not been interpreted by the Design/Engineering/Architecture projection: ctime |
| symbol | string | This code node has not been interpreted by the Design/Engineering/Architecture projection: string |
| symbol | formatLogTimestamp | This code node has not been interpreted by the Design/Engineering/Architecture projection: formatLogTimestamp |
| symbol | onLogsSourceGpsClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onLogsSourceGpsClicked |
| symbol | onLogsSourceLoraClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onLogsSourceLoraClicked |
| symbol | onLogsSourceMqttClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onLogsSourceMqttClicked |
| symbol | buildPacketLogEntry | This code node has not been interpreted by the Design/Engineering/Architecture projection: buildPacketLogEntry |
| symbol | refreshLogsLogic | This code node has not been interpreted by the Design/Engineering/Architecture projection: refreshLogsLogic |
| symbol | platform/gtk/gtk_uconsole_layout_spec.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_layout_spec.h |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | buildMapSourceButton | This code node has not been interpreted by the Design/Engineering/Architecture projection: buildMapSourceButton |
| symbol | buildMapContextPopover | This code node has not been interpreted by the Design/Engineering/Architecture projection: buildMapContextPopover |
| symbol | makeMapRailViewport | This code node has not yet been interpreted by the Design/Engineering/Architecture projection: Design/Engineering/Architecture projection interpretation: makeMapRailViewport |
| symbol | launchMapLayout | This code node has not been interpreted by Design/Engineering/Architecture projection: launchMapLayout |
| symbol | platform/gtk/gtk_uconsole_layout_spec.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_layout_spec.h |
| symbol | platform/gtk/gtk_uconsole_mqtt_settings.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_mqtt_settings.h |
| symbol | platform/gtk/gtk_uconsole_shell.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_shell.h |
| symbol | platform/gtk/gtk_uconsole_widgets.h | This code node has not been interpreted by the Design/Engineering/Architecture projection: platform/gtk/gtk_uconsole_widgets.h |
| symbol | sys/clock.h | This code node has not been interpreted by the Design/Engineering/Architecture projection Projection interpretation: sys/clock.h |
| symbol | algorithm | This code node has not been interpreted by the Design/Engineering/Architecture projection: algorithm |
| symbol | chrono | This code node has not been interpreted by the Design/Engineering/Architecture projection: chrono |
| symbol | cmath | This code node has not been interpreted by the Design/Engineering/Architecture projection: cmath |
| symbol | cstdio | This code node has not been interpreted by the Design/Engineering/Architecture projection: cstdio |
| symbol | exception | This code node has not been interpreted by the Design/Engineering/Architecture projection: exception |
| symbol | future | This code node has not yet been interpreted by the Design/Engineering/Architecture projection: future |
| symbol | sstream | This code node has not yet been interpreted by the Design/Engineering/Architecture projection: sstream |
| symbol | string | This code node has not been interpreted by the Design/Engineering/Architecture projection: string |
| symbol | utility | This code node has not been interpreted by the Design/Engineering/Architecture projection: utility |
| symbol | vector | This code node has not been interpreted by the Design/Engineering/Architecture projection: vector |
| symbol | tileKey | This code node has not been interpreted by the Design/Engineering/Architecture projection: tileKey |
| symbol | mapFetchRetryDelaySeconds | This code node has not been interpreted by the Design/Engineering/Architecture projection: mapFetchRetryDelaySeconds |
| symbol | mapGridSignature | This code node has not been interpreted by the Design/Engineering/Architecture projection: mapGridSignature |
| symbol | missingContourTiles | This code node has not been interpreted by the Design/Engineering/Architecture projection: missingContourTiles |
| symbol | onMapSourceClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapSourceClicked |
| symbol | onMapZoomInClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapZoomInClicked |
| symbol | onMapZoomOutClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapZoomOutClicked |
| symbol | onMapRecenterClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapRecenterClicked |
| symbol | formatMapCoordinate | This code node has not been interpreted by the Design/Engineering/Architecture projection: formatMapCoordinate |
| symbol | formatMapDecimal | This code node has not been interpreted by the Design/Engineering/Architecture projection: formatMapDecimal |
| symbol | mapDegToRad | This code node has not been interpreted by the Design/Engineering/Architecture projection: mapDegToRad |
| symbol | mapDistanceMeters | This code node has not been interpreted by the Design/Engineering/Architecture projection: mapDistanceMeters |
| symbol | formatMapDistance | This code node has not been interpreted by the Design/Engineering/Architecture projection Projection interpretation: formatMapDistance |
| symbol | formatMapNodeAge | This code node has not been interpreted by Design/Engineering/Architecture projection: formatMapNodeAge |
| symbol | updateMapMeasureStatus | This code node has not been interpreted by Design/Engineering/Architecture projection: updateMapMeasureStatus |
| symbol | setMapMeasurePoint | This code node has not been interpreted by the Design/Engineering/Architecture projection: setMapMeasurePoint |
| symbol | refreshAfterMapContextAction | This code node has not been interpreted by the Design/Engineering/Architecture projection: refreshAfterMapContextAction |
| symbol | onMapContextCenterClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapContextCenterClicked |
| symbol | onMapContextZoomInClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapContextZoomInClicked |
| symbol | onMapContextZoomOutClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapContextZoomOutClicked |
| symbol | onMapContextMeasureStartClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapContextMeasureStartClicked |
| symbol | onMapContextMeasureEndClicked | This code node has not been interpreted by the Design/Engineering/Architecture projection: onMapContextMeasureEndClicked |
| symbol | coordinateAtPointer | This code node has not been interpreted by the Design/Engineering/Architecture projection: coordinateAtPointer |
## Machine model
Full JSON: docs/code-understanding/code-first-discovery-spine.json