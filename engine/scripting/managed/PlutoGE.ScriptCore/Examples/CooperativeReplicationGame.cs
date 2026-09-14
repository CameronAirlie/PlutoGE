using System.Numerics;
using PlutoGE.ScriptCore.Networking;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>Two players hold separate pads to open a gate. The server simulates all movement.
/// Attach before starting a server with MaxClients = 2. Call Tick after server.Poll.</summary>
public sealed class CooperativeReplicationGame : IDisposable
{
    public const ushort InputChannel = 81;
    public const ushort PlayerType = 1, GateType = 2, GateOpenProperty = 1;
    private sealed class Player(ulong id, Vector3 position)
    {
        public ulong Id = id;
        public Vector3 Position = position;
        public byte Input;
        public double LastInput;
    }
    private readonly NetworkServer _server;
    private readonly ReplicationServerSession _session;
    private readonly Dictionary<int, Player> _players = new();
    private readonly ulong _gate;
    private double _time, _publishElapsed;
    private bool _disposed;
    public bool GateOpen { get; private set; }

    public CooperativeReplicationGame(NetworkServer server, ulong sessionId)
    {
        ArgumentNullException.ThrowIfNull(server);
        if (server.MaxClients != 2) throw new ArgumentException("The cooperative sample requires MaxClients = 2.", nameof(server));
        _server = server;
        _session = new ReplicationServerSession(server, sessionId);
        _gate = _session.Authority.Spawn(GateType, 0, 0, new Vector3(0, 0, 5), Quaternion.Identity);
        server.ClientConnected += Join;
        server.ClientDisconnected += Leave;
        server.MessageReceived += Receive;
    }

    private void Join(int peer)
    {
        if (_players.Count >= 2) return;
        // Freed slots are reused, while network entity IDs are never reused.
        float x = _players.Values.Any(player => player.Position.X < 0) ? 2 : -2;
        var position = new Vector3(x, 0, -3);
        var id = _session.Authority.Spawn(PlayerType, peer, 0, position, Quaternion.Identity);
        if (id != 0) _players.Add(peer, new Player(id, position) { LastInput = _time });
    }
    private void Leave(int peer) => _players.Remove(peer);
    private void Receive(NetworkMessage message)
    {
        if (message.Channel != InputChannel || message.Payload.Length != 1 ||
            !_players.TryGetValue(message.PeerId, out var player)) return;
        byte input = message.Payload.Span[0];
        if ((input & 0xf0) != 0) return;
        player.Input = input;
        player.LastInput = _time;
    }

    /// <summary>Input bits: forward 1, back 2, left 4, right 8. No client pose or entity ID is accepted.</summary>
    public void Tick(float deltaTime)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!float.IsFinite(deltaTime) || deltaTime < 0 || deltaTime > 0.1f)
            throw new ArgumentOutOfRangeException(nameof(deltaTime));
        _time += deltaTime;
        foreach (var (peer, player) in _players)
        {
            byte input = _time - player.LastInput <= 0.25 ? player.Input : (byte)0;
            var direction = new Vector3(((input & 8) != 0 ? 1 : 0) - ((input & 4) != 0 ? 1 : 0), 0,
                ((input & 1) != 0 ? 1 : 0) - ((input & 2) != 0 ? 1 : 0));
            if (direction.LengthSquared() > 1) direction = Vector3.Normalize(direction);
            player.Position = Vector3.Clamp(player.Position + direction * (3 * deltaTime), new(-10, 0, -10), new(10, 0, 10));
            _session.Authority.Update(player.Id, 0, player.Position, Quaternion.Identity);
        }
        GateOpen = _players.Count == 2 &&
            _players.Values.Any(player => Vector3.DistanceSquared(player.Position, new(-2, 0, 0)) < 0.64f) &&
            _players.Values.Any(player => Vector3.DistanceSquared(player.Position, new(2, 0, 0)) < 0.64f);
        _session.Authority.Update(_gate, 0, new(0, GateOpen ? 3 : 0, 5), Quaternion.Identity,
            new Dictionary<ushort, float> { [GateOpenProperty] = GateOpen ? 1 : 0 });
        _publishElapsed += deltaTime;
        if (_publishElapsed >= 0.05)
        {
            _publishElapsed %= 0.05;
            _session.Publish();
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _server.ClientConnected -= Join;
        _server.ClientDisconnected -= Leave;
        _server.MessageReceived -= Receive;
        _session.Dispose();
        _players.Clear();
    }
}
