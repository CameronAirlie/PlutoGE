using System.Security.Cryptography;
using PlutoGE.ScriptCore.Networking;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>Attach to a persistent scene entity in two players. One hosts; both control a player with WASD.</summary>
public sealed class CooperativeReplicationSample : ScriptBehaviour
{
    [SerializedField] private bool host = false;
    [SerializedField] private string address = "127.0.0.1";
    [SerializedField] private int port = 7777;
    [SerializedField] private string playerPrefab = "";
    [SerializedField] private string gatePrefab = "";
    private NetworkServer? _server;
    private CooperativeReplicationGame? _game;
    private NetworkClient? _client;
    private ReplicationClientSession? _session;
    private ReplicatedScene? _scene;
    private CancellationTokenSource? _cancellation;
    private Task? _connection;
    private double _time, _inputElapsed;

    public override void OnCreate()
    {
        if (port is < 1 or > 65535 || string.IsNullOrWhiteSpace(playerPrefab) || string.IsNullOrWhiteSpace(gatePrefab))
        {
            Debug.LogWarning("Cooperative sample requires a port and player/gate prefab references.");
            return;
        }
        try
        {
            if (host)
            {
                _server = new NetworkServer { MaxClients = 2, MaxPayloadSize = EntityReplicationAuthority.MaxPayloadBytes };
                ulong sessionId;
                do { sessionId = BitConverter.ToUInt64(RandomNumberGenerator.GetBytes(8)); } while (sessionId == 0);
                _game = new CooperativeReplicationGame(_server, sessionId);
                _server.Start((ushort)port);
            }
            _client = new NetworkClient { MaxPayloadSize = EntityReplicationAuthority.MaxPayloadBytes };
            _session = new ReplicationClientSession(_client);
            _session.SessionStarted += BindScene;
            _session.SessionEnded += ReleaseScene;
            _cancellation = new CancellationTokenSource();
            _connection = _client.ConnectAsync(host ? "127.0.0.1" : address, (ushort)port, _cancellation.Token);
        }
        catch (Exception error) { Debug.LogWarning(error.Message); OnDestroy(); }
    }

    private void BindScene()
    {
        ReleaseScene();
        _scene = new ReplicatedScene(_session!.Replica!, new Dictionary<ushort, Func<GameObject?>>
        {
            [CooperativeReplicationGame.PlayerType] = () => Prefab.Instantiate(playerPrefab),
            [CooperativeReplicationGame.GateType] = () => Prefab.Instantiate(gatePrefab)
        });
    }
    private void ReleaseScene() { _scene?.Dispose(); _scene = null; }

    public override void OnUpdate(float deltaTime)
    {
        if (_session is null || !float.IsFinite(deltaTime) || deltaTime < 0) return;
        try
        {
            if (_connection?.IsFaulted == true) throw _connection.Exception!.GetBaseException();
            float step = Math.Min(deltaTime, 0.1f);
            _time += step;
            _server?.Poll();
            _game?.Tick(step);
            _session.Poll(_time);
            _scene?.Update(_time);
            _inputElapsed += step;
            if (_session.LocalPeerId != 0 && _inputElapsed >= 0.05)
            {
                _inputElapsed %= 0.05;
                byte input = (byte)((Input.IsKeyDown(KeyCode.W) ? 1 : 0) | (Input.IsKeyDown(KeyCode.S) ? 2 : 0) |
                    (Input.IsKeyDown(KeyCode.A) ? 4 : 0) | (Input.IsKeyDown(KeyCode.D) ? 8 : 0));
                _client!.Send(CooperativeReplicationGame.InputChannel, [input]);
            }
        }
        catch (Exception error) { Debug.LogWarning(error.Message); OnDestroy(); }
    }

    public override void OnDestroy()
    {
        ReleaseScene();
        _session?.Dispose(); _session = null;
        _cancellation?.Cancel();
        // Connect may still be completing on the socket thread. Its continuation
        // owns final transport cleanup; it never touches the native scene.
        var client = _client;
        var cancellation = _cancellation;
        if (_connection is { } connection)
            _ = connection.ContinueWith(completed => { _ = completed.Exception; client?.Dispose(); cancellation?.Dispose(); }, TaskScheduler.Default);
        else { client?.Dispose(); cancellation?.Dispose(); }
        _client = null; _cancellation = null; _connection = null;
        _game?.Dispose(); _game = null;
        _server?.Dispose(); _server = null;
    }
}
