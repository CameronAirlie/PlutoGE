using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Native UI events, in the order of PlutoGE::scene::UIEventType.</summary>
public enum UIEventType
{
    PointerEnter,
    PointerExit,
    PointerDown,
    PointerUp,
    Click,
    BeginDrag,
    Drag,
    EndDrag,
    Drop,
    Scroll,
    Select,
    Deselect,
    Move,
    Submit,
    Cancel,
    ValueChanged,
    EndEdit,
    PointerMove,
}

/// <summary>
/// The native (RectTransform) UI: focus, input capture and the events controls raise. Control wrappers
/// such as <see cref="UISliderComponent"/> expose typed events built on <see cref="Subscribe"/>.
/// </summary>
public static class UI
{
    private sealed record Subscription(Delegate Handler, Action Invoke);

    private static readonly object Gate = new();
    private static readonly Dictionary<(uint Entity, UIEventType Type), List<Subscription>> Subscriptions = new();
    private static readonly ScriptBridge.NativeUIEventRecord[] EventBuffer = new ScriptBridge.NativeUIEventRecord[256];
    private static ulong _lastSequence;

    /// <summary>The focused control (keyboard and gamepad navigation), or null.</summary>
    public static GameObject? Focused
    {
        get
        {
            var id = ScriptBridge.GetUIFocus();
            return id == 0 ? null : new GameObject(id);
        }
        set => ScriptBridge.SetUIFocus(value?.EntityId ?? 0);
    }

    /// <summary>True while interactive UI has the pointer; gameplay input should ignore it.</summary>
    public static bool IsPointerOverUI => ScriptBridge.IsPointerOverUI();

    /// <summary>True while a text field is being edited; gameplay input should ignore the keyboard.</summary>
    public static bool IsKeyboardCaptured => ScriptBridge.IsUIKeyboardCaptured();

    /// <summary>Every event controls raised in the latest UI update: clicks, value changes and finished edits.</summary>
    public static event Action<GameObject, UIEventType>? EventRaised;

    /// <summary>
    /// Calls <paramref name="invoke"/> whenever <paramref name="entity"/> raises <paramref name="type"/>.
    /// <paramref name="handler"/> identifies the subscription for <see cref="Unsubscribe"/>.
    /// </summary>
    public static void Subscribe(uint entity, UIEventType type, Delegate? handler, Action invoke)
    {
        if (handler is null || entity == 0) return;
        lock (Gate)
        {
            if (!Subscriptions.TryGetValue((entity, type), out var list))
                Subscriptions[(entity, type)] = list = [];
            list.Add(new Subscription(handler, invoke));
        }
    }

    public static void Unsubscribe(uint entity, UIEventType type, Delegate? handler)
    {
        if (handler is null) return;
        lock (Gate)
        {
            if (!Subscriptions.TryGetValue((entity, type), out var list)) return;
            var index = list.FindLastIndex(subscription => subscription.Handler == handler);
            if (index >= 0) list.RemoveAt(index);
            if (list.Count == 0) Subscriptions.Remove((entity, type));
        }
    }

    // Raises the events of a new native UI update. Called before scripts update; repeat calls in the
    // same frame do nothing.
    internal static void DispatchEvents()
    {
        var count = ScriptBridge.PollUIEvents(EventBuffer, out var sequence);
        if (sequence == 0 || sequence == _lastSequence) return;
        _lastSequence = sequence;
        for (var index = 0; index < count; ++index)
        {
            var record = EventBuffer[index];
            var type = (UIEventType)record.Type;
            Subscription[] targets;
            lock (Gate)
            {
                targets = Subscriptions.TryGetValue((record.Entity, type), out var list) ? list.ToArray() : [];
            }
            foreach (var subscription in targets)
            {
                try { subscription.Invoke(); }
                catch (Exception exception) { Debug.LogError($"UI {type} handler failed: {exception}"); }
            }
            try { EventRaised?.Invoke(new GameObject(record.Entity), type); }
            catch (Exception exception) { Debug.LogError($"UI event handler failed: {exception}"); }
        }
    }

    // A destroyed script's handlers must not run (nor keep its assembly loaded).
    internal static void ForgetSubscriber(object instance)
    {
        lock (Gate)
        {
            foreach (var key in Subscriptions.Keys.ToArray())
            {
                var list = Subscriptions[key];
                list.RemoveAll(subscription => ReferenceEquals(subscription.Handler.Target, instance));
                if (list.Count == 0) Subscriptions.Remove(key);
            }
            EventRaised = RemoveTarget(EventRaised, instance);
        }
    }

    internal static void ClearSubscriptions()
    {
        lock (Gate)
        {
            Subscriptions.Clear();
            EventRaised = null;
            _lastSequence = 0;
        }
    }

    private static Action<GameObject, UIEventType>? RemoveTarget(Action<GameObject, UIEventType>? handlers, object target)
    {
        if (handlers is null) return null;
        foreach (var handler in handlers.GetInvocationList())
            if (ReferenceEquals(handler.Target, target))
                handlers -= (Action<GameObject, UIEventType>)handler;
        return handlers;
    }
}
