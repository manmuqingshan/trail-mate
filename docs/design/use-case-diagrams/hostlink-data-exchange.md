# Use Case: Exchange application data with external host via HostLink

Status: **confirmed integration behavior**
Business Boundary: External application integrated with host

## User goal

Have supported local hosts read device status/GPS, submit allowed configuration and application data, and receive explicit responses or asynchronous events; bad frames, unknown commands, and disconnections cannot leave half application operations.

## Main scene

1. `SessionRuntime` enters Waiting/Connected from Stopped, and enters Ready after completing the handshake.
2. The frame codec checks magic/length/type/sequence; the router only maps legal frames to status, GPS, configuration or app-data handler.
3. The command handler checks the capability and payload, calls the bounded service, and forms a response/error frame.
4. Session manages TX sequence, queue and throttling; after the host is disconnected, the session-scoped pending state is cleared and returns to Waiting.

## Rules and Failures

- HostLink is a native integration protocol, not equal to USB Mass Storage, nor equal to P4↔C6 internal companion link.
- Incomplete handshake does not accept business commands.
- Illegal length, unknown command, sequence/codec errors return a clear error or close the session.
- Configuration changes must be based on the results returned by the service, and success cannot be displayed directly after decoding.

Source code: `modules/core_hostlink/include/hostlink/session_runtime.h`, HostLink frame router/codecs, platform hostlink transports.

## Drill down

- [Activity](hostlink-data-exchange/activity.md)
- [Sequence](hostlink-data-exchange/sequences/sequence-hostlink-data-exchange.md)
- [State Machine](hostlink-data-exchange/state-machines/hostlink-session.md)
