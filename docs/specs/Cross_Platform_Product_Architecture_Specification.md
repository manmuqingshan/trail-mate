# Cross-Platform Product Architecture Specification

```text
docs/specification/CROSS_PLATFORM_PRODUCT_ARCHITECTURE_SPEC.md
```

# 1. Core problem redefined

Trail Mate is not a single firmware, nor is it a single Linux app. It is a cross-platform outdoor communication/navigation product family.

The variants it faces are not one-dimensional.

```text
ESP32
  - T-LoRa Pager
  - T-Deck
  - T-Deck Pro
  - T-Watch S3
  - Cardputer Zero
 - Other SX1262 / SX127x / GPS / screen combination

nRF52
  - GAT562
  - RNode-like endpoint
  - BLE phone host
  - LoRa coprocessor
 - Independent low-power Mesh node

Linux
  - uConsole AIO2
  - RPi
  - Cardputer Zero Linux shell
  - desktop simulator
  - headless daemon
```

At the same time, the UI is not the same:

```text
- LVGL embedded UI
- ASCII / TUI UI
- GTK desktop-class UI
- CLI
- Headless daemon
- Web / remote control future shell
```

So the goal of the specification is not as simple as "dividing the code into core/platform/app", but to ensure that:

```text
The same business semantics will not drift due to the board, chip, OS, UI technology stack, and scheduling model.
```

------

# 2. Total layered model

The complete architecture should be:

```text
Target Manifest
    ↓
Product Composition
    ↓
App Services
    ↓
Presentation Models
    ↓
Shell / Renderer
```

Parallel support layer:

```text
Board Package
Platform Runtime
Capability Drivers
Protocol Cores
Storage Backends
Concurrency Policy
```

More specific:

```text
docs/targets/*.yaml
 Declare product goals

boards/*
platform/*/boards/*
 Declare hardware facts

platform/*
 Provide platform capability adaptation

modules/core_*
 Provide shared areas, protocols, use cases, performance models

apps/*
 Combine target products

shells or apps/*/ui/*
 Specific UI technology stack
```

------

# 3. The triple distinction of Target / Board / Platform must be introduced

We mixed them before:

```text
ESP32 target
ESP32 platform
ESP32 board
```

These three cannot be mixed anymore.

------

## 3.1 Platform

Platform is the chip/OS/running environment.

For example:

```text
esp32
nrf52
linux
test
simulator
```

Platform decision:

```text
- Available SDK
- Thread/task model
- ISR model
- Storage API
- Random number API
- Time API
- BLE stack
- Filesystem capabilities
- Build toolchain
```

Platform does not decide:

```text
- Which LoRa chip is connected to which pin
- Which screen resolution
- Product UI shape
- What features users see
```

------

## 3.2 Board Variant

Boards are specific hardware variants.

For example:

```text
t_lora_pager
t_deck
t_deck_pro
t_watch_s3
gat562_mesh_evb_pro
uconsole_aio2
cardputer_zero
```

Board determines the hardware facts:

```text
- LoRa chip model
- LoRa SPI bus / reset / busy / dio1 / tx_en
- GPS UART / power pin / PPS
- display driver / resolution / rotation
- input device
- battery / charger / PMU
- SD card / filesystem
- BLE antenna / coexistence constraints
```

Board does not decide:

```text
- How to send direct message
- How to respond to Meshtastic BLE
- How to interpret MeshCore command
- How to store messages in ChatService
- How to lay out UI page
```

------

## 3.3 Target

Target is the product component.

For example:

```text
trailmate-tpager-esp32
trailmate-tdeck-esp32
trailmate-gat562-nrf52-node
trailmate-uconsole-aio2-linux
trailmate-uconsole-nrf52-radio-proxy
trailmate-linux-headless
trailmate-linux-ascii
trailmate-linux-gtk
trailmate-esp32-lvgl
```

Target determines:

```text
- Which platform to use
- Which board to use
- Which capabilities are enabled
- Where is the state authority?
- Which UI shell to use
- Which storage backend to use
- Which concurrency policy to use
- Which protocol profile to use
```

Target must have a manifest.

------

# 4. Target Manifest must be upgraded

The previous manifest only covered capability/authority. Now to cover:

```text
platform
board
runtime
concurrency
ui_shell
renderer
presentation_model
capabilities
protocols
authorities
storage
build features
race policy
```

Suggested template:

```yaml
target: "trailmate-tpager-esp32"
status: "draft"
updated: "2026-05-12"

product:
  family: "trail-mate"
  form_factor: "handheld | watch | linux_handheld | headless | simulator"
  interaction_class: "embedded_lvgl | ascii_tui | desktop_gtk | cli | headless"

platform:
  kind: "esp32 | nrf52 | linux | test"
  sdk: "arduino_esp32 | esp_idf | zephyr | arduino_nrf52 | linux_posix"
  language_profile: "embedded_cpp | linux_cpp"
  memory_tier: "tiny | small | medium | large"
  filesystem: "none | littlefs | sd | posix"
  dynamic_allocation: "forbidden | limited | allowed"

board:
  id: "t_lora_pager"
  package: "platform/esp/boards/t_lora_pager"
  display:
    present: true
    width: 222
    height: 480
    rotation: 90
    driver: "..."
  input:
    keyboard: false
    buttons: true
    touch: false
    encoder: false
    trackball: false
  lora:
    present: true
    chip: "sx1262"
    owner: "board_facts_only"
  gps:
    present: true
    owner: "board_facts_only"
  power:
    battery: true
    charger: true
    deep_sleep: true

runtime:
  scheduler: "freertos | zephyr_workqueue | linux_event_loop | single_thread_tick | test_fake"
  ui_thread: "main_task | dedicated_task | gtk_main | terminal_loop | none"
  radio_thread: "task | workqueue | event_loop | poll_tick"
  gps_thread: "task | workqueue | event_loop | poll_tick"
  ble_thread: "stack_callback | task | event_loop | none"
  isr_policy: "defer_to_queue"
  lock_policy: "single_owner | mutex | message_passing | immutable_snapshot"

ui:
  shell: "lvgl | ascii | gtk | cli | headless"
  renderer: "lvgl | ascii_canvas | gtk4 | stdout | none"
  presentation_model_host: "esp32 | nrf52 | linux | none"
  layout_profile: "compact_handheld | watch | desktop_workbench | terminal_panel | none"
  input_model: "buttons | keyboard | touch | mixed | none"

execution:
  app_host: "esp32 | nrf52 | linux | none"
  mesh_core_host: "esp32 | nrf52 | linux | none"
  gps_core_host: "esp32 | nrf52 | linux | none"
  phone_core_host: "esp32 | nrf52 | linux | none"
  hostlink_core_host: "esp32 | nrf52 | linux | none"
  ui_host: "esp32 | nrf52 | linux | none"

authorities:
  identity: "esp32 | nrf52 | linux | none"
  peer_key_store: "esp32 | nrf52 | linux | none"
  node_store: "esp32 | nrf52 | linux | none"
  message_store: "esp32 | nrf52 | linux | none"
  location: "esp32 | nrf52 | linux | none"
  time: "esp32 | nrf52 | linux | none"
  config: "esp32 | nrf52 | linux | none"
  device_status: "esp32 | nrf52 | linux | none"
  ui_state: "esp32 | nrf52 | linux | none"

capabilities:
  lora:
    state: "unsupported | absent | present | unbound | ready | degraded | simulated | error"
    endpoint_host: "esp32 | nrf52 | linux | external | none"
    mode: "local_radio | packet_proxy | command_proxy | none"
  gps:
    state: "unsupported | absent | present | unbound | ready | degraded | simulated | error"
    endpoint_host: "esp32 | nrf52 | linux | external | none"
    mode: "local_uart | raw_stream_proxy | fix_proxy | command_proxy | simulated | none"
  ble:
    state: "unsupported | absent | present | unbound | ready | degraded | simulated | error"
    endpoint_host: "esp32 | nrf52 | linux | none"
    stack: "nimble | bluefruit | bluez | none"
    roles:
      meshtastic_phone: "server | client | disabled | unsupported"
      meshcore_phone: "server | client | disabled | unsupported"
      trailmate_control: "server | client | disabled | unsupported"

protocols:
  meshtastic:
    enabled: true
    radio_profile_source: "shared_core"
    ble_phone_core: "shared_core | disabled"
  meshcore:
    enabled: true
    radio_profile_source: "shared_core"
    ble_phone_core: "shared_core | disabled"
  hostlink:
    enabled: true
    frame_core: "shared_core"

storage:
  backend: "esp_nvs | nrf52_flash | sqlite | file | fake | none"
  identity: "..."
  peer_keys: "..."
  config: "..."
  messages: "..."
  node_store: "..."

race_policy:
  app_to_ui: "snapshot"
  radio_to_app: "event_queue"
  gps_to_app: "event_queue"
  ble_to_app: "command_queue"
  isr_to_driver: "defer_only"
  store_access: "single_owner_or_mutex"
  ui_update: "ui_thread_only"

build:
  app: "apps/..."
  board_package: "platform/.../boards/..."
  enabled_modules:
    - "core_chat"
    - "core_mesh"
    - "core_gps"
    - "core_phone"
    - "core_device"
    - "ui_presentation"
  disabled_modules: []
```

------

# 5. Board Package specification

A new board level specification must be added:

```text
docs/specification/BOARD_PACKAGE_SPEC.md
```

## 5.1 Board Package Responsibilities

Board package only has hardware facts and board level bring-up.

Allow:

```text
- pin map
- bus map
- power rails
- display facts
- input facts
- battery facts
- radio hardware facts
- gps hardware facts
- board init
- board sleep/wake hooks
```

Forbidden:

```text
- ChatService
- ContactService
- DirectMessageService
- MeshtasticPhoneCore
- MeshCorePhoneCore
- ConfigService business explanation
- UI page status
- BLE phone protocol
- GPS business policy
- LoRa packet semantics
```

## 5.2 Board Package structure

```text
platform/
  esp/
    boards/
      t_lora_pager/
        board_manifest.yaml
        include/board/t_lora_pager_board.h
        src/t_lora_pager_board.cpp
        include/board/t_lora_pager_pins.h

      t_deck/
      t_watch_s3/

  nrf52/
    boards/
      gat562_mesh_evb_pro/
        board_manifest.yaml
        include/board/gat562_board.h
        src/gat562_board.cpp

  linux/
    boards/
      uconsole_aio2/
        board_manifest.yaml
        include/board/uconsole_aio2_facts.h
        src/uconsole_aio2_facts.cpp
```

## 5.3 Board Manifest

Each board must have:

```yaml
board: "t_lora_pager"
platform: "esp32"

facts:
  display:
    present: true
    width: 222
    height: 480
    rotation: 90
    driver: "st7789"
  lora:
    present: true
    chip: "sx1262"
    spi_bus: "spi2"
    reset_pin: 8
    busy_pin: 7
    dio1_pin: 33
    power_enable_pin: null
    dio2_as_rf_switch: true
    dio3_tcxo_voltage: 1.8
  gps:
    present: true
    uart: "uart1"
    power_enable_pin: 21
    pps_pin: null
  input:
    buttons: true
    keyboard: false
    touch: false
  storage:
    sd: true
    internal_flash: true
  power:
    battery: true
    charger: true
```

This is different from the target manifest:

```text
board_manifest.yaml explains what the hardware has.
target_manifest.yaml describes how the product uses these hardware.
```

------

# 6. Runtime / Concurrency specifications

This is the key point you just pointed out: different ESP32 boards, nRF52, and Linux will face different race conditions.

Must add:

```text
docs/specification/RUNTIME_CONCURRENCY_SPEC.md
```

## 6.1 Race sources

Trail Mate has at least these concurrent sources:

```text
- Radio IRQ
- Radio RX task / poll loop
- Radio TX completion
- GPS UART RX
- GPS parser task
- BLE stack callback
- BLE notify queue
- UI event loop
- LVGL tick / input
- GTK main loop
- ASCII terminal input loop
- HostLink USB/serial RX
- Storage write
- Config update
- Power/sleep/wake
- Timer / retry / ACK timeout
```

If these are not standardized, they will lead to:

```text
- UI is updated in non-UI thread
- BLE callback directly changes ChatService
- radio IRQ directly triggers business
- GPS parser and UI read and write location at the same time
- Concurrent config update and radio apply
- radio/gps/ble when sleeping Still accessing hardware
- SQLite/NVS/flash concurrent writing
```

------

## 6.2 General rules: all external inputs are converted into events

Forbidden:

```text
ISR -> direct business
BLE callback -> direct business
UART callback -> direct business
UI callback -> Directly change the underlying driver
```

Required:

```text
ISR / callback / transport
    -> event queue / command queue
        -> app service owner
            -> state update
                -> presentation snapshot
                    -> UI thread render
```

------

## 6.3 Single owner rule

Each mutable service must have an owner context.

```text
ChatService owner: app service context
MeshSession owner: mesh context
GpsService owner: gps context
ConfigService owner: app service context
DeviceStatus owner: device service context
UI State owner: UI context
```

Other threads/tasks cannot be modified directly and can only deliver command/event.

------

## 6.4 Snapshot Rules

UI should not read mutable service internal objects directly.

Should:

```text
App Service
    -> immutable snapshot / projection
        -> Presentation Model
            -> Shell Renderer
```

For example:

```cpp
struct ChatListSnapshot {
    ConversationRow rows[32];
    size_t count;
    uint32_t version;
};
```

Render the UI after obtaining the snapshot.
Continuing updates in the background will not destroy the UI.

------

## 6.5 UI Thread Only Rules

Different UI technology stacks must comply with "UI can only be updated in its own UI context".

### LVGL

```text
- lv_obj_* can only be called in LVGL owner task/thread
- Other tasks can only deliver UI commands
- The LVGL object cannot be changed directly from BLE/radio/GPS callback
```

### GTK

```text
- GTK widget can only be called in GTK main loop Update
- The background thread delivers updates through idle/source/channel
```

### ASCII / TUI

```text
- The terminal buffer has a single renderer owner
- The input thread and the refresh thread cannot write stdout at the same time
- Background events only update the model or submit render request
```

### Headless

```text
- There is no UI thread
- Only export snapshot / logs / API
```

------

## 6.6 ISR Rules

ISR can only:

```text
- Clear interrupt
- Record minimum flag
- Deliver lightweight event
```

ISR prohibits:

```text
- malloc/free
- storage write
- BLE notify
- UI update
- protobuf encode/decode
- direct message send
- GPS parse
```

------

## 6.7 Storage Concurrency Rules

Storage backend must declare:

```text
- single writer
- multi reader
- transaction support
- async write support
- erase/write blocking behavior
```

ESP32 NVS, nRF52 flash, SQLite are completely different:

```text
ESP32 NVS:
  blocking, limited write endurance, usually mutex protected

nRF52 flash:
  erase/write expensive, often async, must avoid callback/context conflict

SQLite:
  transaction, file lock, can support stronger consistency but must avoid UI thread blocking
```

So the store port cannot be just:

```cpp
bool save(...)
```

At least:

```cpp
StoreResult
StoreCapabilities
StoreConcurrencyPolicy
```

------

# 7. The UI architecture must be split into Presentation Model + Renderer

What you said ASCII/LVGL/GTK is key.
The business layer cannot be allowed to organize state for LVGL, nor can GTK be allowed to reuse LVGL pages.

Must add:

```text
docs/specification/UI_PRESENTATION_ARCHITECTURE_SPEC.md
```

------

## 7.1 Three-layer UI

```text
App Service
 Business status and actions

Presentation Model
 UI-independent page/workspace status and actions

Shell / Renderer
    LVGL / ASCII / GTK / CLI / Headless
```

------

## 7.2 App Service

For example:

```text
ChatService
ContactService
TeamService
LocationService
MeshService
ConfigService
DeviceStatusService
```

Not allowed to know:

```text
LVGL
GTK
terminal
screen size
font
button
layout
```

------

## 7.3 Presentation Model

The Presentation Model is responsible for turning the App Service into a "presentable state".

For example:

```text
ChatWorkspaceModel
MapWorkspaceModel
SettingsWorkspaceModel
DeviceStatusModel
GpsStatusModel
MeshStatusModel
```

It can know:

```text
- Which conversation is currently selected
- The abstract cursor of the current list scroll position
- Actions of the current workspace
- Field formatting
- Row/column/panel concepts
- Compact/desktop/terminal presentation profile
```

But you cannot know:

```text
lv_obj_t
GtkWidget
ncurses WINDOW
stdout
framebuffer
```

------

## 7.4 Renderer / Shell

Renderer only knows the specific technology.

```text
LVGL renderer:
  ChatWorkspaceModel -> lv_obj_t tree

ASCII renderer:
  ChatWorkspaceModel -> text grid / ANSI escape

GTK renderer:
  ChatWorkspaceModel -> GtkWidget tree

CLI renderer:
  ChatWorkspaceModel -> command output
```

------

## 7.5 UI technology stack does not allow business operations

Forbidden:

```text
Message dedup is directly implemented in lvgl_chat_page.cpp
Gtk_map_page.cpp is used to directly parse GPS NMEA
ascii_mesh_view.cpp is used to directly read peer key store
```

Allow:

```text
Renderer calls presentation model action:
  sendMessage()
  selectContact()
  toggleLayer()
  applyConfigPatch()
```

------

# 8. UI Target Profile

Different UIs should have profiles instead of relying on ifdef to judge everywhere.

```yaml
ui:
  shell: "lvgl"
  renderer: "lvgl"
  layout_profile: "compact_handheld"
  screen:
    width: 222
    height: 480
    density: "small"
  input_model:
    primary: "buttons"
    text_input: "limited"
```

ASCII:

```yaml
ui:
  shell: "ascii"
  renderer: "ascii_canvas"
  layout_profile: "terminal_panel"
  terminal:
    min_columns: 80
    min_rows: 24
    color: "ansi"
  input_model:
    primary: "keyboard"
```

GTK:

```yaml
ui:
  shell: "gtk"
  renderer: "gtk4"
  layout_profile: "desktop_workbench"
  window:
    default_width: 1280
    default_height: 720
  input_model:
    primary: "keyboard_pointer"
```

Headless:

```yaml
ui:
  shell: "headless"
  renderer: "none"
  layout_profile: "none"
```

------

# 9. The relationship between Board Variant and UI

Board can tell you:

```text
Screen size, input device, rotation direction
```

But Board cannot decide:

```text
Which page layout to use
```

Target determines:

```text
Which shell/profile to use for the products on this board
```

For example, the same Linux:

```text
linux_uconsole_gtk
linux_uconsole_ascii
linux_headless
```

 can share the same set of AppService and PresentationModel, but the Shell is different.

------

# 10. Capability specifications need to add Board Binding

Previously the capability only said endpoint_host. Now add:

```text
capability binding
```

Because it is also ESP32, the LoRa/GPS wiring of different boards is different.

```yaml
capability_bindings:
  lora:
    board_provider: "t_lora_pager.radio"
    platform_driver: "esp_sx1262_packet_radio"
    protocol_binding: "meshtastic_or_meshcore"
    runtime_owner: "mesh_task"

  gps:
    board_provider: "t_lora_pager.gps"
    platform_driver: "esp_uart_gnss"
    parser: "core_gps_nmea"
    runtime_owner: "gps_task"

  display:
    board_provider: "t_lora_pager.display"
    platform_driver: "esp_lcd_panel"
    renderer: "lvgl"
    runtime_owner: "ui_task"

  input:
    board_provider: "t_lora_pager.input"
    platform_driver: "esp_input_driver"
    event_target: "ui_command_queue"
```

This can avoid:

```text
AppContext directly knows the GPS pin of T-LoRa Pager
BLE service directly knows the GAT562 board
UI directly reads board display rotation
```

------

# 11. Product Composition Layer Product Composition

The Apps layer cannot expand infinitely into a God object.
Product Composition Root should be defined.

```text
apps/<target>/
  main.cpp
  target_manifest.yaml
  product_composition.cpp
  product_composition.h
  ui_composition.cpp
  runtime_composition.cpp
  capability_composition.cpp
```

Responsibilities:

```text
- Read/reference target manifest
- Create board facts provider
- Create platform drivers
- Create stores
- Create protocol cores
- Create app services
- Create presentation models
- Create shell renderer
- Connect event queues
```

Forbidden:

```text
- Write direct message business in composition root
- Parse BLE phone protocol in composition root
- Parse NMEA in composition root
- Write UI business data in composition root
```

------

# 12. Event Bus / Command Bus specifications

When sharing across platforms, the event model must be clear.

It is recommended to add:

```text
modules/core_runtime
```

or define it as a specification first.

## 12.1 Event type layering

```text
HardwareEvent
  RadioIrq
  GpsBytesAvailable
  BleConnected
  ButtonPressed

CapabilityEvent
  RadioPacketReceived
  GpsFixUpdated
  BleWriteReceived

ProtocolEvent
  MeshtasticPacketReceived
  MeshCoreFrameReceived
  PhoneCommandReceived

AppEvent
  MessageReceived
  ContactUpdated
  LocationUpdated
  ConfigChanged

PresentationEvent
  ChatListChanged
  MapViewportChanged
  DeviceStatusChanged

ShellEvent
  KeyPressed
  TouchClicked
  WindowResized
```

Prohibit low-level event skipping:

```text
HardwareEvent -> UI
BLE callback -> ChatModel
Radio IRQ -> ContactService
```

------

## 12.2 Command type layering

```text
UiCommand
  SendMessageClicked
  SelectContact
  ToggleMapLayer

AppCommand
  SendDirectMessage
  ApplyConfigPatch
  MarkConversationRead

CapabilityCommand
  ConfigureRadio
  StartGps
  StartBleAdvertising

DriverCommand
  WriteSpi
  SetGpio
```

UI can only send `UiCommand` or call presentation action.
PresentationModel is converted to AppCommand.
AppService then transfers to CapabilityCommand.

------

# 13. The configuration system must distinguish three types of configurations

A lot of cross-platform drift comes from config being mixed together.

Must distinguish:

## 13.1 Product Config

User meaning configuration:

```text
-Current protocol Meshtastic/MeshCore
- region
- modem preset
- screen timeout
- map layer preference
- team mode
```

Ownership:

```text
core_config + AppService
```

## 13.2 Platform Config

Platform operation configuration:

```text
- Linux data root
- SQLite path
- serial device path
- framebuffer path
- GTK window size
```

Ownership:

```text
platform/app target
```

## 13.3 Board Facts

Hardware Facts:

```text
- pins
- bus
- display resolution
- GPS UART
```

Ownership:

```text
board package
```

It is forbidden to put Board Facts into Product Config.
Do not allow the user UI to modify the board pin.
Disable Meshtastic BLE config from writing Linux path.
It is forbidden to directly change the radio driver without ConfigService in GTK settings.

------

# 14. Shared Core code rules must be further stricted

Because nRF52 + ESP32 + Linux + multiple UIs must be supported, shared modules must be hierarchical.

## 14.1 Core Portable Level

```text
core_portable
```

Allow:

```text
- fixed buffer
- no exception
- no RTTI
- limited STL or no STL
- no heap hidden allocation
- explicit capacity
```

Used for:

```text
core_mesh
core_gps parser
core_phone codec
core_config schema
```

## 14.2 Core Rich Level

```text
core_rich
```

Allows richer structure, but cannot enter MCU. Required path:

```text
- std::vector
- std::string
- indexes
- search
- Linux-friendly projection
```

Used for:

```text
Linux search
GTK presentation model maybe
map package index
diagnostics
```

Target manifest declares which level can be used.

------

# 15. Presentation Model should also be divided into portable/rich

Embedded and Linux UI are different, and they should not be forced to share a huge presentation model.

Suggestions:

```text
modules/ui_presentation/
  include/ui_presentation/
    chat/
      chat_summary_model.h        # portable
      chat_workspace_model.h      # richer
    map/
      map_compact_model.h
      map_workbench_model.h
    device/
      device_status_model.h
```

Rules:

```text
- compact model can be used for LVGL/ASCII/small screens
- workbench model can be used in GTK/uConsole
- The two share AppService, not layout objects
```

Example:

```text
ChatSummaryModel:
  conversation rows
  selected row
  unread count
  simple actions

ChatWorkbenchModel:
  conversation rail
  transcript
  node inspector
  compose state
  search/filter
```

LVGL does not have to eat GTK's workspace model.
GTK does not have to reuse LVGL pages.

------

# 16. ASCII UI special rules

ASCII/TUI is not "simple log output", it is also a shell.

Required constraints:

```text
ASCII renderer can only consume presentation model.
Service/store cannot be read directly.
Cannot access radio/GPS/BLE directly.
```

ASCII renderer output should pass:

```text
AsciiCanvas
AsciiLayout
AsciiTheme
TerminalInputAdapter
```

Instead of `printf()` everywhere in the business code.

```cpp
class AsciiCanvas {
public:
    void drawText(int x, int y, TextStyle style, const char* text);
    void drawBox(int x, int y, int w, int h);
    void flush();
};
```

In this way, CLI/TUI can also maintain architectural boundaries in the future.

------

# 17. LVGL special rules

LVGL pages must be thinned.

Forbidden:

```text
lvgl page directly:
- read and write store
- parse protocol
- access board
- access radio driver
- access gps driver
- modify config backend
```

Allow:

```text
lvgl page:
- Bind presentation model
- Render snapshot
- Send UI action
```

Recommended structure:

```text
platform/esp/.../ui/lvgl_shell/
  lvgl_chat_page.cpp
  lvgl_map_page.cpp
  lvgl_settings_page.cpp

modules/ui_presentation/
  chat_model.h
  map_model.h
  settings_model.h
```

------

# 18. GTK special rules

GTK is a desktop shell, not a Linux business layer.

Forbidden:

```text
gtk page:
- Maintain ContactService by yourself
- Parse Meshtastic node info by yourself
- Spell device status by yourself
- Read SQLite table by yourself and bypass AppService
```

Allow:

```text
gtk page:
- Display richer presentation model
- Send action
- Process keyboard/mouse/window
```

GTK main thread rules must be hard-coded:

```text
All GtkWidget mutations must happen on GTK main loop.
Background services publish snapshots/events only.
```

------

# 19. Headless/CLI rules

Linux may have a daemon or CLI.
It cannot be turned into a "debugging shortcut to bypass the architecture".

CLI commands must also go through AppService:

```text
trailmate send --to ...
    -> CliShell
        -> AppCommand
            -> DirectMessageService
```

Forbidden:

```text
CLI directly opens SQLite and changes the peer key
CLI directly calls the radio driver to send the package
```

Unless the command is explicitly a low-level diagnostic command and placed under diagnostics capability.

------

# 20. Specific constraints on multiple ESP32 board subclasses

You specifically pointed out that different ESP32s have different board subclasses. Things have to be concrete here.

## 20.1 Do not allow `#ifdef BOARD_X` to spread

Forbidden:

```cpp
#ifdef BOARD_T_DECK
  ...
#elif BOARD_T_LORA_PAGER
  ...
#endif
```

 scattered in business, UI, and protocol codes.

Allow it to focus on:

```text
board package
target composition
build config
```

------

## 20.2 Board Facade

Each board implements a unified interface:

```cpp
class IBoardPackage {
public:
    virtual BoardId id() const = 0;
    virtual bool radioFacts(RadioHardwareFacts& out) const = 0;
    virtual bool gpsFacts(GpsHardwareFacts& out) const = 0;
    virtual bool displayFacts(DisplayHardwareFacts& out) const = 0;
    virtual bool inputFacts(InputHardwareFacts& out) const = 0;
    virtual bool powerFacts(PowerHardwareFacts& out) const = 0;
};
```

Board-specific classes can be many:

```text
TLoraPagerBoard
TDeckBoard
TWatchS3Board
Gat562Board
UConsoleAio2Board
```

But the upper layer only looks at `IBoardPackage`.

------

## 20.3 Board Runtime Hooks

Boards can have runtime hooks:

```cpp
class IBoardRuntimeHooks {
public:
    virtual void beforeSleep() = 0;
    virtual void afterWake() = 0;
    virtual void onLowPowerModeChanged(PowerMode mode) = 0;
};
```

But hooks do not allow calling business services and can only handle hardware status.

------

# 21. Race Policy Manifest

Each target must declare a race strategy:

```yaml
race_policy:
  ownership:
    chat_service: "app_task"
    mesh_session: "mesh_task"
    gps_service: "gps_task"
    config_service: "app_task"
    ui_state: "ui_task"
    device_status: "app_task"

  event_paths:
    radio_rx: "radio_irq -> radio_task -> mesh_event_queue -> app_task"
    gps_rx: "uart_irq -> gps_task -> location_event_queue -> app_task"
    ble_write: "ble_callback -> phone_command_queue -> app_task"
    ui_input: "ui_thread -> presentation_action -> app_command_queue"

  forbidden_direct_calls:
    - "ble_callback -> ChatService"
    - "radio_irq -> MeshSession"
    - "gps_task -> lvgl"
    - "gtk_worker -> GtkWidget"
    - "ui_thread -> blocking storage write"
```

This is much more useful than abstractly talking about "pay attention to thread safety".

------

# 22. New specification document list

Finally, it is recommended to add these:

```text
docs/specification/
  CROSS_PLATFORM_PRODUCT_ARCHITECTURE_SPEC.md
  TARGET_MANIFEST_SPEC.md
  BOARD_PACKAGE_SPEC.md
  RUNTIME_CONCURRENCY_SPEC.md
  UI_PRESENTATION_ARCHITECTURE_SPEC.md
  CAPABILITY_AUTHORITY_GLOSSARY.md
  STORAGE_AUTHORITY_SPEC.md
  EVENT_COMMAND_FLOW_SPEC.md
```

It is not all written at once, but Phase 1 should at least establish the documents and core rules.

------

# 23. Directory structure suggestions

```text
modules/
  core_device/
    capability / authority / target vocabulary

  core_runtime/
    event / command / scheduler abstractions

  core_config/
    config schema / patch / validation

  core_mesh/
    LoRa / Meshtastic / MeshCore radio-side business

  core_gps/
    GNSS parser / location / time facts

  core_phone/
    Meshtastic BLE phone core
    MeshCore BLE/control core

  core_chat/
    chat/contact/conversation domain

  ui_presentation/
    UI toolkit independent presentation models

  ui_ascii/
    optional reusable ASCII canvas/render helpers

platform/
  esp/
    boards/
    drivers/
    runtime/
    storage/
    ble/
    ui/lvgl/

  nrf52/
    boards/
    drivers/
    runtime/
    storage/
    ble/

  linux/
    boards/
    drivers/
    runtime/
    storage/
    ui/gtk/
    ui/ascii/

apps/
  esp_tpager/
  esp_tdeck/
  nrf52_gat562/
  linux_uconsole_gtk/
  linux_uconsole_ascii/
  linux_headless/
  linux_sim/
```



------

# Appendix 1: Implementation details

```text
After the business is stable, the goal of cross-platform transformation is not to redesign the business.
It is to separate the already stable business semantics from the platform, board level, UI, protocol channel, and runtime details,
to make it a reusable, testable, and replaceable host application core.
```

Corresponding engineering principles:

```text
Domain defines facts;
UseCase orchestrates business;
ProtocolCore explains the protocol;
CapabilityPort describes the required capabilities;
PlatformAdapter provides capabilities;
BoardPackage describes hardware facts;
RuntimeContext handles scheduling and concurrency;
PresentationModel projects the interface state;
Renderer is only responsible for drawing;
CompositionRoot is responsible for assembly.
```

------

# 1. The hierarchical overview corresponds to the design pattern

First give the global mapping.

| Layers | Responsibilities | Main design patterns | Purpose |
| ----------------------------- | -------------------- | --------------------------------------------------- | ----------------------------------------- |
| Domain | Define business facts and value objects | Value Object, Entity | De-platform, unify semantics |
| UseCase / Application Service | Orchestrate business processes | Application Service, Command Handler, State Machine | The only implementation of stable business |
| Protocol Core | Interpret external protocols | Strategy, Codec, State Machine, Mapper | Meshtastic/MeshCore/BLE/HostLink protocol reuse |
| Capability Ports | Define required capabilities | Ports and Adapters, Repository Interface | Isolate platform capabilities |
| Platform Adapters | Implement capabilities | Adapter, Repository, Proxy, Null Object | ESP32/nRF52/Linux Replaceable |
| Board Package | Describe hardware facts | Abstract Factory, Provider | Isolate different boards |
| Runtime Context | Handling concurrent scheduling | Active Object, Reactor, Command Queue, Event Queue | Resolving race conditions |
| Config Core | Management configuration semantics | Schema, Patch, Validator, Repository | Unified configuration changes for multiple entries |
| Device Core | Aggregation capability status | Snapshot, Projection, Facade | UI/BLE/HostLink shared device status |
| Presentation Model | UI state-independent | MVVM, Presenter, CQRS Read Model | LVGL/ASCII/GTK reuse |
| Renderer / Shell | Drawing and input | Renderer, Adapter, Command | UI technology stack isolation |
| Composition Root | Assembly object | Dependency Injection, Abstract Factory, Builder | Control dependency direction |

This is not "pattern stuffing". Each pattern corresponds to a specific structural problem.

------

# 2. Domain layer: the atomic layer that stabilizes business semantics

## 2.1 Goal

The Domain layer only defines "facts" and "values" in the business.

For example:

```text
NodeId
Peer
Contact
Conversation
Message
LocationFix
TimeFact
RadioPacket
MeshProtocol
DeviceCapability
ConfigValue
```

It does not do I/O, does not adjust the platform API, and does not know the UI.

## 2.2 Usage mode

### Value Object

For immutable business values:

```cpp
namespace domain {

struct NodeId {
    uint32_t value = 0;

    bool isValid() const {
        return value != 0;
    }

    bool operator==(const NodeId& other) const {
        return value == other.value;
    }
};

struct MessageId {
    uint64_t value = 0;
};

struct Timestamp {
    uint64_t epoch_seconds = 0;
    bool valid = false;
};

}
```

Purpose:

```text
Free the semantics of "node ID", "message ID" and "timestamp" from uint32_t / uint64_t.
```

Avoid:

```cpp
uint32_t node;
uint32_t peer;
uint32_t id;
uint32_t from;
```

Humbling around.

------

### Entity

Used for business objects with identity and life cycle:

```cpp
namespace chat {

struct Message {
    domain::MessageId id;
    domain::NodeId from;
    domain::NodeId to;
    domain::Timestamp timestamp;
    MessageDirection direction;
    MessageStatus status;
    FixedString<240> text;
};

struct Contact {
    domain::NodeId node_id;
    FixedString<32> short_name;
    FixedString<64> long_name;
    ContactSource source;
    TrustState trust;
};

}
```

Purpose:

```text
Make business objects such as messages, contacts, nodes, etc. semantically consistent on all platforms.
```

------

## 2.3 Domain layer prohibitions

Domain prohibition include:

```cpp
Arduino.h
Preferences.h
sqlite3.h
freertos/...
zephyr/...
nrf_...
esp_...
lvgl.h
gtk/...
```

Domain prohibition:

```text
- Access NVS / SQLite / Flash
- Send LoRa packet
- Analyze BLE characteristic
- Access GPS UART
- Update UI
- Lock
- Starting thread
```

------

# 3. UseCase layer: the only realization of stable business

The business has been stabilized, so what the UseCase layer needs to do is:

```text
Consolidate the stable business process into the only realization.
```

For example:

```text
Send direct message
Receive Mesh packet
Update contacts
GPS fix update
BLE phone command processing
Configuration changes
Device status aggregation
```

## 3.1 Usage mode: Application Service

For example, DirectMessageService:

```cpp
class DirectMessageService {
public:
    DirectMessageService(
        MeshProtocolStrategy& protocol,
        PeerIdentityService& identity,
        IPacketRadio& radio,
        IClock& clock,
        IMeshEventSink& events
    )
        : protocol_(protocol),
          identity_(identity),
          radio_(radio),
          clock_(clock),
          events_(events) {}

    SendResult sendDirect(const DirectMessageCommand& cmd) {
        if (!cmd.isValid()) {
            return SendResult::fail(SendFailure::InvalidInput);
        }

        LocalIdentity local;
        auto local_result = identity_.ensureLocalIdentity(local);
        if (!local_result.ok) {
            return SendResult::fail(SendFailure::LocalIdentityMissing);
        }

        PeerPublicKey peer_key;
        auto peer_result = identity_.findPeerKey(cmd.to, peer_key);
        if (!peer_result.ok) {
            return SendResult::fail(SendFailure::PeerKeyMissing);
        }

        EncodedPacket packet;
        auto build_result = protocol_.buildDirectMessage(
            local,
            peer_key,
            cmd,
            packet
        );

        if (!build_result.ok) {
            return SendResult::fail(SendFailure::PacketBuildFailed);
        }

        auto tx_result = radio_.send(packet.bytes());
        if (!tx_result.ok) {
            return SendResult::fail(SendFailure::RadioSendFailed);
        }

        events_.emit(MeshEvent::messageSent(cmd.to));
        return SendResult::ok();
    }

private:
    MeshProtocolStrategy& protocol_;
    PeerIdentityService& identity_;
    IPacketRadio& radio_;
    IClock& clock_;
    IMeshEventSink& events_;
};
```

Purpose:

```text
ESP32, nRF52, and Linux use the same DirectMessageService when sending direct messages.
```

The only platform differences are:

```text
IPacketRadio
ILocalIdentityStore
IPeerKeyStore
IClock
IRandom
```

------

## 3.2 Usage mode: Command Handler

For operations coming from UI, BLE, HostLink, and CLI, the underlying services should not be called directly, but should be unified into Commands.

```cpp
struct SendDirectMessageCommand {
    domain::NodeId to;
    FixedBytes<240> text;
};

struct ApplyConfigPatchCommand {
    ConfigPatch patch;
};

using AppCommand = Variant<
    SendDirectMessageCommand,
    ApplyConfigPatchCommand,
    MarkConversationReadCommand,
    StartGpsCommand,
    SetMeshProtocolCommand
>;
```

Unified processing:

```cpp
class AppCommandHandler {
public:
    CommandResult handle(const AppCommand& command) {
        return visit(command, [this](const auto& cmd) {
            return handleTyped(cmd);
        });
    }

private:
    CommandResult handleTyped(const SendDirectMessageCommand& cmd) {
        DirectMessageCommand direct;
        direct.to = cmd.to;
        direct.payload = cmd.text.view();
        return direct_message_.sendDirect(direct).toCommandResult();
    }

    CommandResult handleTyped(const ApplyConfigPatchCommand& cmd) {
        return config_service_.applyPatch(cmd.patch);
    }

private:
    DirectMessageService& direct_message_;
    ConfigService& config_service_;
};
```

Purpose:

```text
UI / BLE / HostLink / CLI only issue commands and do not directly manipulate internal business objects.
```

------

## 3.3 Usage mode: State Machine

MeshSession, BLE phone session, GPS runtime, and HostLink session should all be explicit state machines.

For example, MeshSession:

```cpp
enum class MeshSessionState {
    Stopped,
    Starting,
    Ready,
    Degraded,
    Error,
};

class MeshSession {
public:
    void start();
    void stop();
    void tick(uint32_t now_ms);
    void onRadioPacket(const RadioRxPacket& packet);
    SendResult sendDirect(const DirectMessageCommand& cmd);

private:
    MeshSessionState state_ = MeshSessionState::Stopped;
};
```

Purpose:

```text
Avoid a bunch of bools:
radioReady
meshStarted
gpsSynced
bleConnected
configApplied
```

The state machine can accurately express:

```text
Stopped -> Starting -> Ready -> Degraded -> Error
```

------

# 4. Protocol Core layer: separation of protocol semantics and transport

This is the most confusing place in BLE/Meshtastic/MeshCore.

## 4.1 The protocol core is divided into four categories

```text
core_mesh/protocol/meshtastic
core_mesh/protocol/meshcore
core_phone/protocol/meshtastic
core_phone/protocol/meshcore
core_hostlink
```

Difference:

```text
Meshtastic radio protocol:
  LoRa packet / protobuf / PKI / channel / direct packet

Meshtastic phone protocol:
  BLE ToRadio / FromRadio / admin / config

MeshCore radio protocol:
  frame / sign / encrypt / payload

MeshCore phone/control protocol:
  BLE/NUS command / device status / contact / telemetry

HostLink protocol:
  USB/Serial frame / command / event
```

These should not be mixed in the BLE host or radio adapter.

------

## 4.2 Usage mode: Strategy

Meshtastic and MeshCore are different protocol strategies.

```cpp
class MeshProtocolStrategy {
public:
    virtual ~MeshProtocolStrategy() = default;

    virtual ProtocolKind kind() const = 0;

    virtual PacketBuildResult buildDirectMessage(
        const LocalIdentity& local,
        const PeerPublicKey& peer,
        const DirectMessageCommand& cmd,
        EncodedPacket& out
    ) = 0;

    virtual PacketParseResult parseRadioPacket(
        const RadioRxPacket& packet,
        MeshProtocolEvent& out
    ) = 0;

    virtual RadioConfig deriveRadioConfig(
        const MeshConfig& config
    ) = 0;
};
```

Implementation:

```cpp
class MeshtasticProtocolStrategy final : public MeshProtocolStrategy {
    ...
};

class MeshCoreProtocolStrategy final : public MeshProtocolStrategy {
    ...
};
```

Purpose:

```text
The upper DirectMessageService does not care whether it is Meshtastic or MeshCore.
The protocol only replaces strategy.
```

------

## 4.3 Usage mode: Codec

Codec only does encoding/decoding and does not make business decisions.

```cpp
class MeshtasticPacketCodec {
public:
    DecodeResult decode(ByteView bytes, MeshtasticPacket& out);
    EncodeResult encode(const MeshtasticPacket& packet, FixedBuffer& out);
};
```

Codec prohibited:

```text
- Check peer key
- Decide whether to send
- Decide whether to update contact
- Access radio
- Access storage
```

------

## 4.4 Usage mode: Mapper / Translator

Mapper is required between BLE phone protocol and AppCommand.

```cpp
class MeshtasticPhoneCommandMapper {
public:
    MapResult toAppCommand(
        const MeshtasticToRadio& input,
        AppCommand& out
    );

    MapResult fromAppEvent(
        const AppEvent& event,
        MeshtasticFromRadio& out
    );
};
```

Purpose:

```text
Meshtastic BLE's ToRadio/FromRadio are external protocol objects.
AppCommand/AppEvent are internal business objects.
The two cannot contaminate each other.
```

------

# 5. Capability Ports: Capability interface

This layer is the core of Hexagonal Architecture / Ports and Adapters.

## 5.1 Usage mode: Port Interface

Define the port according to the capabilities required by the business.

### Radio

```cpp
class IPacketRadio {
public:
    virtual ~IPacketRadio() = default;

    virtual RadioResult configure(const RadioConfig& config) = 0;
    virtual RadioResult send(ByteView packet) = 0;
    virtual RadioResult poll(RadioRxPacket& out) = 0;
    virtual RadioStatus status() const = 0;
};
```

### GPS

```cpp
class IGnssByteStream {
public:
    virtual ~IGnssByteStream() = default;
    virtual IoResult read(uint8_t* out, size_t cap, size_t& len) = 0;
};

class ILocationSource {
public:
    virtual ~ILocationSource() = default;
    virtual bool latest(LocationFix& out) = 0;
};

class ITimeAuthority {
public:
    virtual ~ITimeAuthority() = default;
    virtual bool nowEpoch(uint64_t& out) = 0;
    virtual uint32_t nowMonotonicMs() = 0;
    virtual TimeSyncStatus syncStatus() = 0;
};
```

### BLE Transport

```cpp
class IBleGattHost {
public:
    virtual ~IBleGattHost() = default;

    virtual BleResult startAdvertising(const BleAdvertisement& adv) = 0;
    virtual BleResult registerService(const BleServiceDefinition& service) = 0;
    virtual BleResult notify(BleCharacteristicId id, ByteView payload) = 0;
};
```

### Storage

```cpp
class IPeerKeyStore {
public:
    virtual ~IPeerKeyStore() = default;

    virtual StoreResult get(domain::NodeId node, PeerPublicKey& out) = 0;
    virtual StoreResult put(const PeerPublicKey& key) = 0;
    virtual StoreResult remove(domain::NodeId node) = 0;
    virtual StoreCapabilities capabilities() const = 0;
};
```

Purpose:

```text
UseCase doesn't know about NVS / SQLite / nRF52 flash.
ProtocolCore doesn't know about Bluefruit / NimBLE / BlueZ.
LocationService does not know UART/Linux fd.
```

------

## 5.2 Usage mode: Repository

Store port uses Repository mode.

```cpp
class IMessageRepository {
public:
    virtual StoreResult append(const chat::Message& message) = 0;
    virtual StoreResult listConversation(
        domain::NodeId peer,
        MessageCursor cursor,
        MessagePage& out
    ) = 0;
    virtual StoreResult markRead(domain::MessageId id) = 0;
};
```

Implementation:

```text
EspNvsPeerKeyStore
Nrf52FlashPeerKeyStore
SqlitePeerKeyStore
FakePeerKeyStore
```

Purpose:

```text
The same business service can run on different storage backends.
```

------

## 5.3 Usage mode: Null Object

Some targets do not have certain capabilities, such as headless without display, and Linux currently does not support BLE.

Don't write everywhere:

```cpp
if (ble != nullptr) ...
```

Use Null Object:

```cpp
class NullBleGattHost final : public IBleGattHost {
public:
    BleResult startAdvertising(const BleAdvertisement&) override {
        return BleResult::unsupported();
    }

    BleResult registerService(const BleServiceDefinition&) override {
        return BleResult::unsupported();
    }

    BleResult notify(BleCharacteristicId, ByteView) override {
        return BleResult::unsupported();
    }
};
```

Purpose:

```text
The ability not to exist is also an explicit object, not a null branch.
```

------

# 6. Platform Adapter layer: Wrapping the platform API

## 6.1 Usage mode: Adapter

ESP32:

```cpp
class EspSx1262PacketRadio final : public IPacketRadio {
public:
    explicit EspSx1262PacketRadio(const RadioHardwareFacts& facts);

    RadioResult configure(const RadioConfig& config) override {
        // RadioLib / ESP SPI / GPIO
    }

    RadioResult send(ByteView packet) override {
        // platform-specific TX
    }

    RadioResult poll(RadioRxPacket& out) override {
        // platform-specific RX
    }
};
```

nRF52:

```cpp
class Nrf52Sx1262PacketRadio final : public IPacketRadio {
    // Bluefruit / Zephyr / Arduino nRF52 / Nordic SDK specific
};
```

Linux:

```cpp
class LinuxAio2Sx1262PacketRadio final : public IPacketRadio {
    // spidev / gpiochip / epoll etc.
};
```

Purpose:

```text
The inconsistency of the platform API is converged in the Adapter.
```

Adapter prohibits:

```text
- direct message business
- peer key logic
- BLE phone protocol
- GPS business policy
- UI status
```

------

## 6.2 Usage mode: Proxy

Linux + nRF52 radio endpoint, Linux's `IPacketRadio` implementation is not a native radio, but a proxy.

```cpp
class SerialPacketRadioProxy final : public IPacketRadio {
public:
    SerialPacketRadioProxy(ISerialPort& serial);

    RadioResult configure(const RadioConfig& config) override {
        return protocol_.sendConfigure(serial_, config);
    }

    RadioResult send(ByteView packet) override {
        return protocol_.sendPacket(serial_, packet);
    }

    RadioResult poll(RadioRxPacket& out) override {
        return protocol_.readPacket(serial_, out);
    }

private:
    ISerialPort& serial_;
    PacketRadioProxyProtocol protocol_;
};
```

Purpose:

```text
Linux still runs the Mesh business core;
nRF52 is just a packet radio endpoint.
```

This is different from command proxy.

------

## 6.3 Command Proxy

If nRF52 is a smart coprocessor, Linux does not run DirectMessageService but sends commands.

```cpp
class RemoteMeshCommandClient {
public:
    SendResult sendDirect(const DirectMessageCommand& cmd) {
        return link_.sendCommand(HostCommand::sendDirect(cmd));
    }
};
```

At this time:

```text
DirectMessageService in nRF52;
Linux only has RemoteMeshCommandClient.
```

Purpose:

```text
Prevent the same user action from being processed by the two business cores of Linux and nRF52 at the same time.
```

------

# 7. Board Package layer: Multiple ESP32 board variants

## 7.1 Usage mode: Provider / Abstract Factory

Each board provides `IBoardPackage`.

```cpp
class IBoardPackage {
public:
    virtual ~IBoardPackage() = default;

    virtual BoardId id() const = 0;

    virtual bool radioFacts(RadioHardwareFacts& out) const = 0;
    virtual bool gpsFacts(GpsHardwareFacts& out) const = 0;
    virtual bool displayFacts(DisplayHardwareFacts& out) const = 0;
    virtual bool inputFacts(InputHardwareFacts& out) const = 0;
    virtual bool powerFacts(PowerHardwareFacts& out) const = 0;
};
```

T-LoRa Pager:

```cpp
class TLoraPagerBoard final : public IBoardPackage {
public:
    BoardId id() const override {
        return BoardId::TLoraPager;
    }

    bool radioFacts(RadioHardwareFacts& out) const override {
        out.chip = RadioChip::Sx1262;
        out.spi_bus = SpiBusId::Spi2;
        out.reset = GpioPin{8};
        out.busy = GpioPin{7};
        out.dio1 = GpioPin{33};
        out.dio2_as_rf_switch = true;
        out.dio3_tcxo_voltage = 1.8f;
        return true;
    }

    bool displayFacts(DisplayHardwareFacts& out) const override {
        out.width = 222;
        out.height = 480;
        out.rotation = DisplayRotation::Landscape;
        return true;
    }
};
```

T-Deck:

```cpp
class TDeckBoard final : public IBoardPackage {
    // different display/input/radio/gps facts
};
```

Purpose:

```text
The only differences between different ESP32 boards are board package and composition root.
The BOARD_T_DECK / BOARD_TPAGER branches do not appear in business, protocol, and UI models.
```

------

## 7.2 Usage mode: Abstract Factory

Platform creates a driver based on board facts.

```cpp
class EspCapabilityFactory {
public:
    explicit EspCapabilityFactory(const IBoardPackage& board)
        : board_(board) {}

    UniquePtr<IPacketRadio> createPacketRadio() {
        RadioHardwareFacts facts;
        if (!board_.radioFacts(facts)) {
            return makeUnique<NullPacketRadio>();
        }

        if (facts.chip == RadioChip::Sx1262) {
            return makeUnique<EspSx1262PacketRadio>(facts);
        }

        return makeUnique<NullPacketRadio>();
    }

    UniquePtr<IGnssByteStream> createGnssByteStream() {
        GpsHardwareFacts facts;
        if (!board_.gpsFacts(facts)) {
            return makeUnique<NullGnssByteStream>();
        }

        return makeUnique<EspUartGnssByteStream>(facts);
    }

private:
    const IBoardPackage& board_;
};
```

Purpose:

```text
Board only describes facts;
factory creates platform drivers based on facts;
The business layer is completely indifferent.
```

------

# 8. Runtime/Concurrency layer: race problem structuring

This is the biggest difference between embedded and Linux.

You cannot rely on "pay attention to thread safety". Communication patterns must be designed.

## 8.1 Usage mode: Active Object

Each mutable service has owner context.

```text
RadioContext
GpsContext
BleContext
AppContext
UiContext
StorageContext
```

Each context has its own queue:

```cpp
class IRuntimeQueue {
public:
    virtual bool post(RuntimeCommand command) = 0;
    virtual bool poll(RuntimeCommand& out) = 0;
};
```

For example:

```text
Radio IRQ
  -> RadioContext queue
    -> MeshSession.onRadioPacket
      -> AppContext queue
        -> ChatService.onMessageReceived
          -> UiContext snapshot event
```

Purpose:

```text
 Mutable state is not directly shared across tasks / threads / callbacks.
```

------

## 8.2 Usage mode: Command Queue

UI / BLE / HostLink cannot directly change the service, only deliver commands.

```cpp
class AppCommandQueue {
public:
    bool post(const AppCommand& command);
    bool poll(AppCommand& out);
};
```

BLE callback:

```cpp
void MeshtasticBleHost::onWrite(ByteView bytes) {
    PhoneInputEvent event;
    event.source = PhoneInputSource::Ble;
    event.bytes.assign(bytes);

    phone_queue_.post(event);
}
```

PhoneCore context:

```cpp
void PhoneRuntime::tick() {
    PhoneInputEvent event;
    while (queue_.poll(event)) {
        auto result = meshtastic_phone_core_.handleInput(event.bytes);
        dispatchPhoneOutputs(result);
    }
}
```

Purpose:

```text
BLE stack callback does not directly enter ChatService / ConfigService / GPS.
```

------

## 8.3 Usage mode: Event Queue

Business results are published through events.

```cpp
struct AppEvent {
    AppEventKind kind;
    AppEventPayload payload;
    uint32_t version;
};
```

For example:

```text
MessageReceived
ContactUpdated
LocationUpdated
ConfigChanged
DeviceStatusChanged
```

After receiving it, the UI does not read the internal object directly, but requests a snapshot.

------

## 8.4 Usage mode: Immutable Snapshot

The UI does not read the mutable service directly.

```cpp
struct DeviceStatusSnapshot {
    CapabilityState lora;
    CapabilityState gps;
    CapabilityState ble;
    TimeSyncStatus time;
    uint32_t version;
};

struct ChatListSnapshot {
    ConversationRow rows[32];
    size_t count = 0;
    uint32_t selected_index = 0;
    uint32_t version = 0;
};
```

AppService generates snapshot:

```cpp
class ChatProjectionService {
public:
    ChatListSnapshot buildChatListSnapshot() const;
};
```

Purpose:

```text
LVGL/GTK/ASCII all render snapshots and do not hold business internal references.
```

------

## 8.5 ISR rules

ISR only defers.

```cpp
void IRAM_ATTR radioDio1Isr() {
    radio_irq_queue.postFromIsr(RadioIrqEvent::Dio1);
}
```

Forbidden:

```text
In ISR:
- protobuf decode
- packet decrypt
- storage write
- BLE notify
- UI update
- DirectMessageService
```

------

## 8.6 UI Thread Only

### LVGL

```cpp
void UiRuntime::tick() {
    UiCommand cmd;
    while (ui_queue_.poll(cmd)) {
        lvgl_renderer_.apply(cmd);
    }

    lv_timer_handler();
}
```

All `lv_obj_*` are only in UI runtime.

### GTK

```cpp
void GtkShell::onAppSnapshot(DeviceStatusSnapshot snapshot) {
    g_main_context_invoke(nullptr, [](gpointer data) {
        auto* self = static_cast<GtkShell*>(data);
        self->renderOnGtkThread();
        return G_SOURCE_REMOVE;
    }, this);
}
```

### ASCII

```cpp
void AsciiShell::render(const AppSnapshot& snapshot) {
    canvas_.clear();
    renderer_.draw(snapshot, canvas_);
    canvas_.flushSingleWriter();
}
```

Purpose:

```text
Each UI technology stack has its own thread/loop constraints, but the business layer does not need to know.
```

------

# 9. Config Core: Multi-entry unified configuration

The configuration will be modified from these entries:

```text
LVGL Settings
GTK Settings
ASCII/CLI
Meshtastic BLE Admin
MeshCore BLE Command
HostLink
Configuration file
```

If they write directly to the storage, they will inevitably drift.

## 9.1 Usage mode: Schema + Patch + Validator

```cpp
struct ConfigPatch {
    Optional<MeshProtocolKind> mesh_protocol;
    Optional<RegionCode> region;
    Optional<ModemPreset> modem_preset;
    Optional<uint32_t> screen_timeout_sec;
    Optional<bool> gps_enabled;
};

class ConfigValidator {
public:
    ConfigValidationResult validate(
        const AppConfig& current,
        const ConfigPatch& patch,
        const TargetCapabilitySnapshot& target
    );
};
```

ConfigService:

```cpp
class ConfigService {
public:
    ConfigResult applyPatch(const ConfigPatch& patch) {
        auto validation = validator_.validate(current_, patch, target_);
        if (!validation.ok) {
            return ConfigResult::fail(validation.failure);
        }

        AppConfig next = current_;
        patch.applyTo(next);

        auto store_result = store_.save(next);
        if (!store_result.ok) {
            return ConfigResult::storeFailed(store_result.failure);
        }

        current_ = next;
        events_.emit(AppEvent::configChanged());
        return ConfigResult::ok();
    }

private:
    AppConfig current_;
    IConfigStore& store_;
    ConfigValidator& validator_;
    IAppEventSink& events_;
    TargetCapabilitySnapshot target_;
};
```

Purpose:

```text
All entrances use the same ConfigService.
Meshtastic BLE Admin does not change NVS directly.
GTK Settings does not directly change SQLite.
HostLink does not directly change the internal fields of AppContext.
```

------

# 10. Device Core: Capability status aggregation

BLE/HostLink/UI all require device status. We cannot fight each other.

## 10.1 Usage mode: Facade + Projection

```cpp
class DeviceStatusService {
public:
    DeviceStatusSnapshot snapshot() const {
        DeviceStatusSnapshot out;
        out.lora = radio_monitor_.state();
        out.gps = location_monitor_.state();
        out.ble = ble_monitor_.state();
        out.time = time_authority_.syncStatus();
        out.battery = power_monitor_.snapshot();
        return out;
    }

private:
    IRadioMonitor& radio_monitor_;
    ILocationMonitor& location_monitor_;
    IBleMonitor& ble_monitor_;
    ITimeAuthority& time_authority_;
    IPowerMonitor& power_monitor_;
};
```

Purpose:

```text
MeshtasticPhoneCore, MeshCorePhoneCore, HostLink, LVGL, GTK, and ASCII all consume the same DeviceStatusSnapshot.
```

------

# 11. BLE architecture: Transport Host + Phone Core

This is a piece that must be dismantled.

## 11.1 BLE Host

```cpp
class MeshtasticBleHost {
public:
    void start() {
        ble_.registerService(makeMeshtasticServiceDefinition());
        ble_.startAdvertising(makeAdvertisement());
    }

    void onToRadioWrite(ByteView bytes) {
        phone_input_queue_.post(PhoneInputEvent::meshtastic(bytes));
    }

    void notifyFromRadio(ByteView bytes) {
        ble_.notify(from_radio_char_, bytes);
    }

private:
    IBleGattHost& ble_;
    IPhoneInputQueue& phone_input_queue_;
};
```

BLE Host only does:

```text
advertising
service
characteristic
write callback
notify
connection
MTU/chunking
```

------

## 11.2 MeshtasticPhoneCore

```cpp
class MeshtasticPhoneCore {
public:
    PhoneProcessResult handleToRadio(ByteView bytes) {
        MeshtasticToRadio msg;
        auto decode = codec_.decodeToRadio(bytes, msg);
        if (!decode.ok) {
            return PhoneProcessResult::protocolError();
        }

        AppCommand command;
        auto mapped = mapper_.toAppCommand(msg, command);
        if (mapped.ok) {
            app_commands_.post(command);
        }

        return buildResponses(msg);
    }

    bool nextNotification(MeshtasticFromRadio& out);

private:
    MeshtasticPhoneCodec codec_;
    MeshtasticPhoneCommandMapper mapper_;
    IPhoneAppFacade& app_;
    IAppCommandSink& app_commands_;
};
```

Purpose:

```text
Meshtastic phone API semantics in shared core.
ESP32/nRF52 BLE file is only responsible for BLE transport.
```

------

## 11.3 MeshCorePhoneCore

```cpp
class MeshCorePhoneCore {
public:
    PhoneProcessResult handleFrame(ByteView bytes) {
        MeshCorePhoneCommand cmd;
        auto decoded = codec_.decode(bytes, cmd);
        if (!decoded.ok) {
            return PhoneProcessResult::protocolError();
        }

        return dispatcher_.dispatch(cmd);
    }

private:
    MeshCorePhoneCodec codec_;
    MeshCorePhoneCommandDispatcher dispatcher_;
    IMeshCorePhoneFacade& app_;
};
```

Purpose:

```text
MeshCore BLE/NUS commands are explained in shared core.
BLE host does not spell contact/status/device info.
```

------

# 12. GPS architecture: ByteStream + Parser + LocationService + TimeAuthority

## 12.1 GnssByteStream

Platform related:

```cpp
class EspUartGnssByteStream final : public IGnssByteStream {
    IoResult read(uint8_t* out, size_t cap, size_t& len) override;
};
```

Linux:

```cpp
class LinuxSerialGnssByteStream final : public IGnssByteStream {
    ...
};
```

nRF52:

```cpp
class Nrf52UartGnssByteStream final : public IGnssByteStream {
    ...
};
```

------

## 12.2 NMEA Parser

Sharing:

```cpp
class NmeaParser {
public:
    NmeaParseResult feed(ByteView bytes);
    bool latestFix(LocationFix& out) const;
    bool latestTime(TimeSyncFact& out) const;
};
```

------

## 12.3 LocationService

```cpp
class LocationService {
public:
    void onGnssBytes(ByteView bytes) {
        auto result = parser_.feed(bytes);

        LocationFix fix;
        if (parser_.latestFix(fix)) {
            auto filtered = jitter_filter_.apply(fix);
            latest_ = filtered;
            events_.emit(AppEvent::locationUpdated());
        }

        TimeSyncFact time;
        if (parser_.latestTime(time)) {
            time_authority_.observe(time);
        }
    }

    bool latest(LocationFix& out) const {
        if (!latest_.valid) return false;
        out = latest_;
        return true;
    }

private:
    NmeaParser parser_;
    GpsJitterFilter jitter_filter_;
    ITimeAuthorityUpdater& time_authority_;
    IAppEventSink& events_;
    LocationFix latest_;
};
```

Purpose:

```text
BLE/Mesh/HostLink/UI no longer reads GPS driver directly.
```

------

# 13. UI architecture: Presentation Model + Renderer

## 13.1 AppService does not know UI

```cpp
class ChatService {
public:
    SendResult sendMessage(...);
    void onMessageReceived(...);
    ChatListSnapshot buildListSnapshot(...);
};
```

Do not appear:

```cpp
lv_obj_t*
GtkWidget*
printf
```

------

## 13.2 Presentation Model

```cpp
class ChatWorkspaceModel {
public:
    ChatWorkspaceSnapshot snapshot() const {
        ChatWorkspaceSnapshot out;
        out.conversations = chat_.buildConversationRows();
        out.active_thread = chat_.buildActiveThread();
        out.device_status = device_.snapshot();
        return out;
    }

    CommandResult sendMessage(const UiSendMessageAction& action) {
        AppCommand cmd = AppCommand::sendDirect(action.peer, action.text);
        return commands_.post(cmd);
    }

    void selectConversation(domain::NodeId peer) {
        selected_peer_ = peer;
    }

private:
    ChatService& chat_;
    DeviceStatusService& device_;
    IAppCommandSink& commands_;
    domain::NodeId selected_peer_;
};
```

Purpose:

```text
LVGL / ASCII / GTK can consume ChatWorkspaceModel,
But layout implementation is not shared.
```

------

## 13.3 LVGL Renderer

```cpp
class LvglChatRenderer {
public:
    void render(const ChatWorkspaceSnapshot& snapshot) {
        // only lv_obj_* operations here
    }

    void onButtonSendClicked() {
        UiSendMessageAction action;
        action.peer = selectedPeerFromUi();
        action.text = readTextInput();
        model_.sendMessage(action);
    }

private:
    ChatWorkspaceModel& model_;
};
```

------

## 13.4 ASCII Renderer

```cpp
class AsciiChatRenderer {
public:
    void render(const ChatWorkspaceSnapshot& snapshot, AsciiCanvas& canvas) {
        canvas.drawBox(0, 0, 30, 20);
        canvas.drawText(1, 1, "Conversations");

        for (size_t i = 0; i < snapshot.conversations.count; ++i) {
            canvas.drawText(1, 2 + i, snapshot.conversations.rows[i].title.c_str());
        }
    }

private:
    ChatWorkspaceModel& model_;
};
```

------

## 13.5 GTK Renderer

```cpp
class GtkChatWorkspace {
public:
    void applySnapshot(const ChatWorkspaceSnapshot& snapshot) {
        // must run on GTK main loop
        updateConversationList(snapshot.conversations);
        updateTranscript(snapshot.active_thread);
        updateInspector(snapshot.device_status);
    }

private:
    ChatWorkspaceModel& model_;
};
```

Purpose:

```text
Reuse the presentation model between UI technology stacks, but not the widget/page.
```

------

# 14. App Composition Root: Dependency Injection

In the end, each app only does assembly.

## 14.1 ESP32 T-Pager LVGL

```cpp
class EspTPagerComposition {
public:
    void build() {
        board_ = makeUnique<TLoraPagerBoard>();

        EspCapabilityFactory factory(*board_);

        radio_ = factory.createPacketRadio();
        gnss_ = factory.createGnssByteStream();
        ble_ = factory.createBleGattHost();
        display_ = factory.createDisplayHost();

        identity_store_ = makeUnique<EspNvsLocalIdentityStore>();
        peer_key_store_ = makeUnique<EspNvsPeerKeyStore>();
        message_store_ = makeUnique<EspMessageStore>();

        protocol_ = makeUnique<MeshtasticProtocolStrategy>();

        identity_service_ = makeUnique<PeerIdentityService>(
            *identity_store_,
            *peer_key_store_,
            random_,
            clock_
        );

        direct_message_ = makeUnique<DirectMessageService>(
            *protocol_,
            *identity_service_,
            *radio_,
            clock_,
            app_events_
        );

        chat_model_ = makeUnique<ChatWorkspaceModel>(
            chat_service_,
            device_status_,
            app_commands_
        );

        shell_ = makeUnique<LvglShell>(*chat_model_, ...);
    }
};
```

## 14.2 Linux uConsole GTK

```cpp
class LinuxUConsoleGtkComposition {
public:
    void build() {
        board_ = makeUnique<UConsoleAio2Board>();

        LinuxCapabilityFactory factory(*board_);

        radio_ = factory.createPacketRadio();
        gnss_ = factory.createGnssByteStream();

        identity_store_ = makeUnique<SqliteLocalIdentityStore>(db_);
        peer_key_store_ = makeUnique<SqlitePeerKeyStore>(db_);
        message_store_ = makeUnique<SqliteMessageStore>(db_);

        protocol_ = makeUnique<MeshtasticProtocolStrategy>();

        identity_service_ = makeUnique<PeerIdentityService>(
            *identity_store_,
            *peer_key_store_,
            random_,
            clock_
        );

        direct_message_ = makeUnique<DirectMessageService>(
            *protocol_,
            *identity_service_,
            *radio_,
            clock_,
            app_events_
        );

        chat_model_ = makeUnique<ChatWorkspaceModel>(
            chat_service_,
            device_status_,
            app_commands_
        );

        shell_ = makeUnique<GtkShell>(*chat_model_, ...);
    }
};
```

The business objects are the same.
Replaces adapter, store, runtime, renderer.

------

# 15. Self-certification method: How this architecture proves that it is correct

This is very important. Architecture cannot rely solely on explanation, it must be proven by testing.

## 15.1 Contract Test

Each port has a contract test.

```cpp
void runPeerKeyStoreContract(IPeerKeyStore& store) {
    PeerPublicKey key = makeTestKey();

    REQUIRE(store.put(key).ok());

    PeerPublicKey loaded;
    REQUIRE(store.get(key.node_id, loaded).ok());
    REQUIRE(loaded == key);

    REQUIRE(store.remove(key.node_id).ok());
    REQUIRE(store.get(key.node_id, loaded).isNotFound());
}
```

Run the same test:

```text
FakePeerKeyStore
EspNvsPeerKeyStore
Nrf52FlashPeerKeyStore
SqlitePeerKeyStore
```

If they all pass, it means that the business layer really does not care about the storage backend.

------

## 15.2 UseCase Behavior Test

```cpp
TEST("DirectMessageService sends via any packet radio") {
    FakePeerKeyStore keys;
    FakeLocalIdentityStore identity;
    FakePacketRadio radio;
    FakeClock clock;

    MeshtasticProtocolStrategy protocol;
    PeerIdentityService peer_identity(identity, keys, random, clock);
    DirectMessageService service(protocol, peer_identity, radio, clock, events);

    keys.put(makePeerKey());
    identity.save(makeLocalIdentity());

    auto result = service.sendDirect(makeDirectMessage());

    REQUIRE(result.ok);
    REQUIRE(radio.sentPacketCount() == 1);
}
```

This test does not require ESP32, does not require Linux, does not require nRF52.
Indicates that the business core can run independently.

------

## 15.3 Renderer Test

Same snapshot:

```cpp
ChatWorkspaceSnapshot snapshot = makeChatSnapshot();
```

Tested separately:

```text
LvglChatRenderer does not change the business
AsciiChatRenderer outputs stable text
GtkChatWorkspace only updates the widget model
```

Purpose:

```text
UI rendering does not have business state.
```

------

## 15.4 Race Test

Use fake runtime to simulate the sequence of events:

```text
BLE write
Radio RX
GPS fix
Config patch
UI refresh
```

Verification:

```text
Service will not be changed directly across threads
All changes pass queue/snapshot
```

------

# 16. Final dependency direction

Must always satisfy:

```text
Renderer
  -> PresentationModel
    -> AppService
      -> UseCase
        -> Domain
        -> Ports
          <- PlatformAdapter
```

Board is only used by Factory / CompositionRoot:

```text
CompositionRoot
  -> BoardPackage
  -> PlatformFactory
```

Protocol:

```text
UseCase
  -> ProtocolStrategy
    -> Codec / CryptoFlow / Mapper
```

BLE:

```text
BleHost
  -> PhoneCore
    -> AppFacade / AppCommandSink
```

GPS:

```text
GnssDriver
  -> GnssParser
    -> LocationService
      -> TimeAuthority / AppEvent
```

UI:

```text
AppService
  -> Snapshot
    -> PresentationModel
      -> Renderer
```

------

# 17. Summary of architectural core patterns

Finally, condense the mode and purpose again.

| Mode | Where to use | What to solve |
| ------------------------- | ----------------------------- | ------------------------------- |
| Hexagonal Architecture | Overall structure | Separation of business and platform |
| Ports and Adapters | Capability interface | ESP32/nRF52/Linux replacement implementation |
| Repository | Store | NVS/Flash/SQLite unification |
| Strategy | Meshtastic/MeshCore | Protocol switching |
| Codec | Protocol encoding and decoding | Separation of protocol bytes and services |
| Mapper | External protocol ↔ Internal command | Prevent ToRadio/FromRadio from polluting the business |
| Application Service | UseCase | The only realization of stable business |
| Command Handler | UI/BLE/HostLink input | Multi-entry unified action |
| State Machine | Mesh/BLE/GPS/HostLink session | State explicit |
| Facade | Phone/App/DeviceStatus | Reduce cross-module coupling |
| Adapter | Platform driver | Packaging SDK/API |
| Proxy | Linux+nRF52 endpoint | Remote capability localization |
| Null Object | Unsupported capabilities | Eliminate null branches |
| Abstract Factory | Board → Driver | Board level variant isolation |
| Active Object | Runtime context | Eliminate race conditions |
| Event Queue | Asynchronous events | Cross-thread safety |
| Command Queue | User/protocol command | Multi-entry unification |
| Immutable Snapshot | UI status | Prevent UI from reading mutable service |
| MVVM / Presentation Model | UI reuse | LVGL/ASCII/GTK separation |
| Renderer | UI drawing | Technology stack isolation |
| Composition Root | apps | Centralized dependency injection |
| ------------------------- | ----------------------------- | ------------------------------- |
