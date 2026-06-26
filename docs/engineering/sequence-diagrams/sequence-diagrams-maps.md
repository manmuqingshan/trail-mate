# Sequence Diagrams

根索引：[docs/engineering/engineering-maps.md](../engineering-maps.md)

说明：由真实调用关系恢复的运行时协作片段。

| Diagram | Confidence | Document | HTML | Summary |
| --- | --- | --- | --- | --- |
| 动态协作：tick 调用 log_loop_interval | high | [md](tick-calls-log_loop_interval/sequence-diagram.md) | [html](tick-calls-log_loop_interval/sequence-diagram.html) | 打开 tick 到 log_loop_interval 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：add_status_line 调用 add_label | high | [md](add_status_line-calls-add_label/sequence-diagram.md) | [html](add_status_line-calls-add_label/sequence-diagram.html) | 打开 add_status_line 到 add_label 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：add_u32_line 调用 add_label | high | [md](add_u32_line-calls-add_label/sequence-diagram.md) | [html](add_u32_line-calls-add_label/sequence-diagram.html) | 打开 add_u32_line 到 add_label 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：add_hex_line 调用 add_status_line | high | [md](add_hex_line-calls-add_status_line/sequence-diagram.md) | [html](add_hex_line-calls-add_status_line/sequence-diagram.html) | 打开 add_hex_line 到 add_status_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：companion_enter 调用 add_label | high | [md](companion_enter-calls-add_label/sequence-diagram.md) | [html](companion_enter-calls-add_label/sequence-diagram.html) | 打开 companion_enter 到 add_label 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：companion_enter 调用 add_status_line | high | [md](companion_enter-calls-add_status_line/sequence-diagram.md) | [html](companion_enter-calls-add_status_line/sequence-diagram.html) | 打开 companion_enter 到 add_status_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：companion_enter 调用 add_u32_line | high | [md](companion_enter-calls-add_u32_line/sequence-diagram.md) | [html](companion_enter-calls-add_u32_line/sequence-diagram.html) | 打开 companion_enter 到 add_u32_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：companion_enter 调用 add_hex_line | high | [md](companion_enter-calls-add_hex_line/sequence-diagram.md) | [html](companion_enter-calls-add_hex_line/sequence-diagram.html) | 打开 companion_enter 到 add_hex_line 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 可继续下钻到 1 个 Component Diagram。 |
| 动态协作：startEsp32LvglLoopRuntime 调用 canStartEsp32LvglLoopRuntime | high | [md](startesp32lvglloopruntime-calls-canstartesp32lvglloopruntime/sequence-diagram.md) | [html](startesp32lvglloopruntime-calls-canstartesp32lvglloopruntime/sequence-diagram.html) | 打开 startEsp32LvglLoopRuntime 到 canStartEsp32LvglLoopRuntime 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：hasEsp32LvglRuntimeTargetProfile 调用 esp32LvglRuntimeTargetProfile | high | [md](hasesp32lvglruntimetargetprofile-calls-esp32lvglruntimetargetprofile/sequence-diagram.md) | [html](hasesp32lvglruntimetargetprofile-calls-esp32lvglruntimetargetprofile/sequence-diagram.html) | 打开 hasEsp32LvglRuntimeTargetProfile 到 esp32LvglRuntimeTargetProfile 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：showBootUi 调用 lockUi | high | [md](showbootui-calls-lockui/sequence-diagram.md) | [html](showbootui-calls-lockui/sequence-diagram.html) | 打开 showBootUi 到 lockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：showBootUi 调用 unlockUi | high | [md](showbootui-calls-unlockui/sequence-diagram.md) | [html](showbootui-calls-unlockui/sequence-diagram.html) | 打开 showBootUi 到 unlockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：setBootLog 调用 lockUi | high | [md](setbootlog-calls-lockui/sequence-diagram.md) | [html](setbootlog-calls-lockui/sequence-diagram.html) | 打开 setBootLog 到 lockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：setBootLog 调用 unlockUi | high | [md](setbootlog-calls-unlockui/sequence-diagram.md) | [html](setbootlog-calls-unlockui/sequence-diagram.html) | 打开 setBootLog 到 unlockUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：canRunEsp32LvglStartupRuntime 调用 canStartEsp32LvglLoopRuntime | high | [md](canrunesp32lvglstartupruntime-calls-canstartesp32lvglloopruntime/sequence-diagram.md) | [html](canrunesp32lvglstartupruntime-calls-canstartesp32lvglloopruntime/sequence-diagram.html) | 打开 canRunEsp32LvglStartupRuntime 到 canStartEsp32LvglLoopRuntime 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：runEsp32LvglStartupRuntime 调用 canRunEsp32LvglStartupRuntime | high | [md](runesp32lvglstartupruntime-calls-canrunesp32lvglstartupruntime/sequence-diagram.md) | [html](runesp32lvglstartupruntime-calls-canrunesp32lvglstartupruntime/sequence-diagram.html) | 打开 runEsp32LvglStartupRuntime 到 canRunEsp32LvglStartupRuntime 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：runEsp32LvglStartupRuntime 调用 showBootUi | high | [md](runesp32lvglstartupruntime-calls-showbootui/sequence-diagram.md) | [html](runesp32lvglstartupruntime-calls-showbootui/sequence-diagram.html) | 打开 runEsp32LvglStartupRuntime 到 showBootUi 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |
| 动态协作：runEsp32LvglStartupRuntime 调用 setBootLog | high | [md](runesp32lvglstartupruntime-calls-setbootlog/sequence-diagram.md) | [html](runesp32lvglstartupruntime-calls-setbootlog/sequence-diagram.html) | 打开 runEsp32LvglStartupRuntime 到 setBootLog 的动态协作片段，检查这次调用在相关组件、结构切片和业务下钻中承担什么运行职责。 |

## 地图变更记录

### 0.1.30-alpha - 2026-06-25T09:19:20.669Z

- 更新 Sequence Diagrams maps，当前包含 18 个具体文档。
