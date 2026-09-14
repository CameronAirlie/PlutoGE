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
