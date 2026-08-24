# Sequence: Host, Session and Handler
```mermaid
sequenceDiagram
  actor Host as External Host
  participant Transport as HostLink Transport
  participant Session as SessionRuntime
  participant Codec as Frame Codec/Router
  participant Service as Status/GPS/Config/AppData Service
  Host->>Transport: connect + hello
  Transport->>Session: connected / handshake frame
  Session-->>Host: ready response
  Host->>Session: command frame(seq,payload)
  Session->>Codec: decode + route
  Codec->>Service: validated command
  Service-->>Codec: explicit result
  Codec->>Session: response/error frame
  Session-->>Host: ordered TX
  Host--xTransport: disconnect
  Transport->>Session: link lost; clear session pending
```

## Scenarios and responsibilities

Transport manages byte streams and connection events; SessionRuntime manages handshake, generation, pending request and ordered TX; Codec/Router verifies frames and selects handlers; Service executes bounded application commands.

## Handshake and session

ready response is only sent after version/capability negotiation is completed. A new generation is created for each connection; late frames, handler results, and TX callbacks from old connections must not enter the new session.

## Request/response association

`seq` is unique within the session or managed by window. Router completes size, type and capability verification before calling Service. Side effects: handler returns committed result before encoding success; timeout or rejected returns stable error code.

## TX order and back pressure

Session maintains a fixed capacity of ordered TX. The ordering rules for asynchronous events and command responses must be clear; unbounded allocation is not allowed when the queue is full. Disconnect and clean up pending commands, but the business has been committed and cannot be rolled back to "not executed".

## test

 Covers handshake failure, repeated seq, handler lateness, TX full, response encoding failure, command submitted when disconnected and fast reconnection.
