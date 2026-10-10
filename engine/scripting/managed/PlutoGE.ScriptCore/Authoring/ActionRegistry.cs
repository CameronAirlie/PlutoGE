namespace PlutoGE.ScriptCore.Authoring;

/// <summary>Instance-owned actions. Registration tokens prevent stale controllers from removing replacements.</summary>
public sealed class ActionRegistry
{
    private readonly Dictionary<string, Action> _actions = new(StringComparer.Ordinal);

    public IDisposable Register(string name, Action action)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(action);
        if (!_actions.TryAdd(name, action)) throw new InvalidOperationException($"Action '{name}' is already registered.");
        return new Subscription(() => { if (_actions.TryGetValue(name, out var current) && current == action) _actions.Remove(name); });
    }

    public bool Invoke(string name)
    {
        if (!_actions.TryGetValue(name, out var action)) return false;
        action();
        return true;
    }

    internal sealed class Subscription(Action dispose) : IDisposable
    {
        private Action? _dispose = dispose;
        public void Dispose()
        {
            var callback = Interlocked.Exchange(ref _dispose, null);
            try { callback?.Invoke(); }
            catch { Interlocked.CompareExchange(ref _dispose, callback, null); throw; }
        }
    }
}
