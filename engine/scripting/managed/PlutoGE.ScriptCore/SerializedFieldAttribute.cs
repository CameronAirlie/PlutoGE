using System;

namespace PlutoGE.ScriptCore;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class SerializedFieldAttribute : Attribute
{
}

/// <summary>
/// Marks a serialized <see cref="string"/> as a reference to a project asset of <see cref="Type"/>. The
/// inspector offers a picker of matching assets, and the asset is kept as a dependency when cooking.
/// Fields of a typed asset class (such as <see cref="InputActionMap"/>) need no attribute.
/// </summary>
[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public class AssetReferenceAttribute(AssetType type) : Attribute
{
    public AssetType Type { get; } = type;
}

public sealed class MaterialAssetAttribute() : AssetReferenceAttribute(AssetType.Material);

public sealed class InputMappingAssetAttribute() : AssetReferenceAttribute(AssetType.InputMapping);
