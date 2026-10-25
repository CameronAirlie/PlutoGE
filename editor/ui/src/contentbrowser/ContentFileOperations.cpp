#include "PlutoGE/ui/contentbrowser/ContentFileOperations.h"

#include "PlutoGE/asset_import/AssetMoveService.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/contentbrowser/ContentPaths.h"

#include <algorithm>
#include <system_error>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::string_view kMetadataExtension = ".plutometa";

        std::string GenericUtf8(const std::filesystem::path &path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }

        std::string DisplayName(const std::filesystem::path &path)
        {
            return PathToUtf8(path.filename());
        }

        bool Exists(const std::filesystem::path &path)
        {
            std::error_code error;
            return std::filesystem::exists(std::filesystem::symlink_status(path, error));
        }

        bool IsDirectory(const std::filesystem::path &path)
        {
            std::error_code error;
            return std::filesystem::is_directory(path, error);
        }

        // Meshes keep per-instance material overrides beside them.
        std::filesystem::path MaterialOverridesPath(const std::filesystem::path &meshPath)
        {
            auto overrides = meshPath;
            overrides += ".materials";
            return overrides;
        }

        bool IsMesh(const std::filesystem::path &path)
        {
            return path.extension() == ".plutomesh";
        }

        // Copies receive fresh identities on the next asset scan.
        void RemoveIdentitySidecars(const std::filesystem::path &copied)
        {
            std::error_code error;
            if (!IsDirectory(copied))
            {
                std::filesystem::remove(assets::GetAssetMetadataPath(copied), error);
                return;
            }
            std::vector<std::filesystem::path> sidecars;
            for (std::filesystem::recursive_directory_iterator it(copied, error), end; it != end && !error; it.increment(error))
            {
                if (it->path().extension() == kMetadataExtension) sidecars.push_back(it->path());
            }
            for (const auto &sidecar : sidecars) std::filesystem::remove(sidecar, error);
        }
    }

    ContentFileOperations::ContentFileOperations(const assets::Project &project)
        : m_project(project), m_assetRoot(project.GetAssetDirectoryPath().lexically_normal())
    {
    }

    bool ContentFileOperations::IsInside(const std::filesystem::path &path) const
    {
        const auto relative = path.lexically_normal().lexically_relative(m_assetRoot);
        return !relative.empty() && relative != "." && *relative.begin() != "..";
    }

    bool ContentFileOperations::IsEditable(const std::filesystem::path &path) const
    {
        return IsInside(path) && Exists(path) && !assets::IsAssetInfrastructurePath(m_project.GetRootDirectory(), path);
    }

    std::string ContentFileOperations::MakeReference(const std::filesystem::path &path) const
    {
        return std::string(assets::Project::kProjectAssetScheme) +
               GenericUtf8(path.lexically_normal().lexically_relative(m_assetRoot));
    }

    std::vector<std::filesystem::path> ContentFileOperations::RemoveNested(std::span<const std::filesystem::path> items)
    {
        std::vector<std::filesystem::path> kept;
        for (const auto &item : items)
        {
            const auto normalized = item.lexically_normal();
            const bool nested = std::any_of(items.begin(), items.end(), [&](const std::filesystem::path &other)
                                            {
                                                const auto parent = other.lexically_normal();
                                                if (parent == normalized) return false;
                                                const auto relative = normalized.lexically_relative(parent);
                                                return !relative.empty() && *relative.begin() != "..";
                                            });
            if (!nested && std::find(kept.begin(), kept.end(), normalized) == kept.end())
            {
                kept.push_back(normalized);
            }
        }
        return kept;
    }

    ContentOperationReport ContentFileOperations::CreateFolder(const std::filesystem::path &parent, std::string_view name) const
    {
        ContentOperationReport report;
        if (const auto problem = ValidateContentItemName(name); !problem.empty())
        {
            report.errors.push_back(problem);
            return report;
        }
        if (parent.lexically_normal() != m_assetRoot && !IsEditable(parent))
        {
            report.errors.push_back("Folders can only be created inside the asset directory.");
            return report;
        }
        const auto folder = MakeUniqueContentPath(parent, name, {});
        std::error_code error;
        if (!std::filesystem::create_directory(folder, error) || error)
        {
            report.errors.push_back("Could not create folder '" + DisplayName(folder) + "': " +
                                    (error ? error.message() : std::string("it already exists")));
            return report;
        }
        report.results.push_back(folder);
        return report;
    }

    bool ContentFileOperations::MoveOne(const std::filesystem::path &source, const std::filesystem::path &destination,
                                        ContentOperationReport &report) const
    {
        std::string error;
        if (!assetimport::MoveProjectAsset(m_project, MakeReference(source), MakeReference(destination), &error))
        {
            report.errors.push_back("Could not move '" + DisplayName(source) + "': " + error);
            return false;
        }
        report.moved.emplace_back(source, destination);
        report.results.push_back(destination);
        return true;
    }

    ContentOperationReport ContentFileOperations::Rename(const std::filesystem::path &item, std::string_view newName) const
    {
        ContentOperationReport report;
        if (const auto problem = ValidateContentItemName(newName); !problem.empty())
        {
            report.errors.push_back(problem);
            return report;
        }
        if (!IsEditable(item))
        {
            report.errors.push_back("'" + DisplayName(item) + "' cannot be renamed.");
            return report;
        }
        const auto destination = item.parent_path() / PathFromUtf8(newName);
        if (destination == item)
        {
            return report;
        }
        if (Exists(destination))
        {
            report.errors.push_back("An item named '" + std::string(newName) + "' already exists here.");
            return report;
        }
        MoveOne(item, destination, report);
        return report;
    }

    ContentOperationReport ContentFileOperations::Move(std::span<const std::filesystem::path> items,
                                                       const std::filesystem::path &destination) const
    {
        ContentOperationReport report;
        const auto target = destination.lexically_normal();
        if (target != m_assetRoot && !IsEditable(target))
        {
            report.errors.push_back("Items can only be moved into folders inside the asset directory.");
            return report;
        }
        for (const auto &item : RemoveNested(items))
        {
            if (item.parent_path() == target)
            {
                continue;
            }
            if (!IsEditable(item))
            {
                report.errors.push_back("'" + DisplayName(item) + "' cannot be moved.");
                continue;
            }
            const auto relative = target.lexically_relative(item);
            if (target == item || (!relative.empty() && *relative.begin() != ".."))
            {
                report.errors.push_back("Cannot move '" + DisplayName(item) + "' into itself.");
                continue;
            }
            const auto moved = target / item.filename();
            if (Exists(moved))
            {
                report.errors.push_back("'" + DisplayName(item) + "' already exists in the destination.");
                continue;
            }
            MoveOne(item, moved, report);
        }
        return report;
    }

    bool ContentFileOperations::CopyOne(const std::filesystem::path &source, const std::filesystem::path &destination,
                                        ContentOperationReport &report) const
    {
        std::error_code error;
        if (IsDirectory(source))
        {
            std::filesystem::copy(source, destination, std::filesystem::copy_options::recursive, error);
        }
        else
        {
            std::filesystem::copy_file(source, destination, std::filesystem::copy_options::none, error);
            if (!error && IsMesh(source) && Exists(MaterialOverridesPath(source)))
            {
                std::filesystem::copy_file(MaterialOverridesPath(source), MaterialOverridesPath(destination),
                                           std::filesystem::copy_options::none, error);
            }
        }
        if (error)
        {
            std::error_code cleanup;
            std::filesystem::remove_all(destination, cleanup);
            report.errors.push_back("Could not copy '" + DisplayName(source) + "': " + error.message());
            return false;
        }
        RemoveIdentitySidecars(destination);
        report.results.push_back(destination);
        return true;
    }

    ContentOperationReport ContentFileOperations::Copy(std::span<const std::filesystem::path> items,
                                                       const std::filesystem::path &destination) const
    {
        ContentOperationReport report;
        const auto target = destination.lexically_normal();
        if (target != m_assetRoot && !IsEditable(target))
        {
            report.errors.push_back("Items can only be copied into folders inside the asset directory.");
            return report;
        }
        for (const auto &item : RemoveNested(items))
        {
            if (!IsEditable(item))
            {
                report.errors.push_back("'" + DisplayName(item) + "' cannot be copied.");
                continue;
            }
            const auto relative = target.lexically_relative(item);
            if (IsDirectory(item) && (target == item || (!relative.empty() && *relative.begin() != "..")))
            {
                report.errors.push_back("Cannot copy folder '" + DisplayName(item) + "' into itself.");
                continue;
            }
            CopyOne(item, MakeUniqueContentCopyPath(target, item), report);
        }
        return report;
    }

    ContentOperationReport ContentFileOperations::Delete(std::span<const std::filesystem::path> items) const
    {
        ContentOperationReport report;
        // Imports and cooks hold the same lock; never delete underneath them.
        assetimport::ProjectImportLock lock;
        std::string lockError;
        if (!lock.TryAcquire(m_project.GetRootDirectory(), &lockError))
        {
            report.errors.push_back("Cannot delete while assets are being imported: " + lockError);
            return report;
        }
        for (const auto &item : RemoveNested(items))
        {
            if (!IsEditable(item))
            {
                report.errors.push_back("'" + DisplayName(item) + "' cannot be deleted.");
                continue;
            }
            std::error_code error;
            if (IsDirectory(item))
            {
                std::filesystem::remove_all(item, error);
            }
            else
            {
                std::filesystem::remove(item, error);
                if (!error)
                {
                    std::error_code sidecarError;
                    std::filesystem::remove(assets::GetAssetMetadataPath(item), sidecarError);
                    if (IsMesh(item)) std::filesystem::remove(MaterialOverridesPath(item), sidecarError);
                }
            }
            if (error)
            {
                report.errors.push_back("Could not delete '" + DisplayName(item) + "': " + error.message());
                continue;
            }
            report.moved.emplace_back(item, std::filesystem::path{});
        }
        return report;
    }

    ContentOperationReport ContentFileOperations::Import(std::span<const std::filesystem::path> externalItems,
                                                         const std::filesystem::path &destination) const
    {
        ContentOperationReport report;
        const auto target = destination.lexically_normal();
        if (target != m_assetRoot && !IsEditable(target))
        {
            report.errors.push_back("Files can only be imported into folders inside the asset directory.");
            return report;
        }
        for (const auto &item : externalItems)
        {
            if (!Exists(item))
            {
                report.errors.push_back("'" + DisplayName(item) + "' no longer exists.");
                continue;
            }
            if (IsInside(item) && IsDirectory(item) && IsContentFolderAncestorOrSelf(
                    GenericUtf8(item.lexically_relative(m_assetRoot)),
                    GenericUtf8(target.lexically_relative(m_assetRoot))))
            {
                report.errors.push_back("Cannot import folder '" + DisplayName(item) + "' into itself.");
                continue;
            }
            const bool directory = IsDirectory(item);
            const auto name = directory ? item.filename() : item.stem();
            const auto extension = directory ? std::filesystem::path{} : item.extension();
            CopyOne(item, MakeUniqueContentPath(target, PathToUtf8(name), PathToUtf8(extension)), report);
        }
        return report;
    }
}
