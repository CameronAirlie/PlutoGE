using System.Text;
using System.Text.Json;

namespace PlutoGE.ScriptCore.Networking;

/// <summary>A complete application message received from a remote peer.</summary>
public readonly record struct NetworkMessage(int PeerId, ushort Channel, ReadOnlyMemory<byte> Payload)
{
    public string GetString() => Encoding.UTF8.GetString(Payload.Span);

    public T? GetJson<T>(JsonSerializerOptions? options = null) =>
        JsonSerializer.Deserialize<T>(Payload.Span, options);
}

/// <summary>Helpers for creating message payloads.</summary>
public static class NetworkPayload
{
    public static byte[] FromString(string value) => Encoding.UTF8.GetBytes(value);

    public static byte[] FromJson<T>(T value, JsonSerializerOptions? options = null) =>
        JsonSerializer.SerializeToUtf8Bytes(value, options);
}
