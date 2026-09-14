namespace PlutoGE.ScriptCore.Networking;

// Callbacks own received payloads until Poll. Bound both callback count and bytes;
// overload is terminal, so a replica never continues after silently lost messages.
internal sealed class NetworkEventQueue
{
    private readonly Queue<(Action Callback, int Bytes)> _queue = new();
    private readonly object _gate = new();
    private int _bytes;
    private bool _overflow;
    public bool Overflowed { get { lock (_gate) return _overflow; } }
    public void Enqueue(Action callback, int bytes = 0)
    {
        lock (_gate)
        {
            if (_overflow) return;
            if (_queue.Count >= 1024 || bytes > 16 * 1024 * 1024 - _bytes)
            { _overflow = true; return; }
            _queue.Enqueue((callback, bytes));
            _bytes += bytes;
        }
    }
    public bool TryDequeue(out Action callback)
    {
        lock (_gate)
        {
            if (_queue.TryDequeue(out var entry)) { _bytes -= entry.Bytes; callback = entry.Callback; return true; }
            callback = null!;
            return false;
        }
    }
    public void Clear()
    {
        lock (_gate) { _queue.Clear(); _bytes = 0; }
        // Overflow stays terminal. Create a new transport instance to reconnect.
    }
}
