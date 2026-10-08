#include "PlutoGE/asset_import/ImportDependencyIndex.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include "PlutoGE/assets/AssetPathPolicy.h"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    namespace
    {
        std::string PathKey(const std::filesystem::path &path)
        {
            const auto bytes = path.lexically_normal().generic_u8string();
            std::string key(reinterpret_cast<const char *>(bytes.data()), bytes.size());
#ifdef _WIN32
            for (auto &character : key) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
#endif
            return key;
        }
        bool Fail(std::string *error, const std::string &message)
        { if (error) *error = message; return false; }
    }
    bool ImportDependencyIndex::Replace(const std::vector<ImportState> &states, std::string *errorMessage)
    {
        std::unordered_map<std::string, std::vector<std::string>> candidate;
        std::set<std::string> owners, sources;
        for (const auto &state : states)
        {
            std::string validated;
            if (!SerializeImportState(state, validated, errorMessage)) return false;
            if (!owners.insert(state.ownerId).second || !sources.insert(state.sourceReference).second)
                return Fail(errorMessage, "Duplicate import dependency owner or source.");
            for (const auto &input : state.inputs) candidate[PathKey(input.path)].push_back(state.sourceReference);
        }
        for (auto &[path, references] : candidate)
        {
            std::sort(references.begin(), references.end());
            references.erase(std::unique(references.begin(), references.end()), references.end());
        }
        m_ownersByInput = std::move(candidate);
        if (errorMessage) errorMessage->clear();
        return true;
    }
    std::vector<std::string> ImportDependencyIndex::FindAffected(const std::vector<std::filesystem::path> &changedPaths) const
    {
        std::set<std::string> affected;
        for (const auto &path : changedPaths)
        {
            if (!path.is_absolute()) continue;
            const auto found = m_ownersByInput.find(PathKey(path));
            if (found != m_ownersByInput.end()) affected.insert(found->second.begin(), found->second.end());
        }
        return {affected.begin(), affected.end()};
    }
    bool FindAffectedModelImports(const assets::Project &project, const std::vector<std::filesystem::path> &changedPaths,
                                  std::vector<std::string> &sources, std::string *errorMessage, bool *allPathsIndexed)
    {
        try
        {
            if (!assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), errorMessage)) return false;
            std::vector<std::filesystem::path> normalized;
            for (const auto &path : changedPaths)
            {
                if (!path.is_absolute()) return Fail(errorMessage, "Changed import input paths must be absolute.");
                std::error_code error;
                if (std::filesystem::is_regular_file(path, error)) normalized.push_back(std::filesystem::canonical(path));
                else
                {
                    std::filesystem::path parent;
                    if (!content::ResolveDirectoryForCreation(path.parent_path(), parent, errorMessage)) return false;
                    normalized.push_back(parent / path.filename());
                }
            }
            auto inventory = project;
            inventory.RefreshAssetRegistry();
            ImportStateStore store(project.GetRootDirectory());
            std::vector<ImportState> states;
            std::set<std::string> unknown, owners;
            for (const auto &entry : inventory.GetManifest().assetEntries)
            {
                if (entry.type != assets::ProjectAssetType::Model) continue;
                assets::AssetMetadata metadata;
                const auto status = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(project.ResolveAssetReference(entry.reference)), metadata, errorMessage);
                if (status == assets::AssetMetadataStatus::Missing) { unknown.insert(entry.reference); continue; }
                if (status != assets::AssetMetadataStatus::Success) return false;
                if (!owners.insert(metadata.id).second) return Fail(errorMessage, "Duplicate model source identity in dependency inventory.");
                ImportState state;
                if (store.Load(metadata.id, state) != assets::AssetMetadataStatus::Success || state.sourceReference != entry.reference)
                    unknown.insert(entry.reference);
                else states.push_back(std::move(state));
            }
            ImportDependencyIndex index;
            if (!index.Replace(states, errorMessage)) return false;
            auto affected = index.FindAffected(normalized);
            unknown.insert(affected.begin(), affected.end());
            const bool covered = !normalized.empty() && std::all_of(normalized.begin(), normalized.end(),
                [&](const auto &path) { return !index.FindAffected({path}).empty(); });
            std::vector<std::string> candidate(unknown.begin(), unknown.end());
            sources = std::move(candidate);
            if (allPathsIndexed) *allPathsIndexed = covered;
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { return Fail(errorMessage, exception.what()); }
    }
}
