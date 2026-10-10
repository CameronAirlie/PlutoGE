#include "PlutoGE/assets/ModelGenerationSnapshot.h"
#include "PlutoGE/assets/ArtifactGenerationLock.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include "PlutoGE/platform/ContentPack.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::assets
{
    namespace
    {
        void Ordinary(const std::filesystem::path &path, bool directory)
        {
            const auto status = std::filesystem::symlink_status(path);
            if (std::filesystem::is_symlink(status) ||
                (directory ? !std::filesystem::is_directory(status) : !std::filesystem::is_regular_file(status)))
                throw std::runtime_error("Retained generation contains a non-ordinary path.");
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("Retained generation contains a reparse point.");
#endif
        }
        std::filesystem::path Relative(const std::string &reference)
        {
            if (!Project::IsProjectAssetReference(reference) || NormalizeAssetReference(reference) != reference)
                throw std::runtime_error("Retained generation requires normalized project locations.");
            const auto relative = std::filesystem::path(reference.substr(Project::kProjectAssetScheme.size()));
            if (relative.empty() || relative.has_root_path() ||
                std::any_of(relative.begin(), relative.end(), [](const auto &part) { return part == ".." || part == "." || part.empty(); }))
                throw std::runtime_error("Invalid retained generation location.");
            return relative;
        }
        bool HashGenerationFile(const std::filesystem::path &path, content::ContentDigest &digest, std::string *error)
        {
            if (!content::IsMounted(path)) return content::HashFileContent(path, digest, error);
            content::InputFile input(path, std::ios::binary);
            if (!input.is_open()) { if (error) *error = "Packed generation file is unavailable."; return false; }
            content::ContentHasher hasher;
            std::array<char, 64 * 1024> bytes;
            while (input)
            {
                input.read(bytes.data(), bytes.size());
                hasher.Update(std::as_bytes(std::span(bytes.data(), static_cast<std::size_t>(input.gcount()))));
            }
            if (!input.eof()) { if (error) *error = "Packed generation file could not be read."; return false; }
            digest = hasher.Finalize();
            if (error) error->clear();
            return true;
        }
        std::filesystem::path File(const std::filesystem::path &directory, const std::string &reference)
        {
            const auto relative = Relative(reference);
            if (content::IsMounted(directory))
            {
                const auto path = directory / "Files" / relative;
                std::error_code error;
                if (!content::IsRegularFile(path, error) || error)
                    throw std::runtime_error("Packed generation file is unavailable.");
                return path;
            }
            auto parent = directory / "Files";
            Ordinary(parent, true);
            for (const auto &part : relative.parent_path()) { parent /= part; Ordinary(parent, true); }
            const auto path = parent / relative.filename();
            Ordinary(path, false);
            if (!content::IsPathWithinDirectory(std::filesystem::canonical(path), directory))
                throw std::runtime_error("Retained generation file escapes its boundary.");
            return path;
        }
        std::string Read(const std::filesystem::path &directory, const ModelGeneratedFile &descriptor, std::size_t limit)
        {
            const auto path = File(directory, descriptor.reference);
            std::error_code error;
            const auto size = content::FileSize(path, error);
            if (error) throw std::runtime_error("Retained generation descriptor is unavailable.");
            if (size > limit) throw std::runtime_error("Retained generation descriptor exceeds its byte limit.");
            std::string bytes(static_cast<std::size_t>(size), '\0');
            content::InputFile input(path, std::ios::binary);
            if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) ||
                content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size()))) != descriptor.digest)
                throw std::runtime_error("Retained generation descriptor differs from accepted bytes.");
            return bytes;
        }
    }
    bool ReadModelGenerationSnapshot(const Project &project, const std::string &sourceAssetId,
        const content::ContentDigest &generation, const ModelGeneratedFile &packageArtifact,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage)
    {
        try
        {
            if (project.GetManifest().assetPipelineVersion < 3 || sourceAssetId.empty() ||
                std::none_of(generation.begin(), generation.end(), [](auto byte) { return byte != 0; }) ||
                Relative(packageArtifact.reference).extension() != ".plutomodel")
                throw std::runtime_error("Retained model generations require a Library project and an accepted model package.");
            const bool packed = content::IsMounted(project.GetRootDirectory());
            const auto root = packed ? std::filesystem::absolute(project.GetRootDirectory()).lexically_normal() :
                std::filesystem::canonical(project.GetRootDirectory());
            std::shared_ptr<ArtifactGenerationLock> lease;
            std::string error;
            const auto snapshots = root / "ModelSnapshots";
            const auto retained = snapshots / content::DigestToHex(generation);
            std::error_code inspectionError;
            const auto retainedStatus = packed ? std::filesystem::file_status{} : std::filesystem::symlink_status(retained, inspectionError);
            if (inspectionError && inspectionError != std::errc::no_such_file_or_directory)
                throw std::runtime_error("Cannot inspect authored model snapshot.");
            const bool authored = std::filesystem::exists(retainedStatus);
            const auto cookedRoot = project.GetAssetDirectoryPath() / ".pluto-generations";
            const auto cookedDirectory = cookedRoot / content::DigestToHex(generation);
            const bool cooked = packed || (!authored && std::filesystem::is_regular_file(root / "PlutoCook.manifest") &&
                std::filesystem::exists(cookedDirectory));
            const auto directory = cooked ? cookedDirectory : authored ? retained : root / "Library/Artifacts" / content::DigestToHex(generation);
            if (cooked)
            {
                if (!packed)
                {
                    Ordinary(project.GetAssetDirectoryPath(), true); Ordinary(cookedRoot, true); Ordinary(directory, true);
                    if (!content::IsPathWithinDirectory(std::filesystem::canonical(directory), root))
                        throw std::runtime_error("Cooked model generation escapes its project.");
                }
            }
            else if (authored)
            {
                // Published snapshots are immutable authored project data and are
                // never removed by cache collection. They remain usable without Library.
                Ordinary(snapshots, true); Ordinary(directory, true);
                if (!content::IsPathWithinDirectory(std::filesystem::canonical(directory), root))
                    throw std::runtime_error("Authored model snapshot escapes its project.");
            }
            else
            {
                lease = std::make_shared<ArtifactGenerationLock>();
                if (!lease->TryAcquire(root, generation, ArtifactGenerationLockMode::SharedReader, &error)) throw std::runtime_error(error);
                Ordinary(root / "Library", true); Ordinary(root / "Library/Artifacts", true); Ordinary(directory, true);
            }
            ModelGenerationSnapshot candidate;
            candidate.generation = generation; candidate.packageArtifact = packageArtifact; candidate.authoredSnapshot = authored;
            if (!ParseModelAsset(Read(directory, packageArtifact, 16 * 1024 * 1024), candidate.package, &error)) throw std::runtime_error(error);
            if (candidate.package.sourceAssetId != sourceAssetId) throw std::runtime_error("Retained model package owner differs from its accepted source.");
            ModelHierarchyArtifact hierarchyArtifact;
            if (ReadModelHierarchyArtifact(candidate.package, hierarchyArtifact, &error) != ModelHierarchyArtifactStatus::Success ||
                !ParseModelHierarchyAsset(Read(directory, {hierarchyArtifact.reference, hierarchyArtifact.digest}, 32 * 1024 * 1024), candidate.hierarchy, &error))
                throw std::runtime_error(error);
            if (candidate.hierarchy.sourceAssetId != sourceAssetId) throw std::runtime_error("Retained hierarchy owner differs from its package.");
            AssetMetadata metadata; metadata.id = sourceAssetId;
            metadata.extensionRecords.push_back("MODEL_ARTIFACT\t1\t" + content::DigestToHex(generation));
            std::vector<ImportedAssetStorage> ownedStorage;
            if (!BuildModelArtifactStorage(project, metadata, candidate.package, ownedStorage, &error)) throw std::runtime_error(error);
            std::set<std::string> ownedLocations, currentOwnerLocations;
            for (const auto &object : candidate.package.objects) ownedLocations.insert(object.reference);
            if (ownedStorage.size() != ownedLocations.size())
                throw std::runtime_error("Retained model package lacks complete private object baselines.");
            std::vector<AssetObjectDescriptor> objects;
            if (currentCatalog) for (const auto &object : currentCatalog->GetObjects())
            {
                if (object.identity.assetId == sourceAssetId && object.identity.localObjectId != 0)
                    currentOwnerLocations.insert(object.location);
                else objects.push_back(object);
            }
            std::vector<ImportedAssetStorage> storage;
            if (currentStorage) for (const auto &entry : currentStorage->GetEntries())
                if (!currentOwnerLocations.contains(entry.reference)) storage.push_back(entry);
            const auto scoped = [&](const std::string &reference)
            { return "project://.pluto-generations/" + content::DigestToHex(generation) + "/" + Relative(reference).generic_string(); };
            for (const auto &object : candidate.package.objects)
            {
                if (!object.localId) throw std::runtime_error("Retained imported objects require nonzero local identities.");
                const auto location = scoped(object.reference);
                if (std::any_of(objects.begin(), objects.end(), [&](const auto &existing) { return existing.location == location; }))
                    throw std::runtime_error("Retained generation virtual location conflicts with another asset.");
                objects.push_back({{sourceAssetId, object.localId}, object.type, AssetOwnership::Imported, object.name, location});
            }
            for (auto &entry : ownedStorage)
            {
                const auto path = File(directory, entry.reference);
                content::ContentDigest digest;
                if (!HashGenerationFile(path, digest, &error) || digest != entry.digest)
                    throw std::runtime_error("Retained model object differs from its accepted baseline.");
                entry.path = path; entry.reference = scoped(entry.reference); entry.generationLease = lease;
                storage.push_back(std::move(entry));
            }
            auto catalog = std::make_shared<AssetCatalog>();
            auto map = std::make_shared<AssetStorageMap>();
            if (!catalog->Replace(std::move(objects), &error) || !map->Replace(std::move(storage), &error)) throw std::runtime_error(error);
            AssetReference mesh;
            if (!ParseAssetReference(candidate.hierarchy.meshReference, mesh, &error) || mesh.assetId != sourceAssetId ||
                !catalog->Find(mesh) || catalog->Find(mesh)->type != ProjectAssetType::Mesh)
                throw std::runtime_error("Retained hierarchy mesh is absent from its package.");
            candidate.catalog = std::move(catalog); candidate.storage = std::move(map);
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = error.what(); return false; }
    }
    bool PrepareStaticModelGenerationSnapshot(const Project &project,
        const ModelGenerationSnapshot &artifacts, StaticModelGenerationSnapshot &output, std::string *errorMessage)
    {
        try
        {
            AssetReference identity;
            std::string error;
            if (!artifacts.catalog || !artifacts.storage ||
                !ParseAssetReference(artifacts.hierarchy.meshReference, identity, &error) ||
                identity.assetId != artifacts.package.sourceAssetId || !identity.localObjectId)
                throw std::runtime_error("Static generation requires verified source-owned artifacts.");
            const auto *object = artifacts.catalog->Find(identity);
            const auto *storage = object ? artifacts.storage->Find(object->location) : nullptr;
            if (!object || object->type != ProjectAssetType::Mesh || !storage || !storage->available)
                throw std::runtime_error("Static generation mesh is unavailable.");
            content::ContentDigest before;
            if (!HashGenerationFile(storage->path, before, &error) || before != storage->digest)
                throw std::runtime_error("Static generation mesh differs from its accepted baseline.");
            AssetManager reader;
            reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory,
                project.GetManifest().assetPipelineVersion);
            reader.SetAssetSnapshot(artifacts.catalog, artifacts.storage);
            render::MeshConfig config;
            MeshAssetMetadata metadata;
            StaticModelGenerationSnapshot candidate;
            if (!reader.LoadMeshAssetData(artifacts.hierarchy.meshReference, config, candidate.defaultMaterials, metadata, &error))
                throw std::runtime_error(error);
            content::ContentDigest after;
            if (!HashGenerationFile(storage->path, after, &error) || after != before ||
                metadata.sourceAssetId != identity.assetId || metadata.sourceObjectId != identity.localObjectId ||
                !config.skeleton.joints.empty())
                throw std::runtime_error("Static generation mesh changed, has incompatible provenance or requires skinning.");
            if (!PrepareStaticModelInstanceLayout(artifacts.hierarchy, config.submeshes.size(), candidate.generation.layout, &error))
                throw std::runtime_error(error);
            for (const auto &binding : candidate.generation.layout.bindings)
                if (config.submeshes[binding.submeshIndex].animatedNodeIndex >= 0)
                    throw std::runtime_error("Static generation binding requires an animation-node transform.");
            candidate.generation.meshDigest = before;
            candidate.generation.submeshCount = config.submeshes.size();
            candidate.generation.materialSlotCount = candidate.defaultMaterials.size();
            candidate.artifacts = artifacts;
            output = std::move(candidate); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    bool ValidateStaticModelInstanceBaseline(const StaticModelInstanceState &state,
        const StaticModelGenerationSnapshot &generation, std::string *errorMessage)
    {
        std::string actual, expected, error;
        if (!SerializeStaticModelInstanceState(state, actual, &error))
        { if (errorMessage) *errorMessage = error; return false; }
        auto candidate = state;
        candidate.artifactGenerationKey = generation.artifacts.generation;
        candidate.packageArtifact = generation.artifacts.packageArtifact;
        candidate.accepted = generation.generation;
        candidate.defaultMaterials = generation.defaultMaterials;
        if (!SerializeStaticModelInstanceState(candidate, expected, &error) || expected != actual)
        {
            if (errorMessage) *errorMessage = "Saved model instance baseline differs from its verified accepted generation.";
            return false;
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }

}
