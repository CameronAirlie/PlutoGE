# Entity replication

M13 now includes a bounded snapshot protocol above the existing managed transport,
an authority model, an interpolating replica and a scene binding adapter. This is
accompanied by a cooperative pressure-pad sample and reproducible multi-client
regressions. Interactive native rendering/playtesting remains pending.

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

## Transport session lifecycle

`ReplicationServerSession` and `ReplicationClientSession` provide session
negotiation, automatic late-join snapshots and disconnect despawns. Attach them
before starting/connecting their transports, and reserve their channel (80 by
default). The application owns transport disposal. Use a fresh, nonzero server
session ID for each new game; clients learn it through a versioned welcome frame.
An established client rejects attempts to replace its session through messages.

```csharp
using var host = new ReplicationServerSession(server, sessionId);
server.Start(port);
// On the game thread: server.Poll(), update host.Authority, then host.Publish().
// Connect native section unloads explicitly to host.UnloadSection(networkSectionId).

using var session = new ReplicationClientSession(client);
ReplicatedScene? scene = null;
session.SessionStarted += () => scene = new ReplicatedScene(session.Replica!,
    new Dictionary<ushort, Func<GameObject?>> {
        [1] = () => Prefab.Instantiate("project://Prefabs/Player.plutoprefab")
    });
session.SessionEnded += () => { scene?.Dispose(); scene = null; };
await client.ConnectAsync(address, port);
// OnUpdate: session.Poll(simulationSeconds); scene?.Update(simulationSeconds);
// OnDestroy: dispose scene, session and client.
```

Poll the client through the session wrapper with finite, nondecreasing simulation
seconds. Callbacks run on that thread. `LocalPeerId` identifies ownership after
`SessionStarted`; disconnect clears it and the replica before `SessionEnded`.
Reconnect negotiates a fresh replica. Poll overflow also clears the session before
propagating the transport error. Publish returns the number of accepted sends;
applications choose snapshot cadence and handle outbound pressure. If the welcome
cannot be queued, reconnect the client; later snapshots cannot replace negotiation.
The welcome is 18 little-endian bytes: magic `PULS`, ushort version 1, ulong session
ID and positive int peer ID. This is identity negotiation, not authentication.

The managed regression also exercises these wrappers with two clients, section
unload, forged session replacement, disconnect cleanup and reconnect.

## Cooperative sample

Attach `PlutoGE.ScriptCore.Examples.CooperativeReplicationSample` to a persistent
entity in a scene with a camera, a floor and visible pad markers centered at
(-2, 0, 0) and (2, 0, 0). Assign locally registered player and gate prefab assets;
their roots should be positioned at the origin in their prefab files. Launch two
players. Enable `host` on one and set the other player's `address` to that host.
Both use port 7777 by default. The host also connects as a player.

WASD moves each player. Hold both pads to raise the gate at (0, 0, 5). The reusable
`CooperativeReplicationGame` runs the authority simulation: clients send only four
movement bits, speed and arena bounds are server-controlled, input expires after
250 ms, and snapshots are published at 20 Hz. Gate state is a replicated property;
its transform also moves so the sample requires no custom property component.
This sample demonstrates cooperative state, not collision-authoritative movement.
It caps simulation steps at 100 ms and does not provide prediction or reconciliation.

Automated tests drive two real clients onto the pads, verify the shared gate,
input timeout and disconnect cleanup. Native ABI stubs additionally verify scene
binding poses, despawns, generation changes, reentrant callbacks, and transitions
inside prefab factories/property callbacks. The adapter rechecks lifetime after
those callbacks before touching another native entity.

Native section IDs are local to each player. Applications still map their logical
network sections explicitly and call the server session's `UnloadSection`; those
IDs must not be inferred from independently loaded client section handles.
This delivery does not include prediction, reconciliation or interest management.
