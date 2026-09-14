using System.Net;
using System.Net.Sockets;
using System.Numerics;
using System.Runtime.InteropServices;
using PlutoGE.ScriptCore;
using PlutoGE.ScriptCore.Networking;

static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
var authority = new EntityReplicationAuthority(42);
ulong id = authority.Spawn(1, 7, 3, Vector3.Zero, Quaternion.Identity);
var replica = new EntityReplicationReplica(42);
var initial = authority.Snapshot();
Check(!replica.Apply(initial, 7, 0), "Unauthorized sender accepted");
Check(replica.Apply(initial, 0, 0), "Initial spawn rejected");
Check(!replica.Apply(initial, 0, 1), "Stale packet accepted");
Check(!authority.Update(id, 8, Vector3.One, Quaternion.Identity), "Ownership bypass");
Check(authority.Update(id, 7, new Vector3(10, 0, 0), Quaternion.Identity, new Dictionary<ushort, float> { [1] = 0.75f }), "Owner update rejected");
var second = authority.Snapshot();
Check(replica.Apply(second, 0, 1), "Updated snapshot rejected");
Check(replica.Sample(id, 1.25, out var position, out _) && position.X == 5, "Interpolation incorrect");
Check(replica.Entities[id].Properties[1] == 0.75f, "Replicated property lost");
for (int length = 0; length < second.Length; ++length)
    Check(!new EntityReplicationReplica(42).Apply(second.AsSpan(0, length), 0, 0), "Truncated frame accepted");
var corrupt = authority.Snapshot(); corrupt[4] = 99;
Check(!replica.Apply(corrupt, 0, 2) && replica.Entities.Count == 1, "Version rejection mutated state");
Check(!new EntityReplicationReplica(99).Apply(initial, 0, 0), "Wrong session accepted");
authority.Disconnect(7);
Check(replica.Apply(authority.Snapshot(), 0, 2) && replica.Entities.Count == 0, "Disconnect despawn failed");
ulong next = authority.Spawn(1, 0, 3, Vector3.Zero, Quaternion.Identity);
Check(next > id, "Entity IDs reused");
authority.UnloadSection(3);
Check(replica.Apply(authority.Snapshot(), 0, 3) && replica.Entities.Count == 0, "Section cleanup failed");
var bounded = new EntityReplicationAuthority(100);
for (int i = 0; i < EntityReplicationAuthority.MaxEntities; ++i) Check(bounded.Spawn(1, 0, 0, Vector3.Zero, Quaternion.Identity) != 0, "Early capacity rejection");
Check(bounded.Spawn(1, 0, 0, Vector3.Zero, Quaternion.Identity) == 0, "Entity bound exceeded");
Check(bounded.Snapshot().Length <= EntityReplicationAuthority.MaxPayloadBytes, "Payload bound exceeded");
var abi = typeof(SceneManager).Assembly.GetType("PlutoGE.ScriptCore.Native.ScriptBridge+NativeSceneStreamingRequest")!;
Check(Marshal.SizeOf(abi) == 48 && Marshal.OffsetOf(abi, "Path").ToInt32() == 32, "Streaming ABI layout mismatch");
var queueType = typeof(NetworkClient).Assembly.GetType("PlutoGE.ScriptCore.Networking.NetworkEventQueue")!;
var queue = Activator.CreateInstance(queueType)!;
var enqueue = queueType.GetMethod("Enqueue")!;
for (int i = 0; i < 17; ++i) enqueue.Invoke(queue, new object[] { (Action)(() => {}), 1024 * 1024 });
Check((bool)queueType.GetProperty("Overflowed")!.GetValue(queue)!, "Receive byte budget was not enforced");
queue = Activator.CreateInstance(queueType)!;
for (int i = 0; i <= 1024; ++i) enqueue.Invoke(queue, new object[] { (Action)(() => {}), 0 });
Check((bool)queueType.GetProperty("Overflowed")!.GetValue(queue)!, "Receive callback budget was not enforced");

// Reproducible two-client session over the existing reliable transport.
var reservation = new TcpListener(IPAddress.Loopback, 0);
reservation.Start(); var port = (ushort)((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop();
await using var server = new NetworkServer { MaxPayloadSize = EntityReplicationAuthority.MaxPayloadBytes };
await using var first = new NetworkClient { MaxPayloadSize = EntityReplicationAuthority.MaxPayloadBytes };
await using var late = new NetworkClient { MaxPayloadSize = EntityReplicationAuthority.MaxPayloadBytes };
var host = new EntityReplicationAuthority(123);
var firstReplica = new EntityReplicationReplica(123);
var lateReplica = new EntityReplicationReplica(123);
var peers = new List<int>();
double time = 0;
server.ClientConnected += peer => { peers.Add(peer); Check(server.Send(peer, 80, host.Snapshot()), "Join snapshot send failed"); };
server.ClientDisconnected += peer => { host.Disconnect(peer); server.Broadcast(80, host.Snapshot()); };
first.MessageReceived += message => Check(firstReplica.Apply(message.Payload.Span, message.PeerId, time), "First client rejected snapshot");
late.MessageReceived += message => Check(lateReplica.Apply(message.Payload.Span, message.PeerId, time), "Late client rejected snapshot");
server.Start(port, bindAddress: "127.0.0.1");
await first.ConnectAsync("127.0.0.1", port);
async Task PumpUntil(Func<bool> done)
{
    var deadline = DateTime.UtcNow.AddSeconds(5);
    while (!done())
    {
        Check(DateTime.UtcNow < deadline, "Multi-client test timeout");
        time += 0.01;
        server.Poll(); first.Poll(); late.Poll();
        await Task.Delay(1);
    }
}
await PumpUntil(() => peers.Count == 1);
ulong cooperativeEntity = host.Spawn(1, peers[0], 0, Vector3.One, Quaternion.Identity);
Check(server.Broadcast(80, host.Snapshot()) == 1, "Spawn broadcast failed");
await PumpUntil(() => firstReplica.Entities.Count == 1);
await late.ConnectAsync("127.0.0.1", port);
await PumpUntil(() => lateReplica.Entities.Count == 1);
Check(lateReplica.Entities[cooperativeEntity].Position == Vector3.One, "Late join missed current state");
await first.DisconnectAsync();
await PumpUntil(() => lateReplica.Entities.Count == 0);
Console.WriteLine("Replication validation and two-client late-join/disconnect tests passed.");

// Session wrappers negotiate IDs rather than relying on a shared hard-coded session.
reservation.Start(); port = (ushort)((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop();
await using var sessionServer = new NetworkServer();
await using var sessionClient = new NetworkClient();
await using var joiningClient = new NetworkClient();
using var serverSession = new ReplicationServerSession(sessionServer, 987);
using var clientSession = new ReplicationClientSession(sessionClient);
using var joiningSession = new ReplicationClientSession(joiningClient);
int starts = 0, ends = 0;
clientSession.SessionStarted += () => starts++;
clientSession.SessionEnded += () => ends++;
sessionServer.Start(port, bindAddress: "127.0.0.1");
await sessionClient.ConnectAsync("127.0.0.1", port);
async Task PumpSessions(Func<bool> done)
{
    var deadline = DateTime.UtcNow.AddSeconds(5);
    while (!done())
    {
        Check(DateTime.UtcNow < deadline, "Negotiated session timeout");
        time += 0.01;
        sessionServer.Poll(); clientSession.Poll(time); joiningSession.Poll(time);
        await Task.Delay(1);
    }
}
await PumpSessions(() => clientSession.Replica is not null);
Check(starts == 1 && clientSession.LocalPeerId > 0 && clientSession.Replica!.Session == 987, "Session negotiation failed");
var owned = serverSession.Authority.Spawn(1, clientSession.LocalPeerId, 5, Vector3.One, Quaternion.Identity);
serverSession.Publish();
await PumpSessions(() => clientSession.Replica!.Entities.ContainsKey(owned));
// An established connection cannot be switched to a different session by traffic.
var establishedReplica = clientSession.Replica;
var forgedWelcome = new byte[18];
System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(forgedWelcome, 0x534c5550);
System.Buffers.Binary.BinaryPrimitives.WriteUInt16LittleEndian(forgedWelcome.AsSpan(4), 1);
System.Buffers.Binary.BinaryPrimitives.WriteUInt64LittleEndian(forgedWelcome.AsSpan(6), 555);
System.Buffers.Binary.BinaryPrimitives.WriteInt32LittleEndian(forgedWelcome.AsSpan(14), 999);
sessionServer.Send(clientSession.LocalPeerId, 80, forgedWelcome);
sessionServer.Send(clientSession.LocalPeerId, 80, new EntityReplicationAuthority(555).Snapshot());
// A subsequent ordered snapshot acts as a delivery barrier for rejected packets.
serverSession.Authority.Update(owned, 0, new Vector3(2, 0, 0), Quaternion.Identity);
serverSession.Publish();
await PumpSessions(() => clientSession.Replica!.Entities[owned].Position.X == 2);
Check(ReferenceEquals(establishedReplica, clientSession.Replica) && starts == 1, "Traffic replaced established session");
await joiningClient.ConnectAsync("127.0.0.1", port);
await PumpSessions(() => joiningSession.Replica?.Entities.ContainsKey(owned) == true);
Check(joiningSession.LocalPeerId != clientSession.LocalPeerId, "Peer identity collision");
serverSession.UnloadSection(5);
await PumpSessions(() => joiningSession.Replica!.Entities.Count == 0 && clientSession.Replica!.Entities.Count == 0);
owned = serverSession.Authority.Spawn(1, clientSession.LocalPeerId, 0, Vector3.Zero, Quaternion.Identity);
serverSession.Publish();
await PumpSessions(() => joiningSession.Replica!.Entities.ContainsKey(owned));
await sessionClient.DisconnectAsync();
await PumpSessions(() => ends == 1 && joiningSession.Replica!.Entities.Count == 0);
Check(clientSession.Replica is null && clientSession.LocalPeerId == 0, "Disconnect retained session state");
await sessionClient.ConnectAsync("127.0.0.1", port);
await PumpSessions(() => starts == 2);
Check(clientSession.Replica!.Session == 987 && ends == 1, "Reconnect negotiation failed");
Console.WriteLine("Negotiated session, section unload, disconnect and reconnect tests passed.");
await CooperativeSampleTests.Run();
ReplicatedSceneTests.Run();
