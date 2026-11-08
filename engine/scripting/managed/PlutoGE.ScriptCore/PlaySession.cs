using System.Collections;
using System.Reflection;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Resets a static field or property when Play starts. Static state otherwise survives from one play session to
/// the next, because the script assembly stays loaded between sessions. Collections are cleared in place, so
/// <c>static readonly List&lt;T&gt;</c> registries work. Other values return to their type's default (0, false,
/// null); set a different starting value in an <see cref="Application.PlayStarted"/> handler.
/// </summary>
/// <example><code>[ResetOnPlay] private static readonly List&lt;Enemy&gt; Active = [];</code></example>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class ResetOnPlayAttribute : Attribute
{
}

// Engine-side play session bookkeeping: resets engine and [ResetOnPlay] statics and raises Application events.
internal static class PlaySession
{
    private static readonly HashSet<string> Warned = new(StringComparer.Ordinal);

    internal static void Begin(Assembly? userAssembly)
    {
        ResetEngineState();
        if (userAssembly is not null)
            ResetMarkedStatics(userAssembly);
        Application.SetPlaying(true);
        Application.RaisePlayStarted();
    }

    internal static void End()
    {
        Application.RaisePlayStopped();
        Application.SetPlaying(false);
        ResetEngineState();
    }

    private static void ResetEngineState()
    {
        GamePause.IsPaused = false;
        Timers.Clear();
    }

    private static void ResetMarkedStatics(Assembly assembly)
    {
        Type[] types;
        try
        {
            types = assembly.GetTypes();
        }
        catch (ReflectionTypeLoadException exception)
        {
            types = exception.Types.Where(type => type is not null).ToArray()!;
        }

        const BindingFlags flags = BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly;
        foreach (var type in types)
        {
            if (type.ContainsGenericParameters)
                continue;
            foreach (var field in type.GetFields(flags))
            {
                if (field.GetCustomAttribute<ResetOnPlayAttribute>() is null)
                    continue;
                Reset(type, field.Name, field.FieldType, () => field.GetValue(null),
                      field.IsInitOnly || field.IsLiteral ? null : value => field.SetValue(null, value));
            }

            foreach (var property in type.GetProperties(flags))
            {
                if (property.GetCustomAttribute<ResetOnPlayAttribute>() is null || property.GetMethod is null)
                    continue;
                Reset(type, property.Name, property.PropertyType, () => property.GetValue(null),
                      property.SetMethod is null ? null : value => property.SetValue(null, value));
            }
        }
    }

    private static void Reset(Type owner, string name, Type valueType, Func<object?> read, Action<object?>? write)
    {
        var current = read();
        // Clear collections in place so readonly registries keep their instance.
        if (current is IList list && !list.IsReadOnly && !list.IsFixedSize)
        {
            list.Clear();
            return;
        }

        if (current is IDictionary dictionary && !dictionary.IsReadOnly)
        {
            dictionary.Clear();
            return;
        }

        if (current is not null && current.GetType().GetMethod("Clear", Type.EmptyTypes) is { } clear &&
            current is IEnumerable and not string)
        {
            clear.Invoke(current, null);
            return;
        }

        if (write is null)
        {
            if (Warned.Add(owner.FullName + "." + name))
                Debug.LogWarning($"[ResetOnPlay] {owner.Name}.{name} is read-only and not a collection, so it cannot be reset.");
            return;
        }

        write(valueType.IsValueType ? Activator.CreateInstance(valueType) : null);
    }
}
