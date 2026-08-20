# GAT562 requirements document

## 1. Document purpose

This document defines the product goals, capability boundaries, functional requirements, non-functional requirements and acceptance criteria of the `gat562_mesh_evb_pro` firmware.

The goal is not to make a "demo firmware that can be compiled", but to make one:

- capable of stable startup
- capable of stable interaction
- Can truly undertake LoRa / BLE / GNSS / local settings
- Can be used as a formal hardware environment for more subsequent hardware adaptation reference templates

.

---

## 2. Product positioning

`GAT562` is a:

- small screen device
- Limited input capabilities
- No touch
- No full keyboard
- No Chinese input
- No large-screen complex UI
- But with real wireless communication and identity capabilities

's handheld node.

 Its core value is not complex interaction, but:

- As a real Meshtastic / MeshCore dual-protocol node
- As a BLE mobile phone pairing and configuration entrance
- As a LoRa air interface node
- As an independent device with basic local UI

---

## 3. Overall goals

`gat562_mesh_evb_pro` firmware must meet the following overall goals:

1. Stable startup, no random freezes, screen freezes, or stuck in a certain initialization stage
2. Main loop stability, no UI / LoRa / BLE / GNSS / File system tasks compete with each other and become inactive
3. The UI is not an empty shell, and the page entry must correspond to the real capabilities
4. The changeable items in Settings must be truly effective and can be persisted to disk
5. LoRa, BLE, GNSS, and identity broadcast must be actual business capabilities, not mock / no-op
6. Keep the boundaries of `GAT562` clear and do not introduce functions that are not suitable for the hardware form

---

## 4. Product boundaries

### 4.1 Capabilities that must be supported

- Meshtastic real capabilities
- MeshCore real capabilities
- BLE mobile phone connection and basic configuration capabilities
- LoRa transceiver capabilities
- GNSS basic capabilities
- Local identity display
- Local setting management
-Screens saver/main menu/key business page

### 4.2 Clearly unsupported capabilities

The following capabilities do not belong to the scope of `GAT562` and must be excluded during subsequent implementation:

- All Team-related capabilities
- HostLink / PC Link
- SD / Card capabilities
- Chinese input method
- Pinyin input
- Chinese font / CJK font path
- Complex large screen page
- Heavy interaction process not suitable for limited input devices

### 4.3 Input boundary

`GAT562` only needs to support:

- English
-Numbers
-Symbols

No need to support:

-Chinese input
-Pinyin candidates
-Handwriting/touch input

---

## 5. Architecture Principles

### 5.1 Basic Principles

All subsequent implementations must abide by the following principles:

- Behavior first, abstraction second
- Stable environment first, shared modules second
- Ensure the board-level operation chain is reliable first, and then do cross-platform abstraction
- It is not allowed to change owner / lifecycle / startup order / shared boundary at the same time

### 5.2 Module boundary principle

Shared module `modules/*` should only carry:

- Protocol logic
- Pure business logic
- Runtime independent of specific hardware core

The board level or platform layer should carry:

-Pins
-Power-on sequence
-Bus initialization
-Chip coordination
-Device life cycle
- Interrupt and polling rhythm
- Host/context/runtime bound to specific peripherals

### 5.3 Hardware adaptation principles

When adding or modifying the hardware environment:

- It is not allowed to use an unstable board-level environment as a shared abstract verification baseline
- You must first have a stable reference environment
- Changes to `GAT562` must not damage the ESP environment
- Changes to shared modules must first verify that at least one ESP environment can be compiled and passed

---

## 6. Startup and runtime requirements

### 6.1 Startup requirements

After the device is powered on, the following process should be completed:

1. Power on the basic hardware
2. Displayed as working
3. The startup log is visible
4. The board-level initialization is advanced in sequence
5. Successfully enter the screensaver page or the main interface

Do not appear:

-Stuck in `lora ok`
-Stuck on `gnss ok`
-Stuck on the screensaver page but the time does not update
-Stuck on the screensaver page and the joystick does not respond
-The main loop stops after entering a certain page

### 6.2 Startup log requirements

Startup logs must:

- Can reflect the current initialization stage
- Can help determine which module is stuck
- Can be observed without relying on the logo

User preference requirements:

- Only keep rolling logs when booting
- Do not display irrelevant logos

### 6.3 Main loop requirements

The main loop must run continuously and be able to support at the same time:

- board runtime
- input poll
- UI refresh
- LoRa poll
- BLE poll
- GNSS poll
- Settings/status maintenance

The main loop must not be blocked for a long time due to any module:

- Long-term blocking
- Deadlock
- Starvation
- Full bus

 leading to global deactivation.

### 6.4 IC coordination requirements

The following chip and bus coordination must be explicitly considered:

- OLED / I2C
- GNSS / UART
- LoRa / SPI
- BLE / SoftDevice / Bluefruit
- InternalFS / Flash
- Input GPIO
- LED GPIO

Requirements:

- The pins have clear uses and no conflicts are allowed
- The initialization sequence is clear
- The polling frequency is reasonable
- Avoid long-term blocking between I2C/SPI/Flash and UI rendering

---

## 7. UI requirements

### 7.1 Overall UI requirements

UI must be a "limited but real" UI:

-The number of pages can be small
-The page capacity cannot be empty
-The entrance must be accessible
- There must be real capabilities behind the page

### 7.2 Startup page

Requirements:

- Support rolling logs
- Do not display redundant logos
- Automatically switch to the screen saver after startup is completed

### 7.3 Screensaver page

Screen saver page must support:

- Current time display
- Time continuous update
- Year, month, day and week display
- Display protocol abbreviation on the left side of the top bar: `mt` / `mc`
- The current LoRa frequency is displayed on the right side of the top bar, with the unit `MHz`
- The frequency display must handle decimals correctly, such as `478.875MHz`
- The top horizontal bar displays

Interaction requirements:

- From the screensaver page, you can use the joystick to enter the main menu
-no stuck
- No time freeze allowed

### 7.4 Main menu

Requirements:

- Can be entered from the screensaver page
- Each menu entry is valid
- No dead entry
- No feedback after entering the no-op page

### 7.5 Settings page

Requirements:

- Accessible from the main menu
- Clear classification
- All displayed items are either truly effective, or clearly not within the scope of this hardware and hidden
- A large number of empty shell items are not allowed

### 7.6 Font and typesetting

User preference requirements:

- Keep the font size of the page header as small as possible
- Keep the spacing between rows as small as possible
- Small screen information density is given priority

---

## 8. Settings functional requirements

## 8.1 System class

Each setting item in System must meet:

- Have a clear source
- Have a real current value
- Changes can take effect
- Can be placed on the disk

 It is not allowed to only display static placeholder copy.

## 8.2 Chat class

Chat settings must be implemented according to the requirements of "real access, not empty shells".

The following capabilities are required:

1. Protocol
2. TX
3. Region
4. Preset
5. Channel
6. User Name
7. Short Name
8. PSK
9. Encrypt

### 8.2.1 Protocol

Requirements:

- The current chat protocol can be switched
- At least Meshtastic / MeshCore
- The protocol used by UI / runtime / LoRa after switching is consistent

### 8.2.2 TX

Requirements:

- Configurable launch-related parameters
- Configuration items must actually apply to LoRa configuration

### 8.2.3 Region / Preset / Channel

Requirements:

- Must actually drive the wireless configuration
- Changes will affect the current protocol wireless parameters
- The displayed value is consistent with the actual effective value

### 8.2.4 User Name / Short Name

Requirements:

- Not just change the BLE name
- Must be linked to local display copy
- Must be linked to Meshtastic nodeinfo
- Must be linked to MeshCore identity-related display and air interface broadcast

That is:

- Identity seen by the local UI
- BLE broadcast/device name
- Identity information in LoRa air interface

 must be consistent or derived according to clear rules.

### 8.2.5 PSK / Encrypt

Requirements:

- It can't just be a UI check box
- It must actually affect the protocol sending and receiving
- It must be persistent

---

## 9. Communication requirements

### 9.1 Meshtastic

Must support real Meshtastic business capabilities, including but not limited to:

-Basic LoRa transceiver
-Node identity broadcast
-Channel related configuration
-BLE data path with mobile phones
- NodeInfo synchronization

### 9.2 MeshCore

Must support real MeshCore business capabilities, including but not limited to:

-Identity broadcast
-Native identity display
-Air interface identity linkage
-LoRa data path

### 9.3 Dual protocol boundary

Requirements:

- `GAT562` supports Meshtastic / MeshCore
- The screensaver page only needs to display the current protocol abbreviation
- Team related capabilities are not within the scope

---

## 10. LoRa requirements

### 10.1 Basic transceiver

Must support:

- LoRa initialization
-Receive data
-Send data
-Select parameters according to protocol

### 10.2 Wireless parameter display

Must support:

- Current frequency display
- Consistent with the actual configuration
- Decimal places are displayed accurately, and no rough rounding into wrong values ​​is allowed

### 10.3 Background processing

Must support:

- Receive polling
- Message inbound
- Corresponding protocol processing
- Transmit and resume reception

---

## 11. BLE requirements

### 11.1 Basic capabilities

Must support:

- Mobile phone connection
- Normal broadcast
- The device name is correct
- Basic transceiver links are available

### 11.2 Identity linkage

BLE names cannot be managed in isolation and must obey the local identity policy.

### 11.3 Stability

BLE must not:

- Seize the main loop and cause the UI to freeze
- Cause a startup deadlock
- Causes LoRa / GNSS deactivation

---

## 12. GNSS requirements

Must support:

- GNSS initialization
-Basic positioning data reading
- Scheduled task running
- Time calibration related capabilities

Must not:

- Only initialize once and then have no subsequent scheduling
- Cause the main loop to be dragged by the GNSS task

---

## 13. Identity and broadcast requirements

### 13.1 Native identity

This machine must have a unified identity source, including at least:

- long name
- short name
- node id

### 13.2 Screen saver display

User preference requirements:

- Screen saver/display page only needs to display a short `node id`

### 13.3 Air interface linkage

Must support:

- Meshtastic Identity Broadcast
- MeshCore Identity Broadcast
- Native display copywriting linkage

Requirements:

- After the settings are modified, the identity broadcast will remain consistent with the native display

---

## 14. Input requirements

### 14.1 Joystick/Buttons

Must support:

- Up, down, left, and right middle buttons
- Input debounce
- Activity detection
- Wake up from screensaver
- Menu navigation

### 14.2 Input debugging requirements

During the troubleshooting phase, the input layer should be able to output:

-Original pin status
-Direction event after debouncing
-Activity timestamp

But the official version should not keep high-frequency noise logs for a long time.

---

## 15. File system and persistence requirements

Must support:

- Setting up persistence
- Message persistence
- Peer information persistence

Requirements:

- There is a recovery strategy when the file system is damaged
- The recovery strategy cannot cause endless restarts or long-term freezes
- The persistence path cannot stop the main loop

---

## 16. Volume and resource requirements

### 16.1 Flash

Requirements:

- Firmware must be stable for burning
- Not just "compilation is not 100%"
- Actual UF2/bootloader compatibility must also be met

### 16.2 RAM

Requirements:

- It is not allowed to freeze due to exhaustion of RAM after UI/protocol/file system combination
- Must pay attention to runtime peaks, not just static compiled data

### 16.3 Streamlining requirements

In order to control the size, it is necessary to remove:

- Pinyin input
- Chinese input method
- Chinese font library/CJK path

---

## 17. Acceptance criteria

`GAT562` The environment must meet at least the following standards before it can enter the "continuously evolveable" state.

### 17.1 Startup acceptance

- Can be burned successfully
- The rolling log can be seen after power-on
- Will not be stuck in the startup state
- Can enter the screen saver page

### 17.2 UI Acceptance

- The screen saver time is continuously updated
- The date/week is displayed normally
- The protocol abbreviation is displayed normally
- The frequency display is accurate
- The main menu can be entered from the screen saver
- The menu can be navigated normally

### 17.3 Settings acceptance

- The setting items are not empty shells
- The changeable items can actually take effect
- It can be persisted
- It will be maintained after restarting

### 17.4 Communication acceptance

- BLE can be connected
- LoRa can send and receive
- Meshtastic can broadcast identity
- MeshCore can broadcast identity
- User Name / Short Name is effective for local display and air interface identity linkage

### 17.5 Stability acceptance

-Continuous operation will not cause the main loop to stop due to a single module
-The UI will not enter a static state of suspended animation
-The joystick will not be deactivated
-The log will not stop at a fixed stage and the system will become unresponsive

---

## 18. Development sequence suggestions

For subsequent recovery and development, it is recommended to proceed strictly in the following order:

1. Stable startup chain
2. Stable input chain
3. Stable UI basic page
4. Stable LoRa transceiver
5. Stable BLE connection
6. Stabilize GNSS scheduled tasks
7. Access real Settings
8. Access real identity linkage
9. Complete Meshtastic / MeshCore dual-protocol business closed loop

Reverse order is prohibited, for example:

- Pump shared first when the startup chain is unstable modules
- Stack pages first when the input is deactivated
- Do high-level abstraction before LoRa/BLE is running smoothly

---

## 19. Conclusion

The goal of `GAT562` is not to make a "large and comprehensive" multimedia terminal, but to make one:

- Small screen
- Limited input
- No Team
- No Chinese input
- No SD
- No HostLink

But with real:

- Meshtastic
- MeshCore
- BLE
- LoRa
- GNSS
- Native Settings
- Stable device environment for identity linkage

 capability.

All subsequent implementation, reconstruction, and shared module extraction must obey this goal and boundary.
