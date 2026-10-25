#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::ui
{
    // Outcome of a (possibly batched) operation. Batches keep going after a
    // failure so one bad item never blocks the rest; every failure is listed.
    struct ContentOperationReport
    {
        // Paths that now exist because of the operation, in input order.
        std::vector<std::filesystem::path> results;
        // Source -> destination for renames and moves, so callers can remap
        // selections, history and clipboards. Deleted items map to an empty path.
        std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moved;
        std::vector<std::string> errors;
        // Completed, but with something the user should know about.
        std::vector<std::string> warnings;

        [[nodiscard]] bool Succeeded() const { return errors.empty(); }
        [[nodiscard]] bool Changed() const { return !results.empty() || !moved.empty(); }
    };

    enum class ContentDeleteMode
    {
        Trash,     // Recoverable from the OS trash.
        Permanent, // Removed immediately.
    };

    // Moves one path to a trash; returns false with a reason on failure.
    using ContentTrashFunction = std::function<bool(const std::filesystem::path &, std::string *)>;

    // File system edits for the content browser, confined to the project's
    // asset directory. Renames and moves go through the asset move service so
    // identity sidecars travel with their assets and asset IDs stay stable.
    // Copies drop identity sidecars so each copy receives a fresh ID, and
    // deletes remove the sidecars that belong to an asset.
    class ContentFileOperations
    {
    public:
        // `trash` defaults to the platform trash (SystemShell's MoveToTrash).
        explicit ContentFileOperations(const assets::Project &project, ContentTrashFunction trash = {});

        // True for existing paths strictly inside the asset directory that are
        // not project infrastructure (and therefore safe to edit).
        [[nodiscard]] bool IsEditable(const std::filesystem::path &path) const;

        ContentOperationReport CreateFolder(const std::filesystem::path &parent, std::string_view name = "New Folder") const;
        ContentOperationReport Rename(const std::filesystem::path &item, std::string_view newName) const;
        // Items already in `destination` are skipped rather than reported.
        ContentOperationReport Move(std::span<const std::filesystem::path> items, const std::filesystem::path &destination) const;
        // Copies get unique "<name> Copy" names; use the item's parent to duplicate.
        ContentOperationReport Copy(std::span<const std::filesystem::path> items, const std::filesystem::path &destination) const;
        // Trashing also trashes an asset's sidecars so restoring brings back
        // its identity; an item that cannot be trashed is left in place.
        ContentOperationReport Delete(std::span<const std::filesystem::path> items,
                                      ContentDeleteMode mode = ContentDeleteMode::Trash) const;
        // Copies files or folders from outside the project (for example an OS
        // drop), keeping their names and adding a number on collisions. Source
        // models bring only the files they reference, in a folder named after
        // the model, so a .gltf keeps its buffers and textures.
        ContentOperationReport Import(std::span<const std::filesystem::path> externalItems, const std::filesystem::path &destination) const;

        // Removes items contained in another listed folder; acting on the
        // folder already covers them.
        [[nodiscard]] static std::vector<std::filesystem::path> RemoveNested(std::span<const std::filesystem::path> items);

    private:
        [[nodiscard]] std::string MakeReference(const std::filesystem::path &path) const;
        [[nodiscard]] bool IsInside(const std::filesystem::path &path) const;
        bool MoveOne(const std::filesystem::path &source, const std::filesystem::path &destination, ContentOperationReport &report) const;
        bool CopyOne(const std::filesystem::path &source, const std::filesystem::path &destination, ContentOperationReport &report) const;

        bool ImportModel(const std::filesystem::path &source, const std::filesystem::path &destination,
                         ContentOperationReport &report) const;

        const assets::Project &m_project;
        ContentTrashFunction m_trash;
        std::filesystem::path m_assetRoot;
    };
}
