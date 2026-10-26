using System.Collections.Concurrent;
using System.Globalization;
using System.Numerics;
using System.Reflection;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Names the native component type (its ComponentRegistry name) a managed wrapper represents.</summary>
[AttributeUsage(AttributeTargets.Class, Inherited = false)]
public sealed class NativeComponentAttribute(string typeName) : Attribute
{
    public string TypeName { get; } = typeName;
}

/// <summary>
/// Base for wrappers of native components reached through the generic property bridge: properties are
/// read and written by their serialized names, so a wrapper is a class with
/// <see cref="NativeComponentAttribute"/> and typed properties, and needs no bridge code of its own.
/// </summary>
public abstract class RegisteredComponent : ComponentReference
{
    private static readonly ConcurrentDictionary<Type, string> TypeNames = new();

    protected RegisteredComponent(uint entityId) : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Registered;

    /// <summary>The native type name, as in scenes and the Add Component menu.</summary>
    public string NativeTypeName => TypeNameOf(GetType());

    /// <summary>Whether the component still exists on its entity.</summary>
    public bool Exists => ScriptBridge.HasRegisteredComponent(EntityId, NativeTypeName);

    internal static bool IsRegisteredType(Type type) =>
        typeof(RegisteredComponent).IsAssignableFrom(type) && !type.IsAbstract;

    internal static string TypeNameOf(Type type) =>
        TypeNames.GetOrAdd(type, static t => t.GetCustomAttribute<NativeComponentAttribute>()?.TypeName ??
            throw new InvalidOperationException($"{t.Name} needs a [NativeComponent] attribute."));

    internal static T? Create<T>(uint entityId) where T : class =>
        Activator.CreateInstance(typeof(T), BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public,
                                 null, [entityId], CultureInfo.InvariantCulture) as T;

    // ---- Property access by serialized name ------------------------------------------------------

    /// <summary>A property's serialized text, or null when it does not exist.</summary>
    public string? GetProperty(string name) => ScriptBridge.GetComponentProperty(EntityId, NativeTypeName, name);

    /// <summary>Sets a property from its serialized text; false when it does not exist.</summary>
    public bool SetProperty(string name, string value) => ScriptBridge.SetComponentProperty(EntityId, NativeTypeName, name, value);

    protected string GetString(string name) => GetProperty(name) ?? string.Empty;
    protected void SetString(string name, string value) => SetProperty(name, value ?? string.Empty);

    protected float GetFloat(string name, float fallback = 0.0f) =>
        float.TryParse(GetProperty(name), NumberStyles.Float, CultureInfo.InvariantCulture, out var value) ? value : fallback;
    protected void SetFloat(string name, float value) => SetProperty(name, value.ToString("R", CultureInfo.InvariantCulture));

    protected int GetInt(string name, int fallback = 0) =>
        int.TryParse(GetProperty(name), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : fallback;
    protected void SetInt(string name, int value) => SetProperty(name, value.ToString(CultureInfo.InvariantCulture));

    protected bool GetBool(string name) => GetProperty(name) is "true" or "1";
    protected void SetBool(string name, bool value) => SetProperty(name, value ? "true" : "false");

    protected TEnum GetEnum<TEnum>(string name) where TEnum : struct, Enum => (TEnum)Enum.ToObject(typeof(TEnum), GetInt(name));
    protected void SetEnum<TEnum>(string name, TEnum value) where TEnum : struct, Enum => SetInt(name, Convert.ToInt32(value, CultureInfo.InvariantCulture));

    protected Vector2 GetVector2(string name) => ParseVector(GetProperty(name), 2) is { } v ? new Vector2(v[0], v[1]) : Vector2.Zero;
    protected void SetVector2(string name, Vector2 value) => SetProperty(name, FormatVector(value.X, value.Y));

    protected Vector4 GetVector4(string name) => ParseVector(GetProperty(name), 4) is { } v ? new Vector4(v[0], v[1], v[2], v[3]) : Vector4.Zero;
    protected void SetVector4(string name, Vector4 value) => SetProperty(name, FormatVector(value.X, value.Y, value.Z, value.W));

    protected GameObject? GetEntity(string name) =>
        uint.TryParse(GetProperty(name), NumberStyles.Integer, CultureInfo.InvariantCulture, out var id) && id != 0 ? new GameObject(id) : null;
    protected void SetEntity(string name, GameObject? value) =>
        SetProperty(name, (value?.EntityId ?? 0).ToString(CultureInfo.InvariantCulture));

    private static float[]? ParseVector(string? text, int count)
    {
        if (string.IsNullOrEmpty(text)) return null;
        var parts = text.Split(',');
        if (parts.Length != count) return null;
        var values = new float[count];
        for (var index = 0; index < count; ++index)
            if (!float.TryParse(parts[index], NumberStyles.Float, CultureInfo.InvariantCulture, out values[index]))
                return null;
        return values;
    }

    private static string FormatVector(params float[] values) =>
        string.Join(",", values.Select(value => value.ToString("R", CultureInfo.InvariantCulture)));
}
