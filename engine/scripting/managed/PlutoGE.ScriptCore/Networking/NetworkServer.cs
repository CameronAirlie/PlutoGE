using System.Collections.Concurrent;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;

namespace PlutoGE.ScriptCore.Networking;

/// <summary>A reliable multiplayer server. Call <see cref="Poll"/> from OnUpdate.</summary>
public sealed class NetworkServer : IAsyncDisposable, IDisposable
{
    private readonly ConcurrentDictionary<int, NetworkPeer> _peers = new();
    private readonly ConcurrentQueue<Action> _events = new();
    private CancellationTokenSource? _cancellation;
    private TcpListener? _listener;
    private int _nextPeerId;

    public bool IsRunning => _listener is not null;
    public int ClientCount => _peers.Count;
    public int MaxPayloadSize { get; init; } = NetworkProtocol.DefaultMaxPayloadSize;
    public int OutboundQueueCapacity { get; init; } = 1024;

    public event Action<int>? ClientConnected;
    public event Action<int>? ClientDisconnected;
    public event Action<NetworkMessage>? MessageReceived;
    public event Action<Exception>? Error;

    public void Start(ushort port, int backlog = 128, string bindAddress = "0.0.0.0")
    {
        if (IsRunning)
            throw new InvalidOperationException("The server is already running.");
        if (MaxPayloadSize < 1)
            throw new InvalidOperationException("MaxPayloadSize must be positive.");
        if (OutboundQueueCapacity < 1)
            throw new InvalidOperationException("OutboundQueueCapacity must be positive.");
        if (backlog < 1)
            throw new ArgumentOutOfRangeException(nameof(backlog));
        if (!IPAddress.TryParse(bindAddress, out var address))
            throw new ArgumentException("Bind address must be an IPv4 or IPv6 address.", nameof(bindAddress));

        _cancellation = new CancellationTokenSource();
        _listener = new TcpListener(address, port);
        _listener.Server.SetSocketOption(
            SocketOptionLevel.Socket,
            SocketOptionName.ReuseAddress,
            true);
        _listener.Start(backlog);
        _ = AcceptLoopAsync(_listener, _cancellation.Token);
    }

    public bool Send(int peerId, ushort channel, ReadOnlySpan<byte> payload) =>
        _peers.TryGetValue(peerId, out var peer) && peer.Send(channel, payload);

    public bool SendString(int peerId, ushort channel, string value) =>
        Send(peerId, channel, NetworkPayload.FromString(value));

    public bool SendJson<T>(
        int peerId, ushort channel, T value, JsonSerializerOptions? options = null) =>
        Send(peerId, channel, NetworkPayload.FromJson(value, options));

    public int Broadcast(ushort channel, ReadOnlySpan<byte> payload, int exceptPeerId = 0)
    {
        var sent = 0;
        foreach (var peer in _peers.Values)
        {
            if (peer.Id != exceptPeerId && peer.Send(channel, payload))
                sent++;
        }
        return sent;
    }

    public int BroadcastString(ushort channel, string value, int exceptPeerId = 0) =>
        Broadcast(channel, NetworkPayload.FromString(value), exceptPeerId);

    public int BroadcastJson<T>(
        ushort channel, T value, int exceptPeerId = 0, JsonSerializerOptions? options = null) =>
        Broadcast(channel, NetworkPayload.FromJson(value, options), exceptPeerId);

    /// <summary>Dispatches queued callbacks on the calling thread.</summary>
    public int Poll(int maxEvents = 256)
    {
        if (maxEvents < 1)
            throw new ArgumentOutOfRangeException(nameof(maxEvents));
        var count = 0;
        while (count < maxEvents && _events.TryDequeue(out var callback))
        {
            callback();
            count++;
        }
        return count;
    }

    public async Task StopAsync()
    {
        var listener = Interlocked.Exchange(ref _listener, null);
        if (listener is null)
            return;
        listener.Stop();
        _cancellation?.Cancel();
        var peers = _peers.Values.ToArray();
        _peers.Clear();
        foreach (var peer in peers)
            await peer.DisposeAsync();
        _cancellation?.Dispose();
        _cancellation = null;
    }

    private async Task AcceptLoopAsync(TcpListener listener, CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                var client = await listener.AcceptTcpClientAsync(cancellationToken);
                var peerId = Interlocked.Increment(ref _nextPeerId);
                var peer = new NetworkPeer(peerId, client, MaxPayloadSize, OutboundQueueCapacity);
                if (!_peers.TryAdd(peerId, peer))
                {
                    await peer.DisposeAsync();
                    continue;
                }
                _events.Enqueue(() => ClientConnected?.Invoke(peerId));
                _ = peer.RunAsync(
                    frame => _events.Enqueue(() => MessageReceived?.Invoke(
                        new NetworkMessage(peerId, frame.Channel, frame.Payload))),
                    exception => OnPeerClosed(peerId, exception));
            }
        }
        catch (Exception exception) when (
            exception is not OperationCanceledException && IsRunning)
        {
            _events.Enqueue(() => Error?.Invoke(exception));
        }
    }

    private void OnPeerClosed(int peerId, Exception? exception)
    {
        if (!_peers.TryRemove(peerId, out _))
            return;
        if (exception is not null)
            _events.Enqueue(() => Error?.Invoke(exception));
        _events.Enqueue(() => ClientDisconnected?.Invoke(peerId));
    }

    public void Dispose() => StopAsync().GetAwaiter().GetResult();
    public ValueTask DisposeAsync() => new(StopAsync());
}
