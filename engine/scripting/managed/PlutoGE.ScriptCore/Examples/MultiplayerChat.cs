using PlutoGE.ScriptCore.Networking;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Minimal host-or-client example. Set Host to true on one instance and enter
/// that machine's address on the others.
/// </summary>
public sealed class MultiplayerChat : ScriptBehaviour
{
    [SerializedField] private bool host = true;
    [SerializedField] private string serverAddress = "127.0.0.1";
    [SerializedField] private int port = 7777;
    [SerializedField] private string playerName = "Player";

    private NetworkServer? _server;
    private NetworkClient? _client;

    public override void OnCreate()
    {
        if (port is < 1 or > ushort.MaxValue)
        {
            Debug.LogError($"Invalid network port: {port}");
            return;
        }

        if (host)
        {
            _server = new NetworkServer();
            _server.ClientConnected += peerId =>
                Debug.Log($"Client {peerId} connected");
            _server.MessageReceived += message =>
            {
                var text = message.GetString();
                Debug.Log(text);
                _server.BroadcastString(message.Channel, text);
            };
            _server.Error += exception => Debug.LogError(exception.Message);
            _server.Start((ushort)port);
            Debug.Log($"Server listening on port {port}");
        }
        else
        {
            _client = new NetworkClient();
            _client.Connected += () =>
                _client.SendString(1, $"{playerName} joined");
            _client.MessageReceived += message => Debug.Log(message.GetString());
            _client.Error += exception => Debug.LogError(exception.Message);
            _ = ConnectAsync();
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        _server?.Poll();
        _client?.Poll();

        if (_client?.IsConnected == true && Input.IsKeyPressed(KeyCode.Space))
            _client.SendString(1, $"{playerName}: hello!");
    }

    public override void OnDestroy()
    {
        _server?.Dispose();
        _client?.Dispose();
        _server = null;
        _client = null;
    }

    private async Task ConnectAsync()
    {
        try
        {
            await _client!.ConnectAsync(serverAddress, (ushort)port);
        }
        catch (Exception exception)
        {
            Debug.LogError($"Connection failed: {exception.Message}");
        }
    }
}
