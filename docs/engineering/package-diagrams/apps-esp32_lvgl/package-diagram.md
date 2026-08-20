# Module boundary: apps/esp32_lvgl

Image type: Package Diagrams
Status: candidate
Confidence: high
Project version: 0.1.30-alpha
Git:34aad0bffa2f / main / dirty
Updated on: 2026-06-25T09:19:20.669Z

## Positioning

Explain the package/module boundaries, number of files, number of symbols and cross-module dependencies of apps/esp32_lvgl.

## How to read the picture

- This Package Diagram is centered on apps/esp32_lvgl and shows the file size, symbol size and cross-module dependencies observed when it serves as a project module boundary.
- The arrows in the figure represent the cross-module relationships observed by the local warehouse evidence, which are mainly used to understand the direction of technical dependencies; it is not the business process sequence, nor the runtime message timing.
- The main external dependencies currently observed include: firmware, apps/linux_uconsole_gtk.

## Technical Complexity Analysis

- apps/esp32_lvgl currently contains 31 files and 402 symbols belonging to the technical organization boundaries identified by the Software Architecture Model.
- The cross-module relationship is as follows: it is referenced or called 217 times by other modules, and it actively depends on or calls external modules 3 times, so it is relied on more by other modules.
- There is currently no obvious abnormality in external dependence, but it is still necessary to judge whether the dependence direction is stable based on specific business entrances.

## Correlation with business complexity

- apps/esp32_lvgl is not the business story itself, but the technical boundaries that may pass when business capabilities are implemented.
- If the evidence, entry or drill-down diagram of a Use Case in the organization/process model falls in apps/esp32_lvgl, the Use Case should be linked back to this Package Diagram to indicate which engineering module the business story is hosted by.
 - The current association is still CANDIDATE: Technical boundaries can only be explained here based on warehouse evidence and are not a substitute for the organization/process model's confirmation of the business story, actors and business goals.

## Governance suggestions

- When adding a new function, give priority to confirming that it belongs to the stable responsibility of the module, rather than falling into the module because of the convenience of calling.
- Keep the dependency direction of this module interpretable to avoid forming an implicit public toolbox.
- When the business Use Case document references this module, the specific entry, call chain or configuration evidence should be recorded in the Use Case drill-down document.

## UML / technical diagram

```mermaid
flowchart LR
  package_node["apps/esp32_lvgl"]
  dependency_1["firmware"]
  package_node --> dependency_1
  dependency_2["apps/linux_uconsole_gtk"]
  package_node --> dependency_2
```

## coverage

-Module path: apps/esp32_lvgl
-Number of files: 31
-Number of symbols: 402
-Dependent or called by other modules: 217
- Depends on or calls external modules: 3

## Drill-down of semantic elements in the diagram

### apps/esp32_lvgl

- Element type: package
- Description: apps/esp32_lvgl is the central project boundary of the current Package Diagram, which is used to observe its own scale, dependency direction and drill-down technical complexity.
- Technical role: Technical organizational boundary: It aggregates files, symbols and cross-module relationships under apps/esp32_lvgl into a discussable engineering unit.
- Why it appears: Local repository evidence observes enough files, symbols, or cross-module relationships under apps/esp32_lvgl that it deserves to be promoted to a package-level entry in the software structure model.
- Relationship meaning: The arrows pointing from apps/esp32_lvgl to other nodes in the figure indicate that the current boundary depends on external package/module; it is dependent or called 217 times by other modules and depends on or called external modules 3 times, which is used to determine whether it is more like a stable reuse boundary or an orchestration/bridging boundary.
- Drill down intent: Drill down into this node to continue viewing the key components, structural collaboration slices, running links, deployment nodes, and complexity hotspots within apps/esp32_lvgl to understand how this project boundary carries functional changes.
-Business correlation: This node is not the business story itself, but the Use Case that falls into apps/esp32_lvgl in the organization/process model can refer to this as the technology bearing boundary. The current association is still CANDIDATE.
- Change impact: Modifying the public entry, dependency direction or directory boundary of apps/esp32_lvgl may affect the verification path of component diagrams, sequence fragments, deployment configurations and related business stories that reference it.
- Confidence: high
- Evidence:
  - package scope: apps/esp32_lvgl
 - Module path: apps/esp32_lvgl
 - Number of files: 31
 - Number of symbols: 402
 - Depends on or called by other modules: 217
 - Depends on or calls external modules: 3
  - apps/esp32_lvgl/APP_SHELL_MANIFEST.md
  - apps/esp32_lvgl/CMakeLists.txt
 - Risk:
 - If you only regard this node as a directory name, you will miss its responsibility as a stable project boundary.
 - If signs of dependency on external modules continue to increase, it may be a sign that the boundary is taking on too much orchestration or bridging responsibility.
- Question:
 - The current repository evidence does not yet explicitly trace the package to a Use Case; therefore the business association remains a candidate.
- Drill down: [Function node: main](../../component-diagrams/apps-esp32_lvgl-main/component-diagram.md) - Open function node: main to confirm which specific object within apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: contains](../../component-diagrams/apps-esp32_lvgl-contains/component-diagram.md) - Open the function node: contains to confirm which specific object within apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: companion_enter](../../component-diagrams/apps-esp32_lvgl-companion_enter/component-diagram.md) - Open function node: companion_enter to confirm which specific object within apps/esp32_lvgl assumes entry, orchestration, adaptation, contract or shared responsibilities. Focus on checking the code anchor apps/esp32_lvgl/src/esp32_lvgl_idf_app_registry.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Dynamic collaboration: tick calls log_loop_interval](../../sequence-diagrams/tick-calls-log_loop_interval/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: tick -> log_loop_interval. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: add_status_line calls add_label](../../sequence-diagrams/add_status_line-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: add_status_line -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: add_u32_line calls add_label](../../sequence-diagrams/add_u32_line-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: add_u32_line -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: add_hex_line calls add_status_line](../../sequence-diagrams/add_hex_line-calls-add_status_line/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl into a readable collaboration: add_hex_line -> add_status_line. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: companion_enter calls add_label](../../sequence-diagrams/companion_enter-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: companion_enter -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: companion_enter calls add_status_line](../../sequence-diagrams/companion_enter-calls-add_status_line/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: companion_enter -> add_status_line. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.

### firmware

- Element type: package
- Description: firmware is the currently observed external technology boundary dependency of apps/esp32_lvgl; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observed a cross-module factual relationship between apps/esp32_lvgl and firmware, so the dependency was put into the Package Diagram instead of just hidden in the code import/call.
- Relationship meaning: apps/esp32_lvgl -> firmware indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill-down firmware can view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
-Business association: apps/esp32_lvgl If a user-visible capability is hosted, the dependency on firmware may be the operating mechanism, extension point, or governance constraint of the capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of firmware may cause chain changes in the calling chain, packaging entry, agent workflow or UI behavior of apps/esp32_lvgl.
- Confidence: high
- Evidence:
  - dependency edge: apps/esp32_lvgl -> firmware
  - firmware/c6_companion/CMakeLists.txt
  - firmware/c6_companion/components/tm_ble/CMakeLists.txt
  - firmware/c6_companion/components/tm_ble/include/tm_ble.h
  - firmware/c6_companion/components/tm_ble/tm_ble.c
  - firmware/c6_companion/components/tm_diag/CMakeLists.txt
  - firmware/c6_companion/components/tm_diag/include/tm_diag.h
  - firmware/c6_companion/components/tm_diag/tm_diag.c
 - Risk:
 - Cross-module dependencies can only prove technical relationships, not business relationships directly.
 - If this dependency only exists for implementation convenience, future changes may cause boundary drift or implicit public toolboxes.
- Question:
 - There is currently no evidence that apps/esp32_lvgl's dependency on firmware is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: firmware](../firmware/package-diagram.md) - Open firmware's own package-level boundary and check whether apps/esp32_lvgl depends on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.
-Drill down: [Service Object: main](../../component-diagrams/firmware-main/component-diagram.md) - Open Service Object: main to confirm which specific object within the firmware is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor firmware/c6_companion/tests/test_tm_services_functional.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Interface component: tm_services_record_error](../../component-diagrams/firmware-tm_services_record_error/component-diagram.md) - Open interface component: tm_services_record_error to confirm which specific object within the firmware is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor firmware/c6_companion/components/tm_services/tm_services.c, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.

### apps/linux_uconsole_gtk

- Element type: package
- Description: apps/linux_uconsole_gtk is the currently observed external technology boundary dependency of apps/esp32_lvgl; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observed a cross-module factual relationship between apps/esp32_lvgl and apps/linux_uconsole_gtk, so the dependency was put into the Package Diagram instead of just hidden in the code import/call.
- Relationship meaning: apps/esp32_lvgl -> apps/linux_uconsole_gtk indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill down into apps/linux_uconsole_gtk to view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
- Business correlation: apps/esp32_lvgl If hosting a user-visible capability, the dependency on apps/linux_uconsole_gtk may be the operating mechanism, extension point, or governance constraint of that capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of apps/linux_uconsole_gtk may cause chain changes in the call chain, packaging entry, agent workflow or UI behavior of apps/esp32_lvgl.
- Confidence: high
- Evidence:
  - dependency edge: apps/esp32_lvgl -> apps/linux_uconsole_gtk
  - apps/linux_uconsole_gtk/APP_SHELL_MANIFEST.md
  - apps/linux_uconsole_gtk/CMakeLists.txt
  - apps/linux_uconsole_gtk/packaging/trailmate-uconsole.desktop
  - apps/linux_uconsole_gtk/packaging/trailmate-uconsole.png
  - apps/linux_uconsole_gtk/README.md
  - apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.cpp
  - apps/linux_uconsole_gtk/src/linux_uconsole_gtk_app_shell.h
 - Risk:
 - Cross-module dependencies can only prove technical relationships, not business relationships directly.
 - If this dependency only exists for implementation convenience, future changes may cause boundary drift or implicit public toolboxes.
- Question:
 - Current evidence does not demonstrate that apps/esp32_lvgl's dependence on apps/linux_uconsole_gtk is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: apps/linux_uconsole_gtk](../apps-linux_uconsole_gtk/package-diagram.md) - Open apps/linux_uconsole_gtk's own package-level boundary and check whether apps/esp32_lvgl depends on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.
 - Drill down: [Function node: launchSettingsLayout](../../component-diagrams/apps-linux_uconsole_gtk-launchsettingslayout/component-diagram.md) - Open function node: launchSettingsLayout to confirm apps/linux_uconsole_gtk Which specific internal object is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: makeLabel](../../component-diagrams/apps-linux_uconsole_gtk-makelabel/component-diagram.md) - Open the function node: makeLabel to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_widgets.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: makeSettingsRow](../../component-diagrams/apps-linux_uconsole_gtk-makesettingsrow/component-diagram.md) - Open the function node: makeSettingsRow to confirm apps/linux_uconsole_gtk Which specific internal object is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: refreshUi](../../component-diagrams/apps-linux_uconsole_gtk-refreshui/component-diagram.md) - Open the function node: refreshUi to confirm which specific object within apps/linux_uconsole_gtk assumes entry, orchestration, adaptation, contract or shared responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_shell.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: refreshMap](../../component-diagrams/apps-linux_uconsole_gtk-refreshmap/component-diagram.md) - Open the function node: refreshMap to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: main](../../component-diagrams/apps-linux_uconsole_gtk-main/component-diagram.md) - Open function node: main to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: launchMapLayout](../../component-diagrams/apps-linux_uconsole_gtk-launchmaplayout/component-diagram.md) - Open function node: launchMapLayout to confirm apps/linux_uconsole_gtk Which specific internal object is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: expect](../../component-diagrams/apps-linux_uconsole_gtk-expect/component-diagram.md) - Open function node: expect to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.

## Drill-down UML

- [Function node: main](../../component-diagrams/apps-esp32_lvgl-main/component-diagram.md) - Open function node: main to confirm which specific object inside apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
- [Function node: contains](../../component-diagrams/apps-esp32_lvgl-contains/component-diagram.md) - Open function node: contains to confirm which specific object within apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
- [Function node: companion_enter](../../component-diagrams/apps-esp32_lvgl-companion_enter/component-diagram.md) - Open function node: companion_enter to confirm which specific object within apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/src/esp32_lvgl_idf_app_registry.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
- [Dynamic collaboration: tick calls log_loop_interval](../../sequence-diagrams/tick-calls-log_loop_interval/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: tick -> log_loop_interval. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
- [Dynamic collaboration: add_status_line calls add_label](../../sequence-diagrams/add_status_line-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: add_status_line -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
- [Dynamic collaboration: add_u32_line calls add_label](../../sequence-diagrams/add_u32_line-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: add_u32_line -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
- [Dynamic collaboration: add_hex_line calls add_status_line](../../sequence-diagrams/add_hex_line-calls-add_status_line/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl into a readable collaboration: add_hex_line -> add_status_line. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
- [Dynamic collaboration: companion_enter calls add_label](../../sequence-diagrams/companion_enter-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: companion_enter -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
- [Dynamic collaboration: companion_enter calls add_status_line](../../sequence-diagrams/companion_enter-calls-add_status_line/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: companion_enter -> add_status_line. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.

## Evidence

- apps/esp32_lvgl/APP_SHELL_MANIFEST.md
- apps/esp32_lvgl/CMakeLists.txt
- apps/esp32_lvgl/library.json
- apps/esp32_lvgl/README.md
- apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp
- apps/esp32_lvgl/src/esp32_lvgl_app_shell.h
- apps/esp32_lvgl/src/esp32_lvgl_arduino_app_registry.cpp
- apps/esp32_lvgl/src/esp32_lvgl_arduino_app_runtime_access.cpp

## Problem

- There are no open issues yet.

## Change record

### 0.1.30-alpha - 2026-06-25T09:19:20.669Z

- Generated from local repository evidence Module boundary: apps/esp32_lvgl.
