using System.Buffers.Binary;

namespace PlutoGE.ScriptCore.Networking;

internal static class NetworkProtocol
{
    public const int HeaderSize = 6;
    public const int DefaultMaxPayloadSize = 1024 * 1024;

    public static byte[] Frame(ushort channel, ReadOnlySpan<byte> payload, int maxPayloadSize)
    {
        if (payload.Length > maxPayloadSize)
            throw new ArgumentOutOfRangeException(nameof(payload),
                $"Payload is {payload.Length} bytes; the configured limit is {maxPayloadSize}.");

        var frame = new byte[HeaderSize + payload.Length];
        BinaryPrimitives.WriteInt32BigEndian(frame, payload.Length);
        BinaryPrimitives.WriteUInt16BigEndian(frame.AsSpan(4), channel);
        payload.CopyTo(frame.AsSpan(HeaderSize));
        return frame;
    }

    public static async ValueTask<NetworkFrame?> ReadAsync(
        Stream stream, int maxPayloadSize, CancellationToken cancellationToken)
    {
        var header = new byte[HeaderSize];
        if (!await ReadExactlyAsync(stream, header, allowCleanEnd: true, cancellationToken))
            return null;

        var payloadLength = BinaryPrimitives.ReadInt32BigEndian(header);
        if (payloadLength < 0 || payloadLength > maxPayloadSize)
            throw new InvalidDataException(
                $"Remote payload length {payloadLength} is outside the allowed range.");

        var payload = new byte[payloadLength];
        if (payloadLength > 0)
            await ReadExactlyAsync(stream, payload, allowCleanEnd: false, cancellationToken);

        return new NetworkFrame(
            BinaryPrimitives.ReadUInt16BigEndian(header.AsSpan(4)), payload);
    }

    private static async ValueTask<bool> ReadExactlyAsync(
        Stream stream, Memory<byte> buffer, bool allowCleanEnd, CancellationToken cancellationToken)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = await stream.ReadAsync(buffer[offset..], cancellationToken);
            if (read == 0)
            {
                if (allowCleanEnd && offset == 0)
                    return false;
                throw new EndOfStreamException("Connection ended in the middle of a message.");
            }
            offset += read;
        }
        return true;
    }

    internal readonly record struct NetworkFrame(ushort Channel, byte[] Payload);
}
