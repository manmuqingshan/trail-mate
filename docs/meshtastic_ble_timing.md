# Meshtastic BLE interaction timing

This article organizes the BLE interaction timing between the `Meshtastic` Android client and the current `trail-mate` nRF52 firmware. The purpose is to provide a unified baseline for subsequent troubleshooting of "App keeps stopping at connecting" and `FromRadio/FromNum` compatibility issues.

This article does not discuss UI, GPS, LoRa or flash persistence issues, but only focuses on Meshtastic BLE handshake, configuration flow and connection completion determination.

> Normative spec: `docs/specification/MESHTASTIC_ANDROID_BLE_CONNECTION_SPEC.md`.
> This file is a diagnostic timing note. If it conflicts with the specification, the specification wins.

## 1. Overall conclusion

The Meshtastic BLE connection on the Android side is not:

- Connection successful
- Receive one or two notifications
- Consider it connected immediately

The real determination is a two-phase configuration handshake:

1. GATT connection is established, service discovery and notification subscription are completed.
2. App issues Stage 1: `ToRadio.want_config_id = CONFIG_NONCE`
3. The device returns a whole configuration stream until `config_complete_id(CONFIG_NONCE)`
4. App issues Stage 2: `ToRadio.want_config_id = NODE_INFO_NONCE`
5. The device returns a whole `node_info` stream until `config_complete_id(NODE_INFO_NONCE)`
6. App switches the connection status from `Connecting` to `Connected`

Therefore, if the App stays at "Connecting" for a long time, it usually does not mean that the BLE physical link is not connected, but that the Meshtastic configuration handshake has not been fully acknowledged.

## 2. Android side timing

### 2.1 Connection establishment

The Android side BLE entrance is:

- `.tmp/meshtastic-android/core/network/src/commonMain/kotlin/org/meshtastic/core/network/radio/BleRadioInterface.kt`
- `.tmp/meshtastic-android/core/ble/src/commonMain/kotlin/org/meshtastic/core/ble/KableMeshtasticRadioProfile.kt`

High-level timing:

1. `BleRadioInterface.connect()` establishes GATT connection
2. `discoverServicesAndSetupCharacteristics()` completes service/profile establishment
3. `fromRadio` / `logRadio` observation is started
4. After a short `CCCD_SETTLE_MS` waiting window
5. Call `service.onConnect()`

Corresponding key code location:

- `BleRadioInterface.discoverServicesAndSetupCharacteristics()`
- `BleRadioInterface.service.onConnect()`

### 2.2 How App reads FromRadio

The Android side does not simply rely on `FROMNUM` notification.

The behavior of `KableMeshtasticRadioProfile.fromRadio` is as follows:

1. If the device supports `FROMRADIOSYNC`, subscribe to this feature directly.
2. If not supported, fall back to legacy mode:
 - Subscribe to `FROMNUM`
 - But actively trigger a drain at the same time
 - Then read `FROMRADIO` in a loop
 - Read until an empty packet is returned

More specifically, legacy Two key actions will occur in the mode:

1. `triggerDrain.tryEmit(Unit)` will actively trigger once when the collector starts.
2. `sendToRadio()` will trigger `triggerDrain.tryEmit(Unit)` again every time it sends a `ToRadio`.

This means:

- After sending `want_config_id`, the App will actively start `read(FROMRADIO)`
- Even if there is no `FROMNUM` notification at a certain moment, the App will not necessarily stop
- `FROMNUM` is more like a steady-state prompt, not the only driving condition

Corresponding key code location:

- `KableMeshtasticRadioProfile.fromRadio`
- `service.observe(fromNum)`
- `service.read(fromRadioChar)`
- `triggerDrain.tryEmit(Unit)`
- `sendToRadio()`

### 2.3 Two-phase handshake

The upper-layer state machine is in:

- `.tmp/meshtastic-android/core/data/src/commonMain/kotlin/org/meshtastic/core/data/manager/MeshConnectionManagerImpl.kt`
- `.tmp/meshtastic-android/core/data/src/commonMain/kotlin/org/meshtastic/core/data/manager/MeshConfigFlowManagerImpl.kt`
- `.tmp/meshtastic-android/core/data/src/commonMain/kotlin/org/meshtastic/core/data/manager/FromRadioPacketHandlerImpl.kt`

#### Stage 1

1. After the connection is established, `MeshConnectionManagerImpl.handleConnected()` calls `startConfigOnly()`
2. `startConfigOnly()` sends:
   - `ToRadio.want_config_id = HandshakeConstants.CONFIG_NONCE`
3. App starts consuming `FromRadio`
4. `FromRadioPacketHandlerImpl` distributes different variants to config flow manager / config handler
5. When receiving:
   - `FromRadio.config_complete_id == CONFIG_NONCE`
6. `MeshConfigFlowManagerImpl.handleConfigComplete()` Enter Stage 1 complete

Stage 1 Typical receiving content during the period includes:

- `my_info`
- `deviceui`
- `metadata`
- `config`
- `moduleConfig`
- `channel`
- `fileInfo`

#### Stage 2

After Stage 1 is completed:

1. `MeshConfigFlowManagerImpl.handleConfigOnlyComplete()` first sends a heartbeat
2. Then call `startNodeInfoOnly()`
3. Send:
   - `ToRadio.want_config_id = HandshakeConstants.NODE_INFO_NONCE`
4. App receives a string of `node_info`
5. When receiving:
   - `FromRadio.config_complete_id == NODE_INFO_NONCE`
6. `MeshConfigFlowManagerImpl.handleNodeInfoComplete()` is actually executed:
   - `serviceRepository.setConnectionState(ConnectionState.Connected)`

In other words, only Stage 2 is completed, Android will consider the connection complete.

## 3. Current firmware side timing

The current nRF52 side entrance is mainly at:

- `platform/nrf52/arduino_common/src/ble/meshtastic_ble.cpp`
- `modules/core_phone/src/meshtastic/meshtastic_phone_core.cpp`

Note: This section describes the interaction semantics that must be achieved, and does not prove that a historical implementation has satisfied this semantics. The current implementation status must be based on the source code and main specification regression.

### 3.1 BLE service layer

The key features exposed by the current Meshtastic BLE service:

- `ToRadio`
- `FromRadio`
- `FromNum`
- `LogRadio`

The key sequence that the main loop must reach is:

1. `processPendingToRadio()`
2. `handleToPhone()`
3. `prepareReadableFromRadio()`

That is:

- First process the `ToRadio` written by the mobile phone
- Then let `MeshtasticPhoneCore` produce the next frame `FromRadio`
- Then preload the next frame into the `FromRadio` characteristic and wait for the App to read

### 3.2 PhoneCore Configuration flow

`MeshtasticPhoneCore` will start spitting out configuration snapshots after receiving `want_config_id`.

The sequence of configuration streams that can be seen in the current log is roughly:

1. `cfg#N start`
2. `frame my_info`
3. `frame deviceui`
4. `frame self_node`
5. Subsequent frames such as metadata/config/module/channel/node
6. `cfg#N complete`

Corresponding log prefix:

- `[BLE][mtcore][cfg#N] start`
- `[BLE][mtcore][cfg#N] frame ...`
- `[BLE][mtcore][cfg#N] complete`

After encoding, each frame will become a `MeshtasticBleFrame` and handed over to the transport layer.

### 3.3 FromNum / FromRadio current implementation

The basic model of the target nRF52 transport is:

1. `MeshtasticPhoneCore.notifyFromNum(from_num)` hands the real `from_num` to nRF52 transport
2. nRF52 transport puts `from_num` into the fixed-depth pending queue
3. The main loop calls `prepareReadableFromRadio()` to let `PhoneCore.popToPhone()` produces the next frame and preloads it into `FROMRADIO`
4. After the phone completes `FROMNUM` subscription and has preloaded frames, transport uses the same real `from_num` to send notifications
5. Enter when App reads `FROMRADIO` `onFromRadioAuthorize()`; This callback only records the read/consumption status and no longer produces protobuf frames
6. The main loop calls `consumeReadableFromRadio()` to consume the read pre-assembled frames and immediately try to change to the next frame
7. The App continues to read until `FROMRADIO` returns an empty packet, indicating that this round of drain is completed

The key constraint is: an artificially empty `FROMRADIO` value cannot be released first between two frames. `consumeReadableFromRadio()` is only allowed to write the characteristic with a length of 0 after confirming that
`PhoneCore.popToPhone()` does not have the next frame; otherwise Android's
`read-until-empty` may determine the configuration flow as drain completion in advance and miss the subsequent `config_complete_id`.

There is a stability boundary here: Bluefruit's read-authorize callback cannot perform `popToPhone()`, MQTT proxy polling, nanopb encoding, or serial port log reactivation. Otherwise, when mobile phone high-frequency drain, over-the-air packet inbound and MQTT downlink are mixed together, USB re-enumeration/disconnection without HardFault log may occur on the nRF52 side.

In order to troubleshoot, the firmware currently also prints these logs:

- `[BLE][nrf52][mt][flow] link-up ...`
- `[BLE][nrf52][mt][flow] from_num subscribed=...`
- `[BLE][nrf52][mt][flow] from_num pending source=... depth=...`
- `[BLE][nrf52][mt][flow] from_num notify value=... source=...`
- `[BLE][nrf52][mt] from_radio read len=...`
- `[BLE][nrf52][mt] from_radio read empty reason=...`

## 4. The core relationship between the timing of both parties

Putting Android and firmware together can be summarized into the following main line:

1. GATT connected
2. App subscribes to `FROMNUM` / `LOGRADIO` and establishes `fromRadio` collector
3. App calls `service.onConnect()`
4. App sends `want_config_id = CONFIG_NONCE`
5. App actively starts draining `FROMRADIO`
6. The firmware provides frame by frame:
   - `my_info`
   - `deviceui`
   - `self_node`
   - ...
   - `config_complete(CONFIG_NONCE)`
7. App switches to Stage 2 and sends `want_config_id = NODE_INFO_NONCE`
8. App drains `FROMRADIO` again
9. The firmware provides several `node_info`
10. The firmware sends `config_complete(NODE_INFO_NONCE)`
11. App is switched to `Connected`

## 5. The most noteworthy deviation points on the current nRF52 side

Based on the real code on the Android side, the most important observation point currently is not "whether there are a large number of `FROMNUM notify`", but the following.

### 5.1 Is the App really reading FromRadio?

Since Android will actively drain `FROMRADIO` after sending `want_config`, so if you cannot see it in the firmware log:

- `[BLE][nrf52][mt] from_radio read len=...`
- `[BLE][nrf52][mt] from_radio read empty`

The problem is more like:

- `FromRadio`'s GATT read in nRF52/Bluefruit There is no real reading of the App
- It is not that the configuration content itself is wrong

### 5.2 Is the empty packet semantics closed?

The legacy mode on the Android side will always read(FROMRADIO)` until an empty packet is returned to end this round of drain.

So the firmware must ensure:

1. When there is a frame, read returns the current frame
2. After the current frame is read, the next read should get the next frame
3. When there are no more frames in this round, an empty packet must be returned

If the last step is not established, the App may always think that the configuration flow is not completely completed.

### 5.3 Stage 1 completion does not equal connection completion

Even if `cfg#1 complete` has appeared in the firmware log, the App may still display "Connecting".

Because for Android:

- Stage 1 complete is only the configuration reading is completed
- You need to run another Stage 2 node-info handshake
- Only when the second `config_complete_id` arrives, the status will become `Connected`

So any link that only completes Stage 1 will keep the UI in `Connecting`

### 5.4 Historical issue: loop Stack overflow

Previously, the nRF52 side has confirmed a historical issue related to the strong BLE configuration stream:

-The configuration stream construction path once pushed the `loop` task stack to `stack_hwm=0`
- This will cause other objects in the same task to be trampled
- Manifested as damaged GPS status, abnormal `SAT` numbers, and guard being overwritten with the word `[BLE`

This problem has been significantly alleviated after the large object stack reduction of `MeshtasticPhoneCore`, but it shows:

- Meshtastic The configuration flow is not a "normal low-overhead path"
 - any timing analysis needs to be looked at together with the task context and memory behavior

## 6. Use this timing to determine faults

Following troubleshooting can be done according to the following judgment method.

### Case A

Phenomena:

- There is `cfg#start`
- There is `cfg#complete`
- But there is no `from_radio read...`

Judgment:

- The App has issued `want_config`
- but the `FROMRADIO` read path does not actually hit the nRF52 firmware
- The GATT read compatibility of `FromRadio` characteristic should be checked

### Situation B

Phenomena:

- There is `from_radio read len=...`
- But there is no `from_radio read empty`

Judgment:

- drain-until-empty does not close the loop
- App is probably still waiting for the end of this round of reading

### Situation C

Phenomena:

- Stage 1's `config_complete(CONFIG_NONCE)` has been sent
- But the App did not enter the second `want_config_id`

Judgment:

- The App did not successfully consume the Stage 1 completion signal
- Priority should be given to checking whether the `config_complete_id` frame really arrives at Android `FromRadioPacketHandler`

### Situation D

Phenomena:

- Stage 2 has also been completed
- But the App is still `Connecting`

Judgment:

- You should check whether the Android side `MeshConfigFlowManagerImpl.handleNodeInfoComplete()` is really triggered
- Or check whether there is node-info flow interruption/state machine being rolled back during Stage 2

## 7. Follow-up suggestions

All subsequent Meshtastic BLE fixes should give priority to the following facts in this article:

1. Android will actively drain `FROMRADIO`
2. `FROMNUM` is not the only driving condition
3. Connection completion relies on two stages `config_complete_id`
4. `FROMRADIO` must have stable semantics of "continuously reading frames until empty packets"
5. On nRF52, we must not only pay attention to the protocol sequence, but also the task stack and callback context

If you continue to debug, it is recommended to keep the following logs:

- `[BLE][nrf52][mt][flow] link-up ...`
- `[BLE][nrf52][mt][flow] from_num subscribed=...`
- `[BLE][nrf52][mt][flow] from_num pending source=... depth=...`
- `[BLE][nrf52][mt][flow] from_num notify value=... source=...`
- `[BLE][nrf52][mt] from_radio read len=...`
- `[BLE][nrf52][mt] from_radio read empty reason=...`
- `[BLE][mtcore][cfg#N] start/frame/complete`
- `[BLE][mtcore][rt] stage=... stack_hwm=...`

These logs are enough to converge the problem to:

-GATT Unable to read
- drain semantics does not close the loop
- Stage 1 not completed
- Stage 2 not completed
- or runtime stack/memory issue
