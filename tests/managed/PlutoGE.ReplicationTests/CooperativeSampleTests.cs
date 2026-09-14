using System.Net;
using System.Net.Sockets;
using PlutoGE.ScriptCore.Examples;
using PlutoGE.ScriptCore.Networking;

internal static class CooperativeSampleTests
{
    private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }
    public static async Task Run()
    {
        var reservation = new TcpListener(IPAddress.Loopback, 0);
        reservation.Start(); var port = (ushort)((IPEndPoint)reservation.LocalEndpoint).Port; reservation.Stop();
        using var server = new NetworkServer { MaxClients = 2 };
        using var game = new CooperativeReplicationGame(server, 456);
        using var first = new NetworkClient();
        using var second = new NetworkClient();
        using var a = new ReplicationClientSession(first);
        using var b = new ReplicationClientSession(second);
        double time = 0;
        int received = 0;
        server.MessageReceived += _ => received++;
        async Task Pump(Func<bool> done)
        {
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (!done())
            {
                Check(DateTime.UtcNow < deadline, "Cooperative sample timeout");
                server.Poll(); a.Poll(time); b.Poll(time);
                await Task.Delay(1);
            }
        }
        server.Start(port, bindAddress: "127.0.0.1");
        await first.ConnectAsync("127.0.0.1", port);
        await second.ConnectAsync("127.0.0.1", port);
        await Pump(() => a.Replica is not null && b.Replica is not null);
        game.Tick(0.05f); time += 0.05;
        await Pump(() => a.Replica!.Entities.Count == 3 && b.Replica!.Entities.Count == 3);
        Check(!game.GateOpen, "Gate started open");
        for (int step = 0; step < 20; ++step)
        {
            int expected = received + 2;
            Check(first.Send(CooperativeReplicationGame.InputChannel, new byte[] { 1 }) &&
                second.Send(CooperativeReplicationGame.InputChannel, new byte[] { 1 }), "Input queue failed");
            await Pump(() => received >= expected);
            game.Tick(0.05f); time += 0.05;
        }
        bool Open(EntityReplicationReplica? replica) => replica?.Entities.Values.Any(state =>
            state.Type == CooperativeReplicationGame.GateType && state.Properties.TryGetValue(1, out float value) && value == 1) == true;
        await Pump(() => Open(a.Replica) && Open(b.Replica));
        Check(game.GateOpen, "Two players did not open the gate");
        // Missing input expires, preventing a disconnected/stalled sender from moving forever.
        for (int step = 0; step < 20; ++step) { game.Tick(0.05f); time += 0.05; }
        Check(game.GateOpen, "Timed-out input continued to move the players");
        await first.DisconnectAsync();
        await Pump(() => a.Replica is null);
        await Pump(() => b.Replica!.Entities.Count == 2);
        game.Tick(0.05f); time += 0.05;
        await Pump(() => !Open(b.Replica));
        Check(!game.GateOpen, "Disconnect left the cooperative objective open");
        Console.WriteLine("Cooperative sample: movement, shared gate, input timeout and disconnect passed.");
    }
}
