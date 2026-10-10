#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ArtifactGenerationLock.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kMagic = "PLUTOHIERARCHY";
        constexpr std::size_t kMaxBytes = 32 * 1024 * 1024;
        constexpr std::size_t kMaxString = 1024 * 1024;
        constexpr std::size_t kMaxCount = 100000;
        constexpr std::string_view kRecord = "MODEL_HIERARCHY";
        bool Fail(std::string *error, const std::string &message) { if (error) *error = message; return false; }
        bool Known(std::string_view line) { return line.substr(0, line.find('\t')) == kRecord; }
        bool Relative(std::string_view reference, std::filesystem::path &relative)
        {
            if (reference.size() > kMaxString || !Project::IsProjectAssetReference(reference) || reference.find_first_of("\t\r\n\0", 0, 4) != std::string_view::npos) return false;
            try { relative = std::filesystem::u8path(reference.substr(Project::kProjectAssetScheme.size())); }
            catch (const std::exception &) { return false; }
            if (relative.empty() || relative.has_root_path() || relative.extension() != ".plutomodelhierarchy") return false;
            for (const auto &part : relative) if (part == ".." || part == ".") return false;
            return true;
        }
        bool Finite(const glm::mat4 &matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row) if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }
        void Validate(const ModelHierarchyAsset &asset)
        {
            std::string identity;
            if (asset.sourceAssetId.empty() || !SerializeAssetReference({asset.sourceAssetId, 0}, identity))
                throw std::runtime_error("Invalid hierarchy source owner.");
            AssetReference mesh;
            if (!asset.meshReference.empty())
            {
                if (!ParseAssetReference(asset.meshReference, mesh) || mesh.assetId != asset.sourceAssetId || !mesh.localObjectId)
                    throw std::runtime_error("Hierarchy mesh must be a logical object owned by its source.");
            }
            const auto &hierarchy = asset.hierarchy;
            if (hierarchy.nodes.size() > kMaxCount || hierarchy.bindings.size() > kMaxCount ||
                hierarchy.sceneRoots.size() > hierarchy.nodes.size() || asset.identities.size() != hierarchy.nodes.size())
                throw std::runtime_error("Invalid hierarchy inventory size.");
            assetimport::ImportedModelHierarchy validated;
            std::string error;
            if (!assetimport::BuildImportedModelHierarchy(hierarchy.nodes, validated, &error)) throw std::runtime_error(error);
            std::set<int> roots;
            for (const auto root : hierarchy.sceneRoots)
                if (root < 0 || root >= static_cast<int>(hierarchy.nodes.size()) || !roots.insert(root).second)
                    throw std::runtime_error("Invalid or duplicate selected hierarchy root.");
            std::set<std::uint64_t> ids;
            std::set<std::string> keys;
            for (const auto &node : asset.identities)
            {
                if (static_cast<unsigned>(node.status) > static_cast<unsigned>(ModelNodeIdentityStatus::UnresolvedAncestor) ||
                    ((node.status == ModelNodeIdentityStatus::Matched) != (node.localId != 0)) ||
                    ((node.localId != 0) != !node.sourceKey.empty()) ||
                    (node.localId && (!node.sourceKey.starts_with("node/") || node.localId == mesh.localObjectId ||
                        !ids.insert(node.localId).second || !keys.insert(node.sourceKey).second)))
                    throw std::runtime_error("Invalid hierarchy node identity.");
            }
            for (const auto &binding : hierarchy.bindings)
                if (binding.nodeIndex < 0 || binding.nodeIndex >= static_cast<int>(hierarchy.nodes.size()) ||
                    binding.transformNodeIndex < -1 || !Finite(binding.bakedTransform))
                    throw std::runtime_error("Invalid hierarchy mesh binding.");
        }
        struct Writer
        {
            std::string bytes;
            void Add(std::string_view value)
            {
                if (value.size() > kMaxBytes - bytes.size()) throw std::runtime_error("Hierarchy artifact exceeds its size limit.");
                bytes.append(value);
            }
            void Number(std::uint64_t value, unsigned width)
            {
                for (unsigned byte = 0; byte < width; ++byte) { const char part = static_cast<char>(value >> (byte * 8)); Add({&part, 1}); }
            }
            void Text(std::string_view value)
            {
                if (value.size() > kMaxString) throw std::runtime_error("Hierarchy string exceeds its size limit.");
                Number(value.size(), 4); Add(value);
            }
            void Matrix(const glm::mat4 &value)
            {
                static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row) Number(std::bit_cast<std::uint32_t>(value[column][row]), 4);
            }
        };
        struct Reader
        {
            std::string_view bytes;
            std::uint64_t Number(unsigned width)
            {
                if (bytes.size() < width) throw std::runtime_error("Truncated hierarchy artifact.");
                std::uint64_t value = 0;
                for (unsigned byte = 0; byte < width; ++byte) value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[byte])) << (byte * 8);
                bytes.remove_prefix(width); return value;
            }
            std::string Text()
            {
                const auto size = Number(4);
                if (size > kMaxString || size > bytes.size()) throw std::runtime_error("Invalid hierarchy string size.");
                std::string text(bytes.substr(0, size)); bytes.remove_prefix(size); return text;
            }
            std::size_t Count(std::size_t minimum)
            {
                const auto count = Number(4);
                if (count > kMaxCount || count > bytes.size() / minimum) throw std::runtime_error("Invalid hierarchy inventory count.");
                return static_cast<std::size_t>(count);
            }
            int Index() { return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(Number(4))); }
            glm::mat4 Matrix()
            {
                glm::mat4 value;
                for (int column = 0; column < 4; ++column)
                    for (int row = 0; row < 4; ++row) value[column][row] = std::bit_cast<float>(static_cast<std::uint32_t>(Number(4)));
                return value;
            }
        };
    }

    bool PrepareStaticModelInstanceLayout(const ModelHierarchyAsset &asset,
        std::size_t submeshCount, StaticModelInstanceLayout &layout, std::string *errorMessage, StaticModelIdentityPolicy identityPolicy)
    {
        try
        {
            Validate(asset);
            if (identityPolicy != StaticModelIdentityPolicy::RequireResolved &&
                identityPolicy != StaticModelIdentityPolicy::IndependentSnapshot)
                throw std::runtime_error("Unknown static hierarchy identity policy.");
            std::vector<assetimport::StaticModelBindingTransform> prepared;
            if (!assetimport::PrepareStaticModelBindingTransforms(asset.hierarchy, prepared, errorMessage)) return false;
            if (!prepared.empty() && asset.meshReference.empty())
                throw std::runtime_error("Static hierarchy bindings require a source-owned mesh reference.");
            assetimport::ImportedModelHierarchy rebuilt;
            if (!assetimport::BuildImportedModelHierarchy(asset.hierarchy.nodes, rebuilt, errorMessage)) return false;
            std::vector<std::vector<int>> children(rebuilt.nodes.size());
            for (int index = 0; index < static_cast<int>(rebuilt.nodes.size()); ++index)
                if (rebuilt.nodes[index].parentNodeIndex >= 0)
                    children[rebuilt.nodes[index].parentNodeIndex].push_back(index);
            StaticModelInstanceLayout candidate;
            candidate.sourceAssetId = asset.sourceAssetId;
            candidate.meshReference = asset.meshReference;
            std::vector<int> indices(rebuilt.nodes.size(), -1);
            // Iterative preorder is deterministic and bounded even for deep trees.
            std::vector<std::pair<int, int>> pending;
            for (auto root = asset.hierarchy.sceneRoots.rbegin(); root != asset.hierarchy.sceneRoots.rend(); ++root)
                pending.emplace_back(*root, -1);
            while (!pending.empty())
            {
                const auto [sourceIndex, parentIndex] = pending.back();
                pending.pop_back();
                const auto &identity = asset.identities[sourceIndex];
                if (identityPolicy == StaticModelIdentityPolicy::RequireResolved &&
                    (identity.status != ModelNodeIdentityStatus::Matched || identity.localId == 0))
                    throw std::runtime_error("Selected model node requires explicit identity repair before linked instantiation.");
                const auto &source = rebuilt.nodes[sourceIndex];
                const int index = static_cast<int>(candidate.nodes.size());
                indices[sourceIndex] = index;
                // A selected root can have an unselected ancestor. Fold that
                // ancestor's world into the root rather than losing placement.
                candidate.nodes.push_back({identity.localId, source.name, parentIndex,
                    parentIndex < 0 ? source.worldTransform : source.localTransform});
                for (auto child = children[sourceIndex].rbegin(); child != children[sourceIndex].rend(); ++child)
                    pending.emplace_back(*child, index);
            }
            for (const auto &binding : prepared)
            {
                if (binding.submeshIndex >= submeshCount)
                    throw std::runtime_error("Static hierarchy binding exceeds the loaded mesh submesh inventory.");
                candidate.bindings.push_back({indices[binding.nodeIndex], binding.submeshIndex, binding.geometryToNode});
            }
            std::string bytes;
            if (!SerializeModelHierarchyAsset(asset, bytes, errorMessage)) return false;
            candidate.hierarchyDigest = content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
            layout = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            return Fail(errorMessage, error.what());
        }
    }

    bool SerializeModelHierarchyAsset(const ModelHierarchyAsset &asset, std::string &bytes, std::string *errorMessage)
    {
        try
        {
            Validate(asset);
            Writer writer;
            writer.Add(kMagic); writer.Number(1, 4);
            writer.Text(asset.sourceAssetId); writer.Text(asset.meshReference);
            writer.Number(asset.hierarchy.nodes.size(), 4);
            for (std::size_t index = 0; index < asset.hierarchy.nodes.size(); ++index)
            {
                const auto &node = asset.hierarchy.nodes[index]; const auto &identity = asset.identities[index];
                writer.Text(node.name); writer.Number(static_cast<std::uint32_t>(node.parentNodeIndex), 4); writer.Matrix(node.localTransform);
                writer.Number(identity.localId, 8); writer.Number(static_cast<unsigned>(identity.status), 1); writer.Text(identity.sourceKey);
            }
            writer.Number(asset.hierarchy.sceneRoots.size(), 4);
            for (const auto root : asset.hierarchy.sceneRoots) writer.Number(static_cast<std::uint32_t>(root), 4);
            writer.Number(asset.hierarchy.bindings.size(), 4);
            for (const auto &binding : asset.hierarchy.bindings)
            {
                writer.Number(static_cast<std::uint32_t>(binding.nodeIndex), 4); writer.Number(binding.submeshIndex, 4);
                writer.Matrix(binding.bakedTransform); writer.Number(static_cast<std::uint32_t>(binding.transformNodeIndex), 4);
                writer.Number(binding.skinned ? 1 : 0, 1);
            }
            bytes = std::move(writer.bytes); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error) { return Fail(errorMessage, error.what()); }
    }

    bool ParseModelHierarchyAsset(std::string_view bytes, ModelHierarchyAsset &asset, std::string *errorMessage)
    {
        try
        {
            if (bytes.size() > kMaxBytes || !bytes.starts_with(kMagic)) throw std::runtime_error("Invalid hierarchy artifact header or size.");
            Reader reader{bytes.substr(kMagic.size())};
            if (reader.Number(4) != 1) throw std::runtime_error("Unsupported hierarchy artifact version.");
            ModelHierarchyAsset parsed;
            parsed.sourceAssetId = reader.Text(); parsed.meshReference = reader.Text();
            const auto count = reader.Count(85);
            parsed.hierarchy.nodes.resize(count); parsed.identities.resize(count);
            for (std::size_t index = 0; index < count; ++index)
            {
                auto &node = parsed.hierarchy.nodes[index]; auto &identity = parsed.identities[index];
                node.name = reader.Text(); node.parentNodeIndex = reader.Index(); node.localTransform = reader.Matrix();
                identity.localId = reader.Number(8); identity.status = static_cast<ModelNodeIdentityStatus>(reader.Number(1)); identity.sourceKey = reader.Text();
            }
            const auto rootCount = reader.Count(4);
            for (std::size_t index = 0; index < rootCount; ++index) parsed.hierarchy.sceneRoots.push_back(reader.Index());
            const auto bindingCount = reader.Count(77);
            for (std::size_t index = 0; index < bindingCount; ++index)
            {
                assetimport::ImportedModelBinding binding;
                binding.nodeIndex = reader.Index(); binding.submeshIndex = static_cast<std::uint32_t>(reader.Number(4));
                binding.bakedTransform = reader.Matrix(); binding.transformNodeIndex = reader.Index();
                const auto skinned = reader.Number(1);
                if (skinned > 1) throw std::runtime_error("Invalid hierarchy skinning flag.");
                binding.skinned = skinned != 0; parsed.hierarchy.bindings.push_back(binding);
            }
            if (!reader.bytes.empty()) throw std::runtime_error("Unexpected trailing hierarchy data.");
            Validate(parsed);
            assetimport::ImportedModelHierarchy rebuilt;
            if (!assetimport::BuildImportedModelHierarchy(parsed.hierarchy.nodes, rebuilt, errorMessage)) return false;
            parsed.hierarchy.nodes = std::move(rebuilt.nodes);
            asset = std::move(parsed); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error) { return Fail(errorMessage, error.what()); }
    }

    bool SaveModelHierarchyAsset(const std::filesystem::path &path, const ModelHierarchyAsset &asset, std::string *errorMessage)
    {
        std::string bytes;
        if (!SerializeModelHierarchyAsset(asset, bytes, errorMessage)) return false;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); output.close();
        return output ? true : Fail(errorMessage, "Cannot finish hierarchy artifact.");
    }

    ModelHierarchyArtifactStatus ReadModelHierarchyArtifact(const ModelAsset &package, ModelHierarchyArtifact &artifact, std::string *errorMessage)
    {
        ModelHierarchyArtifact candidate;
        bool found = false;
        for (const auto &line : package.extensionRecords)
        {
            if (!Known(line)) continue;
            auto invalid = [&] { Fail(errorMessage, "Invalid or duplicate model hierarchy artifact descriptor."); return ModelHierarchyArtifactStatus::Invalid; };
            if (found) return invalid();
            found = true;
            const auto versionEnd = line.find('\t', kRecord.size() + 1);
            if (!line.starts_with(std::string(kRecord) + "\t") || versionEnd == std::string::npos) return invalid();
            if (line.substr(kRecord.size() + 1, versionEnd - kRecord.size() - 1) != "1")
            { Fail(errorMessage, "Unsupported model hierarchy artifact descriptor version."); return ModelHierarchyArtifactStatus::UnsupportedVersion; }
            const auto referenceEnd = line.find('\t', versionEnd + 1);
            if (referenceEnd == std::string::npos) return invalid();
            candidate.reference = line.substr(versionEnd + 1, referenceEnd - versionEnd - 1);
            std::filesystem::path relative;
            if (!Relative(candidate.reference, relative) || !content::ParseContentDigest(line.substr(referenceEnd + 1), candidate.digest)) return invalid();
        }
        if (errorMessage) errorMessage->clear();
        if (!found) return ModelHierarchyArtifactStatus::Missing;
        artifact = std::move(candidate); return ModelHierarchyArtifactStatus::Success;
    }

    bool WriteModelHierarchyArtifact(ModelAsset &package, const ModelHierarchyArtifact &artifact, std::string *errorMessage)
    {
        ModelHierarchyArtifact previous;
        const auto status = ReadModelHierarchyArtifact(package, previous, errorMessage);
        if (status != ModelHierarchyArtifactStatus::Success && status != ModelHierarchyArtifactStatus::Missing) return false;
        std::filesystem::path relative;
        if (!Relative(artifact.reference, relative)) return Fail(errorMessage, "Invalid model hierarchy artifact location.");
        auto updated = package;
        std::erase_if(updated.extensionRecords, Known);
        updated.extensionRecords.push_back(std::string(kRecord) + "\t1\t" + artifact.reference + "\t" + content::DigestToHex(artifact.digest));
        package = std::move(updated); return true;
    }

    bool LoadModelHierarchyAsset(const Project &project, std::string_view sourceReference, ModelHierarchyAsset &asset, std::string *errorMessage)
    {
        try
        {
            AssetMetadata metadata;
            if (LoadAssetMetadata(GetAssetMetadataPath(project.ResolveAssetReference(sourceReference)), metadata, errorMessage) != AssetMetadataStatus::Success) return false;
            ModelAsset package;
            ModelHierarchyArtifact descriptor;
            content::ContentDigest generation;
            const auto packageStatus = ReadModelSourcePackage(metadata, package, errorMessage);
            if (packageStatus == ModelSourcePackageStatus::Missing) return Fail(errorMessage, "Model has no persistent source package; reimport it before reading its hierarchy.");
            if (packageStatus != ModelSourcePackageStatus::Success) return false;
            const auto descriptorStatus = ReadModelHierarchyArtifact(package, descriptor, errorMessage);
            if (descriptorStatus == ModelHierarchyArtifactStatus::Missing) return Fail(errorMessage, "Model has no generated hierarchy artifact; reimport it before reading its hierarchy.");
            if (descriptorStatus != ModelHierarchyArtifactStatus::Success) return false;
            const auto generationStatus = ReadModelArtifactGeneration(metadata, generation, errorMessage);
            if (generationStatus == ModelArtifactGenerationStatus::Missing) return Fail(errorMessage, "Model has no active Library generation.");
            if (generationStatus != ModelArtifactGenerationStatus::Success) return false;
            ArtifactGenerationLock lease;
            if (!lease.TryAcquire(project.GetRootDirectory(), generation, ArtifactGenerationLockMode::SharedReader, errorMessage)) return false;
            const auto library = project.GetRootDirectory() / "Library";
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(library))) throw std::runtime_error("Hierarchy Library root cannot be a symbolic link.");
            const auto canonicalLibrary = std::filesystem::canonical(library);
            std::filesystem::path relative;
            if (!Relative(descriptor.reference, relative)) throw std::runtime_error("Invalid hierarchy artifact location.");
            const auto path = library / "Artifacts" / content::DigestToHex(generation) / "Files" / relative;
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) ||
                !content::IsPathWithinDirectory(std::filesystem::canonical(path), canonicalLibrary))
                throw std::runtime_error("Hierarchy artifact is outside Library or unavailable.");
            const auto size = std::filesystem::file_size(path);
            if (size > kMaxBytes) throw std::runtime_error("Hierarchy artifact exceeds its size limit.");
            std::string bytes(static_cast<std::size_t>(size), '\0');
            std::ifstream input(path, std::ios::binary);
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!input || input.peek() != std::char_traits<char>::eof() ||
                content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size()))) != descriptor.digest)
                throw std::runtime_error("Hierarchy artifact digest mismatch or incomplete read.");
            ModelHierarchyAsset parsed;
            if (!ParseModelHierarchyAsset(bytes, parsed, errorMessage)) return false;
            if (parsed.sourceAssetId != metadata.id || package.sourceAssetId != metadata.id)
                throw std::runtime_error("Hierarchy source owner conflicts with metadata.");
            ModelImportSettings settings;
            if (ReadModelImportSettings(metadata, settings, errorMessage) != ModelImportSettingsStatus::Success) return false;
            for (const auto &identity : parsed.identities)
                if (identity.localId && std::none_of(settings.objects.begin(), settings.objects.end(), [&](const auto &object)
                    { return !object.retired && object.localId == identity.localId && object.sourceKey == identity.sourceKey; }))
                    throw std::runtime_error("Hierarchy node identity conflicts with source correspondence.");
            if (!parsed.meshReference.empty())
            {
                AssetReference mesh;
                if (!ParseAssetReference(parsed.meshReference, mesh) || std::none_of(package.objects.begin(), package.objects.end(), [&](const auto &object)
                    { return object.localId == mesh.localObjectId && object.type == ProjectAssetType::Mesh; }))
                    throw std::runtime_error("Hierarchy mesh identity conflicts with source package.");
            }
            asset = std::move(parsed); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error) { return Fail(errorMessage, error.what()); }
    }
}
