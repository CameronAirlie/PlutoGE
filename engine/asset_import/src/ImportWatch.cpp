#include "PlutoGE/asset_import/ImportWatch.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include <set>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    bool CaptureImportWatchSnapshot(const assets::Project &project, ImportWatchSnapshot &snapshot,
                                     std::string *errorMessage, std::stop_token stop)
    {
        try
        {
            const auto checkStop = [&] { if (stop.stop_requested()) throw std::runtime_error("Import watching cancelled."); };
            checkStop();
            if (!assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), errorMessage)) return false;
            auto inventory = project;
            inventory.RefreshAssetRegistry();
            std::set<std::filesystem::path> paths;
            const auto add = [&](const std::filesystem::path &path) { paths.insert(std::filesystem::absolute(path).lexically_normal()); };
            add(project.GetRootDirectory() / "Library");
            ImportStateStore states(project.GetRootDirectory());
            for (const auto &entry : inventory.GetManifest().assetEntries)
            {
                checkStop();
                if (entry.type != assets::ProjectAssetType::Model) continue;
                const auto source = project.ResolveAssetReference(entry.reference);
                add(source);
                const auto metadataPath = assets::GetAssetMetadataPath(source);
                add(metadataPath);
                assets::AssetMetadata metadata;
                if (assets::LoadAssetMetadata(metadataPath, metadata) != assets::AssetMetadataStatus::Success) continue;
                ImportState state;
                if (states.Load(metadata.id, state) == assets::AssetMetadataStatus::Success)
                    for (const auto &input : state.inputs) add(input.path);
                assets::ModelAsset package;
                if (!assets::LoadModelSourcePackage(project, entry.reference, package)) continue;
                assets::ModelHierarchyArtifact hierarchy;
                content::ContentDigest generation;
                if (assets::ReadModelHierarchyArtifact(package, hierarchy) == assets::ModelHierarchyArtifactStatus::Success &&
                    assets::ReadModelArtifactGeneration(metadata, generation) == assets::ModelArtifactGenerationStatus::Success)
                    add(project.GetRootDirectory() / "Library/Artifacts" / content::DigestToHex(generation) / "Files" /
                        std::filesystem::u8path(hierarchy.reference.substr(assets::Project::kProjectAssetScheme.size())));
                std::vector<assets::ImportedAssetStorage> storage;
                if (assets::BuildModelArtifactStorage(project, metadata, package, storage))
                    for (const auto &file : storage) add(file.path);
                for (const auto &file : package.generatedFiles) add(project.ResolveAssetReference(file.reference));
                for (const auto &object : package.objects)
                    if (object.type == assets::ProjectAssetType::Mesh)
                        add(std::filesystem::path(project.ResolveAssetReference(object.reference)).concat(".materials"));
                add(assets::GetModelManifestPath(project, entry.reference));
            }
            ImportWatchSnapshot candidate;
            for (const auto &path : paths)
            {
                checkStop();
                std::error_code error;
                const auto status = std::filesystem::symlink_status(path, error);
                ImportWatchStamp stamp;
                if (error == std::errc::no_such_file_or_directory || status.type() == std::filesystem::file_type::not_found)
                { candidate.emplace(path, stamp); continue; }
                if (error) throw std::filesystem::filesystem_error("Cannot inspect watched input", path, error);
                stamp.type = status.type();
                if (std::filesystem::is_regular_file(status) || std::filesystem::is_directory(status))
                {
                    stamp.modified = std::filesystem::last_write_time(path);
                    if (std::filesystem::is_regular_file(status)) stamp.size = std::filesystem::file_size(path);
                }
                candidate.emplace(path, stamp);
            }
            snapshot = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error) { if (errorMessage) *errorMessage = error.what(); return false; }
    }

    std::vector<std::filesystem::path> ImportWatchDebouncer::Observe(ImportWatchSnapshot snapshot, Clock::time_point now)
    {
        if (!m_initialized)
        { m_committed = snapshot; m_candidate = std::move(snapshot); m_lastChange = now; m_initialized = true; return {}; }
        if (snapshot != m_candidate)
        { m_candidate = std::move(snapshot); m_lastChange = now; return {}; }
        if (now - m_lastChange < m_quietPeriod || m_candidate == m_committed) return {};
        std::set<std::filesystem::path> changed;
        for (const auto &[path, stamp] : m_committed)
        { const auto found = m_candidate.find(path); if (found == m_candidate.end() || found->second != stamp) changed.insert(path); }
        for (const auto &[path, stamp] : m_candidate)
        { const auto found = m_committed.find(path); if (found == m_committed.end() || found->second != stamp) changed.insert(path); }
        m_committed = m_candidate;
        return {changed.begin(), changed.end()};
    }

    void ImportWatchDebouncer::Reset()
    { m_initialized = false; m_committed.clear(); m_candidate.clear(); m_lastChange = {}; }
}
