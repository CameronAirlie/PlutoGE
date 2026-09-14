using System.Buffers.Binary;

namespace PlutoGE.ScriptCore.Networking;

/// <summary>Game-thread replication host. The caller owns and polls the transport.</summary>
public sealed class ReplicationServerSession : IDisposable
{
    private readonly NetworkServer _transport;
    private readonly ushort _channel;
    private bool _disposed;
    public EntityReplicationAuthority Authority { get; }

    /// <summary>Attach before starting the transport. Reserve the channel for replication.</summary>
    public ReplicationServerSession(NetworkServer transport, ulong session, ushort channel = 80)
    {
        ArgumentNullException.ThrowIfNull(transport);
        if (transport.IsRunning) throw new InvalidOperationException("Attach replication before starting the server.");
        _transport = transport;
        _channel = channel;
        Authority = new EntityReplicationAuthority(session);
        transport.ClientConnected += OnConnected;
        transport.ClientDisconnected += OnDisconnected;
    }

    private void OnConnected(int peer)
    {
        Span<byte> welcome = stackalloc byte[18];
        BinaryPrimitives.WriteUInt32LittleEndian(welcome, 0x534c5550); // PULS
        BinaryPrimitives.WriteUInt16LittleEndian(welcome[4..], 1);
        BinaryPrimitives.WriteUInt64LittleEndian(welcome[6..], Authority.Session);
        BinaryPrimitives.WriteInt32LittleEndian(welcome[14..], peer);
        if (_transport.Send(peer, _channel, welcome))
            _transport.Send(peer, _channel, Authority.Snapshot());
    }

    private void OnDisconnected(int peer)
    {
        Authority.Disconnect(peer);
        Publish();
    }

    /// <summary>Publishes a full snapshot. Return value is the number of peers whose queues accepted it.</summary>
    public int Publish()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        return _transport.Broadcast(_channel, Authority.Snapshot());
    }

    /// <summary>Remove a logical network section and publish its despawns.</summary>
    public int UnloadSection(ulong section)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        Authority.UnloadSection(section);
        return Publish();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _transport.ClientConnected -= OnConnected;
        _transport.ClientDisconnected -= OnDisconnected;
    }
}

/// <summary>Negotiates one server session per connection. Poll with a monotonic game clock.</summary>
public sealed class ReplicationClientSession : IDisposable
{
    private readonly NetworkClient _transport;
    private readonly ushort _channel;
    private double _now;
    private bool _disposed;
    public EntityReplicationReplica? Replica { get; private set; }
    public int LocalPeerId { get; private set; }
    public event Action? SessionStarted;
    public event Action? SessionEnded;

    /// <summary>Attach before connecting. Use this wrapper's Poll instead of transport.Poll.</summary>
    public ReplicationClientSession(NetworkClient transport, ushort channel = 80)
    {
        ArgumentNullException.ThrowIfNull(transport);
        if (transport.IsConnected) throw new InvalidOperationException("Attach replication before connecting.");
        _transport = transport;
        _channel = channel;
        transport.MessageReceived += OnMessage;
        transport.Disconnected += EndSession;
    }

    public int Poll(double now, int maxEvents = 256)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (!double.IsFinite(now) || now < _now) throw new ArgumentOutOfRangeException(nameof(now));
        _now = now;
        try { return _transport.Poll(maxEvents); }
        catch { EndSession(); throw; }
    }

    private void OnMessage(NetworkMessage message)
    {
        if (_disposed || message.Channel != _channel || message.PeerId != 0) return;
        var bytes = message.Payload.Span;
        if (Replica is not null)
        {
            Replica.Apply(bytes, message.PeerId, _now);
            return;
        }
        if (bytes.Length != 18 || BinaryPrimitives.ReadUInt32LittleEndian(bytes) != 0x534c5550 ||
            BinaryPrimitives.ReadUInt16LittleEndian(bytes[4..]) != 1) return;
        ulong session = BinaryPrimitives.ReadUInt64LittleEndian(bytes[6..]);
        int peer = BinaryPrimitives.ReadInt32LittleEndian(bytes[14..]);
        if (session == 0 || peer <= 0) return;
        Replica = new EntityReplicationReplica(session);
        LocalPeerId = peer;
        SessionStarted?.Invoke();
    }

    private void EndSession()
    {
        bool active = Replica is not null;
        Replica?.Clear();
        Replica = null;
        LocalPeerId = 0;
        if (active) SessionEnded?.Invoke();
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _transport.MessageReceived -= OnMessage;
        _transport.Disconnected -= EndSession;
        EndSession();
    }
}
