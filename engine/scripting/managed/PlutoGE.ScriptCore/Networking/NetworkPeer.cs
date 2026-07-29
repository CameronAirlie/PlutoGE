using System.Net.Sockets;
using System.Threading.Channels;

namespace PlutoGE.ScriptCore.Networking;

internal sealed class NetworkPeer : IAsyncDisposable
{
    private readonly TcpClient _client;
    private readonly NetworkStream _stream;
    private readonly Channel<byte[]> _outbound;
    private readonly CancellationTokenSource _cancellation = new();
    private readonly int _maxPayloadSize;
    private int _closed;

    public NetworkPeer(int id, TcpClient client, int maxPayloadSize, int outboundQueueCapacity)
    {
        Id = id;
        _client = client;
        _client.NoDelay = true;
        _stream = client.GetStream();
        _maxPayloadSize = maxPayloadSize;
        _outbound = Channel.CreateBounded<byte[]>(new BoundedChannelOptions(outboundQueueCapacity)
        {
            SingleReader = true,
            SingleWriter = false,
            FullMode = BoundedChannelFullMode.Wait
        });
    }

    public int Id { get; }
    public string RemoteAddress => _client.Client.RemoteEndPoint?.ToString() ?? string.Empty;

    public bool Send(ushort channel, ReadOnlySpan<byte> payload)
    {
        if (Volatile.Read(ref _closed) != 0)
            return false;
        return _outbound.Writer.TryWrite(NetworkProtocol.Frame(channel, payload, _maxPayloadSize));
    }

    public async Task RunAsync(Action<NetworkProtocol.NetworkFrame> onMessage, Action<Exception?> onClosed)
    {
        Exception? error = null;
        var readTask = ReadLoopAsync(onMessage);
        var writeTask = WriteLoopAsync();
        try
        {
            await await Task.WhenAny(readTask, writeTask);
        }
        catch (Exception exception) when (exception is not OperationCanceledException)
        {
            error = exception;
        }
        finally
        {
            _cancellation.Cancel();
            _outbound.Writer.TryComplete();
            _client.Close();
            try { await Task.WhenAll(readTask, writeTask); }
            catch (Exception exception) when (exception is not OperationCanceledException)
            {
                error ??= exception;
            }
            if (Interlocked.Exchange(ref _closed, 1) == 0)
                onClosed(error);
        }
    }

    private async Task ReadLoopAsync(Action<NetworkProtocol.NetworkFrame> onMessage)
    {
        while (!_cancellation.IsCancellationRequested)
        {
            var frame = await NetworkProtocol.ReadAsync(
                _stream, _maxPayloadSize, _cancellation.Token);
            if (frame is null)
                return;
            onMessage(frame.Value);
        }
    }

    private async Task WriteLoopAsync()
    {
        await foreach (var frame in _outbound.Reader.ReadAllAsync(_cancellation.Token))
            await _stream.WriteAsync(frame, _cancellation.Token);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _closed, 1) != 0)
            return;
        _cancellation.Cancel();
        _outbound.Writer.TryComplete();
        _client.Close();
        await Task.CompletedTask;
    }
}
