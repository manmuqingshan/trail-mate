# C4 Model Maps

The architecture view explains the system architecture at the C4 abstraction level: System Context, Container, Component, Code. It is a projection of the UML Model and does not replace the business story of the organization/process model, nor the structural evidence of the software structural model.

## Metadata

Project version: 0.1.30-alpha
Git:34aad0bffa2f / main / dirty
Updated on: 2026-06-25T09:19:32.800Z

## C4 level index

| Level | Count | Maps | Explanation |
| --- | ---: | --- | --- |
| System Context | 1 | [docs/architecture/c4/system-context](system-context/system-context.md) | The relationship between the target software system and drill-down entries to external actors, external systems, and internal containers. |
| Containers | 5 | [docs/architecture/c4/containers](containers/apps-linux_uconsole_gtk/container.md) | An application, service, data store, or runnable/deployable unit within a target system. |
| Components | 4 | [docs/architecture/c4/components](components/apps-esp32_lvgl/component.md) | The main components within a Container that bear clear responsibilities, interfaces, or collaboration contracts. |
| Code Views | 5 | [docs/architecture/c4/code](code/apps-esp32_lvgl/code.md) | A small number of key code elements that need to be viewed when a certain Component falls into the code implementation. |

## C4 drill down tree

- [System context: trail-mate](system-context/system-context.md) - System Context / high
 - [Container boundary: apps/linux_uconsole_gtk](containers/apps-linux_uconsole_gtk/container.md) - Container /high
 - [Component responsibility: apps/linux_uconsole_gtk](components/apps-linux_uconsole_gtk/component.md) - Component / high
 - [Code anchor: apps/linux_uconsole_gtk](code/apps-linux_uconsole_gtk/code.md) - Code / medium
 - [Container boundary: apps/esp32_lvgl](containers/apps-esp32_lvgl/container.md) - Container / high
 - [Component responsibility: apps/esp32_lvgl](components/apps-esp32_lvgl/component.md) - Component / high
 - [Code anchor: apps/esp32_lvgl](code/apps-esp32_lvgl/code.md) - Code / medium
 - [Container boundary: apps/nrf52_node](containers/apps-nrf52_node/container.md) - Container / high
 - [Component responsibility: apps/nrf52_node](components/apps-nrf52_node/component.md) - Component / high
 - [Code anchor: apps/nrf52_node](code/apps-nrf52_node/code.md) - Code / medium
 - [Container boundary: apps/linux_cardputer_zero](containers/apps-linux_cardputer_zero/container.md) - Container / high
 - [Component responsibility: apps/linux_cardputer_zero](components/apps-linux_cardputer_zero/component.md) - Component / medium
 - [Code anchor: apps/linux_cardputer_zero](code/apps-linux_cardputer_zero/code.md) - Code / medium
 - [Container boundary: apps/linux_sim_shell](containers/apps-linux_sim_shell/container.md) - Container / high

## Documentation List

### System Context

 - [System Context: trail-mate](system-context/system-context.md) - Explains trail-mate from the C4 System Context layer The environment in which this target software system exists: who uses it, what external systems it depends on or collaborates with, and where its boundaries are as a black box.

### Containers

 - [Container boundary: apps/linux_uconsole_gtk](containers/apps-linux_uconsole_gtk/container.md) - apps/linux_uconsole_gtk is a C4 Container layer candidate boundary: it must appear as an application, service, data store, runnable unit, or independently deployable/executable system part, rather than a normal directory, code layering, or shared collection of tools.
 - [Container boundary: apps/esp32_lvgl](containers/apps-esp32_lvgl/container.md) - apps/esp32_lvgl is a C4 Container layer candidate boundary: it must appear as an application, service, data store, runnable unit, or independently deployable/executable system part, rather than a normal directory, code layering, or shared collection of tools.
 - [Container boundary: apps/nrf52_node](containers/apps-nrf52_node/container.md) - apps/nrf52_node is a C4 Container layer candidate boundary: it must behave as an application, service, data store, runnable unit, or independently deployable/executable system part, rather than a normal directory, code layering, or shared collection of tools.
 - [Container boundary: apps/linux_cardputer_zero](containers/apps-linux_cardputer_zero/container.md) - apps/linux_cardputer_zero is a C4 Container layer candidate boundary: it must appear as an application, service, data store, runnable unit, or independently deployable/executable system part, rather than a normal directory, code layering, or shared collection of tools.
 - [Container boundary: apps/linux_sim_shell](containers/apps-linux_sim_shell/container.md) - apps/linux_sim_shell is a C4 Container layer candidate boundary: it must behave as an application, service, data store, runnable unit, or independently deployable/executable system part, rather than a normal directory, code layer, or shared collection of tools.

### Components

- [Component responsibility: apps/esp32_lvgl](components/apps-esp32_lvgl/component.md) - Explain the key responsibility units inside apps/esp32_lvgl from the C4 Component layer: entry, page, command, interface, registry, adapter or shared object.
-[Component responsibility: apps/nrf52_node](components/apps-nrf52_node/component.md) - Explain the key responsibility units inside apps/nrf52_node from the C4 Component layer: entry, page, command, interface, registry, adapter or shared object.
- [Component responsibility: apps/linux_uconsole_gtk](components/apps-linux_uconsole_gtk/component.md) - Explain the key responsibility units inside apps/linux_uconsole_gtk from the C4 Component layer: entry, page, command, interface, registry, adapter or shared object.
- [Component responsibility: apps/linux_cardputer_zero](components/apps-linux_cardputer_zero/component.md) - Explain the key responsibility units inside apps/linux_cardputer_zero from the C4 Component layer: entry, page, command, interface, registry, adapter or shared object.

### Code Views

- [Code Anchor: apps/esp32_lvgl](code/apps-esp32_lvgl/code.md) - Interpret a small number of key code anchors within apps/esp32_lvgl from the C4 Code layer. The Code layer is not a code browser and is only used when you need to understand how architectural components fall into specific files/symbols.
- [Code Anchors: apps/linux_uconsole_gtk](code/apps-linux_uconsole_gtk/code.md) - Interprets a few key code anchors within apps/linux_uconsole_gtk from the C4 Code layer. The Code layer is not a code browser and is only used when you need to understand how architectural components fall into specific files/symbols.
- [Code Anchor: apps/nrf52_node](code/apps-nrf52_node/code.md) - Interpret a small number of key code anchors within apps/nrf52_node from the C4 Code layer. The Code layer is not a code browser and is only used when you need to understand how architectural components fall into specific files/symbols.
- [Code Anchors: apps/linux_cardputer_zero](code/apps-linux_cardputer_zero/code.md) - A small number of key code anchors in apps/linux_cardputer_zero are explained from the C4 Code layer. The Code layer is not a code browser and is only used when you need to understand how architectural components fall into specific files/symbols.
- [Code Anchors: apps/linux_sim_shell](code/apps-linux_sim_shell/code.md) - Explanation of a small number of key code anchors within apps/linux_sim_shell from the C4 Code layer. The Code layer is not a code browser and is only used when you need to understand how architectural components fall into specific files/symbols.

## Change Record

### 0.1.30-alpha - 2026-06-25T09:19:32.800Z

- Update C4 Model Maps and generate architectural view documents according to C4 abstraction level.
