using System.Collections.Concurrent;
using System.Net.Sockets;
using System.Text.Json;

namespace PlutoGE.ScriptCore.Networking;

/// <summary>A reliable multiplayer client. Call <see cref="Poll"/> from OnUpdate.</summary>
public sealed class NetworkClient : IAsyncDisposable, IDisposable
{
    private readonly ConcurrentQueue<Action> _events = new();
    private NetworkPeer? _peer;

    public bool IsConnected => _peer is not null;
    public int MaxPayloadSize { get; init; } = NetworkProtocol.DefaultMaxPayloadSize;
    public int OutboundQueueCapacity { get; init; } = 1024;

    public event Action? Connected;
    public event Action? Disconnected;
    public event Action<NetworkMessage>? MessageReceived;
    public event Action<Exception>? Error;

    public async Task ConnectAsync(string host, ushort port, CancellationToken cancellationToken = default)
    {
        if (IsConnected)
            throw new InvalidOperationException("The client is already connected.");
        if (MaxPayloadSize < 1)
            throw new InvalidOperationException("MaxPayloadSize must be positive.");
        if (OutboundQueueCapacity < 1)
            throw new InvalidOperationException("OutboundQueueCapacity must be positive.");

        var tcpClient = new TcpClient();
        try
        {
            await tcpClient.ConnectAsync(host, port, cancellationToken);
            var peer = new NetworkPeer(1, tcpClient, MaxPayloadSize, OutboundQueueCapacity);
            _peer = peer;
            _events.Enqueue(() => Connected?.Invoke());
            _ = peer.RunAsync(
                frame => _events.Enqueue(() => MessageReceived?.Invoke(
                    new NetworkMessage(0, frame.Channel, frame.Payload))),
                exception => OnClosed(peer, exception));
        }
        catch
        {
            tcpClient.Dispose();
            throw;
        }
    }

    public bool Send(ushort channel, ReadOnlySpan<byte> payload) =>
        _peer?.Send(channel, payload) ?? false;

    public bool SendString(ushort channel, string value) =>
        Send(channel, NetworkPayload.FromString(value));

    public bool SendJson<T>(
        ushort channel, T value, JsonSerializerOptions? options = null) =>
        Send(channel, NetworkPayload.FromJson(value, options));

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

    public async Task DisconnectAsync()
    {
        var peer = Interlocked.Exchange(ref _peer, null);
        if (peer is null)
            return;
        await peer.DisposeAsync();
        _events.Enqueue(() => Disconnected?.Invoke());
    }

    private void OnClosed(NetworkPeer peer, Exception? exception)
    {
        if (!ReferenceEquals(Interlocked.CompareExchange(ref _peer, null, peer), peer))
            return;
        if (exception is not null)
            _events.Enqueue(() => Error?.Invoke(exception));
        _events.Enqueue(() => Disconnected?.Invoke());
    }

    public void Dispose() => DisconnectAsync().GetAwaiter().GetResult();
    public ValueTask DisposeAsync() => new(DisconnectAsync());
}
