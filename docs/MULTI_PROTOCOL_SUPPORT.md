# 多协议支持实现说明

本文档说明当前 Trail Mate 在 Meshtastic、MeshCore、Reticulum 三协议场景下的实现方式。
**协议不会在运行中自动判定或切换**，而是通过设置明确选择其中一个协议运行。

---

## 1) 协议选择方式

当前协议由设置项确定：
- `AppConfig::mesh_protocol`
- 持久化键：`mesh_protocol`
- 取值：
  - `Meshtastic`
  - `MeshCore`
  - `Reticulum`

系统启动时读取配置，并通过 `ProtocolFactory` 创建对应的 adapter。运行期间不会动态切换。

---

## 2) 代码结构（单协议运行）

核心思路：
- **UI 层只与 `IMeshAdapter` 交互，不感知协议**
- 只创建一个 adapter（Meshtastic、MeshCore 或 Reticulum）
- Radio 任务的原始数据直接交给选定 adapter 处理

### 结构 ASCII 图

```
+------------------+
|   UI / UseCase   |
+------------------+
          |
          v
+-------------------------+
|     IMeshAdapter        |
+-------------------------+
          |
          v
+------------------+
| Meshtastic OR    |
| MeshCore OR      |
| Reticulum        |
| Adapter          |
+------------------+
```

---

## 3) 接收流程（无动态判定）

1. Radio 任务收到原始包
2. 直接调用当前 adapter 的 `handleRawPacket()`
3. 解析出的文本消息进入 `ChatService`

> 不再进行协议预判，也不维护节点协议映射。

### 业务状态统一边界

“单协议运行”不表示每个协议可以各自拥有一套 UI 消息状态。

MT / MC / RT adapter 只能把协议事实映射成 protocol-aware event：

- message identity
- queued / sending / sent / delivered / failed
- failure kind
- read/unread reference
- retry eligibility

这些事实必须进入共享的 `MessageLedger`、`ChatDeliveryEventProjector`、
`ReadStateLedger` 和 conversation projection。UI 上的气泡状态 badge、
conversation unread badge、发送失败反馈和 retry 动作都不得从协议 adapter
私有状态直接推断。

完整 owner 边界见
`docs/specification/RUNTIME_OWNERSHIP_BOUNDARY_FREEZE.md`。

---

## 4) Settings 规划

LVGL Settings 按职责分组，而不是按旧的 "Chat/Network/System" 混合分组：

- `Profile`: user name, short name, active protocol, message/contact alerts.
- `Mesh`: protocol-specific mesh identity and internet bridge settings.
  - Meshtastic: region, active chat channel, primary/secondary channel enable/name/PSK, primary/secondary MQTT uplink/downlink flags, encryption mode, Meshtastic MQTT client settings.
  - MeshCore: channel slot/name/key and MeshCore MQTT client settings.
  - Reticulum: bearer policy, local identity display, Wi-Fi gateway host/port, auto Wi-Fi, anonymous peer.
- `Radio`: protocol-specific LoRa/air parameters.
  - Meshtastic: preset/manual modem parameters, TX power, hop limit, TX enable, channel slot, frequency offset/override, duty/utilization controls.
  - MeshCore: region preset, frequency/bandwidth/SF/CR, TX power, repeat/flood/profile controls.
  - Reticulum: manual LoRa parameters and TX enable when bearer policy includes LoRa; Wi-Fi-gateway-only mode hides LoRa radio settings.
- `Wi-Fi`: Wi-Fi enable/status/scan/SSID/password/connect/disconnect.
- `Location`: GPS receiver, position strategy, NMEA export, map source, contours, and track recording.
- `Device`: locale/IME, screen, speaker, vibration, C6 companion, time zone/date-time, and battery gauge calibration.
- `Maintenance`: firmware update, settings backup/restore, debug logs, mesh/node/message resets, factory reset.

Bluetooth is intentionally absent from Settings. For the ESP Arduino firmware profile, `TRAIL_MATE_ENABLE_BLE=0`, the NimBLE dependency is not part of the PlatformIO lib set, and the real ESP `src/ble/` implementation is excluded from the firmware build. Persisted or restored `ble_enabled=true` values are normalized back to `false`.

### Settings implementation contract

Settings items are bound through `SettingId` and `settings::ui::spec` instead of ad-hoc `pref_key` string dispatch in the page logic. Protocol visibility, dynamic option ownership, translated-label rules, and settings-store ownership are centralized in that spec layer. Page event handlers switch on `SettingId`, while `pref_key` remains only as the persistence key and as a one-time binding input for legacy aggregate item declarations.

Resource constraints are part of the contract:
- MT/MC channel key helpers use fixed-size caller-owned buffers and bounded key lengths.
- SD backup/restore blob handling uses bounded stack buffers instead of dynamic byte vectors.
- Wi-Fi scan fills the Settings page's fixed network slots directly; it does not allocate an intermediate dynamic scan list.

## 5) Phone Independence Boundary

The device-side goal is to operate without a phone. The first required channel-management surface is:

- Meshtastic Primary channel: enabled flag, name, PSK, MQTT uplink/downlink flags.
- Meshtastic Secondary channel: enabled flag, name, PSK, MQTT uplink/downlink flags.
- MeshCore channel: slot, name, key.

These settings write into the existing `AppConfig` / `MeshConfig` fields and use the same NVS and SD backup/restore paths as the runtime configuration. Meshtastic PSK generation creates a 16-byte key by default, encoded as 32 uppercase hex characters; manual entry also accepts the existing 16/32-byte Meshtastic PSK formats. MeshCore channel key generation creates a 16-byte key.

## 当前实现状态说明

- Meshtastic：功能完整（含 NodeInfo、channel identity/config、plain MQTT client）
- MeshCore：RAW_CUSTOM 文本收发闭环，channel slot/name/key 可在设备端配置，plain MQTT client 已有独立配置入口
- Reticulum：作为默认独立协议运行，Settings 暴露 bearer/gateway/identity 配置；产品 call path 只支持 Sideband-compatible LXST
- BLE 手机桥：ESP Arduino 当前产品固件不编译、不启动、不在 Settings 中展示

---

## 扩展建议

如需支持更多协议：
- 在 Settings 中新增协议选项
- 新建对应 adapter
- 仍保持“运行时单协议”的策略，避免混跑与误判
