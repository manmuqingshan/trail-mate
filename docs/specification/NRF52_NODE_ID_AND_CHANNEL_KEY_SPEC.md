# nRF52 NodeId and Channel Key Specification

This specification freezes two nRF52 identity-related boundaries that have drifted
multiple times during implementation:

- how an nRF52 device derives and keeps its local `NodeId`;
- what "MT/MC key" means in LoRa settings.

The goal is to prevent future code changes from confusing stable node identity,
channel/shared-key material, and protocol public-key identity.

## Scope

This specification applies to nRF52 standalone firmware targets, including:

- `GAT562 Mesh EVB Pro`
- `T-Echo-Lite-KeyShield`

It covers runtime behavior in nRF52 builds only. ESP32 phone/companion behavior may
share helpers, but must not weaken the rules here.

## Terms

`NodeId` means the 32-bit local mesh node number used as the sender identity in
Meshtastic/MeshCore application logic.

`Device address` means the immutable nRF FICR device address registers:

- `NRF_FICR->DEVICEADDR[0]`
- `NRF_FICR->DEVICEADDR[1]`

`Device ID` means the immutable nRF FICR device id registers:

- `NRF_FICR->DEVICEID[0]`
- `NRF_FICR->DEVICEID[1]`

`Node store` means the persisted contact/node cache, such as `/chat_nodes.bin`.
It may contain this device's own node entry.

`MT channel key` or `MT PSK` means the Meshtastic channel pre-shared key stored in
`MeshConfig::primary_key` or `MeshConfig::secondary_key`.

`AQ==` means the Meshtastic base64 representation of the one-byte PSK shorthand
`0x01`. It is not a public key. Meshtastic treats shorthand `1` as the special
default channel key and expands it to the protocol default 16-byte PSK.

`MC channel key` or `MC group key` means the MeshCore shared key used for group
or public-channel encrypted payloads. Current Trail-mate storage reuses the first
16 bytes of `MeshConfig::primary_key` / `secondary_key` for MeshCore channel key
material.

`Public key` means asymmetric identity key material used by PKI/identity flows,
for example Meshtastic PKI public keys or MeshCore identity public keys. Public
keys are not LoRa channel keys.

## NodeId Rules

### Stable Device-Derived NodeId

nRF52 firmware MUST derive the primary local `NodeId` from the immutable FICR
device address.

The default derivation is:

```text
node_id =
  DEVICEADDR[0].byte2 << 24 |
  DEVICEADDR[0].byte3 << 16 |
  DEVICEADDR[1].byte0 << 8  |
  DEVICEADDR[1].byte1
```

If the derived value is non-zero and not otherwise reserved, the firmware MUST use
it as the local `NodeId`.

The local `NodeId` MUST NOT change because:

- firmware is reflashed;
- BLE support is compiled out;
- settings are normalized;
- LoRa protocol changes between MT and MC;
- the node store already contains the same id;
- this device's own NodeInfo/Position was persisted.

### Reserved Value Fallback

Fallback generation is allowed only when the device-derived `NodeId` is reserved.
Reserved values are:

- `0`
- `0xFFFFFFFF`
- values below `4`

Fallback generation may use `DEVICEID[0]` and `DEVICEID[1]` as entropy.

The node store may be consulted only during fallback candidate selection. It MUST
NOT be used to reject a valid device-derived `NodeId`.

### Node Store Boundary

The node store is a contact/cache projection, not the source of local identity.

The node store may contain this device's own node entry. That entry MUST NOT be
interpreted as a collision with local identity.

If the node store contains an old fallback `NodeId` caused by a previous bug, it
is stale cache data. The correct fix is to keep the device-derived `NodeId` stable
and let UI/runtime cleanup remove or ignore the stale entry.

### BLE Boundary

BLE visibility, BLE enabled state, and BLE compile-time support MUST NOT affect
the local `NodeId`.

Disabling BLE for GAT562 or any other nRF52 target MUST only affect BLE transport
and BLE UI. It MUST NOT alter identity derivation.

### Protocol Boundary

Meshtastic and MeshCore adapters MUST observe the same local `NodeId` for the
same device boot.

Protocol switching MUST NOT generate a new `NodeId`.

MeshCore identity public keys and Meshtastic PKI keys are separate protocol
identity materials. They MUST NOT replace the stable local `NodeId` unless a
future explicit migration spec changes this rule.

## Channel Key Rules

### LoRa Settings Key Meaning

When users ask to edit MT/MC "keys" in nRF LoRa settings, the default meaning is:

- MT channel PSK / channel key;
- MC group/channel shared key;
- not Meshtastic PKI public key;
- not MeshCore identity public key.

The LoRa settings UI SHOULD expose channel/shared-key material because nRF devices
may run without a phone configuration source.

### Meshtastic Channel PSK

Meshtastic channel PSK is represented by `MeshConfig::primary_key` and
`MeshConfig::secondary_key`.

Allowed MT PSK forms:

- empty / length 0: no channel crypto;
- one-byte shorthand, such as `AQ==` for shorthand `1`;
- expanded 16-byte AES-128 key;
- expanded 32-byte AES-256 key.

The UI MUST preserve and display the distinction between shorthand and expanded
keys when possible.

`AQ==` MUST be described as "Meshtastic default PSK shorthand" or equivalent.
It MUST NOT be described as a public key.

### MeshCore Channel Key

MeshCore group/channel key material currently comes from the first 16 bytes of
`MeshConfig::primary_key` or `MeshConfig::secondary_key`.

If no configured key is present and the MeshCore channel is public/empty, runtime
may use the MeshCore public group PSK fallback.

The UI SHOULD expose MC channel/shared key as a first-class editable setting
rather than hiding it behind MT terminology.

### Public-Key Identity Is Separate

MT PKI keys and MC identity keys belong to an Identity/Security surface, not the
LoRa radio/channel page.

Identity/Security UI may show:

- local MT public-key fingerprint;
- local MC public-key fingerprint;
- import/export/reset of local identity keypairs;
- peer key trust/forget operations from node detail pages.

Those features MUST use "public key" terminology. LoRa channel PSK settings MUST
not.

## Required UI Consequences

nRF `Settings > LORA` SHOULD include editable channel/shared-key entries:

- `MT KEY` or `MT PSK`
- `MT SECONDARY KEY` if secondary channel editing is supported
- `MC KEY` or `MC GROUP KEY`

The UI SHOULD accept at least:

- Meshtastic base64 PSK strings such as `AQ==`;
- hex strings for expanded raw keys;
- an explicit public/default option for MeshCore when applicable.

The UI SHOULD keep asymmetric public-key identity under a separate
`IDENTITY`/`SECURITY` page or node detail action.

## Non-Goals

This spec does not define:

- the final nRF key editor layout;
- QR import/export UX;
- peer public-key trust policy;
- migration from existing generic `pki_pub` / `pki_priv` namespaces.

Those require separate implementation specs.

