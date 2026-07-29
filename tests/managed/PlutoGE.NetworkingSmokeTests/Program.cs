using System.Net;
using System.Net.Sockets;
using PlutoGE.ScriptCore.Networking;

var port = GetFreePort();
await using var server = new NetworkServer();
await using var firstClient = new NetworkClient();
await using var secondClient = new NetworkClient();

var connectedPeers = new List<int>();
var serverMessages = new List<NetworkMessage>();
var firstMessages = new List<NetworkMessage>();
var secondMessages = new List<NetworkMessage>();

server.ClientConnected += connectedPeers.Add;
server.MessageReceived += message =>
{
    serverMessages.Add(message);
    server.Broadcast(message.Channel, message.Payload.Span, message.PeerId);
};
firstClient.MessageReceived += firstMessages.Add;
secondClient.MessageReceived += secondMessages.Add;

server.Start((ushort)port, bindAddress: "127.0.0.1");
await Task.WhenAll(
    firstClient.ConnectAsync("127.0.0.1", (ushort)port),
    secondClient.ConnectAsync("127.0.0.1", (ushort)port));

await PumpUntilAsync(() => connectedPeers.Count == 2, server, firstClient, secondClient);
Check(firstClient.SendString(7, "hello"), "first client could not queue a message");
await PumpUntilAsync(
    () => serverMessages.Count == 1 && secondMessages.Count == 1,
    server, firstClient, secondClient);

Check(serverMessages[0].Channel == 7, "server received the wrong channel");
Check(serverMessages[0].GetString() == "hello", "server received the wrong payload");
Check(firstMessages.Count == 0, "excluded sender received its own broadcast");
Check(secondMessages[0].GetString() == "hello", "second client received the wrong payload");

var source = new PlayerState("pilot", 42);
Check(server.SendJson(connectedPeers[0], 9, source), "server could not queue JSON");
await PumpUntilAsync(
    () => firstMessages.Concat(secondMessages).Any(message => message.Channel == 9),
    server, firstClient, secondClient);
var jsonMessage = firstMessages.Concat(secondMessages).Single(message => message.Channel == 9);
Check(jsonMessage.GetJson<PlayerState>() == source, "JSON did not round-trip");

Console.WriteLine("Networking smoke tests passed.");

static async Task PumpUntilAsync(
    Func<bool> condition, NetworkServer server, params NetworkClient[] clients)
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    while (!condition())
    {
        timeout.Token.ThrowIfCancellationRequested();
        server.Poll();
        foreach (var client in clients)
            client.Poll();
        await Task.Delay(5, timeout.Token);
    }
}

static int GetFreePort()
{
    var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    var port = ((IPEndPoint)listener.LocalEndpoint).Port;
    listener.Stop();
    return port;
}

static void Check(bool condition, string message)
{
    if (!condition)
        throw new InvalidOperationException(message);
}

internal sealed record PlayerState(string Name, int Score);
