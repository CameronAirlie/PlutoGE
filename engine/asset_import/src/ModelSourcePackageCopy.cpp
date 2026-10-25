#include "PlutoGE/asset_import/ModelSourcePackageCopy.h"

#include "PlutoGE/import/MeshImporter.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <set>
#include <system_error>

namespace PlutoGE::assetimport
{
    namespace
    {
        bool Fail(std::string *errorMessage, std::string message)
        {
            if (errorMessage) *errorMessage = std::move(message);
            return false;
        }

        std::filesystem::path CommonDirectory(const std::filesystem::path &a, const std::filesystem::path &b)
        {
            std::filesystem::path common;
            auto left = a.begin();
            auto right = b.begin();
            for (; left != a.end() && right != b.end() && *left == *right; ++left, ++right)
            {
                common /= *left;
            }
            return common;
        }

        int Depth(const std::filesystem::path &path)
        {
            return static_cast<int>(std::distance(path.begin(), path.end()));
        }
    }

    bool IsSupportedModelSourceFile(const std::filesystem::path &path)
    {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return extension == ".gltf" || extension == ".glb" || extension == ".fbx";
    }

    ModelPackageCopyPlan PlanModelPackageCopy(const std::filesystem::path &source,
                                              std::span<const std::filesystem::path> dependencies,
                                              const std::filesystem::path &destinationDirectory)
    {
        const auto model = source.lexically_normal();
        const auto modelDirectory = model.parent_path();
        const int minimumDepth = Depth(modelDirectory) - kMaxModelPackageParentLevels;

        // Root the package at the deepest directory holding the model and every
        // nearby dependency; far-away dependencies do not drag the root upward.
        auto root = modelDirectory;
        std::vector<std::filesystem::path> nearby;
        ModelPackageCopyPlan plan;
        std::set<std::filesystem::path> seen{model};
        for (const auto &dependency : dependencies)
        {
            const auto file = dependency.lexically_normal();
            if (!seen.insert(file).second)
            {
                continue;
            }
            const auto common = CommonDirectory(modelDirectory, file.parent_path());
            if (common.empty() || Depth(common) < minimumDepth)
            {
                plan.flattened.push_back(file);
                continue;
            }
            if (Depth(common) < Depth(root)) root = common;
            nearby.push_back(file);
        }

        plan.copiedSource = destinationDirectory / model.lexically_relative(root);
        plan.files.emplace_back(model, plan.copiedSource);
        for (const auto &file : nearby)
        {
            plan.files.emplace_back(file, destinationDirectory / file.lexically_relative(root));
        }
        const auto flatDirectory = plan.copiedSource.parent_path();
        std::set<std::filesystem::path> used;
        for (const auto &[from, to] : plan.files) used.insert(to);
        for (const auto &file : plan.flattened)
        {
            // Keep flattened names unique if two far-away files share one.
            auto target = flatDirectory / file.filename();
            for (int index = 2; used.contains(target); ++index)
            {
                target = flatDirectory / (file.stem().string() + "_" + std::to_string(index) + file.extension().string());
            }
            used.insert(target);
            plan.files.emplace_back(file, target);
        }
        return plan;
    }

    bool CollectModelSourceDependencies(const std::filesystem::path &source,
                                        std::vector<std::filesystem::path> &dependencies,
                                        std::string *errorMessage)
    {
        try
        {
            const auto absolute = std::filesystem::weakly_canonical(source);
            const auto imported = MeshImporter{}.ImportMeshSourceAsset(absolute.string(), {}, MeshSourceCachePolicy::Bypass);
            dependencies.clear();
            for (const auto &dependency : imported.sourceDependencies)
            {
                const auto path = std::filesystem::weakly_canonical(
                    std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(dependency.data()), dependency.size())));
                if (path != absolute && std::find(dependencies.begin(), dependencies.end(), path) == dependencies.end())
                {
                    dependencies.push_back(path);
                }
            }
            return true;
        }
        catch (const std::exception &exception)
        {
            return Fail(errorMessage, std::string("Cannot read model dependencies: ") + exception.what());
        }
    }

    bool CopyModelSourcePackage(const std::filesystem::path &source,
                                const std::filesystem::path &destinationDirectory,
                                ModelPackageCopyPlan &plan,
                                std::string *errorMessage)
    {
        std::vector<std::filesystem::path> dependencies;
        return CollectModelSourceDependencies(source, dependencies, errorMessage) &&
               CopyModelSourcePackage(source, dependencies, destinationDirectory, plan, errorMessage);
    }

    bool CopyModelSourcePackage(const std::filesystem::path &source,
                                std::span<const std::filesystem::path> dependencies,
                                const std::filesystem::path &destinationDirectory,
                                ModelPackageCopyPlan &plan,
                                std::string *errorMessage)
    {
        std::error_code error;
        if (std::filesystem::exists(destinationDirectory, error))
        {
            return Fail(errorMessage, "Destination already exists: " + destinationDirectory.string());
        }
        for (const auto &dependency : dependencies)
        {
            if (!std::filesystem::is_regular_file(dependency, error))
            {
                return Fail(errorMessage, "Model dependency is missing: " + dependency.string());
            }
        }

        plan = PlanModelPackageCopy(std::filesystem::weakly_canonical(source), dependencies, destinationDirectory);
        for (const auto &[from, to] : plan.files)
        {
            std::filesystem::create_directories(to.parent_path(), error);
            if (!error) std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error);
            if (error)
            {
                std::error_code cleanup;
                std::filesystem::remove_all(destinationDirectory, cleanup);
                return Fail(errorMessage, "Failed to copy '" + from.string() + "': " + error.message());
            }
        }
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
