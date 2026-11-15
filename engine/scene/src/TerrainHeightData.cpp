#include "PlutoGE/scene/TerrainHeightData.h"

#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr char Magic[4] = {'P', 'L', 'H', 'T'};
        constexpr std::uint32_t BorderFlag = 1u;

        void Fail(std::string *error, std::string message)
        {
            if (error)
                *error = std::move(message);
        }

        void PutU32(std::vector<std::uint8_t> &out, std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<std::uint8_t>(value >> shift));
        }

        void PutF32(std::vector<std::uint8_t> &out, float value) { PutU32(out, std::bit_cast<std::uint32_t>(value)); }

        void PutU16(std::vector<std::uint8_t> &out, std::uint16_t value)
        {
            out.push_back(static_cast<std::uint8_t>(value));
            out.push_back(static_cast<std::uint8_t>(value >> 8));
        }

        std::uint32_t GetU32(const std::uint8_t *bytes)
        {
            return std::uint32_t(bytes[0]) | std::uint32_t(bytes[1]) << 8 | std::uint32_t(bytes[2]) << 16 | std::uint32_t(bytes[3]) << 24;
        }

        std::uint16_t GetU16(const std::uint8_t *bytes, bool bigEndian = false)
        {
            return bigEndian ? static_cast<std::uint16_t>(bytes[0] << 8 | bytes[1]) : static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8);
        }

        float GetF32(const std::uint8_t *bytes, bool bigEndian = false)
        {
            if (!bigEndian)
                return std::bit_cast<float>(GetU32(bytes));
            const std::uint8_t swapped[4] = {bytes[3], bytes[2], bytes[1], bytes[0]};
            return std::bit_cast<float>(GetU32(swapped));
        }

        float Finite(float value) { return std::isfinite(value) ? value : 0.0f; }

        std::string LowerExtension(std::string_view path)
        {
            const auto dot = path.find_last_of('.');
            const auto slash = path.find_last_of("/\\");
            if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
                return {};
            std::string extension(path.substr(dot));
            for (auto &character : extension)
                character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            return extension;
        }

        // Reverses the rows of a field in place (file top-row-first order to near-edge-first).
        void FlipRows(TerrainHeightField &field)
        {
            for (int z = 0; z < field.depth / 2; ++z)
                std::swap_ranges(field.samples.begin() + static_cast<std::ptrdiff_t>(z) * field.width,
                                 field.samples.begin() + static_cast<std::ptrdiff_t>(z + 1) * field.width,
                                 field.samples.begin() + static_cast<std::ptrdiff_t>(field.depth - 1 - z) * field.width);
        }

        // The grid of a headerless file: the given size, or the square that the sample count makes.
        bool RawDimensions(std::size_t samples, const TerrainHeightImportOptions &options, int &width, int &depth, std::string *error)
        {
            width = options.rawWidth;
            depth = options.rawDepth;
            if (width <= 0 && depth <= 0)
            {
                const auto side = static_cast<int>(std::llround(std::sqrt(static_cast<double>(samples))));
                width = depth = side;
            }
            else if (width <= 0)
                width = depth > 0 ? static_cast<int>(samples / static_cast<std::size_t>(depth)) : 0;
            else if (depth <= 0)
                depth = static_cast<int>(samples / static_cast<std::size_t>(width));
            if (width < 2 || depth < 2 || width > TerrainHeightFile::MaxDimension || depth > TerrainHeightFile::MaxDimension ||
                static_cast<std::size_t>(width) * static_cast<std::size_t>(depth) != samples)
            {
                Fail(error, "The raw file's size does not match a " + std::to_string(width) + " x " + std::to_string(depth) +
                                " grid; give its width and depth.");
                return false;
            }
            return true;
        }

        std::optional<TerrainHeightImport> DecodeImage(std::span<const std::uint8_t> bytes, const TerrainHeightImportOptions &options,
                                                       std::string *error)
        {
            if (bytes.size() > static_cast<std::size_t>(INT_MAX))
            {
                Fail(error, "The image is too large.");
                return std::nullopt;
            }
            const auto *data = reinterpret_cast<const stbi_uc *>(bytes.data());
            const int size = static_cast<int>(bytes.size());
            int width = 0, height = 0, channels = 0;
            TerrainHeightImport result;
            // The first channel is the height (greyscale images have only one).
            if (stbi_is_16_bit_from_memory(data, size))
            {
                stbi_us *pixels = stbi_load_16_from_memory(data, size, &width, &height, &channels, 1);
                if (!pixels)
                {
                    Fail(error, std::string("Could not decode the image: ") + stbi_failure_reason());
                    return std::nullopt;
                }
                result.field.samples.resize(static_cast<std::size_t>(width) * height);
                for (std::size_t index = 0; index < result.field.samples.size(); ++index)
                    result.field.samples[index] = static_cast<float>(pixels[index]) / 65535.0f;
                stbi_image_free(pixels);
                result.bitsPerSample = 16;
            }
            else
            {
                stbi_uc *pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 1);
                if (!pixels)
                {
                    Fail(error, std::string("Could not decode the image: ") + stbi_failure_reason());
                    return std::nullopt;
                }
                result.field.samples.resize(static_cast<std::size_t>(width) * height);
                for (std::size_t index = 0; index < result.field.samples.size(); ++index)
                    result.field.samples[index] = static_cast<float>(pixels[index]) / 255.0f;
                stbi_image_free(pixels);
                result.bitsPerSample = 8;
            }
            result.field.width = width;
            result.field.depth = height;
            if (!result.field.Valid())
            {
                Fail(error, "A heightmap needs at least 2 x 2 pixels.");
                return std::nullopt;
            }
            if (options.flipRows)
                FlipRows(result.field);
            return result;
        }

        std::optional<TerrainHeightImport> DecodeRaw(std::span<const std::uint8_t> bytes, bool isFloat,
                                                     const TerrainHeightImportOptions &options, std::string *error)
        {
            const std::size_t stride = isFloat ? 4 : 2;
            if (bytes.empty() || bytes.size() % stride != 0)
            {
                Fail(error, std::string("A raw ") + (isFloat ? "32-bit float" : "16-bit") + " file's size must be a multiple of " +
                                std::to_string(stride) + " bytes.");
                return std::nullopt;
            }
            TerrainHeightImport result;
            if (!RawDimensions(bytes.size() / stride, options, result.field.width, result.field.depth, error))
                return std::nullopt;
            result.field.samples.resize(bytes.size() / stride);
            for (std::size_t index = 0; index < result.field.samples.size(); ++index)
            {
                const auto *sample = bytes.data() + index * stride;
                result.field.samples[index] = isFloat ? Finite(GetF32(sample, options.rawBigEndian))
                                                      : static_cast<float>(GetU16(sample, options.rawBigEndian)) / 65535.0f;
            }
            result.normalized = !isFloat;
            result.bitsPerSample = isFloat ? 32 : 16;
            if (options.flipRows)
                FlipRows(result.field);
            return result;
        }
    }

    float TerrainHeightField::Sample(float x, float z) const
    {
        if (!Valid())
            return 0.0f;
        x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
        z = std::clamp(z, 0.0f, static_cast<float>(depth - 1));
        const int x0 = std::min(static_cast<int>(x), width - 2);
        const int z0 = std::min(static_cast<int>(z), depth - 2);
        const float tx = x - static_cast<float>(x0);
        const float tz = z - static_cast<float>(z0);
        const float lower = At(x0, z0) + (At(x0 + 1, z0) - At(x0, z0)) * tx;
        const float upper = At(x0, z0 + 1) + (At(x0 + 1, z0 + 1) - At(x0, z0 + 1)) * tx;
        return lower + (upper - lower) * tz;
    }

    std::size_t TerrainHeightBorder::Index(int width, int depth, int x, int z)
    {
        if (x < -Width || x >= width + Width || z < -Width || z >= depth + Width)
            return npos;
        const auto row = static_cast<std::size_t>(width + 2 * Width);
        const auto column = static_cast<std::size_t>(depth);
        if (z < 0)
            return static_cast<std::size_t>(z + Width) * row + static_cast<std::size_t>(x + Width);
        if (z >= depth)
            return (Width + static_cast<std::size_t>(z - depth)) * row + static_cast<std::size_t>(x + Width);
        const std::size_t sides = 2 * Width * row;
        if (x < 0)
            return sides + static_cast<std::size_t>(x + Width) * column + static_cast<std::size_t>(z);
        if (x >= width)
            return sides + (Width + static_cast<std::size_t>(x - width)) * column + static_cast<std::size_t>(z);
        return npos;
    }

    std::vector<float> TerrainHeightBorder::Clamped(const TerrainHeightField &field)
    {
        std::vector<float> ring(Size(field.width, field.depth));
        if (!field.Valid())
            return ring;
        for (int z = -Width; z < field.depth + Width; ++z)
            for (int x = -Width; x < field.width + Width; ++x)
                if (const auto index = Index(field.width, field.depth, x, z); index != npos)
                    ring[index] = field.At(std::clamp(x, 0, field.width - 1), std::clamp(z, 0, field.depth - 1));
        return ring;
    }

    void TerrainSampleRect::Include(const TerrainSampleRect &other)
    {
        if (other.Empty())
            return;
        if (Empty())
        {
            *this = other;
            return;
        }
        x0 = std::min(x0, other.x0);
        z0 = std::min(z0, other.z0);
        x1 = std::max(x1, other.x1);
        z1 = std::max(z1, other.z1);
    }

    namespace TerrainHeightFile
    {
        std::vector<std::uint8_t> Encode(const TerrainHeightData &data, TerrainHeightEncoding encoding)
        {
            if (!data.Valid())
                return {};
            float low = std::numeric_limits<float>::max();
            float high = std::numeric_limits<float>::lowest();
            for (const float sample : data.field.samples)
                low = std::min(low, sample), high = std::max(high, sample);
            for (const float sample : data.border)
                low = std::min(low, sample), high = std::max(high, sample);

            const std::size_t count = data.field.samples.size() + data.border.size();
            std::vector<std::uint8_t> out;
            out.reserve(HeaderSize + count * (encoding == TerrainHeightEncoding::Unorm16 ? 2 : 4));
            out.insert(out.end(), std::begin(Magic), std::end(Magic));
            PutU32(out, Version);
            PutU32(out, static_cast<std::uint32_t>(data.field.width));
            PutU32(out, static_cast<std::uint32_t>(data.field.depth));
            PutU32(out, static_cast<std::uint32_t>(encoding));
            PutU32(out, data.border.empty() ? 0u : BorderFlag);
            PutF32(out, low);
            PutF32(out, high);

            const float range = high - low;
            const auto put = [&](float sample) {
                if (encoding == TerrainHeightEncoding::Unorm16)
                    PutU16(out, range > 0.0f ? static_cast<std::uint16_t>(std::lround(std::clamp((sample - low) / range, 0.0f, 1.0f) * 65535.0f)) : 0);
                else
                    PutF32(out, sample);
            };
            for (const float sample : data.field.samples)
                put(sample);
            for (const float sample : data.border)
                put(sample);
            return out;
        }

        bool IsHeightFile(std::span<const std::uint8_t> bytes)
        {
            return bytes.size() >= HeaderSize && std::memcmp(bytes.data(), Magic, sizeof(Magic)) == 0;
        }

        std::optional<TerrainHeightData> Decode(std::span<const std::uint8_t> bytes, std::string *error)
        {
            if (!IsHeightFile(bytes))
            {
                Fail(error, "Not a .plutoheight file.");
                return std::nullopt;
            }
            const auto *header = bytes.data();
            const std::uint32_t version = GetU32(header + 4);
            const auto width = static_cast<std::int64_t>(GetU32(header + 8));
            const auto depth = static_cast<std::int64_t>(GetU32(header + 12));
            const std::uint32_t encodingValue = GetU32(header + 16);
            const std::uint32_t flags = GetU32(header + 20);
            const float low = GetF32(header + 24);
            const float high = GetF32(header + 28);
            if (version == 0 || version > Version)
            {
                Fail(error, "Unsupported .plutoheight version " + std::to_string(version) + ".");
                return std::nullopt;
            }
            if (width < 2 || depth < 2 || width > MaxDimension || depth > MaxDimension)
            {
                Fail(error, "Invalid .plutoheight grid size.");
                return std::nullopt;
            }
            if (encodingValue > static_cast<std::uint32_t>(TerrainHeightEncoding::Unorm16))
            {
                Fail(error, "Unknown .plutoheight sample encoding.");
                return std::nullopt;
            }
            const auto encoding = static_cast<TerrainHeightEncoding>(encodingValue);
            const std::size_t stride = encoding == TerrainHeightEncoding::Unorm16 ? 2 : 4;
            const auto samples = static_cast<std::size_t>(width * depth);
            const std::size_t borderSamples = (flags & BorderFlag) ? TerrainHeightBorder::Size(static_cast<int>(width), static_cast<int>(depth)) : 0;
            if (bytes.size() < HeaderSize + (samples + borderSamples) * stride)
            {
                Fail(error, "The .plutoheight file is truncated.");
                return std::nullopt;
            }

            TerrainHeightData data;
            data.field.width = static_cast<int>(width);
            data.field.depth = static_cast<int>(depth);
            data.field.samples.resize(samples);
            data.border.resize(borderSamples);
            const float range = std::isfinite(high - low) ? high - low : 0.0f;
            const auto *cursor = bytes.data() + HeaderSize;
            const auto read = [&]() {
                float value = 0.0f;
                if (encoding == TerrainHeightEncoding::Unorm16)
                    value = (std::isfinite(low) ? low : 0.0f) + static_cast<float>(GetU16(cursor)) / 65535.0f * range;
                else
                    value = Finite(GetF32(cursor));
                cursor += stride;
                return value;
            };
            for (auto &sample : data.field.samples)
                sample = read();
            for (auto &sample : data.border)
                sample = read();
            return data;
        }
    }

    TerrainHeightSourceFormat TerrainHeightFormatFromPath(std::string_view path)
    {
        const auto extension = LowerExtension(path);
        if (extension == TerrainHeightFile::Extension)
            return TerrainHeightSourceFormat::PlutoHeight;
        if (extension == ".r16" || extension == ".raw")
            return TerrainHeightSourceFormat::Raw16;
        if (extension == ".r32" || extension == ".f32")
            return TerrainHeightSourceFormat::Raw32Float;
        if (extension.empty())
            return TerrainHeightSourceFormat::Auto;
        return TerrainHeightSourceFormat::Image;
    }

    std::optional<TerrainHeightImport> DecodeTerrainHeightSource(std::span<const std::uint8_t> bytes, std::string_view path,
                                                                 const TerrainHeightImportOptions &options, std::string *error)
    {
        auto format = options.format;
        if (format == TerrainHeightSourceFormat::Auto)
            format = TerrainHeightFile::IsHeightFile(bytes) ? TerrainHeightSourceFormat::PlutoHeight : TerrainHeightFormatFromPath(path);
        if (format == TerrainHeightSourceFormat::Auto)
            format = TerrainHeightSourceFormat::Image;

        switch (format)
        {
        case TerrainHeightSourceFormat::PlutoHeight:
        {
            auto data = TerrainHeightFile::Decode(bytes, error);
            if (!data)
                return std::nullopt;
            TerrainHeightImport result;
            result.field = std::move(data->field);
            result.border = std::move(data->border);
            result.normalized = false;
            result.bitsPerSample = 32;
            return result;
        }
        case TerrainHeightSourceFormat::Raw16:
            return DecodeRaw(bytes, false, options, error);
        case TerrainHeightSourceFormat::Raw32Float:
            return DecodeRaw(bytes, true, options, error);
        default:
            return DecodeImage(bytes, options, error);
        }
    }

    TerrainHeightField ResampleHeightField(const TerrainHeightField &field, int width, int depth)
    {
        TerrainHeightField result;
        if (!field.Valid() || width < 2 || depth < 2)
            return result;
        result.width = width;
        result.depth = depth;
        result.samples.resize(static_cast<std::size_t>(width) * depth);
        const float scaleX = static_cast<float>(field.width - 1) / static_cast<float>(width - 1);
        const float scaleZ = static_cast<float>(field.depth - 1) / static_cast<float>(depth - 1);
        for (int z = 0; z < depth; ++z)
            for (int x = 0; x < width; ++x)
                result.At(x, z) = field.Sample(static_cast<float>(x) * scaleX, static_cast<float>(z) * scaleZ);
        return result;
    }
}
