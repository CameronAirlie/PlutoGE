#include "PlutoGE/asset_import/AssetMoveService.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelSourcePackage.h"

#include <cerrno>
#include <exception>
#include <system_error>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <stdio.h>
#endif

namespace PlutoGE::assetimport
{
    namespace
    {
        bool Fail(std::string *error, const std::string &message)
        {
            if (error) *error = message;
            return false;
        }
        bool PathsEqual(const std::filesystem::path &left, const std::filesystem::path &right)
        {
#ifdef _WIN32
            const auto &a = left.native();
            const auto &b = right.native();
            return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
#else
            return left == right;
#endif
        }
        bool Within(const std::filesystem::path &path, const std::filesystem::path &root)
        {
            auto part = path.begin();
            for (const auto &component : root)
            {
                if (part == path.end() || !PathsEqual(*part, component)) return false;
                ++part;
            }
            return part != path.end();
        }
        std::filesystem::path CanonicalIfPresent(const std::filesystem::path &path)
        {
            std::error_code error;
            auto canonical = std::filesystem::canonical(path, error);
            return error ? path : canonical;
        }
        bool ValidateMoveOwnership(const assets::Project &project, const std::filesystem::path &root, const std::filesystem::path &source, bool directory, std::string *errorMessage)
        {
            if (PathsEqual(source.extension(), std::filesystem::path(".plutomodel"))) return Fail(errorMessage, "Generated model manifests cannot be moved independently.");
            auto inventory = project;
            inventory.RefreshAssetRegistry();
            for (const auto &entry : inventory.GetManifest().assetEntries)
            {
                if (entry.type != assets::ProjectAssetType::Model) continue;
                const auto modelSource = CanonicalIfPresent((root / std::filesystem::u8path(entry.reference.substr(assets::Project::kProjectAssetScheme.size()))).lexically_normal());
                assets::AssetMetadata metadata;
                const auto status = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(modelSource), metadata, errorMessage);
                if (status != assets::AssetMetadataStatus::Success && status != assets::AssetMetadataStatus::Missing) return false;
                assets::ModelAsset package;
                const auto packageStatus = assets::ReadModelSourcePackage(metadata, package, errorMessage);
                if (packageStatus != assets::ModelSourcePackageStatus::Success && packageStatus != assets::ModelSourcePackageStatus::Missing) return false;
                if (packageStatus == assets::ModelSourcePackageStatus::Missing)
                {
                    const auto manifest = assets::FindModelManifestPath(project, entry.reference);
                    if (!std::filesystem::exists(manifest)) continue;
                    if (!assets::LoadModelAsset(manifest.string(), package, errorMessage)) return false;
                }
                if (PathsEqual(CanonicalIfPresent(assets::FindModelManifestPath(project, entry.reference)), source))
                    return Fail(errorMessage, "Generated model manifests cannot be moved independently.");
                if (directory && Within(modelSource, source))
                    return Fail(errorMessage, "Moving an imported model directory requires package reference migration, which is not yet supported.");
                for (const auto &object : package.objects)
                {
                    if (!assets::Project::IsProjectAssetReference(object.reference))
                        return Fail(errorMessage, "Imported package location requires repair before moving assets.");
                    const auto generated = CanonicalIfPresent((root / std::filesystem::u8path(object.reference.substr(assets::Project::kProjectAssetScheme.size()))).lexically_normal());
                    if (PathsEqual(generated, source) || (directory && Within(generated, source)) ||
                        PathsEqual(std::filesystem::path(generated).concat(".materials"), source))
                        return Fail(errorMessage, "Imported model objects cannot be moved independently. Extract an authored copy first.");
                }
            }
            return true;
        }

        bool RenameExclusive(const std::filesystem::path &source, const std::filesystem::path &destination,
                             std::error_code &error)
        {
#ifdef _WIN32
            if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
            error = std::error_code(GetLastError(), std::system_category());
#elif defined(__linux__) && defined(SYS_renameat2)
            // RENAME_NOREPLACE: preflight alone cannot protect a concurrent writer.
            if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), 1U) == 0) return true;
            error = std::error_code(errno, std::generic_category());
#elif defined(__APPLE__)
            if (renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL) == 0) return true;
            error = std::error_code(errno, std::generic_category());
#else
            error = std::make_error_code(std::errc::operation_not_supported);
#endif
            return false;
        }
        bool Absent(const std::filesystem::path &path, std::error_code &error)
        {
            const auto status = std::filesystem::symlink_status(path, error);
            if (error == std::errc::no_such_file_or_directory) error.clear();
            return !error && status.type() == std::filesystem::file_type::not_found;
        }
    }

    bool MoveProjectAsset(const assets::Project &project, std::string_view sourceReference,
                          std::string_view destinationReference, std::string *errorMessage)
    {
        try
        {
            if (!assets::Project::IsProjectAssetReference(sourceReference) ||
                !assets::Project::IsProjectAssetReference(destinationReference))
                return Fail(errorMessage, "Asset moves require project references.");
            ProjectImportLock lock;
            if (!lock.TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
            std::error_code error;
            const auto root = std::filesystem::canonical(project.GetAssetDirectoryPath(), error);
            if (error) return Fail(errorMessage, "Cannot resolve asset root: " + error.message());
            if (!ImportFileTransaction::Recover(project.GetRootDirectory(), root, errorMessage)) return false;
            const auto projectRoot = std::filesystem::canonical(project.GetRootDirectory(), error);
            if (error) return Fail(errorMessage, "Cannot resolve project root: " + error.message());
            const auto source = (root / std::filesystem::u8path(sourceReference.substr(assets::Project::kProjectAssetScheme.size()))).lexically_normal();
            const auto destination = (root / std::filesystem::u8path(destinationReference.substr(assets::Project::kProjectAssetScheme.size()))).lexically_normal();
            if (assets::IsAssetInfrastructurePath(projectRoot, source) ||
                assets::IsAssetInfrastructurePath(projectRoot, destination))
                return Fail(errorMessage, "Asset moves cannot target project infrastructure.");
            if (!Within(source, root) || !Within(destination, root))
                return Fail(errorMessage, "Asset move must remain inside the asset directory.");
            const auto sourceStatus = std::filesystem::symlink_status(source, error);
            if (error || (!std::filesystem::is_regular_file(sourceStatus) && !std::filesystem::is_directory(sourceStatus)))
                return Fail(errorMessage, "Asset source must be an existing ordinary file or directory.");
            const auto resolvedSource = std::filesystem::canonical(source, error);
            if (error || !Within(resolvedSource, root)) return Fail(errorMessage, "Asset source escapes the asset directory.");
            const auto resolvedParent = std::filesystem::canonical(destination.parent_path(), error);
            if (error || (!PathsEqual(resolvedParent, root) && !Within(resolvedParent, root)))
                return Fail(errorMessage, "Asset destination parent is missing or outside the asset directory.");
            if (assets::IsAssetInfrastructurePath(projectRoot, resolvedSource) ||
                assets::IsAssetInfrastructurePath(projectRoot, resolvedParent / destination.filename()))
                return Fail(errorMessage, "Asset move resolves into project infrastructure.");
            if (std::filesystem::is_directory(sourceStatus) && Within(destination, source))
                return Fail(errorMessage, "Cannot move a directory into itself.");
            if (!Absent(destination, error)) return Fail(errorMessage, "Asset destination already exists or cannot be inspected.");
            if (!ValidateMoveOwnership(project, root, resolvedSource, std::filesystem::is_directory(sourceStatus), errorMessage)) return false;
            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves{{source, destination}};
            if (std::filesystem::is_regular_file(sourceStatus))
            {
                const auto sourceMetadata = assets::GetAssetMetadataPath(source);
                const auto destinationMetadata = assets::GetAssetMetadataPath(destination);
                assets::AssetMetadata metadata;
                const auto status = assets::LoadAssetMetadata(sourceMetadata, metadata, errorMessage);
                if (status != assets::AssetMetadataStatus::Success && status != assets::AssetMetadataStatus::Missing) return false;
                if (!Absent(destinationMetadata, error))
                    return Fail(errorMessage, "Asset destination has an existing identity sidecar; preserve it for repair.");
                if (status == assets::AssetMetadataStatus::Success) moves.emplace_back(sourceMetadata, destinationMetadata);
                if (PathsEqual(source.extension(), std::filesystem::path(".plutomesh")))
                {
                    auto oldBindings = source;
                    oldBindings += ".materials";
                    auto newBindings = destination;
                    newBindings += ".materials";
                    if (!Absent(newBindings, error)) return Fail(errorMessage, "Destination material overrides already exist.");
                    const bool missing = Absent(oldBindings, error);
                    if (error) return Fail(errorMessage, "Cannot inspect material overrides: " + error.message());
                    if (!missing)
                    {
                        const auto status = std::filesystem::symlink_status(oldBindings, error);
                        if (error || !std::filesystem::is_regular_file(status)) return Fail(errorMessage, "Material overrides must be an ordinary file.");
                        moves.emplace_back(oldBindings, newBindings);
                    }
                }
            }
            std::size_t completed = 0;
            for (; completed < moves.size(); ++completed)
            {
                if (RenameExclusive(moves[completed].first, moves[completed].second, error)) continue;
                std::string message = "Asset move failed: " + error.message();
                while (completed > 0)
                {
                    --completed;
                    std::error_code rollbackError;
                    if (!RenameExclusive(moves[completed].second, moves[completed].first, rollbackError))
                        message += "; rollback failed for " + moves[completed].first.string() + ": " + rollbackError.message() + ". Manual repair required";
                }
                return Fail(errorMessage, message);
            }
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error) { return Fail(errorMessage, "Asset move failed: " + std::string(error.what())); }
    }
}
