# Activity Diagram: Connection and Resource Request

```mermaid
flowchart TD
 Enable["User enables Wi-Fi / select network"] --> Credentials{"Are there credentials available?"}
 Credentials -- No --> Ask["Require selection of SSID and password"]
 Credentials -- Yes --> Connect{"Connection successful?"}
 Connect -- No --> Status["Display disconnection/failure/backoff"]
 Connect -- Yes --> Request["Function submission Client + AccessKind + Priority"]
 Request --> Policy{"Screen, OTA, Call, Busy allowed?"}
 Policy -- No --> Deny["Return specific Decision; caller delays or stops"]
 Policy -- Yes --> Lease["Issue Lease + revoke generation"]
 Lease --> Work["Bounded network work/traffic" budget"]
 Work --> Revoked{"The lease was revoked?"}
 Revoked -- Yes --> Stop["Stop and release immediately"]
 Revoked -- No --> Release["release after completion"]
```

## Questions answered by this picture

This activity separates "connecting to an access point" and "a function is allowed to use network resources". Just because the Wi-Fi icon shows connected, it does not mean that Package, Firmware, MQTT or Call can start network work unconditionally.

## Request model

 Each job is submitted with `Client + AccessKind + Priority`. The policy also reads screen phase, current connection, backoff, OTA/Call exclusive status, and existing lease. After passing, a Lease with generation and traffic budget is returned; the caller can only perform bounded work within the validity period of the Lease.

## Determination table

| Conditions | Decisions | Caller actions |
| --- | --- | --- |
| Wi-Fi disabled / no credentials | `Disabled` or `NoCredentials` | Boot configuration, do not retry network |
| disconnected / backoff | `Disconnected` or `Backoff` | Defer, wait for status event |
| OTA or Call Exclusive | `ExclusiveOwner` | Stop low-priority work |
| Already have a non-preemptible lease | `Busy` | Reserve business request and re-acquire later |
| Policy allows | `Granted` | Work within budget and check revoke generation |

## Resource release and recovery

All successful paths must `release(Lease)`; exceptions, cancellations and page leaves cannot leak leases. After the high-priority owner revokes the lease, the caller must stop initiating new I/O, cancel or wrap up in-flight requests, and release local resources. Revoking is not a "suggestion suspension", the old Lease has expired after the generation change.

## Source code evidence and testing concerns

`wifi_runtime_impl.h::apply_enabled` manages Wi-Fi running status, and Access Runtime implements acquire/release and exclusive policies. Testing needs to cover no credentials, connection backoff, Call/OTA preemption, repeated releases, old generation usage and traffic budget exhaustion.
