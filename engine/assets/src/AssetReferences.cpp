#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/AssetReference.h"

#include <algorithm>
#include <exception>
#include <array>
#include <charconv>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::size_t MaxRecordSize = 1024 * 1024;
        constexpr std::size_t MaxReferenceSize = 64 * 1024;

        std::filesystem::path FromUtf8(std::string_view value)
        {
            return std::filesystem::path(std::u8string(value.begin(), value.end()));
        }

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }

        std::string Extension(const std::filesystem::path &path)
        {
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return extension;
        }

        std::string Unescape(std::string_view value)
        {
            std::string result;
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                char c = value[i];
                if (c == '\\' && i + 1 < value.size())
                {
                    c = value[++i];
                    if (c == 't') c = '\t';
                    else if (c == 'n') c = '\n';
                    else if (c == 'r') c = '\r';
                }
                result.push_back(c);
            }
            return result;
        }

        void Add(AssetReferenceScan &scan, std::string_view value, std::size_t line, AssetReferenceRole role = AssetReferenceRole::Runtime)
        {
            auto reference = NormalizeAssetReference(value);
            if (!reference.empty()) scan.occurrences.push_back({std::move(reference), line, role});
            else if (value.starts_with("asset://")) scan.errors.push_back("Invalid logical reference at line " + std::to_string(line));
        }

        void SplitValues(AssetReferenceScan &scan, std::string_view value, char delimiter,
                         std::size_t line, bool escaped)
        {
            while (true)
            {
                const auto end = value.find(delimiter);
                const auto field = value.substr(0, end);
                if (escaped) Add(scan, Unescape(field), line);
                else Add(scan, field, line);
                if (end == std::string_view::npos) break;
                value.remove_prefix(end + 1);
            }
        }

        bool DecodeRelativeUri(std::string_view uri, std::string &decoded)
        {
            auto hex = [](unsigned char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            decoded.clear();
            for (std::size_t index = 0; index < uri.size(); ++index)
            {
                auto character = static_cast<unsigned char>(uri[index]);
                if (character == '%')
                {
                    if (index + 2 >= uri.size() || hex(uri[index + 1]) < 0 || hex(uri[index + 2]) < 0) return false;
                    character = static_cast<unsigned char>((hex(uri[index + 1]) << 4) | hex(uri[index + 2]));
                    index += 2;
                }
                if (character < 32 || character == 127) return false;
                decoded.push_back(static_cast<char>(character));
            }
            return true;
        }

        void QuotedValues(AssetReferenceScan &scan, std::string_view value, std::size_t line,
                          const std::filesystem::path &path = {}, const std::filesystem::path &root = {})
        {
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != '\"' && value[i] != '\'') continue;
                const auto start = i;
                const char quote = value[i++];
                std::string field;
                bool closed = false;
                for (; i < value.size(); ++i)
                {
                    if (value[i] == quote) { closed = true; break; }
                    if (value[i] == '\\' && i + 1 < value.size()) ++i;
                    field.push_back(value[i]);
                }
                if (!closed) continue;
                Add(scan, field, line);
                if (root.empty() || field.empty() || field.front() == '#' || field.find(':') != std::string::npos) continue;
                // Stylesheet links and glTF URIs resolve beside their document.
                // RmlUi image sources pass the document URL's directory to
                // JoinPath, which strips its final segment before appending src.
                auto before = value.substr(0, start);
                while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back()))) before.remove_suffix(1);
                if (before.empty() || (before.back() != '=' && before.back() != ':')) continue;
                before.remove_suffix(1);
                while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back()))) before.remove_suffix(1);
                if (before.empty()) continue;
                std::string_view key;
                if (before.back() == '\"' || before.back() == '\'')
                {
                    const auto delimiter = before.back();
                    before.remove_suffix(1);
                    const auto begin = before.find_last_of(delimiter);
                    if (begin != std::string_view::npos) key = before.substr(begin + 1);
                }
                else
                {
                    const auto begin = before.find_last_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_");
                    key = before.substr(begin == std::string_view::npos ? 0 : begin + 1);
                }
                if (key != "src" && key != "href" && key != "uri") continue;
                if (Extension(path) != ".gltf")
                    for (std::size_t amp = 0; (amp = field.find("&amp;", amp)) != std::string::npos; ++amp)
                        field.replace(amp, 5, "&");
                if (key == "uri" && Extension(path) == ".gltf")
                {
                    std::string decoded;
                    if (!DecodeRelativeUri(field, decoded))
                    { scan.errors.push_back("Invalid glTF URI at line " + std::to_string(line)); continue; }
                    field = std::move(decoded);
                }
                const auto base = key == "src" && path.extension() == ".rml"
                    ? path.parent_path().parent_path() : path.parent_path();
                try
                {
                    const auto resolved = (base / FromUtf8(field)).lexically_normal();
                    const auto reference = "project://" + Utf8(resolved.lexically_relative(root));
                    if (key == "uri" && Extension(path) == ".gltf" && NormalizeAssetReference(reference).empty())
                        scan.errors.push_back("glTF URI escapes the asset root at line " + std::to_string(line));
                    else Add(scan, reference, line);
                }
                catch (const std::exception &) { scan.errors.push_back("Invalid relative dependency path at line " + std::to_string(line)); }
            }
        }

        void ParseLine(AssetReferenceScan &scan, std::string_view line, std::size_t number,
                       const std::string &extension, const std::filesystem::path &path,
                       const std::filesystem::path &root)
        {
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty()) return;
            if (extension == ".plutoscene" || extension == ".plutoprefab" ||
                extension == ".plutoscriptable" || extension == ".plutomodel")
            {
                // Tabs, not spaces or punctuation, delimit native scene fields.
                // Human-facing entity names and tags are not dependencies.
                const auto record = line.substr(0, line.find('\t'));
                if (record == "ENTITY" || record == "TAGS" || record == "CLASS") return;
                if (record == "BASE") { QuotedValues(scan, line, number); return; }
                if (record == "OVERRIDE")
                {
                    std::istringstream fields{std::string(line)};
                    std::string token, path;
                    std::uint32_t id;
                    if (fields >> token >> id >> std::quoted(path))
                    {
                        if (path != "Name" && path != "Tags") QuotedValues(scan, line, number);
                    }
                    else scan.errors.push_back("Malformed variant override at line " + std::to_string(number));
                    return;
                }
                SplitValues(scan, line, '\t', number, extension != ".plutomodel");
                return;
            }
            if (extension == ".plutopostprocess")
            {
                QuotedValues(scan, line, number);
                return;
            }
            if (extension == ".cs" || extension == ".rml" || extension == ".rcss" || extension == ".gltf")
            {
                QuotedValues(scan, line, number, path, extension == ".cs" ? std::filesystem::path{} : root);
                return;
            }
            const auto equals = line.find('=');
            if (equals == std::string_view::npos) return;
            const auto key = line.substr(0, equals);
            const auto value = line.substr(equals + 1);
            if ((extension == ".plutomaterial" || extension == ".mat") &&
                (key == "AlbedoTexture" || key == "NormalTexture" || key == "MetallicTexture" || key == "RoughnessTexture" || key == "EmissionTexture"))
            {
                if (!value.empty() && value.find("://") == std::string_view::npos &&
                    !std::filesystem::path(value).is_absolute())
                    Add(scan, "project://" + std::string(value), number);
                else Add(scan, value, number);
            }
            else if ((extension == ".plutomaterial" || extension == ".mat") && key == "ShaderGraphTexture")
            {
                const auto delimiter = value.find('|');
                if (delimiter == std::string_view::npos || delimiter == 0 || value.find('|', delimiter + 1) != std::string_view::npos)
                    scan.errors.push_back("Malformed shader graph texture at line " + std::to_string(number));
                else Add(scan, value.substr(delimiter + 1), number);
            }
            else if (extension == ".plutoanimgraph" || extension == ".plutoshadergraph")
                SplitValues(scan, value, '|', number, false);
            else
                Add(scan, value, number);
        }

        bool IsMeshSourceTrailer(std::istream &input, std::uint32_t version, std::streamoff fileSize)
        {
            if (version != 4 && version != 5) return false;
            const auto position = input.tellg();
            if (position < 0 || fileSize < static_cast<std::streamoff>(position)) return false;
            const auto remaining = fileSize - static_cast<std::streamoff>(position);
            bool source = false;
            if (version == 4) source = remaining == 3;
            else if (remaining >= 19)
            {
                std::array<unsigned char, 8> bytes{};
                input.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
                std::uint64_t ownerSize = 0;
                for (std::size_t index = 0; index < bytes.size(); ++index) ownerSize |= std::uint64_t(bytes[index]) << (index * 8);
                source = input.good() && ownerSize <= MaxReferenceSize && ownerSize == static_cast<std::uint64_t>(remaining - 19);
                if (source) input.seekg(position + static_cast<std::streamoff>(8 + ownerSize + 8));
            }
            if (source)
            {
                std::array<unsigned char, 3> flags{};
                input.read(reinterpret_cast<char *>(flags.data()), flags.size());
                source = input.good() && std::all_of(flags.begin(), flags.end(), [](auto flag) { return flag <= 1; });
            }
            input.clear();
            input.seekg(position);
            return source;
        }

        void ScanBinaryStrings(std::istream &input, AssetReferenceScan &scan, std::stop_token stop, std::uint32_t meshVersion, std::streamoff fileSize)
        {
            // Native mesh/clip/animation files use uint64 length-prefixed UTF-8
            // strings. Recognize whole reference strings, not printable fragments
            // in vertex data, without loading the mesh or allocating its geometry.
            std::array<char, 18> history{};
            std::size_t cursor = 0;
            char c;
            while (input.get(c))
            {
                if ((cursor & 4095) == 0 && stop.stop_requested()) { scan.cancelled = true; return; }
                history[cursor++ % history.size()] = c;
                for (std::string_view prefix : {"project://", "engine://", "asset://"})
                {
                    if (cursor < prefix.size() + 8) continue;
                    bool matches = true;
                    for (std::size_t i = 0; i < prefix.size(); ++i)
                        matches = matches && history[(cursor - prefix.size() + i) % history.size()] == prefix[i];
                    if (!matches) continue;
                    std::uint64_t size = 0;
                    for (std::size_t i = 0; i < 8; ++i)
                        size |= static_cast<std::uint64_t>(static_cast<unsigned char>(history[(cursor - prefix.size() - 8 + i) % history.size()])) << (8 * i);
                    if (size < prefix.size() || size > MaxReferenceSize) continue;
                    std::string reference(prefix);
                    reference.resize(static_cast<std::size_t>(size));
                    if (!input.read(reference.data() + prefix.size(), static_cast<std::streamsize>(size - prefix.size())))
                    {
                        scan.errors.push_back("Truncated serialized reference string.");
                        return;
                    }
                    const auto role = IsMeshSourceTrailer(input, meshVersion, fileSize) ? AssetReferenceRole::ImportSource : AssetReferenceRole::Runtime;
                    Add(scan, reference, 0, role);
                    cursor = 0; // Direct reads consumed the rest of this string.
                    break;
                }
            }
        }
    }

    bool SupportsAssetReferenceScan(const std::filesystem::path &path)
    {
        const auto e = Extension(path);
        return e == ".plutoscene" || e == ".plutoprefab" || e == ".plutomaterial" || e == ".mat" ||
               e == ".plutomesh" || e == ".plutoanim" || e == ".plutoclip" || e == ".plutomodel" ||
               e == ".plutoanimgraph" || e == ".plutoshadergraph" || e == ".plutoparticles" ||
               e == ".plutoloading" || e == ".plutosurface" || e == ".plutopostprocess" || e == ".plutoscriptable" || e == ".plutoinput" ||
               e == ".cs" || e == ".rml" || e == ".rcss" || e == ".gltf";
    }

    std::string NormalizeAssetReference(std::string_view reference)
    {
        if (reference.starts_with("asset://"))
        {
            AssetReference identity;
            std::string normalized;
            if (reference.size() > MaxReferenceSize || !ParseAssetReference(reference, identity) ||
                !SerializeAssetReference(identity, normalized)) return {};
            return normalized;
        }
        const std::size_t prefix = reference.starts_with("project://") ? 10 : reference.starts_with("engine://") ? 9 : 0;
        if (!prefix || reference.size() <= prefix || reference.size() > MaxReferenceSize ||
            std::any_of(reference.begin(), reference.end(), [](unsigned char c) { return c < 32; })) return {};
        std::string relative(reference.substr(prefix));
        std::replace(relative.begin(), relative.end(), '\\', '/');
        const auto path = FromUtf8(relative).lexically_normal();
        const auto normalized = Utf8(path);
        if (path.is_absolute() || path.has_root_name() || normalized == "." || normalized == ".." || normalized.starts_with("../")) return {};
        return std::string(reference.substr(0, prefix)) + normalized;
    }

    AssetReferenceScan ScanAssetReferences(const std::filesystem::path &path, std::stop_token stop,
                                           const std::filesystem::path &assetRoot)
    {
        AssetReferenceScan result;
        if (stop.stop_requested()) { result.cancelled = true; return result; }
        if (!SupportsAssetReferenceScan(path)) return result;
        std::ifstream input(path, std::ios::binary);
        if (!input) { result.errors.push_back("Cannot open asset for reading."); return result; }
        const auto extension = Extension(path);
        const bool sceneRecord = extension == ".plutoscene" || extension == ".plutoprefab";
        const auto recordLimit = sceneRecord ? MaxSceneRecordSize : MaxRecordSize;
        const std::string recordLimitError = sceneRecord ? "Record exceeds 64 MiB at line " : "Record exceeds 1 MiB at line ";
        std::array<char, 4> magic{};
        input.read(magic.data(), magic.size());
        const bool binary = magic == std::array<char, 4>{'L','P','G','M'} ||
                            magic == std::array<char, 4>{'L','P','G','C'} || magic == std::array<char, 4>{'L','P','G','A'};
        input.clear();
        input.seekg(0);
        if (binary)
        {
            std::uint32_t meshVersion = 0;
            if (magic == std::array<char, 4>{'L','P','G','M'})
            {
                std::array<unsigned char, 4> version{};
                input.seekg(4);
                input.read(reinterpret_cast<char *>(version.data()), version.size());
                for (std::size_t index = 0; index < version.size(); ++index) meshVersion |= std::uint32_t(version[index]) << (index * 8);
            }
            input.clear();
            input.seekg(0, std::ios::end);
            const auto size = static_cast<std::streamoff>(input.tellg());
            input.seekg(0);
            ScanBinaryStrings(input, result, stop, meshVersion, size);
        }
        else
        {
            std::string line;
            std::size_t number = 1, bytes = 0;
            bool oversized = false;
            char c;
            while (input.get(c))
            {
                if ((++bytes & 4095) == 0 && stop.stop_requested()) { result.cancelled = true; return result; }
                if (c == '\n')
                {
                    if (!oversized) ParseLine(result, line, number, extension, path, assetRoot);
                    else result.errors.push_back(recordLimitError + std::to_string(number));
                    line.clear(); oversized = false; ++number;
                }
                else if (line.size() < recordLimit) line.push_back(c);
                else oversized = true;
            }
            if (oversized) result.errors.push_back(recordLimitError + std::to_string(number));
            else if (!line.empty()) ParseLine(result, line, number, extension, path, assetRoot);
        }
        if (input.bad()) result.errors.push_back("I/O error while reading asset.");
        result.cancelled = result.cancelled || stop.stop_requested();
        return result;
    }

    AssetReferenceQuery AssetReferenceIndex::Query(const std::filesystem::path &assetRoot, std::string_view target,
                                                  std::stop_token stop, bool forceRefresh)
    {
        AssetReferenceQuery result;
        if (m_root != assetRoot) { m_files.clear(); m_root = assetRoot; }
        const auto normalizedTarget = NormalizeAssetReference(target);
        if (normalizedTarget.empty()) { result.errors.push_back("Invalid target asset reference."); return result; }
        std::set<std::filesystem::path> seen;
        std::error_code ec;
        std::filesystem::recursive_directory_iterator iterator(assetRoot, ec), end;
        if (ec) { result.errors.push_back("Cannot enumerate assets: " + ec.message()); return result; }
        while (iterator != end)
        {
            if (stop.stop_requested()) { result.cancelled = true; return result; }
            const auto path = iterator->path();
            const bool regular = iterator->is_regular_file(ec);
            if (ec) result.errors.push_back(path.string() + ": " + ec.message());
            else if (regular && SupportsAssetReferenceScan(path))
            {
                const auto size = iterator->file_size(ec);
                auto modified = ec ? std::filesystem::file_time_type{} : iterator->last_write_time(ec);
                if (ec) result.errors.push_back(path.string() + ": " + ec.message());
                else
                {
                    seen.insert(path);
                    auto cached = m_files.find(path);
                    if (forceRefresh || cached == m_files.end() || cached->second.size != size || cached->second.modified != modified || !cached->second.scan.errors.empty())
                    {
                        auto scan = ScanAssetReferences(path, stop, assetRoot);
                        if (scan.cancelled) { result.cancelled = true; return result; }
                        const auto afterSize = std::filesystem::file_size(path, ec);
                        const auto afterTime = ec ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, ec);
                        if (ec || afterSize != size || afterTime != modified)
                            scan.errors.push_back("Asset changed during scan; retrying on the next refresh.");
                        cached = m_files.insert_or_assign(path, CachedFile{size, modified, std::move(scan)}).first;
                    }
                    ++result.scannedFiles;
                    const auto owner = "project://" + Utf8(path.lexically_relative(assetRoot));
                    AssetReferenceOwner match{owner};
                    for (const auto &occurrence : cached->second.scan.occurrences)
                        if (occurrence.reference == normalizedTarget)
                        {
                            if (match.occurrences++ == 0) match.firstLine = occurrence.line;
                        }
                    if (match.occurrences) result.owners.push_back(std::move(match));
                    for (const auto &error : cached->second.scan.errors) result.errors.push_back(owner + ": " + error);
                }
            }
            ec.clear();
            iterator.increment(ec);
            if (ec) { result.errors.push_back("Asset enumeration stopped: " + ec.message()); break; }
        }
        std::erase_if(m_files, [&](const auto &entry) { return !seen.contains(entry.first); });
        std::sort(result.owners.begin(), result.owners.end(), [](const auto &a, const auto &b) { return a.reference < b.reference; });
        return result;
    }
}
