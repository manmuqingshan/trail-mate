# 目标 Manifest、Capability 与 Authority

模型状态：**confirmed；它是产品配置模型，不是硬件清单**

## 一个 TargetManifestView 表达什么

`TargetManifestView` 把同一目标的六类事实组合为只读视图：

1. `TargetId`
2. `ProductDescriptor`
3. `PlatformDescriptor`
4. board / runtime / UI binding
5. `CapabilityStatus[]` 与 capability binding
6. `AuthorityBinding[]`

它回答“这个产品目标由哪些 host 和 provider 组合、能力处于什么状态、状态由谁拥有”，而不是只列编译宏。

## Capability 是状态，不是布尔值

`CapabilityState` 的实际词汇：

```text
Unsupported → Absent → Present → Unbound → Initializing → Ready
                                      ↘ Degraded / Simulated / Error
```

枚举不是严格线性状态机，但它避免了把“不支持、硬件缺失、尚未绑定、初始化中、降级、模拟和故障”都压成 `false`。

`CapabilityKind` 覆盖 LoRa、GPS、BLE、HostLink、Storage、Display、Input、Battery、Network、Audio 与 MapStorage。

## Link mode 解释能力位于哪里

- Radio：`LocalRadio / PacketProxy / CommandProxy`
- GPS：`LocalUart / RawStreamProxy / FixProxy / CommandProxy / Simulated`

这让“目标具备 GPS”继续下钻为“本地 UART、原始流代理还是 fix 代理”，避免主控与 companion 同时声称 owner。

## Authority 是数据所有权

`AuthorityBinding` 把 `Identity / PeerKeyStore / NodeStore / MessageStore / Location / Time / Config / DeviceStatus / UiState` 绑定到 `HostKind owner`。Capability 表示能否提供能力，Authority 表示谁对事实负责，两者不能合并成同一字段。

## 需要继续验证的部分

- Manifest 是 view，具体 target declarations 是否完整覆盖所有构建目标需要单独矩阵检查。
- Authority 绑定冲突目前由谁验证，需要追踪 product composition。
- `CapabilityStatus` 只含 state 和 endpoint host，没有 reason code；UI 的诊断能力可能不足。

## 下钻与证据

- [Target → Capability → Authority 解析](capability-resolution.md)
- `modules/core_device/include/device/target_manifest_types.h`
- `modules/core_device/include/device/capability_types.h`
- `modules/core_device/include/device/authority_types.h`
