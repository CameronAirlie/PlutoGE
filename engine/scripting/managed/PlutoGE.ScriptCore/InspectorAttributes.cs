namespace PlutoGE.ScriptCore;

/// <summary>Shows a numeric serialized field as a slider limited to <see cref="Min"/>..<see cref="Max"/>.</summary>
/// <example><code>[SerializedField, Range(0, 100)] public float Health = 100;</code></example>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class RangeAttribute(float min, float max) : Attribute
{
    public float Min { get; } = min;
    public float Max { get; } = max;
}

/// <summary>Text shown when the pointer rests on the field's name in the inspector.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class TooltipAttribute(string text) : Attribute
{
    public string Text { get; } = text;
}

/// <summary>Starts a titled group of fields in the inspector, above this field.</summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class HeaderAttribute(string title) : Attribute
{
    public string Title { get; } = title;
}

/// <summary>
/// Keeps values saved under an earlier name when a serialized field is renamed. Scenes, prefabs and scriptable
/// assets that still use <see cref="OldName"/> load into this field; they are saved under the new name.
/// </summary>
/// <example><code>[SerializedField, FormerlySerializedAs("speed")] public float MoveSpeed = 4;</code></example>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property, AllowMultiple = true)]
public sealed class FormerlySerializedAsAttribute(string oldName) : Attribute
{
    public string OldName { get; } = oldName;
}
