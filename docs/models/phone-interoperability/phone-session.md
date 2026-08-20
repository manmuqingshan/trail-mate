# IPhoneAppFacade to protocol core

```mermaid
sequenceDiagram
  participant App as Phone application adapter
  participant Facade as IPhoneAppFacade
  participant Core as Selected phone protocol core
  participant Transport as Platform transport
  App->>Facade: getTime/getLocation/getStatus/getConfig
  Facade-->>App: typed view + PhoneResult
  App->>Facade: applyConfigPatch or submitCommand
  Facade->>Core: map application intent
  alt Meshtastic
    Core->>Transport: MeshtasticBleFrame / config protocol
  else MeshCore
    Core->>Transport: MeshCoreBleFrame / contact-status protocol
  end
  Transport-->>Core: protocol response/event
  Core-->>Facade: typed result
```

Common Facade does not mean that two protocol cores have the same wire commands, queues, or configuration fields.
