# Module boundary: boards

Image type: Package Diagrams
Status: candidate
Confidence: high
Project version: 0.1.30-alpha
Git:34aad0bffa2f / main / dirty
Updated on: 2026-06-25T09:19:20.669Z

## Positioning

Explain the package/module boundaries of boards, number of files, number of symbols, and cross-module dependencies.

## How to read the picture

- This Package Diagram is centered on boards, showing the file size, symbol size, and cross-module dependencies observed when it serves as a project module boundary.
- The arrows in the figure represent the cross-module relationships observed by the local warehouse evidence, which are mainly used to understand the direction of technical dependencies; it is not the business process sequence, nor the runtime message timing.
- The main external dependencies currently observed include: apps/esp32_lvgl, apps/linux_cardputer_zero, apps/nrf52_node, apps/linux_uconsole_gtk, apps/linux_sim_shell, firmware.

## Technical Complexity Analysis

- boards currently contains 92 files and 1952 symbols belonging to technical organizational boundaries identified by the software architecture model.
- The cross-module relationship is as follows: it is referenced or called 47 times by other modules, and it actively depends on or calls external modules 183 times, so it depends more on external modules.
- More external dependencies may indicate that this module is responsible for orchestrating, aggregating or bridging multiple capabilities; it is currently handled as a candidate technology coupling center.

## Correlation with business complexity

- Boards are not the business story itself, but the technical boundaries that business capabilities may pass through when implemented.
- If the evidence, entry or drill-down diagram of a Use Case in the organization/process model falls on boards, the Use Case should be linked back to this Package Diagram to indicate which engineering module the business story is hosted by.
 - The current association is still CANDIDATE: Technical boundaries can only be explained here based on warehouse evidence and are not a substitute for the organization/process model's confirmation of the business story, actors and business goals.

## Governance suggestions

- When adding a new function, give priority to confirming that it belongs to the stable responsibility of the module, rather than falling into the module because of the convenience of calling.
- If multiple future changes increase the module's external dependencies, consider splitting the port, adapter, or application service boundary.
- When the business Use Case document references this module, the specific entry, call chain or configuration evidence should be recorded in the Use Case drill-down document.

## UML / technical diagram

```mermaid
flowchart LR
  package_node["boards"]
  dependency_1["apps/esp32_lvgl"]
  package_node --> dependency_1
  dependency_2["apps/linux_cardputer_zero"]
  package_node --> dependency_2
  dependency_3["apps/nrf52_node"]
  package_node --> dependency_3
  dependency_4["apps/linux_uconsole_gtk"]
  package_node --> dependency_4
  dependency_5["apps/linux_sim_shell"]
  package_node --> dependency_5
  dependency_6["firmware"]
  package_node --> dependency_6
```

## coverage

-Module path: boards
-Number of files: 92
-Number of symbols: 1952
-Dependent or called by other modules: 47
- Depend on or call external modules: 183

## Drill-down of semantic elements in the diagram

### boards

- Element type: package
- Description: boards are the central project boundary of the current Package Diagram, used to observe its own scale, dependency direction and drill-down technical complexity.
- Technical role: Technical organizational boundary: It aggregates files, symbols and cross-module relationships under boards into a discussable engineering unit.
- Why it appears: Local repository evidence observes enough files, symbols, or cross-module relationships under boards that it deserves to be promoted to a package-level entry in the software structure model.
- Relationship meaning: The arrows pointing from boards to other nodes in the figure indicate that the current boundary depends on external packages/modules; it is dependent on or called 47 times by other modules, and depends on or called external modules 183 times, which is used to determine whether it is more like a stable reuse boundary or an orchestration/bridging boundary.
- Drill down intention: Drill down into this node to continue to view key components, structural collaboration slices, running links, deployment nodes and complexity hotspots within boards to understand how this engineering boundary carries functional changes.
-Business correlation: This node is not the business story itself, but the Use Case that falls on boards in the organization/process model can refer to this as a technology carrying boundary. The current association is still CANDIDATE.
- Change impact: Modifying the public entry, dependency direction or directory boundary of boards may affect the verification path of component diagrams, sequence fragments, deployment configurations and related business stories that reference it.
- Confidence: high
- Evidence:
  - package scope: boards
 - Module path: boards
 - Number of files: 92
 - Number of symbols: 1952
 - Depends on or called by other modules: 47
 - Depends on or calls external modules: 183
  - boards/cardputerzero/board_facts.h
  - boards/cardputerzero/BOARD.md
 - Risk:
 - If you only regard this node as a directory name, you will miss its responsibility as a stable project boundary.
 - If signs of dependency on external modules continue to increase, it may be a sign that the boundary is taking on too much orchestration or bridging responsibility.
- Question:
 - The current repository evidence does not yet explicitly trace the package to a Use Case; therefore the business association remains a candidate.
-Drill down: [Function node: makeBoardProfile](../../component-diagrams/boards-makeboardprofile/component-diagram.md) - Open the function node: makeBoardProfile to confirm which specific object within boards assumes entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor boards/t_echo_lite/include/boards/t_echo_lite/board_profile.h, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: pinNum](../../component-diagrams/boards-pinnum/component-diagram.md) - Open the function node: pinNum to confirm which specific object within the boards assumes entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor boards/t_echo_lite/include/boards/t_echo_lite/board_profile.h, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Structure Collaboration: Structural Slice boards · gat562_mesh_evb_pro/include/boards](../../class-structural-diagrams/boards-gat562_mesh_evb_pro-include-boards/class-structural-diagram.md) - Open Structural Collaboration: Structural Slice boards · gat562_mesh_evb_pro/include/boards is to explain boards from an object collaboration perspective: which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps judge that complexity comes from the distribution of responsibilities, not just the number of files.
 - Drill down: [Structural collaboration: Structural slice boards · t_echo_lite/include/boards](../../class-structural-diagrams/boards-t_echo_lite-include-boards/class-structural-diagram.md) - Open Structural collaboration: Structural slicing boards · t_echo_lite/include/boards is to explain from the object collaboration perspective boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
-Drill down: [Structural Collaboration: Structural Slicing boards · tab5/include/boards](../../class-structural-diagrams/boards-tab5-include-boards/class-structural-diagram.md) - Open Structural Collaboration: Structural Slicing boards · tab5/include/boards is to explain from the perspective of object collaboration boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
 - Drill down: [Structural Collaboration: Structural Slice boards · t_display_p4/include/boards](../../class-structural-diagrams/boards-t_display_p4-include-boards/class-structural-diagram.md) - Open Structural Collaboration: Structural Slice boards · t_display_p4/include/boards It is to explain boards from the perspective of object collaboration: which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps to judge that complexity comes from the distribution of responsibilities, not just the number of files.
 - Drill down: [Structural collaboration: Structural slicing boards · tlora_pager/include/boards](../../class-structural-diagrams/boards-tlora_pager-include-boards/class-structural-diagram.md) - Open Structural collaboration: Structural slicing boards · tlora_pager/include/boards is to explain from the object collaboration perspective boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
 - Drill down: [Dependency cluster: boards technical hotspots](../../technical-hotspots/dependency-cluster--boards/technical-hotspot.md) - View dependency cluster: boards technical hotspot whether this complexity signal will increase the cost of reading, modifying, testing or regression of boards.
 - Drill down: [Candidates for external collaboration: boards/tlora_pager/src/tlora_pager_board.cpp Technical Hotspots](../../technical-hotspots/collaboration-pressure--boards-tlora_pager-src-tlora_pager_board-cpp/technical-hotspot.md) - View Candidates for external collaboration: boards/tlora_pager/src/tlora_pager_board.cpp Technical Hotspot Whether this complexity signal will increase the cost of reading, modifying, testing or regression of boards.

### apps/esp32_lvgl

- Element type: package
- Description: apps/esp32_lvgl is the external technical boundary dependency currently observed by boards; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observes a cross-module factual relationship between boards and apps/esp32_lvgl, so the dependency is put into the Package Diagram instead of just hiding in the code import/call.
- Relationship meaning: boards -> apps/esp32_lvgl indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill down into apps/esp32_lvgl to view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
-Business correlation: boards If hosting a user-visible capability, the dependency on apps/esp32_lvgl may be the operating mechanism, extension point, or governance constraint of that capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of apps/esp32_lvgl may cause chain changes in the boards' call chain, packaging entry, agent workflow or UI behavior.
- Confidence: high
- Evidence:
  - dependency edge: boards -> apps/esp32_lvgl
  - apps/esp32_lvgl/APP_SHELL_MANIFEST.md
  - apps/esp32_lvgl/CMakeLists.txt
  - apps/esp32_lvgl/library.json
  - apps/esp32_lvgl/README.md
  - apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp
  - apps/esp32_lvgl/src/esp32_lvgl_app_shell.h
  - apps/esp32_lvgl/src/esp32_lvgl_arduino_app_registry.cpp
 - Risk:
 - Cross-module dependencies can only prove technical relationships, not business relationships directly.
 - If this dependency only exists for implementation convenience, future changes may cause boundary drift or implicit public toolboxes.
- Question:
 - Current evidence does not prove that boards' dependency on apps/esp32_lvgl is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: apps/esp32_lvgl](../apps-esp32_lvgl/package-diagram.md) - Open apps/esp32_lvgl's own package-level boundary and check whether boards depend on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.
- Drill down: [Function node: main](../../component-diagrams/apps-esp32_lvgl-main/component-diagram.md) - Open function node: main to confirm which specific object within apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: contains](../../component-diagrams/apps-esp32_lvgl-contains/component-diagram.md) - Open the function node: contains to confirm which specific object within apps/esp32_lvgl is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/esp32_lvgl/tests/esp32_lvgl_sd_coredump_contract_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: companion_enter](../../component-diagrams/apps-esp32_lvgl-companion_enter/component-diagram.md) - Open function node: companion_enter to confirm which specific object within apps/esp32_lvgl assumes entry, orchestration, adaptation, contract or shared responsibilities. Focus on checking the code anchor apps/esp32_lvgl/src/esp32_lvgl_idf_app_registry.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Dynamic collaboration: tick calls log_loop_interval](../../sequence-diagrams/tick-calls-log_loop_interval/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: tick -> log_loop_interval. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: add_status_line calls add_label](../../sequence-diagrams/add_status_line-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: add_status_line -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: add_u32_line calls add_label](../../sequence-diagrams/add_u32_line-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: add_u32_line -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: add_hex_line calls add_status_line](../../sequence-diagrams/add_hex_line-calls-add_status_line/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl into a readable collaboration: add_hex_line -> add_status_line. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.
-Drill down: [Dynamic collaboration: companion_enter calls add_label](../../sequence-diagrams/companion_enter-calls-add_label/sequence-diagram.md) - This sequence is opened to restore the static dependency of apps/esp32_lvgl to a readable collaboration: companion_enter -> add_label. Focus on determining whether this is an import, call, reference or message direction, and whether it really affects the running path.

### apps/linux_cardputer_zero

- Element type: package
- Description: apps/linux_cardputer_zero is the external technical boundary dependency currently observed by boards; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observed a cross-module factual relationship between boards and apps/linux_cardputer_zero, so the dependency was put into the Package Diagram instead of just hiding in the code import/call.
- Relationship meaning: boards -> apps/linux_cardputer_zero indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill down into apps/linux_cardputer_zero to view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
-Business correlation: boards If hosting a user-visible capability, the dependency on apps/linux_cardputer_zero may be the running mechanism, extension point, or governance constraint of the capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of apps/linux_cardputer_zero may cause chain changes in the boards' call chain, packaging entry, agent workflow or UI behavior.
- Confidence: high
- Evidence:
  - dependency edge: boards -> apps/linux_cardputer_zero
  - apps/linux_cardputer_zero/APP_SHELL_MANIFEST.md
  - apps/linux_cardputer_zero/CMakeLists.txt
  - apps/linux_cardputer_zero/packaging/trailmate-cardputer-zero-applaunch
  - apps/linux_cardputer_zero/packaging/trailmate-cardputer-zero.desktop
  - apps/linux_cardputer_zero/packaging/trailmate-cardputer-zero.png
  - apps/linux_cardputer_zero/README.md
  - apps/linux_cardputer_zero/src/cardputer_zero_input_method_port.cpp
 - Risk:
 - Cross-module dependencies can only prove technical relationships, not business relationships directly.
 - If this dependency only exists for implementation convenience, future changes may cause boundary drift or implicit public toolboxes.
- Question:
 - There is currently no evidence that boards' dependency on apps/linux_cardputer_zero is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: apps/linux_cardputer_zero](../apps-linux_cardputer_zero/package-diagram.md) - Open apps/linux_cardputer_zero's own package-level boundary and check whether boards depend on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.
-Drill down: [Function node: main](../../component-diagrams/apps-linux_cardputer_zero-main/component-diagram.md) - Open function node: main to confirm which specific object within apps/linux_cardputer_zero is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: contains](../../component-diagrams/apps-linux_cardputer_zero-contains/component-diagram.md) - Open the function node: contains to confirm which specific object within apps/linux_cardputer_zero assumes entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: not_contains](../../component-diagrams/apps-linux_cardputer_zero-not_contains/component-diagram.md) - Open the function node: not_contains to confirm which specific object within apps/linux_cardputer_zero is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
-Drill down: [Function node: read_file](../../component-diagrams/apps-linux_cardputer_zero-read_file/component-diagram.md) - Open the function node: read_file to confirm which specific object within apps/linux_cardputer_zero assumes entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_cardputer_zero/tests/linux_cardputer_zero_business_wiring_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.
 - Drill down: [Candidates for external collaboration: main Technology Hotspot](../../technical-hotspots/collaboration-pressure--main/technical-hotspot.md) - See whether this complexity signal increases the cost of reading, modifying, testing, or regression apps/linux_cardputer_zero.
 - Drill down: [Widely reused candidates: contains technical hotspots](../../technical-hotspots/reuse-pressure--contains/technical-hotspot.md) - See whether this complexity signal increases the cost of reading, modifying, testing, or regression apps/linux_cardputer_zero.

### apps/nrf52_node

- Element type: package
- Description: apps/nrf52_node is the external technical boundary dependency currently observed by boards; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observes a cross-module factual relationship between boards and apps/nrf52_node, so the dependency is put into the Package Diagram instead of just hiding in the code import/call.
- Relationship meaning: boards -> apps/nrf52_node indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill down into apps/nrf52_node to view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
-Business correlation: boards If hosting a user-visible capability, the dependency on apps/nrf52_node may be the running mechanism, extension point, or governance constraint of the capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of apps/nrf52_node may cause chain changes in the boards' call chain, packaging entry, agent workflow or UI behavior.
- Confidence: high
- Evidence:
  - dependency edge: boards -> apps/nrf52_node
  - apps/nrf52_node/APP_SHELL_MANIFEST.md
  - apps/nrf52_node/library.json
  - apps/nrf52_node/README.md
  - apps/nrf52_node/src/nrf52_node_app_facade_runtime.cpp
  - apps/nrf52_node/src/nrf52_node_app_facade_runtime.h
  - apps/nrf52_node/src/nrf52_node_app_runtime_access.cpp
  - apps/nrf52_node/src/nrf52_node_app_runtime_access.h
 - Risk:
 - Cross-module dependencies can only prove technical relationships, not business relationships directly.
 - If this dependency only exists for implementation convenience, future changes may cause boundary drift or implicit public toolboxes.
- Question:
 - There is currently no evidence that boards' dependency on apps/nrf52_node is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: apps/nrf52_node](../apps-nrf52_node/package-diagram.md) - Open apps/nrf52_node's own package-level boundary and check whether boards depend on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.

### apps/linux_uconsole_gtk

- Element type: package
- Description: apps/linux_uconsole_gtk is the external technical boundary dependency currently observed by boards; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observed a cross-module fact relationship between boards and apps/linux_uconsole_gtk, so the dependency was put into the Package Diagram instead of just hiding in the code import/call.
- Relationship meaning: boards -> apps/linux_uconsole_gtk indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill down into apps/linux_uconsole_gtk to view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
- Business correlation: boards If hosting a user-visible capability, the dependency on apps/linux_uconsole_gtk may be the operating mechanism, extension point, or governance constraint of that capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of apps/linux_uconsole_gtk may cause chain changes in the boards' call chain, packaging entry, agent workflow or UI behavior.
- Confidence: high
- Evidence:
  - dependency edge: boards -> apps/linux_uconsole_gtk
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
 - There is currently no evidence that boards' dependency on apps/linux_uconsole_gtk is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: apps/linux_uconsole_gtk](../apps-linux_uconsole_gtk/package-diagram.md) - Open apps/linux_uconsole_gtk's own package-level boundary and check whether boards depend on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.
 - Drill down: [Function node: launchSettingsLayout](../../component-diagrams/apps-linux_uconsole_gtk-launchsettingslayout/component-diagram.md) - Open function node: launchSettingsLayout to confirm apps/linux_uconsole_gtk Which specific internal object is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: makeLabel](../../component-diagrams/apps-linux_uconsole_gtk-makelabel/component-diagram.md) - Open the function node: makeLabel to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_widgets.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: makeSettingsRow](../../component-diagrams/apps-linux_uconsole_gtk-makesettingsrow/component-diagram.md) - Open the function node: makeSettingsRow to confirm apps/linux_uconsole_gtk Which specific internal object is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_settings_layout.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: refreshUi](../../component-diagrams/apps-linux_uconsole_gtk-refreshui/component-diagram.md) - Open the function node: refreshUi to confirm which specific object within apps/linux_uconsole_gtk assumes entry, orchestration, adaptation, contract or shared responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_shell.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: refreshMap](../../component-diagrams/apps-linux_uconsole_gtk-refreshmap/component-diagram.md) - Open the function node: refreshMap to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_logic.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
-Drill down: [Function node: main](../../component-diagrams/apps-linux_uconsole_gtk-main/component-diagram.md) - Open function node: main to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: launchMapLayout](../../component-diagrams/apps-linux_uconsole_gtk-launchmaplayout/component-diagram.md) - Open function node: launchMapLayout to confirm apps/linux_uconsole_gtk Which specific internal object is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/src/platform/gtk/gtk_uconsole_map_layout.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Function node: expect](../../component-diagrams/apps-linux_uconsole_gtk-expect/component-diagram.md) - Open function node: expect to confirm which specific object within apps/linux_uconsole_gtk is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.

### apps/linux_sim_shell

- Element type: package
- Description: apps/linux_sim_shell is the external technical boundary dependency currently observed by boards; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observes a cross-module fact relationship between boards and apps/linux_sim_shell, so the dependency is put into the Package Diagram instead of just hiding in the code import/call.
- Relationship meaning: boards -> apps/linux_sim_shell indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill down into apps/linux_sim_shell to view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
-Business correlation: boards If hosting a user-visible capability, the dependency on apps/linux_sim_shell may be the running mechanism, extension point, or governance constraint of that capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or running mode of apps/linux_sim_shell may cause chain changes in the boards' call chain, packaging entry, agent workflow or UI behavior.
- Confidence: high
- Evidence:
  - dependency edge: boards -> apps/linux_sim_shell
  - apps/linux_sim_shell/APP_SHELL_MANIFEST.md
  - apps/linux_sim_shell/CMakeLists.txt
  - apps/linux_sim_shell/README.md
  - apps/linux_sim_shell/src/linux_sim_app_shell.cpp
  - apps/linux_sim_shell/src/linux_sim_app_shell.h
  - apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.cpp
  - apps/linux_sim_shell/src/linux_sim_runtime_entry_adoption_probe.h
 - Risk:
 - Cross-module dependencies can only prove technical relationships, not business relationships directly.
 - If this dependency only exists for implementation convenience, future changes may cause boundary drift or implicit public toolboxes.
- Question:
 - There is currently no evidence that boards' dependence on apps/linux_sim_shell is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: apps/linux_sim_shell](../apps-linux_sim_shell/package-diagram.md) - Open apps/linux_sim_shell's own package-level boundary and check whether boards depend on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.

### firmware

- Element type: package
- Description: firmware is the external technical boundary dependency currently observed by boards; it indicates that the current module is not implemented in isolation, but requires the help of another set of engineering capabilities to complete its responsibilities.
-Technical role: Cross-module technology dependency boundary: The current package requires another package/module to provide capabilities, contracts, configuration or running support.
- Why it appears: Local repository evidence observes a cross-module factual relationship between boards and firmware, so the dependency is put into the Package Diagram instead of just hidden in the code import/call.
- Relationship meaning: boards -> firmware indicates that the local warehouse evidence observes a cross-module relationship; it explains the technical dependency direction, but does not directly prove the business process.
- Drill-down intention: Drill-down firmware can view its own Package Diagram, and then continue to enter its components, structures, sequences or hotspots to determine whether the current dependency falls on the entry, runtime, tool registration, model adaptation or infrastructure boundary.
-Business correlation: boards If carrying user-visible capabilities, the dependency on firmware may be the operating mechanism, extension point, or governance constraint of the capability. This business association needs to be confirmed by the Use Case evidence of the organization/process model.
- Impact of changes: Modifying the public interface, path or operation mode of the firmware may cause chain changes in the boards' call chain, packaging entry, agent workflow or UI behavior.
- Confidence: high
- Evidence:
  - dependency edge: boards -> firmware
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
 - Current evidence does not demonstrate that boards' dependency on firmware is directly related to a Use Case, runtime command, or configuration decision.
 - The current dependency direction is a candidate according to the warehouse fact record, and no architectural decision document has been found to prove that it is a stable boundary.
 - Drill down: [Module Boundary: firmware](../firmware/package-diagram.md) - Open firmware's own package-level boundary and check whether boards depend on it by borrowing run commands, shared capabilities, governance tools, model adaptation, or infrastructure responsibilities.
-Drill down: [Service Object: main](../../component-diagrams/firmware-main/component-diagram.md) - Open Service Object: main to confirm which specific object within the firmware is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor firmware/c6_companion/tests/test_tm_services_functional.cpp, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
 - Drill down: [Interface component: tm_services_record_error](../../component-diagrams/firmware-tm_services_record_error/component-diagram.md) - Open interface component: tm_services_record_error to confirm which specific object within the firmware is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor firmware/c6_companion/components/tm_services/tm_services.c, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will propagate.

## Drill-down UML

- [Function Node: makeBoardProfile](../../component-diagrams/boards-makeboardprofile/component-diagram.md) - Open the Function Node: makeBoardProfile to confirm which specific object inside the boards is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor boards/t_echo_lite/include/boards/t_echo_lite/board_profile.h, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
- [Function Node: pinNum](../../component-diagrams/boards-pinnum/component-diagram.md) - Opening Function Node: pinNum is to confirm which specific object within boards is responsible for entry, orchestration, adaptation, contract or sharing responsibilities. Focus on checking the code anchor boards/t_echo_lite/include/boards/t_echo_lite/board_profile.h, and whether its referenced/calling relationship and external dependency/calling relationship mean that changes will spread.
- [Structural Collaboration: Structural Slicing boards · gat562_mesh_evb_pro/include/boards](../../class-structural-diagrams/boards-gat562_mesh_evb_pro-include-boards/class-structural-diagram.md) - Open Structural Collaboration: Structural Slicing boards · gat562_mesh_evb_pro/include/boards is to explain boards from an object collaboration perspective: which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps judge that complexity comes from the distribution of responsibilities, not just the number of files.
- [Structural collaboration: Structural slicing boards · t_echo_lite/include/boards](../../class-structural-diagrams/boards-t_echo_lite-include-boards/class-structural-diagram.md) - Open Structural collaboration: Structural slicing boards · t_echo_lite/include/boards is to explain from the perspective of object collaboration boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
- [Structural collaboration: Structural slice boards · tab5/include/boards](../../class-structural-diagrams/boards-tab5-include-boards/class-structural-diagram.md) - Open Structural collaboration: Structural slicing boards · tab5/include/boards is to explain from the perspective of object collaboration boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
- [Structural collaboration: Structural slicing boards · t_display_p4/include/boards](../../class-structural-diagrams/boards-t_display_p4-include-boards/class-structural-diagram.md) - Open Structural collaboration: Structural slicing boards · t_display_p4/include/boards is to explain from the perspective of object collaboration boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
- [Structural collaboration: Structural slicing boards · tlora_pager/include/boards](../../class-structural-diagrams/boards-tlora_pager-include-boards/class-structural-diagram.md) - Open Structural collaboration: Structural slicing boards · tlora_pager/include/boards is to explain from the perspective of object collaboration boards: Which objects are like portals, which are like orchestration cores, and which are like adaptation/sharing boundaries; it helps determine whether complexity comes from the distribution of responsibilities, not just the number of files.
- [Dependency cluster: boards technical hotspots](../../technical-hotspots/dependency-cluster--boards/technical-hotspot.md) - Check whether this complexity signal will increase the cost of reading, modifying, testing or regression of boards.
- [Candidates for external collaboration: boards/tlora_pager/src/tlora_pager_board.cpp Technical Hotspots](../../technical-hotspots/collaboration-pressure--boards-tlora_pager-src-tlora_pager_board-cpp/technical-hotspot.md) - View Candidates for external collaboration: boards/tlora_pager/src/tlora_pager_board.cpp Technical Hotspot Whether this complexity signal will increase the cost of reading, modifying, testing or regression of boards.

## Evidence

- boards/cardputerzero/board_facts.h
- boards/cardputerzero/BOARD.md
- boards/gat562_mesh_evb_pro.json
- boards/gat562_mesh_evb_pro/board_facts.h
- boards/gat562_mesh_evb_pro/BOARD.md
- boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/board_profile.h
- boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/gat562_board.h
- boards/gat562_mesh_evb_pro/include/boards/gat562_mesh_evb_pro/gps_runtime.h

## Problem

- There are no open issues yet.

## Change record

### 0.1.30-alpha - 2026-06-25T09:19:20.669Z

- Generate module boundaries from local repository evidence: boards.
