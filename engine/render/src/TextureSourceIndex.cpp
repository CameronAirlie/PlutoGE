#include "PlutoGE/render/TextureSourceIndex.h"

#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/render/TextureCooking.h"
#include "PlutoGE/render/rhi/FormatInfo.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string_view>

namespace PlutoGE::render
{
    namespace
    {
        constexpr std::string_view kRecordHeader = "PLUTOTEXSRC\t1";
        constexpr std::string_view kRecordDirectory = "Sources";

        // Record keys: what a file is known by in each kind of index.
        std::string WritableKey(const std::filesystem::path &file, const TextureFileVersion &version)
        {
            return "file\n" + std::filesystem::absolute(file).lexically_normal().generic_string() + "\n" +
                   std::to_string(version.size) + "\n" + std::to_string(version.modified);
        }

        std::optional<std::string> LayerKey(const TextureSourceIndex::Layer &layer, const std::filesystem::path &file,
                                            std::uint64_t size)
        {
            const auto relative = std::filesystem::absolute(file).lexically_normal().lexically_relative(
                std::filesystem::absolute(layer.contentRoot).lexically_normal());
            if (relative.empty() || *relative.begin() == "..")
                return std::nullopt;
            return "content\n" + relative.generic_string() + "\n" + std::to_string(size);
        }

        std::filesystem::path RecordPath(const std::filesystem::path &directory, const std::string &key)
        {
            const auto hex = content::DigestToHex(content::HashContent(std::as_bytes(std::span(key.data(), key.size()))));
            return directory / hex.substr(0, 2) / (hex + ".ptxs");
        }

        template <class T>
        bool ParseNumber(std::string_view text, T &value)
        {
            const auto end = text.data() + text.size();
            const auto [at, error] = std::from_chars(text.data(), end, value);
            return error == std::errc{} && at == end;
        }

        std::vector<std::string_view> Split(std::string_view line, char separator)
        {
            std::vector<std::string_view> fields;
            for (std::size_t begin = 0;;)
            {
                const auto end = line.find(separator, begin);
                fields.push_back(line.substr(begin, end == std::string_view::npos ? std::string_view::npos : end - begin));
                if (end == std::string_view::npos)
                    return fields;
                begin = end + 1;
            }
        }

        std::string FormatVector(const glm::vec4 &value)
        {
            char text[128];
            std::snprintf(text, sizeof(text), "%.9g %.9g %.9g %.9g", value.x, value.y, value.z, value.w);
            return text;
        }

        bool ParseVector(std::string_view text, glm::vec4 &value)
        {
            const auto parts = Split(text, ' ');
            if (parts.size() != 4)
                return false;
            for (int i = 0; i < 4; ++i)
            {
                // from_chars for floats: exact round trip of the %.9g text.
                if (!ParseNumber(parts[static_cast<std::size_t>(i)], value[i]))
                    return false;
            }
            return true;
        }

        // Rows: header; size, channels and means; then one row per sampling.
        std::string Serialize(const TextureSourceRecord &record)
        {
            std::string text(kRecordHeader);
            text += '\n' + std::to_string(record.width) + '\t' + std::to_string(record.height) + '\t' +
                    std::to_string(record.channels) + '\t' + FormatVector(record.means.raw) + '\t' +
                    FormatVector(record.means.srgb) + '\n';
            for (const auto &sampling : record.samplings)
            {
                char policy[17];
                std::snprintf(policy, sizeof(policy), "%016llx", static_cast<unsigned long long>(sampling.policy));
                text += std::to_string(static_cast<unsigned>(sampling.semantic)) + '\t' + (sampling.srgb ? "1" : "0") + '\t' +
                        policy + '\t' + std::to_string(static_cast<unsigned>(sampling.format)) + '\t' +
                        content::DigestToHex(sampling.entry) + '\n';
            }
            return text;
        }

        std::optional<TextureSourceRecord> Parse(std::string_view text)
        {
            std::vector<std::string_view> lines;
            for (auto line : Split(text, '\n'))
            {
                if (!line.empty() && line.back() == '\r')
                    line.remove_suffix(1);
                if (!line.empty())
                    lines.push_back(line);
            }
            if (lines.size() < 2 || lines[0] != kRecordHeader)
                return std::nullopt;
            TextureSourceRecord record;
            const auto size = Split(lines[1], '\t');
            if (size.size() != 5 || !ParseNumber(size[0], record.width) || !ParseNumber(size[1], record.height) ||
                !ParseNumber(size[2], record.channels) || !ParseVector(size[3], record.means.raw) ||
                !ParseVector(size[4], record.means.srgb) || !record.width || !record.height)
                return std::nullopt;
            for (std::size_t index = 2; index < lines.size(); ++index)
            {
                const auto fields = Split(lines[index], '\t');
                unsigned semantic = 0, format = 0;
                TextureSourceSampling sampling;
                if (fields.size() != 5 || !ParseNumber(fields[0], semantic) || semantic > 2 ||
                    (fields[1] != "0" && fields[1] != "1") || fields[2].size() != 16 || !ParseNumber(fields[3], format) ||
                    !content::ParseContentDigest(fields[4], sampling.entry))
                    return std::nullopt;
                const auto policyEnd = fields[2].data() + fields[2].size();
                if (std::from_chars(fields[2].data(), policyEnd, sampling.policy, 16).ptr != policyEnd)
                    return std::nullopt;
                sampling.semantic = static_cast<TextureSemantic>(semantic);
                sampling.srgb = fields[1] == "1";
                sampling.format = static_cast<rhi::Format>(format);
                if (!rhi::IsBlockCompressed(sampling.format))
                    return std::nullopt;
                record.samplings.push_back(sampling);
            }
            return record;
        }

        std::optional<TextureSourceRecord> ReadRecord(const std::filesystem::path &path)
        {
            std::string text;
            if (!content::Exists(path) || !content::ReadFile(path, text))
                return std::nullopt;
            return Parse(text);
        }

        // Adds `sampling` to the record at `path` (replacing one for the same use), written aside and renamed.
        bool WriteRecord(const std::filesystem::path &path, std::uint32_t width, std::uint32_t height, int channels,
                         const TextureMeans &means, const TextureSourceSampling &sampling, TextureSourceRecord *written)
        {
            TextureSourceRecord record;
            if (auto existing = ReadRecord(path); existing && existing->width == width && existing->height == height)
                record = std::move(*existing);
            record.width = width;
            record.height = height;
            record.channels = channels;
            record.means = means;
            std::erase_if(record.samplings, [&](const auto &known) {
                return known.semantic == sampling.semantic && known.srgb == sampling.srgb && known.policy == sampling.policy;
            });
            record.samplings.push_back(sampling);
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            const auto temporary = path.parent_path() / (path.filename().string() + "." +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output << Serialize(record);
                if (!output)
                    error = std::make_error_code(std::errc::io_error);
            }
            if (!error)
                std::filesystem::rename(temporary, path, error);
            if (error)
            {
                std::filesystem::remove(temporary, error);
                return false;
            }
            if (written)
                *written = std::move(record);
            return true;
        }
    }

    TextureMeans ComputeTextureMeans(std::span<const std::byte> pixels)
    {
        // At most about 65536 evenly strided texels: enough for a mean, and cheap for large textures.
        const auto texels = pixels.size() / 4;
        const std::size_t stride = std::max<std::size_t>(1, texels / 65536);
        glm::dvec4 raw(0.0), srgb(0.0);
        std::size_t count = 0;
        const auto decode = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
        for (std::size_t texel = 0; texel < texels; texel += stride, ++count)
            for (int channel = 0; channel < 4; ++channel)
            {
                const double value = static_cast<double>(std::to_integer<unsigned>(pixels[texel * 4 + channel])) / 255.0;
                raw[channel] += value;
                srgb[channel] += channel < 3 ? decode(value) : value;
            }
        const double scale = count ? 1.0 / static_cast<double>(count) : 0.0;
        return {glm::vec4(raw * scale), glm::vec4(srgb * scale)};
    }

    std::uint64_t TextureCompressionPolicy(const TextureCompressionSettings &settings, const SampledFormatQuery &supports)
    {
        std::string text = "policy 1\n" + std::to_string(settings.enabled) + "\n" + std::to_string(settings.minimumTexels) + "\n";
        for (const auto format : {rhi::Format::BC1RgbaUnorm, rhi::Format::BC1RgbaSrgb, rhi::Format::BC3RgbaUnorm,
                                  rhi::Format::BC3RgbaSrgb, rhi::Format::BC4RUnorm, rhi::Format::BC5RgUnorm,
                                  rhi::Format::BC7RgbaUnorm, rhi::Format::BC7RgbaSrgb})
        {
            const auto encoder = FindTextureBlockEncoder(format);
            if (!encoder || !supports || !supports(format))
                continue;
            text += std::to_string(static_cast<unsigned>(format)) + ' ' + std::string(encoder->Identity()) + '\n';
        }
        const auto digest = content::HashContent(std::as_bytes(std::span(text.data(), text.size())));
        std::uint64_t policy = 0;
        for (int i = 0; i < 8; ++i)
            policy = (policy << 8) | digest[static_cast<std::size_t>(i)];
        return policy;
    }

    const TextureSourceSampling *TextureSourceRecord::Find(TextureSemantic semantic, bool srgb, std::uint64_t policy) const
    {
        const auto found = std::find_if(samplings.begin(), samplings.end(), [&](const auto &sampling) {
            return sampling.semantic == semantic && sampling.srgb == srgb && sampling.policy == policy;
        });
        return found == samplings.end() ? nullptr : &*found;
    }

    TextureSourceIndex::TextureSourceIndex(std::filesystem::path directory, std::vector<Layer> readOnlyLayers)
        : m_directory(std::move(directory)), m_layers(std::move(readOnlyLayers))
    {
    }

    std::filesystem::path TextureSourceIndex::DirectoryBeside(const std::filesystem::path &storeDirectory)
    {
        return storeDirectory / kRecordDirectory;
    }

    std::shared_ptr<TextureSourceIndex> TextureSourceIndex::ForSettings(const TextureCompressionSettings &settings)
    {
        if (!settings.enabled)
            return nullptr;
        std::vector<Layer> layers;
        for (const auto &root : settings.cookedRoots)
            layers.push_back({DirectoryBeside(root / kCookedTextureDirectory), root});
        const auto directory = settings.cacheDirectory.empty() ? std::filesystem::path{} : DirectoryBeside(settings.cacheDirectory);
        // One index per configuration, shared by the texture manager and every renderer.
        static std::mutex registryMutex;
        static std::map<std::string, std::weak_ptr<TextureSourceIndex>> registry;
        std::string key = directory.generic_string();
        for (const auto &layer : layers)
            key += '\n' + layer.directory.generic_string();
        std::lock_guard lock(registryMutex);
        auto &slot = registry[key];
        if (auto existing = slot.lock())
            return existing;
        auto index = std::make_shared<TextureSourceIndex>(directory, std::move(layers));
        slot = index;
        return index;
    }

    std::optional<TextureFileVersion> TextureSourceIndex::CurrentVersion(const std::filesystem::path &file)
    {
        std::error_code error;
        const auto size = content::FileSize(file, error);
        if (error)
            return std::nullopt;
        TextureFileVersion version{static_cast<std::uint64_t>(size)};
        if (!content::IsMounted(file))
        {
            const auto modified = std::filesystem::last_write_time(file, error);
            if (error)
                return std::nullopt;
            version.modified = static_cast<std::int64_t>(modified.time_since_epoch().count());
        }
        return version;
    }

    std::optional<TextureSourceRecord> TextureSourceIndex::Find(const std::filesystem::path &file,
                                                                const TextureFileVersion &version) const
    {
        std::vector<std::pair<std::string, std::filesystem::path>> candidates;
        for (const auto &layer : m_layers)
            if (auto key = LayerKey(layer, file, version.size))
                candidates.emplace_back("layer\n" + layer.directory.generic_string() + "\n" + *key, RecordPath(layer.directory, *key));
        if (!m_directory.empty())
        {
            const auto key = WritableKey(file, version);
            candidates.emplace_back(key, RecordPath(m_directory, key));
        }
        for (const auto &[key, path] : candidates)
        {
            {
                std::lock_guard lock(m_mutex);
                if (const auto known = m_known.find(key); known != m_known.end())
                {
                    if (known->second)
                        return known->second;
                    continue;
                }
            }
            auto record = ReadRecord(path);
            std::lock_guard lock(m_mutex);
            m_known.insert_or_assign(key, record);
            if (record)
                return record;
        }
        return std::nullopt;
    }

    bool TextureSourceIndex::Record(const std::filesystem::path &file, const TextureFileVersion &version,
                                    std::uint32_t width, std::uint32_t height, int channels, const TextureMeans &means,
                                    const TextureSourceSampling &sampling) const
    {
        if (m_directory.empty())
            return false;
        const auto key = WritableKey(file, version);
        // One writer at a time: two samplings of a texture finishing together must not drop each other.
        std::lock_guard lock(m_mutex);
        TextureSourceRecord written;
        if (!WriteRecord(RecordPath(m_directory, key), width, height, channels, means, sampling, &written))
            return false;
        m_known.insert_or_assign(key, std::move(written));
        return true;
    }

    bool TextureSourceIndex::RecordInLayer(const Layer &layer, const std::filesystem::path &file, std::uint32_t width,
                                           std::uint32_t height, int channels, const TextureMeans &means,
                                           const TextureSourceSampling &sampling)
    {
        std::error_code error;
        const auto size = std::filesystem::file_size(file, error);
        if (error)
            return false;
        const auto key = LayerKey(layer, file, size);
        if (!key)
            return false;
        // Cooks write each file's samplings from one thread (TextureCook), so no lock is needed across calls.
        return WriteRecord(RecordPath(layer.directory, *key), width, height, channels, means, sampling, nullptr);
    }
}
