# PlutoGE Networking

## Implemented foundation

PlutoGE's first multiplayer layer is a managed .NET 8 module in
`PlutoGE.ScriptCore.Networking`. It is intentionally independent of scenes and
entities so game-specific replication can be composed on top.

The current transport provides:

- one server with multiple concurrent clients;
- reliable, ordered TCP delivery;
- framed messages with application-defined `ushort` channels;
- raw bytes, UTF-8 strings, and JSON convenience methods;
- stable server-side peer IDs for targeted sends;
- broadcast with optional sender exclusion;
- background accept/read/write loops;
- main-thread event delivery through `Poll()`;
- payload-size validation and bounded outbound queues;
- cancellation and synchronous/asynchronous disposal.

The wire frame is six bytes followed by the payload:

| Field | Size | Encoding |
|---|---:|---|
| Payload length | 4 bytes | signed integer, network byte order |
| Channel | 2 bytes | unsigned integer, network byte order |
| Payload | declared length | application-defined |

Connections that send a negative or oversized payload length are closed. The
default maximum payload is 1 MiB and each connection queues at most 1024
outbound frames. `Send` returns `false` if a connection is unavailable or its
queue is full.

## Usage model

Create a `NetworkServer` or `NetworkClient` in a gameplay-owned session object,
subscribe to events, and call `Poll()` during `OnUpdate`. Event handlers may
then safely access ordinary game objects because they execute on the polling
thread, not a socket thread.

Channels form the game's protocol:

```csharp
public static class GameChannels
{
    public const ushort PlayerJoined = 1;
    public const ushort PlayerInput = 2;
    public const ushort WorldSnapshot = 3;
}
```

Use small immutable DTO records with `SendJson` while prototyping. For frequent
state replication, encode a compact binary payload and use `Send`.

## Planned layers

The transport is the completed first layer. Recommended follow-on modules are:

1. A protocol handshake carrying game version, protocol version, and
   authentication/session data.
2. Server-authoritative entity spawning with stable network object IDs and
   ownership.
3. Snapshot serialization, delta compression, interpolation, and interest
   management.
4. An unreliable sequenced UDP transport for high-rate snapshots while
   retaining TCP for session/control messages.
5. Client prediction and server reconciliation for responsive player movement.
6. Optional encryption or a platform relay for Internet deployment.

These layers should depend on the channel/message abstraction rather than on
TCP directly, preserving the C# API as transports are added.
