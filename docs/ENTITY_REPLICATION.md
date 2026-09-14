# Entity replication

M13 now includes a bounded snapshot protocol above the existing managed transport,
an authority model, an interpolating replica and a scene binding adapter. This is
a partial delivery; the two-client regression is a protocol sample, not yet a
complete cooperative gameplay scene.

`EntityReplicationAuthority` assigns monotonic network IDs independent of native
entity IDs. Register prefab types locally with small numeric IDs. Spawn/despawn,
ownership transfers, properties and section membership are represented by full
snapshots. Full snapshots also initialize late joiners. Session IDs must be agreed
with the server when connecting; negotiate a new nonzero ID for a new session.

```csharp
var authority = new EntityReplicationAuthority(sessionId);
var id = authority.Spawn(1, ownerPeer, sectionId,
    System.Numerics.Vector3.Zero, System.Numerics.Quaternion.Identity);
var packet = authority.Snapshot();
server.Broadcast(80, packet);

var replica = new EntityReplicationReplica(sessionId);
client.MessageReceived += message =>
{
    if (message.Channel == 80)
        replica.Apply(message.Payload.Span, message.PeerId, simulationSeconds);
};
```

Call `NetworkClient.Poll` and replica operations on the game thread. Configure
transport `MaxPayloadSize` to `EntityReplicationAuthority.MaxPayloadBytes`. Check
Send/Broadcast results for outbound pressure; a later full snapshot can supersede
one that could not be queued. `EntityReplicationReplica.Apply` rejects wrong
senders/sessions, unsupported versions, stale sequences, malformed/trailing data,
nonfinite values and duplicate IDs/properties before replacing current state.

The model permits 128 entities, 16 numeric properties per entity and 32 KiB packets.
Network receive callbacks are additionally bounded to 1,024 events and 16 MiB.
Servers default to 128 concurrent clients (configurable through MaxClients).
On receive overflow, Poll closes the transport and throws IOException. Dispose
scene bindings and create a new transport/session to reconnect; do not continue
replication after overflow. Disconnect notifications also now survive cancellation
of a peer's write task during shutdown.

The server may publish any entity; client updates must match ownership. Ownership
is not gameplay validation: applications must validate inputs/movement and which
properties a client may change. `Disconnect(peer)` removes owned state, and
`UnloadSection(id)` removes section state before broadcasting the next snapshot.

`ReplicatedScene` maps registered type IDs to local prefab factory functions and
applies interpolated local positions/quaternions. Use root prefab instances so
local coordinates represent the shared world. `ApplyProperties` maps numeric
property IDs into game-specific components. Dispose on disconnect; scene generation
checks prevent cleanup from destroying unrelated entities after scene replacement.
Applications must connect authoritative section unloads to replication cleanup.

Reproduce the two-client late-join/disconnect sample and validation:

```powershell
dotnet run --project tests/managed/PlutoGE.ReplicationTests/PlutoGE.ReplicationTests.csproj --configuration Release
```

Remaining work includes a cooperative gameplay scene, automated native prefab
adapter/lifetime tests, session negotiation and explicit section integration.
This delivery does not include prediction, reconciliation or interest management.
