# Trail Mate Wi-Fi Access Resource Policy

This specification fixes the boundary for Wi-Fi resource usage on constrained
Trail Mate devices. It prevents HTTP downloads, MQTT, Reticulum Wi-Fi gateway,
OTA, SD writes, and UI wake rendering from competing as independent owners of
the same small device resources.

## Distinctions

- `platform::ui::wifi` is the Wi-Fi control plane. It owns STA configuration,
  enable/disable, scanning, saved credentials, and the platform connection API.
- `platform::ui::wifi_access` is the Wi-Fi business access policy. Any product
  feature that wants to use Wi-Fi for protocol traffic or downloads must ask it
  for permission, connection, and traffic budget.
- `platform::ui::http_client` is the only ESP HTTP/TLS byte-stream
  implementation. Product features must not create `esp_http_client` directly.
- MQTT and Reticulum Wi-Fi gateway are long-lived protocol users. They keep
  their own protocol socket state, but connection attempts and pump budgets are
  governed by `wifi_access`.
- OTA is an exclusive Wi-Fi/HTTP activity. During OTA, long-lived protocol
  users must not consume pump budget beyond what `wifi_access` grants.

## Legal Access Paths

| Use case | Required path |
| --- | --- |
| Save Wi-Fi credentials, scan, manual settings connect | `platform::ui::wifi` control-plane APIs |
| HTTP metadata or small text fetch | `platform::ui::http_client::get_text` |
| HTTP file/asset/package download | `platform::ui::http_client::download` |
| Firmware OTA byte stream | `platform::ui::http_client::download` with `AccessKind::OtaDownload` |
| MQTT Wi-Fi connect | `platform::ui::wifi_access::ensure_connected` |
| MQTT socket connect/pump/send | `wifi_access::acquire` and `wifi_access::traffic_budget` |
| Reticulum gateway Wi-Fi connect | `platform::ui::wifi_access::ensure_connected` |
| Reticulum gateway socket connect/pump/send | `wifi_access::acquire` and `wifi_access::traffic_budget` |

## Illegal Access Paths

- No product feature may include or call `esp_http_client` directly outside the
  ESP HTTP runtime.
- MQTT and Reticulum gateway must not call `platform::ui::wifi::connect`
  directly.
- New Wi-Fi-backed protocol runtimes must not invent their own reconnect,
  wake-protection, or pump-budget policy.
- New background downloads must not bypass `wifi_access` by using raw sockets or
  direct HTTP clients.

## Screen-State Policy

Trail Mate is a low-frequency terminal, not a desktop router node.

| Device phase | MQTT | Reticulum Wi-Fi gateway | HTTP/downloads |
| --- | --- | --- | --- |
| Screen on | Connected messaging is allowed with normal terminal budget. | Direct gateway traffic is allowed with terminal budget. | User foreground work is allowed; background work is deferred. |
| Wake protected | Existing sockets may use minimal budget. New reconnects and HTTP starts are deferred. | Existing sockets may use minimal budget. New reconnects are deferred. | Deferred. |
| Screen off | MQTT may connect and normally send/receive. | Gateway may connect and receive with expanded terminal budget. | Queued maintenance/background work may run, one transaction at a time. |
| HTTP active | MQTT and Reticulum are reduced to minimal messaging budget. | Reduced to minimal gateway budget. | Exactly one HTTP transaction owns the HTTP resource. |
| OTA active | Protocol pump budget is suspended. | Protocol pump budget is suspended. | OTA is exclusive. |

The wake-protected phase shields the path from screen sleep to screen wake to
entering the UI. It is intentionally short and prevents reconnect storms, TLS
allocation, route image downloads, and catalog/update checks from causing
visible UI stalls.

## Enforcement

The CI boundary job runs `scripts/check_wifi_access_policy.py`. The check fails
when:

- `esp_http_client` appears outside `platform_ui_http_client_runtime.cpp`.
- ESP protocol/download business modules call `platform::ui::wifi::connect`
  directly instead of going through `wifi_access`.

Any exception must first update this specification and the checker allow-list.
