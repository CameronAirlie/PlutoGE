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
- payload-size validation, bounded outbound queues, and bounded inbound event storage;
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

## Entity replication and sessions

The managed API now includes `EntityReplicationAuthority`,
`EntityReplicationReplica`, `ReplicationServerSession`, `ReplicationClientSession`
and `ReplicatedScene`. Together they provide versioned session negotiation,
network entity IDs, spawn/despawn, ownership, numeric properties, full snapshots,
late join, interpolation, disconnect cleanup and native prefab bindings.

The current model is bounded to 128 entities, 16 numeric properties per entity
and 32 KiB snapshot packets. Applications register prefab factories, validate
gameplay requests, choose publication cadence and map streamed section IDs.
See [Entity replication](ENTITY_REPLICATION.md) for setup, lifecycle rules and
the cooperative sample.

Inbound callbacks are bounded to 1,024 events and 16 MiB; overflow closes the
transport and causes `Poll()` to throw `IOException`. Servers default to 128
concurrent clients, configurable through `MaxClients`.

## Not implemented

The current layer does not provide delta compression, interest management, UDP
snapshot transport, client prediction/reconciliation, matchmaking, authentication,
encryption or relay integration. Session negotiation is not authentication.
