# P1 · [Design is not formed] Configuration lacks version, verification and atomic commit owner

Status: **acknowledged**
Category: **Design Defects/Configuration and Environment**

## Conclusion

Configuration data exists, but the Configuration aggregate responsible for versioning, verification and atomic changes is not formed. `MeshConfig` is located in `chat_types.h`, `AppConfig` collects cross-domain settings, and default values, compatible conversions and persistence are scattered in `core_sys` and platform store.

## Mix of current responsibilities

- Domain settings: protocol, team, positioning, display and other business meanings.
- Product combination: whether a certain setting is allowed for a certain target.
- Persistence schema: field versions, defaults and migrations.
- Platform mechanism: NVS / file / flash reading and writing.

These responsibilities need to be coordinated, but cannot be "owned by default" by one huge struct and multiple platform loaders.

## Target model

- `ConfigurationSnapshot`: Immutable committed snapshot with schema version.
- typed settings: `CommunicationSettings`, `TeamSettings`, `PositioningSettings`, etc.
- `ConfigurationPolicy`: cross-field and capability constraints.
- `ConfigurationService`:validate → migrate → atomically commit.
- `IConfigurationStore`: Only save/read serialized snapshots.

## Invariants

1. Invalid configuration cannot be partially written.
2. Migrations must be explicit, repeatable, and document source versions.
3. Capabilities that are not supported by the target cannot be forcibly enabled by configuration.
4. The platform store does not define business default values.

Evidence: `modules/core_chat/include/chat/domain/chat_types.h`, `modules/core_sys/include/app/app_config.h`, `platform/esp/arduino_common/src/app_config_store.cpp`.
