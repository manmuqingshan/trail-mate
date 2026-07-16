# Reticulum Network Configuration

Trail Mate reads its Reticulum interface and LXMF propagation client setup from:

```text
/trailmate/reticulum/config.json
```

The file is loaded after the SD card becomes available. A valid SD configuration
becomes the active configuration and is cached in NVS as the last-known-good
copy. If the SD card or file is unavailable at the next boot, Trail Mate uses the
cached copy. Legacy Settings values are only used to construct factory defaults
when neither source exists.

The embedded parser accepts at most 2 KB, five nesting levels, 128 structural
tokens, and 128 bytes per JSON string. Its DOM exists only during boot or an
explicit reload and is released before Reticulum applies the new fixed-size
configuration snapshot. Template and last-known-good serialization reuse the
same fixed 2 KB buffer instead of allocating a second output string.

Configuration reload is deferred while a Reticulum call owns the realtime
resource lease. The active interfaces are replaced after the call closes.

## Example

```json
{
  "schema": "trail-mate.reticulum",
  "version": 1,
  "interfaces": [
    {
      "id": "integrated-lora",
      "type": "IntegratedLoRaInterface",
      "enabled": true
    },
    {
      "id": "local-wifi",
      "type": "AutoInterface",
      "enabled": true,
      "group_id": "reticulum",
      "discovery_scope": "link",
      "discovery_port": 29716,
      "data_port": 42671
    },
    {
      "id": "primary-tcp",
      "type": "TCPClientInterface",
      "enabled": true,
      "target_host": "vicliu.i234.me",
      "target_port": 4242
    },
    {
      "id": "backup-tcp",
      "type": "TCPClientInterface",
      "enabled": false,
      "target_host": "backup.example.net",
      "target_port": 4242
    }
  ],
  "lxmf": {
    "propagation": {
      "enabled": true,
      "service_enabled": false,
      "delivery_method": "auto",
      "propagation_node": "auto",
      "sync_on_start": true,
      "sync_interval_seconds": 900,
      "max_messages_per_sync": 32
    }
  }
}
```

## Interface Rules

- `IntegratedLoRaInterface` enables the board's integrated LoRa bearer. Only
  one entry is allowed.
- `AutoInterface` implements the official Reticulum IPv6 link-scope discovery
  and per-peer UDP interface model. Only one entry is allowed.
- `TCPClientInterface` connects to a Reticulum TCP server or gateway. Up to
  three entries can be configured on boards with native Wi-Fi.
- T-Display-P4 uses the C6 companion's single TCP transport and therefore uses
  only the first enabled `TCPClientInterface`. It does not expose an IPv6
  AutoInterface through the companion transport.
- Unknown destinations and announces may fan out over ready interfaces.
  Learned paths, links, proofs, resources, calls, and Nomad requests remain
  bound to the exact ingress interface or learned path interface.

## LXMF Delivery

`delivery_method` accepts:

- `direct`: use opportunistic or direct-link delivery only.
- `propagated`: submit messages to a selected propagation node.
- `auto`: prefer an existing direct link, then opportunistic delivery with a
  usable ratchet, then a discovered propagation node, and finally establish a
  direct link when no propagation node is available.

`propagation_node` can be `auto` or a 32-character propagation destination hash.
Automatic selection prefers an active, fresh node with the lowest known hop
count. Trail Mate generates the official LXMF propagation stamp incrementally,
uploads the encrypted message over an identified propagation link, and marks
the message sent to the propagation node only after the link packet or resource
proof is validated. This state does not claim final recipient delivery.

`service_enabled` is intentionally `false` by default. Enabling it makes this
battery device announce and accept traffic as an LXMF propagation service;
normal message-for-propagation client support does not require it.

During synchronization Trail Mate requests the remote transient-ID list,
downloads only missing messages addressed to its local LXMF delivery
destination, and acknowledges handled IDs. Seen transient IDs are retained in
the bounded propagation runtime to suppress duplicate delivery.
