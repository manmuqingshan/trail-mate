# Use Case: Switch active protocol and submit wireless configuration

Status: **confirmed**

Business boundary: network, identity and directory

Main participants: device user
Supporting systems: Target Capability, AppConfig, MeshAdapterRouter, protocol partition storage, Radio owner

## User Goals

Choose a current protocol between Meshtastic, MeshCore and Reticulum so that the identity, channel/key, wireless parameters, backend and UI use the same committed configuration.

## Preconditions and triggers

- The target manifest declares support for the selected protocol and required radio/bearer.
- The user selects the protocol in Settings, or modifies the region, channel, PSK, LoRa preset, and Reticulum bearer of the current protocol.
- Modify before entering the editing state, and you cannot pretend that the overall configuration has been successfully applied when each field changes.

## Success Scenario

1. `AppConfig` verifies protocol, region, frequency, bandwidth, SF, CR, transmit power, channel and key combination.
2. `MeshAdapterRouter` stops the old backend and clears active references belonging to the old protocol, but does not delete data in other protocol partitions.
3. Load the local identity, peer facts, channel key and protocol settings from the corresponding protocol partition.
4. Create and install the new backend, applying valid user information and wireless configuration.
5. After the backend is started successfully, save the configuration, update the active protocol, and refresh the Chat/Contacts/Network projection.

## Failure and recovery

 - Unsupported by target: reject before releasing old backend.
 - New backend creation or radio configuration fails: enters explicit stopped/error state; MUST NOT show that new protocol is available.
- Persistence failure: running state changes must be distinguished from "saved", and the user is prompted to try again.
 - Switching protocols do not merge NodeIds, keys or message deduplication spaces.

## Business Rules

- There is only one active mesh backend/radio owner at the same time.
 - Meshtastic, MeshCore, Reticulum identities, addressing, channels and ACK semantics remain isolated.
- The RNode bridge is just a Reticulum bearer, not equivalent to the native LXMF identity.

## Source code evidence

- `modules/core_sys/include/app/app_config.h`
- `modules/core_chat/include/chat/infra/mesh_adapter_router.h`
- `apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp`
- `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp`

## Drill down

- [Activity: protocol switching and submission](configure-radio-parameters/activity.md)
- [Sequence: Settings to activity backend](configure-radio-parameters/sequences/sequence-configure-radio-parameters.md)
