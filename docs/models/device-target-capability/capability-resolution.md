# Target → Capability → Authority

```mermaid
flowchart LR
  Target["TargetManifestView"] --> Product["ProductDescriptor"]
  Target --> Platform["PlatformDescriptor"]
  Target --> Status["CapabilityStatus[]"]
  Target --> Binding["CapabilityBindingRef[]"]
  Target --> Authority["AuthorityBinding[]"]
  Status --> Kind["CapabilityKind"]
  Status --> State["CapabilityState"]
  Status --> Endpoint["endpoint HostKind"]
  Binding --> Provider["board provider / platform driver / runtime owner"]
  Authority --> Owner["fact owner HostKind"]
```

Capability 回答“能力处于什么状态并由哪个 endpoint 提供”；Authority 回答“哪一个 host 对某类事实负责”。

