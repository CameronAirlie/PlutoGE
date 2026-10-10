#include "PlutoGE/assets/ModelGenerationRetention.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <algorithm>
#include <map>
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
                throw std::runtime_error("Model snapshot requires ordinary paths.");
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                throw std::runtime_error("Model snapshot cannot traverse reparse points.");
#endif
        }
        std::filesystem::path Relative(const std::string &reference)
        {
            if (!Project::IsProjectAssetReference(reference) || NormalizeAssetReference(reference) != reference)
                throw std::runtime_error("Model snapshot requires normalized project locations.");
            const auto path = std::filesystem::path(reference.substr(Project::kProjectAssetScheme.size()));
            if (path.empty() || path.has_root_path() ||
                std::any_of(path.begin(), path.end(), [](const auto &part) { return part == ".." || part == "." || part.empty(); }))
                throw std::runtime_error("Invalid model snapshot location.");
            return path;
        }
    }
    bool RetainModelGeneration(const Project &project, const std::string &sourceAssetId,
        const content::ContentDigest &generation, const ModelGeneratedFile &packageArtifact,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage)
    {
        try
        {
            const auto root = std::filesystem::canonical(project.GetRootDirectory());
            Ordinary(root, true);
            ProjectAssetLock lock;
            std::string error;
            if (!lock.TryAcquire(root, &error)) throw std::runtime_error(error);
            if (!ValidateNoPendingAssetTransactions(root, &error)) throw std::runtime_error(error);
            ModelGenerationSnapshot accepted;
            if (!ReadModelGenerationSnapshot(project, sourceAssetId, generation, packageArtifact,
                currentCatalog, currentStorage, accepted, &error)) throw std::runtime_error(error);
            if (accepted.authoredSnapshot)
            { output = std::move(accepted); if (errorMessage) errorMessage->clear(); return true; }
            const auto snapshots = root / "ModelSnapshots";
            std::filesystem::create_directory(snapshots); Ordinary(snapshots, true);
            if (!content::IsPathWithinDirectory(std::filesystem::canonical(snapshots), root))
                throw std::runtime_error("Model snapshot root escapes its project.");
            const auto stagingRoot = root / "Library/ModelSnapshotStaging";
            std::filesystem::create_directory(stagingRoot); Ordinary(stagingRoot, true);
            const auto staging = stagingRoot / GenerateAssetId();
            if (!std::filesystem::create_directory(staging)) throw std::runtime_error("Cannot allocate model snapshot staging.");
            Ordinary(staging, true);
            std::map<std::filesystem::path, content::ContentDigest> files;
            const auto add = [&](const ModelGeneratedFile &file)
            {
                const auto relative = Relative(file.reference);
                const auto [entry, inserted] = files.emplace(relative, file.digest);
                if (!inserted && entry->second != file.digest) throw std::runtime_error("Conflicting model snapshot baselines.");
            };
            add(packageArtifact);
            ModelHierarchyArtifact hierarchy;
            if (ReadModelHierarchyArtifact(accepted.package, hierarchy, &error) != ModelHierarchyArtifactStatus::Success)
                throw std::runtime_error(error);
            add({hierarchy.reference, hierarchy.digest});
            for (const auto &file : accepted.package.generatedFiles) add(file);
            const auto source = root / "Library/Artifacts" / content::DigestToHex(generation) / "Files";
            Ordinary(source, true);
            for (const auto &[relative, expected] : files)
            {
                auto parent = source;
                for (const auto &part : relative.parent_path()) { parent /= part; Ordinary(parent, true); }
                const auto input = source / relative;
                Ordinary(input, false);
                const auto destination = staging / "Files" / relative;
                std::filesystem::create_directories(destination.parent_path());
                std::filesystem::copy_file(input, destination);
                content::ContentDigest digest;
                if (!content::HashFileContent(destination, digest, &error) || digest != expected)
                    throw std::runtime_error("Model snapshot copy differs from accepted bytes.");
            }
            // Nothing references staging. Rename publishes the complete immutable
            // tree on the same filesystem; failed/interrupted staging stays in
            // disposable Library and never replaces an authored generation.
            std::filesystem::rename(staging, snapshots / content::DigestToHex(generation));
            ModelGenerationSnapshot retained;
            if (!ReadModelGenerationSnapshot(project, sourceAssetId, generation, packageArtifact,
                currentCatalog, currentStorage, retained, &error)) throw std::runtime_error(error);
            output = std::move(retained); if (errorMessage) errorMessage->clear(); return true;
        }
        catch (const std::exception &error)
        { if (errorMessage) *errorMessage = error.what(); return false; }
    }
    bool RetainStaticModelInstance(const Project &project, const StaticModelInstanceState &state,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *errorMessage)
    {
        ModelGenerationSnapshot artifacts;
        StaticModelGenerationSnapshot prepared;
        if (!ReadModelGenerationSnapshot(project, state.accepted.layout.sourceAssetId,
                state.artifactGenerationKey, state.packageArtifact, currentCatalog, currentStorage, artifacts, errorMessage) ||
            !PrepareStaticModelGenerationSnapshot(project, artifacts, prepared, errorMessage) ||
            !ValidateStaticModelInstanceBaseline(state, prepared, errorMessage)) return false;
        return RetainModelGeneration(project, state.accepted.layout.sourceAssetId, state.artifactGenerationKey,
            state.packageArtifact, currentCatalog, currentStorage, output, errorMessage);
    }

}
