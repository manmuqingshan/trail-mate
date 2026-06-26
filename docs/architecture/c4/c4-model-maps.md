# C4 Model Maps

架构视图以 C4 抽象层级解释系统架构：System Context、Container、Component、Code。它是 UML Model 的投影，不替代组织/过程模型的业务故事，也不替代软件结构模型的结构证据。

## 元数据

项目版本：0.1.30-alpha
Git：34aad0bffa2f / main / dirty
更新于：2026-06-25T09:19:32.800Z

## C4 层级索引

| Level | Count | Maps | Explanation |
| --- | ---: | --- | --- |
| System Context | 1 | [docs/architecture/c4/system-context](system-context/system-context.md) | 目标软件系统与外部参与者、外部系统和内部容器下钻入口之间的关系。 |
| Containers | 5 | [docs/architecture/c4/containers](containers/apps-linux_uconsole_gtk/container.md) | 目标系统内部的应用、服务、数据存储或可运行/部署单元。 |
| Components | 4 | [docs/architecture/c4/components](components/apps-esp32_lvgl/component.md) | 某个 Container 内部承担清晰职责、接口或协作契约的主要组件。 |
| Code Views | 5 | [docs/architecture/c4/code](code/apps-esp32_lvgl/code.md) | 某个 Component 落到代码实现时才需要查看的少量关键代码元素。 |

## C4 下钻树

- [系统上下文：trail-mate](system-context/system-context.md) - System Context / high
  - [容器边界：apps/linux_uconsole_gtk](containers/apps-linux_uconsole_gtk/container.md) - Container / high
    - [组件职责：apps/linux_uconsole_gtk](components/apps-linux_uconsole_gtk/component.md) - Component / high
      - [代码锚点：apps/linux_uconsole_gtk](code/apps-linux_uconsole_gtk/code.md) - Code / medium
  - [容器边界：apps/esp32_lvgl](containers/apps-esp32_lvgl/container.md) - Container / high
    - [组件职责：apps/esp32_lvgl](components/apps-esp32_lvgl/component.md) - Component / high
      - [代码锚点：apps/esp32_lvgl](code/apps-esp32_lvgl/code.md) - Code / medium
  - [容器边界：apps/nrf52_node](containers/apps-nrf52_node/container.md) - Container / high
    - [组件职责：apps/nrf52_node](components/apps-nrf52_node/component.md) - Component / high
      - [代码锚点：apps/nrf52_node](code/apps-nrf52_node/code.md) - Code / medium
  - [容器边界：apps/linux_cardputer_zero](containers/apps-linux_cardputer_zero/container.md) - Container / high
    - [组件职责：apps/linux_cardputer_zero](components/apps-linux_cardputer_zero/component.md) - Component / medium
      - [代码锚点：apps/linux_cardputer_zero](code/apps-linux_cardputer_zero/code.md) - Code / medium
  - [容器边界：apps/linux_sim_shell](containers/apps-linux_sim_shell/container.md) - Container / high

## 文档列表

### System Context

- [系统上下文：trail-mate](system-context/system-context.md) - 从 C4 System Context 层解释 trail-mate 这个目标软件系统所处的环境：谁使用它、它依赖或协作哪些外部系统，以及它作为一个黑盒的边界在哪里。

### Containers

- [容器边界：apps/linux_uconsole_gtk](containers/apps-linux_uconsole_gtk/container.md) - apps/linux_uconsole_gtk 是 C4 Container 层候选边界：它必须表现为应用、服务、数据存储、可运行单元或可独立部署/执行的系统部分，而不是普通目录、代码分层或共享工具集合。
- [容器边界：apps/esp32_lvgl](containers/apps-esp32_lvgl/container.md) - apps/esp32_lvgl 是 C4 Container 层候选边界：它必须表现为应用、服务、数据存储、可运行单元或可独立部署/执行的系统部分，而不是普通目录、代码分层或共享工具集合。
- [容器边界：apps/nrf52_node](containers/apps-nrf52_node/container.md) - apps/nrf52_node 是 C4 Container 层候选边界：它必须表现为应用、服务、数据存储、可运行单元或可独立部署/执行的系统部分，而不是普通目录、代码分层或共享工具集合。
- [容器边界：apps/linux_cardputer_zero](containers/apps-linux_cardputer_zero/container.md) - apps/linux_cardputer_zero 是 C4 Container 层候选边界：它必须表现为应用、服务、数据存储、可运行单元或可独立部署/执行的系统部分，而不是普通目录、代码分层或共享工具集合。
- [容器边界：apps/linux_sim_shell](containers/apps-linux_sim_shell/container.md) - apps/linux_sim_shell 是 C4 Container 层候选边界：它必须表现为应用、服务、数据存储、可运行单元或可独立部署/执行的系统部分，而不是普通目录、代码分层或共享工具集合。

### Components

- [组件职责：apps/esp32_lvgl](components/apps-esp32_lvgl/component.md) - 从 C4 Component 层解释 apps/esp32_lvgl 内部的关键职责单元：入口、页面、命令、接口、注册表、adapter 或共享对象。
- [组件职责：apps/nrf52_node](components/apps-nrf52_node/component.md) - 从 C4 Component 层解释 apps/nrf52_node 内部的关键职责单元：入口、页面、命令、接口、注册表、adapter 或共享对象。
- [组件职责：apps/linux_uconsole_gtk](components/apps-linux_uconsole_gtk/component.md) - 从 C4 Component 层解释 apps/linux_uconsole_gtk 内部的关键职责单元：入口、页面、命令、接口、注册表、adapter 或共享对象。
- [组件职责：apps/linux_cardputer_zero](components/apps-linux_cardputer_zero/component.md) - 从 C4 Component 层解释 apps/linux_cardputer_zero 内部的关键职责单元：入口、页面、命令、接口、注册表、adapter 或共享对象。

### Code Views

- [代码锚点：apps/esp32_lvgl](code/apps-esp32_lvgl/code.md) - 从 C4 Code 层解释 apps/esp32_lvgl 内少量关键代码锚点。Code 层不是代码浏览器，只在需要理解架构组件如何落到具体文件/符号时使用。
- [代码锚点：apps/linux_uconsole_gtk](code/apps-linux_uconsole_gtk/code.md) - 从 C4 Code 层解释 apps/linux_uconsole_gtk 内少量关键代码锚点。Code 层不是代码浏览器，只在需要理解架构组件如何落到具体文件/符号时使用。
- [代码锚点：apps/nrf52_node](code/apps-nrf52_node/code.md) - 从 C4 Code 层解释 apps/nrf52_node 内少量关键代码锚点。Code 层不是代码浏览器，只在需要理解架构组件如何落到具体文件/符号时使用。
- [代码锚点：apps/linux_cardputer_zero](code/apps-linux_cardputer_zero/code.md) - 从 C4 Code 层解释 apps/linux_cardputer_zero 内少量关键代码锚点。Code 层不是代码浏览器，只在需要理解架构组件如何落到具体文件/符号时使用。
- [代码锚点：apps/linux_sim_shell](code/apps-linux_sim_shell/code.md) - 从 C4 Code 层解释 apps/linux_sim_shell 内少量关键代码锚点。Code 层不是代码浏览器，只在需要理解架构组件如何落到具体文件/符号时使用。

## 变更记录

### 0.1.30-alpha - 2026-06-25T09:19:32.800Z

- 更新 C4 Model Maps，并按 C4 抽象层级生成架构视图文档。
