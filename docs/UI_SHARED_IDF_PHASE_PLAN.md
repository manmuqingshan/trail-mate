# UI Shared / IDF Migration Plan

This document is used to explain: how to extract UI logic from each `apps/*` and deposit it into a reusable shared layer that is compatible with both PlatformIO and ESP-IDF entrances.

---

## 1. Goal

### 1.1 Overall goal

1. Concentrate common UI pages, shells, controllers, and runtime interfaces into `modules/ui_shared`
2. Let `apps/esp_pio` and `apps/esp_idf` Only keep the respective platform entrance, startup process and adapter
3. Avoid shared UI directly relying on Arduino or ESP-IDF specific implementation details

### 1.2 Migration principles

- Page structure, interaction and state logic are first deposited into the shared layer
- Platform differences converge to `platform/*` and app runtime adapter
- Do not maintain an implementation of the same page in PIO / IDF respectively
- For capabilities that cannot be shared temporarily, use capability-gated fallback to occupy the space

---

## 2. Layered boundary

### 2.1 `modules/ui_shared`

Host:

- page shell
- Page component
- controller / presenter
- Runtime abstract interface
- Universal fallback page

Should not directly depend on:

- `<Arduino.h>`
- `<Preferences.h>`
- `nvs.h`
- Specific board-level API

### 2.2 `apps/esp_pio` / `apps/esp_idf`

Host:

- startup / boot
- menu / app catalog entry
- loop driver
- life cycle management
- facade / runtime adapter assembly

### 2.3 `platform/*`

Host:

- Device capability implementation
- Screen/sleep/audio/GPS/storage and other platform APIs
- Arduino and IDF respective contract implementation

---

## 3. Current status (as of 2026-03-11)

### 3.1 Shared page skeleton

Has been moved to shared `shell + runtime/components/controller` page:

- `Settings`
- `Chat`
- `GPS / Map`
- `GNSS Sky Plot`
- `Contacts`
- `Team`
- `Tracker`
- `PC Link`
- `USB`
- `SSTV`
- `Protocol Probe` (internal route: `energy_sweep`)
- `Walkie Talkie`

### 3.2 apps side status

`apps/esp_pio/src` and `apps/esp_idf/src` have gradually converged to the following responsibilities:

- `startup_runtime.cpp`
- `loop_runtime.cpp`
- `app_runtime_access.cpp`
- `app_registry.cpp`

Among them `esp_idf` Also included:

- `runtime_config.cpp`
- `app_facade_runtime.cpp`
- `idf_entry.cpp`
- `idf_component_anchor.cpp`

### 3.3 Completed Cleanup

- A batch of old IDF retired stubs have been removed
- `ui_common_stub.cpp` / `ui_status_stub.cpp` in `modules/ui_shared` has been significantly shrunk
- Most of the `ui_*.cpp` retained under `apps/esp_pio/src` only have wrapper responsibilities

---

## 4. Phased plan

## Phase 0: Inventory and hemostasis

### Goal

- First clarify which pages have been shared and which ones still have app private implementation
- Stop adding duplicate page implementation

### Delivery

- Page ownership list
- App private wrapper list
- Shared shell gap list

---

## Phase 1: apps entry convergence

### Goal

Converge `apps/esp_pio` and `apps/esp_idf` into "start + loop + runtime wiring".

### Tasks

1. Align `app_catalog` / `menu` / `startup` / `loop` to shared mode
2. Clean up the historical page wrapper / registry special case in `apps/esp_pio`
3. Convergence Life cycle and runtime access of `app_runtime_access`
4. Unify the startup / loop / event driver mode of `esp_pio` / `esp_idf`

### Complete the standard

- The app directories at both ends no longer carry specific UI logic
- The app entry can stably drive the shared app catalog / shared shell

---

## Stage 2: Page sharing completed

### Goal

Really unify the page layer to shared:

- shell
- host
- fallback
- components / controller / runtime

### Tasks

1. Complete shared `shell + components/runtime`
2. Remove residual page logic on the app side
3. Unify the wiring between app catalog and shared page shell
4. Use capability-gated fallback for missing capabilities

### Complete the standard

- The page structure is only maintained in `modules/ui_shared`
- The fallback behavior is consistent
- The app side no longer copies the page implementation

---

## Phase 3: Platform capability abstraction completed

### Goal

Converge all the device capabilities that shared UI depends on into the adapter contract.

### Requires the ability to abstract

- restart
- kv / config persistence
- screen sleep
- tone / audio preview
- GPS runtime control
- tracker recording hook
- hostlink / USB capability hook
- walkie / sstv / lora support

### Placement

- Arduino is implemented in `platform/esp/arduino_common`
- IDF is implemented in `platform/esp/idf_common` and `apps/esp_idf/*runtime`

### Complete the standard

- `modules/ui_shared` no longer directly contains ESP proprietary header files
- shared only relies on platform contract

---

## Phase 4: Configuration and profile convergence

### Goal

Unify device profile, visual size, and capability differences into runtime config / page profile.

### Key points

- `tab5`
- `tdeck`
- `pager`

### Requirements

- Topbar, height, spacing, icon cards, etc. are driven by shared profile
- Board-level differences are not scattered in the page code

---

## 5. Risk points

- The shared page has completed structural migration, but the runtime hook may still have platform coupling
- PIO / IDF The life cycle rhythms of the two sets of entrances are not completely consistent, and it is easy to have differences in the order of events
- fallback If the design is too weak, it will cover up the real gaps in the short term
- Before the profile is completely unified, layout bifurcation may continue to occur on different devices

---

## 6. Acceptance criteria

- `modules/ui_shared` becomes the only source of truth for the page UI
- `apps/esp_pio` and `apps/esp_idf` are mainly responsible for startup and assembly
- Platform differences only appear in `platform/*` and runtime adapter
-
