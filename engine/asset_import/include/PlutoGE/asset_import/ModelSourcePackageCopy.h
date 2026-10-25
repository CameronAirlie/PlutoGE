#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::assetimport
{
    // True for source model formats the importer reads (.gltf, .glb, .fbx).
    [[nodiscard]] bool IsSupportedModelSourceFile(const std::filesystem::path &path);

    // Where each file of a model source package goes when copied.
    struct ModelPackageCopyPlan
    {
        std::filesystem::path copiedSource; // The model file's destination.
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files; // source -> destination, model first.
        // Dependencies too far from the model to keep their relative layout;
        // they are placed beside the model, which only helps importers that
        // also search by file name.
        std::vector<std::filesystem::path> flattened;
    };

    // How many directories above the model's own directory a dependency may
    // live and still be copied with its relative path intact.
    inline constexpr int kMaxModelPackageParentLevels = 2;

    // Lays out `source` and its `dependencies` (absolute paths) under
    // `destinationDirectory`, preserving their layout relative to the deepest
    // directory containing all of them, so relative references keep working.
    [[nodiscard]] ModelPackageCopyPlan PlanModelPackageCopy(const std::filesystem::path &source,
                                                            std::span<const std::filesystem::path> dependencies,
                                                            const std::filesystem::path &destinationDirectory);

    // The files a source model reads besides itself (external buffers,
    // textures), as reported by the importer that will later import it.
    bool CollectModelSourceDependencies(const std::filesystem::path &source,
                                        std::vector<std::filesystem::path> &dependencies,
                                        std::string *errorMessage = nullptr);

    // Copies a source model and only the files it depends on into
    // `destinationDirectory` (which must not exist yet, so a failure can be
    // rolled back without touching anything else).
    bool CopyModelSourcePackage(const std::filesystem::path &source,
                                const std::filesystem::path &destinationDirectory,
                                ModelPackageCopyPlan &plan,
                                std::string *errorMessage = nullptr);
    // As above, with dependencies the caller already collected (parsing a
    // model is the expensive part, so callers that inspect first reuse it).
    bool CopyModelSourcePackage(const std::filesystem::path &source,
                                std::span<const std::filesystem::path> dependencies,
                                const std::filesystem::path &destinationDirectory,
                                ModelPackageCopyPlan &plan,
                                std::string *errorMessage = nullptr);
}
