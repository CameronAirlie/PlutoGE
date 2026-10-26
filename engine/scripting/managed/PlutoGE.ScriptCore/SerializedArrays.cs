using System.Collections;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Collection members (<c>T[]</c> and <c>List&lt;T&gt;</c>) of serialized fields. Each element converts like a
/// single field of type T. A collection crosses the bridge as one string with every element followed by
/// '\n' (the engine's asset reference array encoding), so an unassigned slot stays distinct from an empty array.
/// </summary>
internal static class SerializedArrays
{
    private const char Terminator = '\n';

    /// <summary>The element type of a supported collection type, or null for any other type.</summary>
    internal static Type? GetElementType(Type type)
    {
        if (type.IsArray)
            return type.GetArrayRank() == 1 ? type.GetElementType() : null;
        return type.IsGenericType && type.GetGenericTypeDefinition() == typeof(List<>) ? type.GetGenericArguments()[0] : null;
    }

    internal static object Create(Type collectionType, Type elementType, IReadOnlyList<object?> elements)
    {
        if (collectionType.IsArray)
        {
            var array = Array.CreateInstance(elementType, elements.Count);
            for (var index = 0; index < elements.Count; index++)
                array.SetValue(elements[index], index);
            return array;
        }

        var list = (IList)Activator.CreateInstance(collectionType, elements.Count)!;
        foreach (var element in elements)
            list.Add(element);
        return list;
    }

    internal static IEnumerable<object?> Enumerate(object? collection) =>
        collection is IEnumerable elements ? elements.Cast<object?>() : [];

    internal static string Encode(IEnumerable<string> elements) =>
        string.Concat(elements.Select(element => element.Replace(Terminator.ToString(), string.Empty) + Terminator));

    /// <summary>Malformed text (not terminated) reads as an empty collection, matching the native decoder.</summary>
    internal static IReadOnlyList<string> Decode(string text) =>
        text.Length == 0 || text[^1] != Terminator ? [] : text[..^1].Split(Terminator);
}
