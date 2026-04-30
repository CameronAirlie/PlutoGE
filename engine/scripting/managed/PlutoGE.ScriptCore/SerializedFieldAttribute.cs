using System;

namespace PlutoGE.ScriptCore;

[AttributeUsage(AttributeTargets.Field | AttributeTargets.Property)]
public sealed class SerializedFieldAttribute : Attribute
{
}