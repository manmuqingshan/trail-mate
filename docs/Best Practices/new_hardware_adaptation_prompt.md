# New hardware adaptation development specifications (Trail-Mate)

This document is not a one-time prompt memo, but an engineering specification that Trail-Mate must abide by when adapting to new hardware in the future.

The goal is not to "run first", but:

1. New hardware can be continuously connected;
2. Old hardware will not be destroyed;
3. Capability boundaries will be clear for a long time;
4. Business logic will not be copied everywhere because of changing boards;
5. Anyone who takes over in the future can quickly determine which layer the code should be written on.

---

## 1. Core design principles

All subsequent new hardware connections must meet the following principles at the same time.

### 1.1 Single responsibility

- `modules/` is responsible for reusable business capabilities, shared strategies, shared controllers, and shared protocol cores.
- `platform/` is responsible for chip/platform related capability adaptation, such as ESP, nRF52, BLE, LoRa, file system, system clock, and platform bridging.
- `boards/` is responsible for specific hardware board differences, board-level pins, board-level device combinations, and board-level profiles.
- `apps/` is responsible for the runtime assembly of specific products/devices. It only does startup / loop / assemble and does not hold reusable business assets.
- `variants/` is responsible for the compilation environment, macros, parameters, `build_src_filter`, and target board environment isolation.

### 1.2 Opening and Closing Principle

- New hardware access is first completed through "new implementation" instead of modifying the behavior of the old board.
- What can be solved by adding `board` / `env` / `bridge` / `provider` should not pollute the existing business core.
- When modifying old code, only the following are allowed:
 - Abstraction lifting;
 - Repeated logic convergence;
 - Compatibility fixes;
 - Naming clarification.

### 1.3 Prioritize capacity consumption and prohibit direct connection of upper layers to board-level details

- The upper layers can only rely on abstract capabilities and cannot rely on specific board types, specific driver instances, and specific pins.
- It is forbidden to write board judgment in `apps/`, `modules/`, UI controller, chat service.
- It is forbidden to use `dynamic_cast<specific board class*>` as the main path.

### 1.4 Configuring a single source of truth

- Board-level parameters come from `variants/*/envs/*.ini` and `boards/*/board_profile.*`.
- Runtime settings come from settings runtime/app config/platform config owner.
- The same configuration is not allowed to store a set of "true values" at multiple levels.

### 1.5 Shared logic must be converged to `modules/`

Anyone that meets any of the following conditions should not remain in the app or platform layer:

- Both ESP and nRF52 will be used;
- The same protocol will be reused on multiple devices;
- The same UI controller will be reused by multiple devices;
- The same identity / announcement / settings / protocol rules will be reused across environments.

---

## 2. Directory hierarchy specification

This is the directory semantics that the current project will abide by for a long time.

## 2.1 `modules/`

`modules/` is the home of shared services and shared capabilities.

Suitable content for `modules/`:

- Protocol core;
- Shared controller;
- Shared page model;
- Shared UI rendering abstraction;
- identity policy;
- self-announcement policy;
- team/chat/contact/core runtime;
- Display abstraction that does not depend on specific boards;
- Business state machine that does not depend on specific platforms.

Contents that should not be placed in `modules/`:

-Specific pin;
-Specific SPI/I2C/UART instance;
-Button number of a certain board;
-OLED address of a certain board;
-Battery of a certain board ADC conversion;
- RadioLib initialization sequence for a certain board.

### Currently confirmed shared module direction

- `modules/core_chat`
  - identity policy
 - self identity provider abstraction
  - self announcement core
 - Meshtastic / MeshCore self-declared core
- `modules/ui_mono_128x64`
 - 128x64 monochrome screen sharing UI/controller/page/display abstraction

## 2.2 `platform/`

`platform/` It is not the "specific device layer", but the "platform/chip/system capability adaptation layer".

For example:

- `platform/esp/*`
- `platform/nrf52/*`

Content suitable for `platform/`:

- BLE runtime;
- LoRa runtime;
- platform-specific identity bridge;
- platform message pump;
- platform file system encapsulation;
- platform radio context;
- platform protocol backend adaptation;
- Platform-side bridging of shared modules.

Contents not suitable for `platform/`:

- The product UI asset ontology of a specific board;
- The exclusive business menu structure of a specific device;
- The startup copy of a certain board;
- Specific product assembly logic.

## 2.3 `boards/`

`boards/` is the specific hardware board layer.

Content suitable for `boards/`:

- `board_profile.h`
- PinMap / BleProfile / LoraProfile / InputProfile / BatteryProfile
- Board-level binding
- Board-level capability combination
- Device runtime bridge of a certain board

The naming must reflect the real board, not an ambiguous abbreviation.

Recommended:

- `boards/gat562_mesh_evb_pro`

Not recommended:

- `boards/gat562`

Reason:

- `gat562` is more like a series name than a specific board name;
- It will be confusing when multiple boards of the same series coexist in the future;
- The directory name must correspond to the real hardware adaptation object.

## 2.4 `apps/`

`apps/` is the assembly layer for specific products/devices to run.

Content suitable for `apps/`:

- startup runtime
- loop runtime
- app context / runtime assembly
- Instance assembly of provider / port
- Life cycle trigger
- Final wiring between modules

Content not suitable for `apps/`:

- Protocol package assembly rules;
- Identity fallback rules;
- Shared UI asset ontology;
- Meshtastic / MeshCore self-declared business core;
- General settings controller logic;
- General screensaver page logic.

One sentence:

- `apps/` is only responsible for "putting together existing modules" and is not responsible for "reinventing modules".

## 2.5 `variants/`

`variants/` is only responsible for environment and compilation isolation.

Content that must be placed here:

- `build_flags`
- `build_src_filter`
- board macro
- screen size
-Board-level include path
-Target environment name

It is forbidden to scatter these configurations into the code to make default values.

For example, screen size:

- Correct: Use `-DSCREEN_WIDTH=128 -DSCREEN_HEIGHT=64` in env
- Wrong: Secretly default to `128x64` in the code

---

## 3. Recommended design patterns

The following patterns have been verified to be effective in this project, and will be used first for subsequent hardware adaptation.

## 3.1 Provider mode

Applicable scenarios:

- A certain type of shared logic needs to read the "current runtime status", but should not rely on a certain app / board / platform details.

Typical example:

- `SelfIdentityProvider`

Responsibilities:

- Only provide data;
- No business;
- No persistence;
- No UI;
- No protocol contracting.

Data suitable for provider:

- Current node id
- Current configured long/short name
- Current default prefix
- Current BLE default name
- Current active mesh config

## 3.2 Port + Core mode

Applicable scenarios:

- Shared business core needs to call platform capabilities, but should not directly rely on hardware.

Typical example:

- `MeshtasticSelfAnnouncementCore` + `MeshtasticSelfAnnouncementPort`
- `MeshCoreSelfAnnouncementCore` + `MeshCoreSelfAnnouncementPort`

Specification:

- `Core` is placed in `modules/`
- The `Port` interface is placed in `modules/`
- The `Port` implementation is placed in `apps/` or `platform/`
- `Core` must not include specific board driver header files

## 3.3 Bridge mode

Applicable scenarios:

- There are still multiple runtimes inside the platform layer Need to read the same type of platform status;
- But you don't want all runtime to depend directly on settings/runtime details.

Typical example:

- `platform/nrf52/self_identity_bridge`

Responsibilities:

- Unified exposure platform internal identity reading interface;
- Isolate `settings_runtime` details;
- Used by platform runtimes such as BLE, radio context, host config, etc.

## 3.4 Composition Root mode

Applicable scenarios:

- An environment needs to ultimately decide "what implementation to install, what provider to connect, and what controller to use."

Typical positions:

- `apps/esp_pio`
- `apps/gat562_mesh_evb_pro`

Responsibilities:

- Only assembly, not wheel building.

## 3.5 Adapter mode

Applicable scenarios:

- The platform already has runtime / backend / driver interface, which is inconsistent with the shared module interface.

For example:

- Platform radio context bridging shared mesh adapter;
- Board-level OLED driver bridging `MonoDisplay`.

---

## 4. The currently established architecture conclusion

## 4.1 Identity / Self-Announcement

The final ownership of this link is now clear:

- `modules/core_chat`
  - `SelfIdentityPolicy`
  - `SelfIdentityProvider`
  - `SelfAnnouncementCore`
  - `MeshtasticSelfAnnouncementCore`
  - `MeshCoreSelfAnnouncementCore`
- `platform/nrf52/settings_runtime`
 - nRF52 local identity configuration true value
- `platform/nrf52/self_identity_bridge`
 - nRF52 platform unified identity read bridge
- `apps/esp_pio`
 - ESP provider assembly
- `apps/gat562_mesh_evb_pro`
 - GAT562 provider Assembled with self-declaring port

Conclusion:

- Identity fallback rules cannot be written in the app;
- Protocol self-announcement rules cannot be written in the app;
- App can only provide provider and port;
- Platform can only provide bridge and runtime adaptation.

## 4.2 UI

Attribution rules for monochrome 128x64 OLED UI:

- Display abstraction, controller, page, flow in `modules/ui_mono_128x64`
- `platform` only retains screen backend / runtime adaptation
- `boards` provides board-level display profile / pin / wiring
- `apps` only assembles UI runtime

Conclusion:

- "Screen saver page/main menu page/settings page business controller" does not belong to app
- The app only decides:
 - Whether to enable this UI
 - What to display backend
 - What data provider to use

## 4.3 Meshtastic / MeshCore

The protocol rules must be returned to shared modules as much as possible:

- Protocol packet shaping
- NodeInfo assembly
- peer/event/payload processing
- identity derivation

The platform layer only does:

- radio transport
- protocol backend runtime
- driver / filesystem / BLE / UART / crypto wiring

---

## 5. Standard process for new hardware adaptation

Any subsequent new board connection will be done in this order.

## Step 1: Do a structural review first

Answer 4 questions before changing the code:

1. Is this a new board problem or a shared module boundary problem?
2. Will this logic be reused by the second board in the future?
3. Does this logic belong to `modules`, `platform`, `boards` or `apps`?
4. Will this modification cause the behavior of the old board to drift?

If the answer to 2 is "will be reused", enter `modules/` first.

## Step 2: Add new environment without polluting the old environment

Must add first:

- `variants/<board>/envs/<board>.ini`

Must be explicitly configured:

- board
- `build_flags`
- `build_src_filter`
- variant include path

Must ensure:

- The new board environment only compiles the source files required for the new board;
- The old board environment does not compile the new board source files by mistake.

## Step 3: Complete board profile

Board-level profile must be completed in `boards/<board>/`:

- PinMap
- InputProfile
- BleProfile
- LoraProfile
- BatteryProfile
- AudioProfile

Rules:

- Board-level parameters are not allowed to be scattered in runtime `.cpp`;
- Runtime can only reference board profile;
- board profile does not make business decisions.

## Step 4: Connect platform bridge first, then app

If a certain capability will be read by multiple runtimes:

- Make bridge first
- Then let BLE / radio / settings / host config share

Do not directly read settings / config true values ​​in multiple runtimes.

## Step 5: app only does assemble

Only allowed in `apps/<device>/`:

- provider implementation
- port implementation
- runtime startup / loop
- controller assembly

Not allowed:

- Business protocol core;
- Universal controller;
- Universal page;
- Reusable state machine.

## Step 6: Dual environment regression compilation

All shared transformations must at least verify:

-Affected ESP environments;
-Affected nRF52 environments.

Current minimum regression standard:

- `platformio run -e tdeck`
- `platformio run -e gat562_mesh_evb_pro`

---

## 6. Naming convention

## 6.1 Directory naming

-Board-level directories must use real board names:
  - `boards/gat562_mesh_evb_pro`
- It is forbidden to use ambiguous series names as specific board directories:
 - `boards/gat562` is not recommended

## 6.2 API naming

The naming must reflect semantics and not be ambiguous.

Recommended:

- `applyUserIdentity`
- `fillSelfIdentityPolicyArgs`
- `getEffectiveUserInfo`
- `buildBleVisibleName`
- `broadcast(...)`

Not recommended:

- `setUserInfo` is used to express "persistence + apply + runtime takes effect"

Note:

- `setXxx` is more like a simple setter;
- If there are persistence, broadcast, and side effects, verbs such as `apply` / `persist` / `broadcast` / `refresh` should be used.

## 6.3 File Naming

Sharing policy files should directly reflect responsibilities:

- `self_identity_policy.*`
- `self_identity_provider.*`
- `self_announcement_core.*`
- `meshtastic_self_announcement_core.*`
- `meshcore_self_announcement_core.*`

Do not use:

- `helper2`
- `runtime_misc`
- `temp_adapter`
- `stage_*`

---

## 7. Anti-pattern list

The following practices will all be considered as requiring refactoring in the future.

## 7.1 Sharing business logic in the app

For example:

- Create your own Meshtastic `PhoneUserArgs` in the app
- Write the MeshCore NodeInfo package yourself in the app
- Write the identity fallback yourself in the app

These should be returned to `modules/`.

## 7.2 Read the true value of settings in multiple runtimes

For example:

- BLE runtime reads user_name by itself
- Read the radio context yourself short_name
- Read the host config yourself user_name

Correct approach:

- Expose via bridge / provider.

## 7.3 Use "compatible temporary code" long-term placeholder

Short-term compatible packaging is allowed, but must meet:

- Only used as a migration bridge;
- No duplicate logic added;
- Can be cleaned up later;
- The name shows that it is a compatibility layer.

If the compatibility layer starts to carry new logic, it means the boundary is wrong.

## 7.4 Hold product UI assets in the platform layer

For example:

- Screen saver page copy
- Main menu structure
- Settings page item definition

This type of content should be placed in the shared UI module or app assembly layer, and should not be buried in the platform runtime.

## 7.5 compilation environment is not isolated

If any of the following situations occurs, it means that the env design is unqualified:

- ESP environment miscompiles nRF52 `src/*.cpp`
- New board environment miscompiles old board board files
- Use `#ifdef` to hide environment boundary errors everywhere

Must be corrected first `build_src_filter`.

---

## 8. Output requirements when adapting to new hardware

When a new hardware adaptation or architecture transformation is completed, the output must include:

1. Access policy summary
2. Actual modified file list
3. Hierarchical ownership description
4. Why it complies with the decoupling principle
5. Compilation verification results
6. Subsequent legacy items

If shared boundary adjustment is involved, it must also be clearly stated:

- Which logic has been moved from app to modules
- Which logic has been converged from settings/runtime to bridge/provider
- Which compatible entries are still retained and when they will be cleaned up in the future

---

## 9. Pre-submission checklist

- [ ] New logic is placed at the correct level
- [ ] Shared logic enters `modules/`
- [ ] The platform reads the true value uniformly through bridge/provider
- [ ] The app only does assembly and does not hold the shared business core
- [ ] There is no new board to judge the pollution of the upper module
- [ ] No temporary cover is used to cover up the missing env configuration
- [ ] Affected ESP environment compilation passed
- [ ] Affected nRF52 environment compilation passed
- [ ] Document synchronization update

---

## 10. Long-term implementation conclusion of the current project

To Trail-Mate Generally speaking, when adapting to more hardware in the future, you should stick to the following project structure for a long time:

- `modules/`: shared business, shared protocol, shared UI, shared strategy
- `platform/`: platform runtime and bridge profile
- `boards/`: specific board cards Bound with board-level capabilities
- `apps/`: specific device runtime assembly
- `variants/`: compilation environment and parameter source of fact

If the ownership of a certain piece of code is uncertain, judge in the following order:

1. Will both platforms be reused? If yes, enter `modules/`
2. Is this a difference in platform capabilities? If yes, go to `platform/`
3. Is this a specific board pin/device difference? If yes, go to `boards/`
4. Is this device-level startup/loop/assembly? If yes, go to `apps/`
5. Is this compilation isolation and parameters? Enter `variants/`

This is the standard answer for subsequent new hardware adaptation.
---

## 11. Board Class and bus lock mandatory constraints (new)

The following rules must be implemented for a long time and cannot be missed again:

### 11.1 `board_profile` is not a replacement for the `Board` class

- Each specific hardware directory must provide a real board-level class, at least inheriting `BoardBase`
- `board_profile.*` Only responsible for static pins, capability boundaries, device addresses, hardware constants
- `board_profile.*` is not responsible for carrying dynamic behaviors such as power management, brightness, battery, RTC, prompts, bus coordination, etc.
- The facade/runtime in `apps/<device>` cannot return empty `getBoard()` for a long time

### 11.2 Board-level capabilities must be unified into the real `Board` class

- GPIO initialization, peripheral power-on, LED, brightness, battery, RTC, input device, prompt sound and other board-level capabilities should be unified into the specific `Board` class first
- The `platform` layer can retain runtime / bridge / adapter, but the same board-level truth value cannot be dispersed in multiple runtimes for long-term maintenance
- `apps` is only responsible for assembly, not directly responsible for board-level peripheral arrangement

### 11.3 Shared `I2C` / `SPI` bus must be explicitly locked

- As long as there are multiple ICs on one board sharing the same `I2C` or `SPI` bus, you must provide a unified bus coordinator
- At least explicit `lock()/unlock()` or RAII guard must be provided, and "single-threaded so no conflict" cannot be assumed by default
- OLED, RTC, PMU, keyboard controller, touch, sensor, audio codec and other collinear devices must use the same bus entrance
- It is forbidden to directly use each other in multiple runtimes `Wire.begin()`, `Wire.setClock()`, `Wire` read and write without unified lock

### 11.4 Board-level check items for new board adaptation

- Whether the real `Board` class has been completed, instead of only `board_profile`
- `getBoard()` Whether to return the real instance
- Whether the unified `I2C` entrance and guard have been established
- Display whether the driver accesses `Wire` through the shared bus coordinator
- When adding RTC / PMU / sensor / keyboard, whether to continue to reuse the same bus lock

### 11.5 Reference implementation requirements

- The board-level implementation of `tdeck` and `pager` is the current reference style
- Before adapting to the new board, you must first check the `BoardBase` closing method and bus lock strategy of the reference board
- If the new board lacks these layers, first supplement the board-level abstraction and bus coordination, and then continue to stack business capabilities

## 12. BLE / LoRa / Settings boundary enforcement constraints (new)

- `boards/<board>/` is responsible for the board-level hardware owner: pins, power gating, bus instances, shared bus locks, and specific peripheral object life cycles.
- `platform/<chip>/` is responsible for a truly reusable platform stack: such as shared `BleManager`, shared BLE service, shared radio transport interface, and platform bridge.
- `apps/<device>/` is only responsible for the composition root: connect the board owner, platform manager, and shared modules, and do not copy the board-specific BLE/LoRa/settings runtime.
- If two boards can share BLE logic, the prerequisite must be "platform stack sharing, board-level differences have been absorbed by the board owner, and the app only does wiring"; "surface sharing" cannot be achieved by copying the manager in the app.
- The true value of `settings` can only have one owner; when multiple runtimes need to be read, they must first be converged through bridge/provider and then reused by BLE/radio/UI/host config.
- If the board-level difference is only product parameters such as "default name/fallback prefix/hardware display name/default broadcast name", they must be injected by the app or board. It is prohibited to hard-code board names such as `GAT562` in shared module or platform shared runtime.
