#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/AssetReference.h"
#include "PlutoGE/assets/ManagedAssetFieldMetadata.h"
#include "PlutoGE/assets/SceneModelInstanceRecord.h"
#include <unordered_map>
#include "ShaderGraphReferenceFields.h"

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

        void Add(AssetReferenceScan &scan, std::string_view value, std::size_t line, AssetReferenceRole role = AssetReferenceRole::Runtime, ProjectAssetType expectedType = ProjectAssetType::Unknown)
        {
            auto reference = NormalizeAssetReference(value);
            if (!reference.empty()) scan.occurrences.push_back({std::move(reference), line, role, {}, expectedType});
            else if (value.starts_with("asset://")) scan.errors.push_back("Invalid logical reference at line " + std::to_string(line));
        }

        struct ModelSceneScanContext
        {
            bool linked = false, sawEntity = false, componentOpen = false;
            std::size_t bytes = 0;
            std::set<std::uint32_t> claimed;
            std::unordered_map<std::uint32_t, std::uint32_t> parents;
            std::set<std::uint32_t> meshComponents;
            std::set<std::string> inlineVersions;
            std::uint32_t componentEntity = 0;
            std::unordered_map<std::uint32_t, std::map<std::string, std::string>> bindingProperties;
            std::unordered_map<std::uint32_t, std::shared_ptr<const StaticModelInstanceState>> bindings;
            std::shared_ptr<const StaticModelInstanceState> component;
        };
        std::string_view Field(std::string_view &line)
        {
            const auto delimiter = line.find('\t');
            const auto field = line.substr(0, delimiter);
            line = delimiter == std::string_view::npos ? std::string_view{} : line.substr(delimiter + 1);
            return field;
        }
        bool OwnedBy(const std::string &reference, const StaticModelInstanceState &state)
        {
            AssetReference identity;
            return ParseAssetReference(reference, identity) && identity.assetId == state.accepted.layout.sourceAssetId && identity.localObjectId;
        }
        bool ModelSceneRecord(AssetReferenceScan &scan, ModelSceneScanContext &context,
            std::string_view line, std::size_t number)
        {
            auto fields = line;
            const auto record = Field(fields);
            if (record == "SCENE")
            {
                if (context.linked && number != 1) scan.errors.push_back("Duplicate linked scene header.");
                if (number == 1) context.linked = fields == "3";
            }
            if (record == "MODEL_INSTANCE")
            {
                auto state = std::make_shared<StaticModelInstanceState>(); std::string error;
                if (!context.linked || context.sawEntity || context.componentOpen || scan.modelInstances.size() >= 4096 ||
                    line.size() > MaxSceneRecordSize - context.bytes || !ParseSceneModelInstanceRecord(line, *state, &error))
                { scan.errors.push_back("Invalid or misplaced model instance at line " + std::to_string(number) + ": " + error); return true; }
                context.bytes += line.size();
                const auto claim = [&](std::uint32_t id) { return context.claimed.insert(id).second; };
                bool unique = claim(state->rootEntityId);
                for (const auto &node : state->nodeEntities) unique = claim(node.sceneEntityId) && unique;
                for (const auto id : state->bindingEntities) unique = claim(id) && unique;
                if (!unique) { scan.errors.push_back("Duplicate model entity ownership at line " + std::to_string(number)); return true; }
                for (const auto id : state->bindingEntities) context.bindings.emplace(id, state);
                const auto firstOccurrence = scan.occurrences.size();
                Add(scan, state->accepted.layout.meshReference, number, AssetReferenceRole::AcceptedGeneration);
                Add(scan, state->packageArtifact.reference, number, AssetReferenceRole::AcceptedGeneration);
                std::string source;
                if (SerializeAssetReference({state->accepted.layout.sourceAssetId, 0}, source))
                    Add(scan, source, number, AssetReferenceRole::ImportSource);
                const auto dependency = [&](const std::string &reference)
                { Add(scan, reference, number, OwnedBy(reference, *state) ? AssetReferenceRole::AcceptedGeneration : AssetReferenceRole::Runtime); };
                for (const auto &reference : state->defaultMaterials) dependency(reference);
                for (const auto &override : state->overrides.materials) dependency(override.reference);
                for (std::size_t index = firstOccurrence; index < scan.occurrences.size(); ++index)
                    if (scan.occurrences[index].role == AssetReferenceRole::AcceptedGeneration)
                        scan.occurrences[index].acceptedInstance = state;
                scan.modelInstances.push_back(std::move(state));
                return true;
            }
            if (!context.linked)
            {
                if (record == "PROPERTY")
                {
                    const auto name = Field(fields);
                    if ((name.starts_with("MaterialSlots.") || name.starts_with("SubmeshOverrides.")) && name.ends_with(".InlineMaterialVersion"))
                        scan.errors.push_back("Complete inline materials require scene format 3 at line " + std::to_string(number));
                }
                return false;
            }
            if (record == "ENTITY")
            {
                context.sawEntity = true;
                if (context.componentOpen) scan.errors.push_back("Unterminated linked component.");
                context.componentOpen = false; context.component.reset();
                const auto idField = Field(fields); const auto parentField = Field(fields);
                std::uint32_t id = 0, parent = 0;
                const auto parsedId = std::from_chars(idField.data(), idField.data() + idField.size(), id);
                const auto parsedParent = std::from_chars(parentField.data(), parentField.data() + parentField.size(), parent);
                if (!id || parsedId.ec != std::errc{} || parsedId.ptr != idField.data() + idField.size() ||
                    parsedParent.ec != std::errc{} || parsedParent.ptr != parentField.data() + parentField.size() ||
                    !context.parents.emplace(id, parent).second)
                    scan.errors.push_back("Invalid or duplicate linked entity at line " + std::to_string(number));
            }
            else if (record == "END_COMPONENT") { context.componentOpen = false; context.component.reset(); }
            else if (record == "COMPONENT")
            {
                if (context.componentOpen) scan.errors.push_back("Nested linked component.");
                context.componentOpen = true;
                context.component.reset();
                context.inlineVersions.clear();
                const auto idField = Field(fields); const auto type = Field(fields);
                std::uint32_t id = 0;
                const auto parsed = std::from_chars(idField.data(), idField.data() + idField.size(), id);
                if (parsed.ec != std::errc{} || parsed.ptr != idField.data() + idField.size())
                    scan.errors.push_back("Invalid linked component entity at line " + std::to_string(number));
                else if (!context.parents.contains(id)) scan.errors.push_back("Missing linked component owner.");
                else if (type == "MeshComponent")
                {
                    if (!context.meshComponents.insert(id).second && context.bindings.contains(id)) scan.errors.push_back("Duplicate linked mesh component.");
                    if (const auto found = context.bindings.find(id); found != context.bindings.end())
                    { context.component = found->second; context.componentEntity = id; }
                }
            }
            if (record == "PROPERTY")
            {
                const auto name = Field(fields); Field(fields); const auto value = Field(fields);
                if ((name.starts_with("MaterialSlots.") || name.starts_with("SubmeshOverrides.")) && name.ends_with(".InlineMaterialVersion"))
                    if (!context.componentOpen || value != "2" || !context.inlineVersions.insert(std::string(name)).second)
                        scan.errors.push_back("Unsupported, duplicate or misplaced inline material version at line " + std::to_string(number));
                if (context.component && (name == "MeshAssetReference" || name == "SourceMeshPath" || name == "SubmeshIndex" || name == "SubmeshCount"))
                    if (!context.bindingProperties[context.componentEntity].emplace(std::string(name), Unescape(value)).second)
                        scan.errors.push_back("Duplicate generated binding property.");
            }
            return false;
        }

        struct ManagedSceneScanContext
        {
            struct Property { std::string name, value; std::size_t line = 0; bool isString = false; };
            bool active = false, invalid = false;
            unsigned sceneVersion = 0;
            std::size_t bytes = 0;
            std::vector<Property> properties;
        };
        void FinishManagedComponent(AssetReferenceScan &scan, ManagedSceneScanContext &context,
            const std::filesystem::path &root)
        {
            if (!context.active || context.invalid) return;
            std::vector<ManagedAssetFieldRecord> records;
            records.reserve(context.properties.size());
            for (const auto &property : context.properties)
                records.push_back({property.name, property.value, property.isString});
            ManagedAssetFieldMetadata metadata;
            std::string error;
            if (!ReadManagedAssetFieldMetadata(records, metadata, &error))
            { scan.errors.push_back(error); return; }
            if (metadata.declared && context.sceneVersion != 3)
            { scan.errors.push_back("Typed managed asset fields require scene format 3."); return; }
            for (const auto &property : context.properties)
            {
                if (!metadata.declared) { Add(scan, property.value, property.line); continue; }
                const auto kind = metadata.fields.find(property.name);
                if (kind == metadata.fields.end() || property.value.empty()) continue;
                const auto expected = ManagedAssetFieldAssetType(kind->second);
                auto reference = property.value;
                if (reference.find("://") == std::string::npos)
                {
                    const auto path = FromUtf8(reference);
                    if (path.is_absolute())
                    {
                        if (root.empty()) { scan.errors.push_back("Typed managed field has an absolute path without an asset root."); continue; }
                        reference = "project://" + Utf8(path.lexically_normal().lexically_relative(root.lexically_normal()));
                    }
                    else reference = "project://" + reference;
                }
                if (NormalizeAssetReference(reference).empty())
                { scan.errors.push_back("Invalid typed managed asset reference at line " + std::to_string(property.line)); continue; }
                Add(scan, reference, property.line, AssetReferenceRole::Runtime, expected);
            }
        }
        bool ManagedSceneRecord(AssetReferenceScan &scan, ManagedSceneScanContext &context,
            std::string_view line, std::size_t number, const std::filesystem::path &root)
        {
            auto fields = line;
            const auto record = Field(fields);
            if (record == "SCENE") context.sceneVersion = fields == "3" ? 3 : fields == "2" ? 2 : fields == "1" ? 1 : 0;
            if (record == "ENTITY" || record == "COMPONENT")
            {
                if (context.active) scan.errors.push_back("Unterminated managed script component.");
                context.active = false;
                context.invalid = false;
                context.bytes = 0;
                context.properties.clear();
                if (record == "COMPONENT") { Field(fields); context.active = Field(fields) == "ScriptComponent"; }
            }
            if (record == "END_COMPONENT" && context.active)
            {
                FinishManagedComponent(scan, context, root);
                context.active = false;
                context.properties.clear();
                return true;
            }
            if (record != "PROPERTY" || !context.active) return false;
            if (context.invalid) return true;
            if (line.size() > MaxSceneRecordSize - context.bytes || context.properties.size() >= kMaxManagedAssetFields * 2 + 2)
            {
                scan.errors.push_back("Managed script component exceeds its field metadata limits.");
                context.invalid = true;
                context.properties.clear();
                return true;
            }
            context.bytes += line.size();
            const auto name = Field(fields), type = Field(fields), value = Field(fields);
            context.properties.push_back({Unescape(name), Unescape(value), number, type == "2"});
            return true;
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
                       const std::filesystem::path &root, ModelSceneScanContext *models = nullptr, ManagedSceneScanContext *managed = nullptr)
        {
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty()) return;
            if (models && (extension == ".plutoscene" || extension == ".plutoprefab") && ModelSceneRecord(scan, *models, line, number)) return;
            if (managed && (extension == ".plutoscene" || extension == ".plutoprefab") && ManagedSceneRecord(scan, *managed, line, number, root)) return;
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
                const auto firstOccurrence = scan.occurrences.size();
                SplitValues(scan, line, '\t', number, extension != ".plutomodel");
                if (models && models->component && record == "PROPERTY")
                    for (std::size_t index = firstOccurrence; index < scan.occurrences.size(); ++index)
                        if (OwnedBy(scan.occurrences[index].reference, *models->component))
                        {
                            scan.occurrences[index].role = AssetReferenceRole::AcceptedGeneration;
                            scan.occurrences[index].acceptedInstance = models->component;
                        }
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
            if (extension == ".plutoshadergraph")
            {
                std::size_t offset = 0, size = 0;
                const auto status = detail::ShaderGraphReferenceField(line, offset, size);
                if (status == detail::ReferenceFieldStatus::Malformed)
                    scan.errors.push_back("Malformed shader graph dependency field at line " + std::to_string(number));
                else if (status == detail::ReferenceFieldStatus::Reference)
                    Add(scan, line.substr(offset, size), number);
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
            else if (extension == ".plutoanimgraph")
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
            ModelSceneScanContext models;
            ManagedSceneScanContext managed;
            char c;
            while (input.get(c))
            {
                if ((++bytes & 4095) == 0 && stop.stop_requested()) { result.cancelled = true; return result; }
                if (c == '\n')
                {
                    if (!oversized) ParseLine(result, line, number, extension, path, assetRoot, &models, &managed);
                    else result.errors.push_back(recordLimitError + std::to_string(number));
                    line.clear(); oversized = false; ++number;
                }
                else if (line.size() < recordLimit) line.push_back(c);
                else oversized = true;
            }
            if (oversized) result.errors.push_back(recordLimitError + std::to_string(number));
            else if (!line.empty()) ParseLine(result, line, number, extension, path, assetRoot, &models, &managed);
            if (managed.active) result.errors.push_back("Unterminated managed script component.");
            if (models.linked)
            {
                if (models.componentOpen) result.errors.push_back("Unterminated linked component.");
                for (const auto id : models.claimed)
                    if (!models.parents.contains(id)) result.errors.push_back("Missing generated model entity.");
                for (const auto &[id, state] : models.bindings)
                    if (!models.meshComponents.contains(id)) result.errors.push_back("Missing generated model mesh component.");
                for (const auto &state : result.modelInstances)
                    for (std::size_t index = 0; index < state->bindingEntities.size(); ++index)
                    {
                        const auto &properties = models.bindingProperties[state->bindingEntities[index]];
                        const auto mesh = properties.find("MeshAssetReference");
                        const auto legacy = properties.find("SourceMeshPath");
                        const auto submesh = properties.find("SubmeshIndex");
                        const auto count = properties.find("SubmeshCount");
                        std::uint64_t address = 0;
                        bool valid = submesh != properties.end();
                        if (valid)
                        {
                            const auto &value = submesh->second;
                            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), address);
                            valid = parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
                        }
                        if (!valid || address != state->accepted.layout.bindings[index].submeshIndex ||
                            count == properties.end() || count->second != "1" ||
                            (mesh == properties.end() && legacy == properties.end()) ||
                            (mesh != properties.end() && mesh->second != state->accepted.layout.meshReference) ||
                            (legacy != properties.end() && legacy->second != state->accepted.layout.meshReference))
                            result.errors.push_back("Generated mesh binding differs from its accepted layout.");
                    }
                std::set<std::uint32_t> checked;
                for (const auto &[id, parent] : models.parents)
                {
                    auto ancestor = id;
                    std::set<std::uint32_t> visited;
                    while (ancestor && !checked.contains(ancestor))
                    {
                        const auto found = models.parents.find(ancestor);
                        if (found == models.parents.end() || !visited.insert(ancestor).second)
                        { result.errors.push_back("Missing or cyclic linked entity parent."); break; }
                        ancestor = found->second;
                    }
                    checked.insert(visited.begin(), visited.end());
                }
            }
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
                        // Where-used queries need reference locations, not full
                        // accepted layouts. Direct cooker scans retain their scope.
                        for (auto &occurrence : scan.occurrences) occurrence.acceptedInstance.reset();
                        scan.modelInstances.clear();
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
