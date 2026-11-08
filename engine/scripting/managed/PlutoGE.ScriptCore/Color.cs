using System.Globalization;
using System.Numerics;

namespace PlutoGE.ScriptCore;

/// <summary>
/// A linear RGBA colour with components from 0 to 1. Serialized fields of this type get a colour picker in the
/// inspector. Converts implicitly to and from <see cref="Vector4"/>, which engine UI and light APIs accept.
/// </summary>
public readonly record struct Color(float R, float G, float B, float A = 1.0f)
{
    public static Color White => new(1, 1, 1);
    public static Color Black => new(0, 0, 0);
    public static Color Clear => new(0, 0, 0, 0);
    public static Color Red => new(1, 0, 0);
    public static Color Green => new(0, 1, 0);
    public static Color Blue => new(0, 0, 1);
    public static Color Yellow => new(1, 1, 0);

    /// <summary>The same colour with alpha <paramref name="alpha"/>.</summary>
    public Color WithAlpha(float alpha) => this with { A = alpha };

    public static Color Lerp(Color from, Color to, float amount)
    {
        amount = Math.Clamp(amount, 0.0f, 1.0f);
        return new Color(from.R + (to.R - from.R) * amount, from.G + (to.G - from.G) * amount,
            from.B + (to.B - from.B) * amount, from.A + (to.A - from.A) * amount);
    }

    /// <summary>Parses "#RRGGBB" or "#RRGGBBAA" (sRGB hex as typed in design tools, converted to linear).</summary>
    public static bool TryParseHex(string text, out Color color)
    {
        color = White;
        var hex = text.Trim().TrimStart('#');
        if ((hex.Length != 6 && hex.Length != 8) ||
            !uint.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var packed))
        {
            return false;
        }

        if (hex.Length == 6)
        {
            packed = (packed << 8) | 0xFF;
        }

        static float Linear(uint channel)
        {
            var srgb = channel / 255.0f;
            return srgb <= 0.04045f ? srgb / 12.92f : MathF.Pow((srgb + 0.055f) / 1.055f, 2.4f);
        }

        color = new Color(Linear((packed >> 24) & 0xFF), Linear((packed >> 16) & 0xFF), Linear((packed >> 8) & 0xFF),
            ((packed & 0xFF) / 255.0f));
        return true;
    }

    public static implicit operator Vector4(Color color) => new(color.R, color.G, color.B, color.A);
    public static implicit operator Color(Vector4 vector) => new(vector.X, vector.Y, vector.Z, vector.W);
}
