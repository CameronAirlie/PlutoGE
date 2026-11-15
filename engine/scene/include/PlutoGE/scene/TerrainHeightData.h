#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::scene
{
    // A grid of height samples, row-major, row 0 at the terrain's near edge (local z = 0), column 0 at local x = 0.
    // What the samples mean (world units, or 0..1 from an image) is up to whoever holds the field.
    struct TerrainHeightField
    {
        int width = 0;
        int depth = 0;
        std::vector<float> samples;

        [[nodiscard]] bool Valid() const
        {
            return width >= 2 && depth >= 2 && samples.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(depth);
        }
        [[nodiscard]] float At(int x, int z) const { return samples[static_cast<std::size_t>(z) * width + x]; }
        float &At(int x, int z) { return samples[static_cast<std::size_t>(z) * width + x]; }
        // Bilinear sample at fractional sample coordinates, clamped to the grid.
        [[nodiscard]] float Sample(float x, float z) const;
        bool operator==(const TerrainHeightField &) const = default;
    };

    // The samples just outside a field's edges, Width deep: what the neighbouring tiles hold there. A terrain uses them
    // instead of repeating its edge samples when it smooths its surface (one sample) and computes normals (one more), so
    // tiles that share an edge shade identically across it. Layout: the rows below (z = -2, -1; x = -2..width+1), the
    // rows above (z = depth, depth+1), then the columns left (x = -2, -1; z = 0..depth-1) and right (x = width, width+1).
    struct TerrainHeightBorder
    {
        static constexpr int Width = 2;
        [[nodiscard]] static std::size_t Size(int width, int depth)
        {
            return 2 * Width * static_cast<std::size_t>(width + 2 * Width) + 2 * Width * static_cast<std::size_t>(depth);
        }
        // Index into the ring of a sample outside the grid and within Width of it, or npos for any other coordinate.
        [[nodiscard]] static std::size_t Index(int width, int depth, int x, int z);
        // The ring a field would have if its edge samples simply repeated (what a lone terrain does).
        [[nodiscard]] static std::vector<float> Clamped(const TerrainHeightField &field);
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    };

    // A half-open rectangle of height samples, [x0, x1) x [z0, z1).
    struct TerrainSampleRect
    {
        int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
        [[nodiscard]] bool Empty() const { return x1 <= x0 || z1 <= z0; }
        void Include(const TerrainSampleRect &other);
        bool operator==(const TerrainSampleRect &) const = default;
    };

    // A copy of the samples in a rectangle, for undoing height edits without copying the whole terrain.
    struct TerrainHeightRegion
    {
        TerrainSampleRect rect;
        std::vector<float> samples; // rows of the rect, z0 first
    };

    // Heights as a terrain stores them in a .plutoheight file: world-unit samples (the terrain's local y) and,
    // optionally, the border ring (empty when absent).
    struct TerrainHeightData
    {
        TerrainHeightField field;
        std::vector<float> border;

        [[nodiscard]] bool Valid() const
        {
            return field.Valid() && (border.empty() || border.size() == TerrainHeightBorder::Size(field.width, field.depth));
        }
        bool operator==(const TerrainHeightData &) const = default;
    };

    enum class TerrainHeightEncoding : std::uint32_t
    {
        Float32 = 0, // exact
        Unorm16 = 1, // half the size; quantized to 1/65535 of the data's height range
    };

    // The .plutoheight format (version 1, little endian):
    //   char[4] magic "PLHT", u32 version, u32 width, u32 depth, u32 encoding, u32 flags (bit 0: border ring follows),
    //   f32 minimum height, f32 maximum height (the unorm16 range; informational for float32)
    //   then width*depth samples, rows of increasing z, then the border ring when flagged.
    namespace TerrainHeightFile
    {
        inline constexpr std::string_view Extension = ".plutoheight";
        inline constexpr std::uint32_t Version = 1;
        inline constexpr std::size_t HeaderSize = 32;
        // Grids larger than this a side are refused (a corrupt header must not allocate gigabytes).
        inline constexpr int MaxDimension = 16385;

        [[nodiscard]] std::vector<std::uint8_t> Encode(const TerrainHeightData &data,
                                                       TerrainHeightEncoding encoding = TerrainHeightEncoding::Float32);
        [[nodiscard]] std::optional<TerrainHeightData> Decode(std::span<const std::uint8_t> bytes, std::string *error = nullptr);
        // True when the bytes start like a .plutoheight file.
        [[nodiscard]] bool IsHeightFile(std::span<const std::uint8_t> bytes);
    }

    // ---- importing heightmaps from other tools ------------------------------------------------------------------

    enum class TerrainHeightSourceFormat
    {
        Auto,        // from the extension, then the content
        Image,       // PNG, TGA, BMP, JPG, PSD, GIF: 16-bit PNGs keep their 16 bits
        Raw16,       // headerless unsigned 16-bit samples (.r16, .raw)
        Raw32Float,  // headerless 32-bit float samples (.r32, .f32)
        PlutoHeight, // .plutoheight
    };

    struct TerrainHeightImportOptions
    {
        TerrainHeightSourceFormat format = TerrainHeightSourceFormat::Auto;
        // Raw files have no header: their size. 0 infers a square grid from the file size.
        int rawWidth = 0;
        int rawDepth = 0;
        bool rawBigEndian = false;
        // Images and raw files list their top row first, and the top row is the terrain's far (+z) edge, so their rows
        // are reversed. Clear for sources already ordered near edge first.
        bool flipRows = true;
    };

    struct TerrainHeightImport
    {
        TerrainHeightField field;
        // Image and raw 16-bit samples are normalized to 0..1 and need a height range; float and .plutoheight samples
        // are heights as stored.
        bool normalized = true;
        std::vector<float> border; // .plutoheight only
        int bitsPerSample = 0;     // 8, 16 or 32
    };

    [[nodiscard]] TerrainHeightSourceFormat TerrainHeightFormatFromPath(std::string_view path);
    // Decodes a heightmap file's bytes. `path` only picks the format when it is Auto.
    [[nodiscard]] std::optional<TerrainHeightImport> DecodeTerrainHeightSource(std::span<const std::uint8_t> bytes,
                                                                               std::string_view path,
                                                                               const TerrainHeightImportOptions &options = {},
                                                                               std::string *error = nullptr);

    // Bilinear resampling to another resolution (the corners stay on the corners).
    [[nodiscard]] TerrainHeightField ResampleHeightField(const TerrainHeightField &field, int width, int depth);
}
