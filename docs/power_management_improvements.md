# Power management improvement suggestions

The current code has implemented some power strategies (for example: turning off GPS/NFC/SENSOR when the screen is sleeping, low battery binning + limiting brightness/GPS frequency, I2C bus mutual exclusion, preventing low battery from entering audio applications, etc.). This document only retains **power-related improvement points that have not been completed or can be further improved** for reference in subsequent iterations.

---

## 1) PMU capabilities are underutilized (medium to high yield)

**Current situation**: BQ25896 is currently mainly used for `isCharging()`, `getBattVoltage()`, and has not yet incorporated VBUS, battery charging current, system voltage and other capabilities into UI display and policy decisions.

**Suggestions**:

- **UI Visibility**: Add:
 - Whether to connect to USB: Use `getVbusVoltage()` to determine whether it is higher than a certain threshold;
 - Charging current: `getChargeCurrent()` to facilitate the distinction between "slow charging/fast charging/not charging".
- **Hardware-level low-voltage protection**: If the BQ25896 driver or XPowers library supports `setSysPowerDownVoltage()` (or equivalent register), set a reasonable system under-voltage shutdown threshold in `begin()` or the first PMU initialization to avoid battery over-discharge.
- **Cooperate with software shutdown**: Keep the existing `softwareShutdown()` logic of not shutting down when USB is plugged in. The shutdown threshold of PMU is only used as a hardware cover.

---

## 2) Battery Gauge configuration/calibration/abnormal recovery (medium profit, need to check the data sheet)

**Status quo**: The SOC, voltage, current and temperature have been read through BQ27220, and the value range check and multi-source fallback have been done in `TLoRaPagerBoard.cpp` (when SOC fails, try to read the register directly, and then fall back to PMU / ADC voltage), but the overall still uses the gauge in a "read-only" manner, lacking profile/OCV calibration and an exception recovery strategy for the gauge itself.

**Suggestions**:

- **Documentation and Constraints**: First confirm whether the I2C interface of BQ27220 on the hardware supports configuration/reset (some boards have pins such as GAUGING fixed and are only suitable for read-only). Explicitly state "currently read-only usage + multi-source fallback" in `docs/` or code comments, and list future extensibility options.
- **Exception recovery**: Based on the existing rollback logic, if gauge reading fails multiple times (such as 3–5 times) or the SOC jumps significantly, you can try:
 - Call `gauge.begin()` or the soft reset interface provided by the library;
 - Record a log with a count to facilitate subsequent troubleshooting of hardware/firmware issues.
- **Calibration/Learning Process Reserved**:
 - Reserve two configuration points of "design capacity/full charge capacity", allowing mass production or advanced users to update through the command line/settings page;
 - Write separate "learning cycle/OCV calibration" documentation and scripts for production lines or developers to execute when needed.

---

## 3) Temperature/Charging Safety Constraints (Medium Gain)

**Status quo**: The temperature has been read from Gauge and a basic range check has been done, but the temperature information has not yet been incorporated into the charge and discharge or performance limit strategies.

**Suggestions**:

- **Temperature gear**: Continue to use `temp_c` in the existing battery reading path (such as where gauge is read in `TLoRaPagerBoard`), and aggregate it into temperature gears (Normal / Hot / Cold) at a higher level.
- **Policy example** (thresholds can be configured):
 - Overheating (e.g. > 45°C):
 - Limit or turn off fast charging (if PMU supports it);
 - Reduce the upper limit of the backlight or lock it at the current lower brightness;
 - Optional: prompt "The device is overheated and the performance has been limited".
 - Supercooling (e.g. < 5°C):
 - Prompt "At low temperature, the power display may be inaccurate";
 - Limit high current discharge when necessary (if the software layer can intervene in the business).
- **Landing point**: Add `handle_temperature(temp_c)` to the `handle_low_battery()` sibling or power state summary, or merge the "power level + temperature level" into a unified `PowerState`, which will be uniformly consumed by backlight/GPS/audio/Mesh and other modules.

---

## 4) CPU/system-level scheduling in low-power mode (medium profit, large changes)

**Current situation**: There is currently no CPU frequency adjustment, and there is no unified "task sleep/pause" mechanism; WiFi/BT is not explicitly turned off in low-power scenarios.

**Suggestions**:

- **CPU Frequency**: Call `setCpuFrequencyMhz(80)` (or 160) when the screen is asleep, and resume 240 after waking up. Required:
 - Encapsulate `setCpuFrequencyMhz` at the board layer;
 - Call in enter/exit of `screenSleepTask` or `enterScreenSleep()` / `exitScreenSleep()`;
 - Verify on the target board Are peripherals such as USB and timers sensitive to frequency changes?
- **Task Scheduling**:
 - Add a "pause flag" or directly suspend the task for non-critical tasks (map tile downloads, non-real-time logs, etc.), pause when the screen sleeps, and resume when waking up;
 - Critical paths (key input, USB, button power management, battery detection, etc.) remain running.
- **BT / WiFi**:
 - If BLE has no connection requirements during screen sleep, you can use NimBLE's low-power/disconnect interface in `enterScreenSleep()` and resume it in `exitScreenSleep()`;
 - If WiFi exists in the project, turning it off during screen sleep can significantly save power; if WiFi is not currently used, you can leave it alone.

---

## 5) LoRa/Mesh strategy under low power (medium benefit, integrated with existing power_tier)

**Status quo**: `handle_low_battery()` + `board.setPowerTier()` has established three power levels of 0 / 1 / 2, and has been implemented in backlight, GPS sampling interval and SSTV / Walkie There are restrictions on the audio path, but the transmit power and packet sending strategy of LoRa/Mesh have not yet been associated with `power_tier`.

**Suggestions**:

- **Transmit power and coverage**:
 - Normal: maintain current settings;
 - Low (tier 1): limit transmit power to medium values ​​(e.g. 10–14dBm);
 - Critical (tier 2): further reduce to 5–10dBm, or allow only necessary control/alarm messages.
- **Packet sending frequency**:
 - For periodic heartbeat/location broadcast, appropriately extend the interval (such as ×2 / ×4) when the battery is low, to avoid maintaining high-frequency packet sending when the battery is already low;
 - Keep interactive messages immediacy and only send them when necessary when the user takes an active action.
- **Configuration Entry**:
 - Add a "Low Power Mesh Policy" simple switch or mode enumeration (such as conservative/default/aggressive) to the settings page, corresponding to different power and interval combinations;
 - Explain in the document the approximate impact of each mode on battery life and coverage to facilitate user selection.

This section can reuse the existing `power_tier` abstraction, only need to add a small amount of logic to the Mesh adaptation layer, and the changes are relatively controllable.
