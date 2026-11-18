namespace PlutoGE.ScriptCore.Networking;

// Terrain volume edits over the network (docs/TERRAIN_VOLUMES.md, "Multiplayer"). Edits are deterministic operations,
// so the server applies each one, then sends it on; clients apply the same edits in the same order (the transport is
// reliable and ordered) and end up with the same ground. A client joining late first gets the whole state
// (TerrainVolumes.CaptureEdits) and then the edits after it. Clients ask; the server decides.
//
// Messages on the channel: [kind byte][payload]. 1 = state (capture bytes), 2 = edit applied by the server,
// 3 = a client's request.
internal static class TerrainVolumeMessages
{
    public const byte State = 1;
    public const byte Applied = 2;
    public const byte Request = 3;

    public static byte[] Edit(byte kind, in TerrainVolumeEdit edit)
    {
        var bytes = new byte[1 + TerrainVolumeEdit.EncodedSize];
        bytes[0] = kind;
        edit.Write(bytes.AsSpan(1));
        return bytes;
    }
}

/// <summary>The server half: applies edits (its own and the requests it accepts) and sends them to every client.</summary>
public sealed class TerrainVolumeServerReplicator : IDisposable
{
    private readonly NetworkServer _server;
    private readonly ushort _channel;

    /// <summary>Decides whether a client's request is applied (reach, tools, rate limits). Null accepts every request.</summary>
    public Func<int, TerrainVolumeEdit, bool>? Accept { get; set; }

    /// <summary>Raised for every edit the server applied, with the client that asked for it (0 for the server itself).</summary>
    public event Action<int, TerrainVolumeEdit, TerrainVolumeEditResult>? Applied;

    public TerrainVolumeServerReplicator(NetworkServer server, ushort channel = 81)
    {
        _server = server ?? throw new ArgumentNullException(nameof(server));
        _channel = channel;
        _server.ClientConnected += OnConnected;
        _server.MessageReceived += OnMessage;
    }

    /// <summary>Applies an edit here and sends it to every client.</summary>
    public TerrainVolumeEditResult Apply(in TerrainVolumeEdit edit) => Apply(0, edit);

    private TerrainVolumeEditResult Apply(int peer, in TerrainVolumeEdit edit)
    {
        var result = TerrainVolumes.Apply(edit);
        if (result.Changed)
            _server.Broadcast(_channel, TerrainVolumeMessages.Edit(TerrainVolumeMessages.Applied, edit));
        Applied?.Invoke(peer, edit, result);
        return result;
    }

    private void OnConnected(int peer)
    {
        var state = TerrainVolumes.CaptureEdits();
        var message = new byte[1 + state.Length];
        message[0] = TerrainVolumeMessages.State;
        state.CopyTo(message, 1);
        _server.Send(peer, _channel, message);
    }

    private void OnMessage(NetworkMessage message)
    {
        if (message.Channel != _channel || message.Payload.Length < 1) return;
        var payload = message.Payload.Span;
        if (payload[0] != TerrainVolumeMessages.Request || !TerrainVolumeEdit.TryRead(payload[1..], out var edit)) return;
        if (Accept is null || Accept(message.PeerId, edit))
            Apply(message.PeerId, edit);
    }

    public void Dispose()
    {
        _server.ClientConnected -= OnConnected;
        _server.MessageReceived -= OnMessage;
    }
}

/// <summary>The client half: takes the server's state and edits, and sends requests.</summary>
public sealed class TerrainVolumeClientReplicator : IDisposable
{
    private readonly NetworkClient _client;
    private readonly ushort _channel;

    /// <summary>Whether edits from the server make debris here too (pieces are local physics, never replicated).</summary>
    public bool LocalDebris { get; set; } = true;

    /// <summary>Raised when the server's whole state arrived (joining) and when one of its edits was applied.</summary>
    public event Action? StateReceived;
    public event Action<TerrainVolumeEdit, TerrainVolumeEditResult>? Applied;

    public TerrainVolumeClientReplicator(NetworkClient client, ushort channel = 81)
    {
        _client = client ?? throw new ArgumentNullException(nameof(client));
        _channel = channel;
        _client.MessageReceived += OnMessage;
    }

    /// <summary>Asks the server for an edit; it happens here when the server sends it back.</summary>
    public bool Request(in TerrainVolumeEdit edit) => _client.Send(_channel, TerrainVolumeMessages.Edit(TerrainVolumeMessages.Request, edit));

    private void OnMessage(NetworkMessage message)
    {
        if (message.Channel != _channel || message.Payload.Length < 1) return;
        var payload = message.Payload.Span;
        switch (payload[0])
        {
        case TerrainVolumeMessages.State:
            TerrainVolumes.RestoreEdits(payload[1..].ToArray());
            StateReceived?.Invoke();
            break;
        case TerrainVolumeMessages.Applied when TerrainVolumeEdit.TryRead(payload[1..], out var edit):
            var local = LocalDebris ? edit : edit with { DebrisPieces = 0 };
            Applied?.Invoke(edit, TerrainVolumes.Apply(local));
            break;
        }
    }

    public void Dispose() => _client.MessageReceived -= OnMessage;
}
