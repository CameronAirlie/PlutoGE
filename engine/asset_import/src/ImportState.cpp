#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ImportDependencyIndex.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/asset_import/ModelArtifactSettings.h"
#include "ModelOutputProvenance.h"
#include "ModelGenerationPublication.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/platform/FilesystemPaths.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <unordered_map>

namespace PlutoGE::assetimport
{
    namespace
    {
        bool Fail(std::string *error, const std::string &message)
        {
            if (error) *error = message;
            return false;
        }
        bool SafeField(std::string_view value) { return !value.empty() && value.find_first_of("\r\n\0", 0, 3) == std::string_view::npos; }
        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.generic_u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }
        bool SafeSource(const std::string &reference)
        {
            if (!SafeField(reference) || !assets::Project::IsProjectAssetReference(reference) ||
                assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Model) return false;
            const auto relative = std::filesystem::u8path(reference.substr(assets::Project::kProjectAssetScheme.size()));
            if (relative.empty() || relative.is_absolute()) return false;
            for (const auto &part : relative) if (part == "..") return false;
            return true;
        }
        bool ToMetadata(const ImportState &state, assets::AssetMetadata &metadata, std::string *error)
        {
            if (!SafeSource(state.sourceReference) || state.inputs.empty()) return Fail(error, "Import state requires a model source and inputs.");
            assets::AssetMetadata candidate{.id=state.ownerId};
            candidate.extensionRecords = {"IMPORT_STATE\t1", "SOURCE\t" + state.sourceReference,
                                          "GENERATION\t" + content::DigestToHex(state.generation)};
            std::set<std::string> identities;
            for (const auto &input : state.inputs)
            {
                const auto path = Utf8(input.path.lexically_normal());
                if (!SafeField(input.identity) || !SafeField(path) || !input.path.is_absolute() || !identities.insert(input.identity).second)
                    return Fail(error, "Import state input is invalid or duplicated.");
                std::ostringstream line;
                line << "INPUT\t" << std::quoted(input.identity) << ' ' << std::quoted(path) << ' ' << content::DigestToHex(input.digest);
                candidate.extensionRecords.push_back(line.str());
            }
            if (!identities.contains("source") || !identities.contains("source-metadata"))
                return Fail(error, "Import state omits required source inputs.");
            metadata = std::move(candidate);
            return true;
        }
        bool FromMetadata(const assets::AssetMetadata &metadata, ImportState &state, std::string *error)
        {
            ImportState candidate;
            candidate.ownerId = metadata.id;
            bool version = false, source = false, generation = false;
            for (const auto &line : metadata.extensionRecords)
            {
                if (line.starts_with("IMPORT_STATE\t"))
                {
                    if (version || line != "IMPORT_STATE\t1") return Fail(error, "Unsupported or duplicate import state schema.");
                    version = true;
                }
                else if (line.starts_with("SOURCE\t"))
                {
                    if (source) return Fail(error, "Duplicate import state source.");
                    candidate.sourceReference = line.substr(7); source = true;
                }
                else if (line.starts_with("GENERATION\t"))
                {
                    if (generation || !content::ParseContentDigest(std::string_view(line).substr(11), candidate.generation))
                        return Fail(error, "Invalid import generation.");
                    generation = true;
                }
                else if (line.starts_with("INPUT\t"))
                {
                    std::istringstream input(line.substr(6));
                    ArtifactInput value;
                    std::string path, digest;
                    if (!(input >> std::quoted(value.identity) >> std::quoted(path) >> digest) ||
                        !content::ParseContentDigest(digest, value.digest)) return Fail(error, "Invalid import input record.");
                    input >> std::ws;
                    if (!input.eof()) return Fail(error, "Trailing import input data.");
                    value.path = std::filesystem::u8path(path);
                    candidate.inputs.push_back(std::move(value));
                }
                else return Fail(error, "Unknown import state record.");
            }
            assets::AssetMetadata validated;
            if (!version || !source || !generation || !ToMetadata(candidate, validated, error)) return Fail(error, "Incomplete import state.");
            state = std::move(candidate);
            return true;
        }
    }

    bool SerializeImportState(const ImportState &state, std::string &text, std::string *errorMessage)
    {
        assets::AssetMetadata metadata;
        return ToMetadata(state, metadata, errorMessage) && assets::SerializeAssetMetadata(metadata, text, errorMessage);
    }
    bool ParseImportState(std::string_view text, ImportState &state, std::string *errorMessage)
    {
        assets::AssetMetadata metadata;
        return assets::ParseAssetMetadata(text, metadata, errorMessage) == assets::AssetMetadataStatus::Success &&
               FromMetadata(metadata, state, errorMessage);
    }
    bool CaptureImportState(const std::string &ownerId, const std::string &sourceReference,
                            const ArtifactManifest &generation, const std::vector<ArtifactInput> &publicationInputs, ImportState &state, std::string *errorMessage)
    {
        ImportState candidate{ownerId, sourceReference, generation.key, generation.recipe.inputs};
        for (const auto &input : publicationInputs)
        {
            if (!IsModelPublicationInput(input.identity)) return Fail(errorMessage, "Publication-only inputs must be authored settings or binding overrides.");
            candidate.inputs.push_back(input);
        }
        for (auto &input : candidate.inputs)
        {
            content::ContentDigest current;
            if (!content::HashFileContent(input.path, current, errorMessage)) return false;
            if (!IsModelPublicationInput(input.identity) && current != input.digest) return Fail(errorMessage, "An import dependency changed before acceptance.");
            input.digest = current;
        }
        assets::AssetMetadata validated;
        if (!ToMetadata(candidate, validated, errorMessage)) return false;
        state = std::move(candidate);
        return true;
    }
    std::filesystem::path ImportStateStore::StatePath(const std::string &ownerId) const
    {
        const auto digest = content::HashContent(std::as_bytes(std::span(ownerId.data(), ownerId.size())));
        return m_projectRoot / "Library/ImportState" / (content::DigestToHex(digest).substr(0, 32) + ".plutometa");
    }
    bool ImportStateStore::Store(const ImportState &state, std::string *errorMessage) const
    {
        assets::AssetMetadata metadata;
        if (!ToMetadata(state, metadata, errorMessage)) return false;
        const auto path = StatePath(state.ownerId);
        std::error_code error;
        const auto projectRoot = std::filesystem::canonical(m_projectRoot, error);
        if (error) return Fail(errorMessage, "Cannot resolve import state project root.");
        std::filesystem::path parent;
        if (!content::ResolveDirectoryForCreation(path.parent_path(), parent, errorMessage)) return false;
        const auto relative = parent.lexically_relative(projectRoot);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            return Fail(errorMessage, "Import state must remain inside the project Library.");
        std::filesystem::create_directories(parent, error);
        if (error) return Fail(errorMessage, "Cannot create import state directory: " + error.message());
        assets::AssetMetadata existing;
        auto status = assets::LoadAssetMetadata(path, existing, errorMessage);
        if (status != assets::AssetMetadataStatus::Success && status != assets::AssetMetadataStatus::Missing)
        {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) || error) return false;
            auto quarantine = path;
            quarantine += ".corrupt-" + assets::GenerateAssetId();
            std::filesystem::rename(path, quarantine, error);
            if (error) return Fail(errorMessage, "Cannot quarantine invalid disposable import state.");
            status = assets::AssetMetadataStatus::Missing;
        }
        return assets::SaveAssetMetadata(path, metadata, status == assets::AssetMetadataStatus::Missing ?
            assets::AssetMetadataWriteMode::CreateOnly : assets::AssetMetadataWriteMode::ReplaceExisting, errorMessage);
    }
    assets::AssetMetadataStatus ImportStateStore::Load(const std::string &ownerId, ImportState &state, std::string *errorMessage) const
    {
        assets::AssetMetadata metadata;
        const auto status = assets::LoadAssetMetadata(StatePath(ownerId), metadata, errorMessage);
        if (status != assets::AssetMetadataStatus::Success) return status;
        if (metadata.id != ownerId || !FromMetadata(metadata, state, errorMessage)) return assets::AssetMetadataStatus::Invalid;
        return assets::AssetMetadataStatus::Success;
    }

    void RememberAcceptedImport(const assets::Project &project, const ImportState &state) noexcept
    {
        try { ImportStateStore(project.GetRootDirectory()).Store(state); }
        catch (...) { /* Disposable state must never turn an accepted import into failure. */ }
    }

    static bool ReconcileModelImportsImpl(const assets::Project &project, std::vector<ImportAssessment> &assessments, std::string *errorMessage, std::stop_token stop,
                                          const std::vector<std::string> *selectedSources)
    {
        try
        {
            if (stop.stop_requested()) return Fail(errorMessage, "Import reconciliation cancelled.");
            if (!assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), errorMessage)) return false;
            auto inventory = project;
            inventory.RefreshAssetRegistry();
            struct SourceMetadata
            {
                assets::AssetMetadata value;
                assets::AssetMetadataStatus status;
                std::string error;
            };
            std::unordered_map<std::string, SourceMetadata> sourceMetadata;
            std::unordered_map<std::string, std::size_t> ownerCounts;
            for (const auto &entry : inventory.GetManifest().assetEntries)
            {
                if (stop.stop_requested()) return Fail(errorMessage, "Import reconciliation cancelled.");
                if (entry.type != assets::ProjectAssetType::Model) continue;
                auto &source = sourceMetadata[entry.reference];
                source.status = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(project.ResolveAssetReference(entry.reference)), source.value, &source.error);
                if (source.status == assets::AssetMetadataStatus::Success) ++ownerCounts[source.value.id];
            }
            std::vector<ImportAssessment> candidate;
            ImportStateStore states(project.GetRootDirectory());
            ArtifactCache cache(project.GetRootDirectory() / "Library/Artifacts");
            for (const auto &entry : inventory.GetManifest().assetEntries)
            {
                if (stop.stop_requested()) return Fail(errorMessage, "Import reconciliation cancelled.");
                if (entry.type != assets::ProjectAssetType::Model) continue;
                if (selectedSources && std::find(selectedSources->begin(), selectedSources->end(), entry.reference) == selectedSources->end()) continue;
                ImportAssessment assessment{entry.reference};
                assessment.automaticImportSafe = !std::filesystem::is_regular_file(assets::FindModelManifestPath(project, entry.reference));
                const auto &source = sourceMetadata.at(entry.reference);
                const auto &metadata = source.value;
                std::string reason = source.error;
                const auto metadataStatus = source.status;
                ImportState state;
                if (metadataStatus == assets::AssetMetadataStatus::Missing) assessment.reason = "Source metadata is missing.";
                else if (metadataStatus != assets::AssetMetadataStatus::Success)
                { assessment.status = ImportReconciliationStatus::Blocked; assessment.reason = reason; }
                else if (ownerCounts.at(metadata.id) > 1)
                { assessment.status = ImportReconciliationStatus::Blocked; assessment.reason = "Duplicate model source identity."; }
                else if ([&]()
                {
                    const auto manifest = assets::FindModelManifestPath(project, entry.reference);
                    assets::ModelAsset previous;
                    const auto packageStatus = assets::ReadModelSourcePackage(metadata, previous, &reason);
                    if (packageStatus != assets::ModelSourcePackageStatus::Success && packageStatus != assets::ModelSourcePackageStatus::Missing) return true;
                    if (packageStatus == assets::ModelSourcePackageStatus::Missing && !std::filesystem::is_regular_file(manifest)) return false;
                    assets::ModelImportSettings settings;
                    assets::AssetManager reader;
                    reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory);
                    if (packageStatus == assets::ModelSourcePackageStatus::Missing && !assets::LoadModelAsset(manifest.string(), previous, &reason)) return true;
                    if (!previous.sourceAssetId.empty() && previous.sourceAssetId != metadata.id)
                    { reason = "Model manifest source identity conflicts with its sidecar."; return true; }
                    const auto settingsStatus = assets::ReadModelImportSettings(metadata, settings, &reason);
                    if (settingsStatus != assets::ModelImportSettingsStatus::Success && settingsStatus != assets::ModelImportSettingsStatus::Missing) return true;
                    if (project.GetManifest().assetPipelineVersion >= 2 && previous.importerVersion >= 2 &&
                        settingsStatus == assets::ModelImportSettingsStatus::Missing)
                    { reason = "Persistent model correspondence is missing from source metadata."; return true; }
                    if (project.GetManifest().assetPipelineVersion >= 2 && previous.importerVersion >= 2 &&
                        !ValidateModelSourceCorrespondence(previous, settings, &reason)) return true;
                    assets::ModelHierarchyArtifact hierarchy;
                    const auto hierarchyStatus = assets::ReadModelHierarchyArtifact(previous, hierarchy, &reason);
                    if (hierarchyStatus != assets::ModelHierarchyArtifactStatus::Success && hierarchyStatus != assets::ModelHierarchyArtifactStatus::Missing) return true;
                    content::ContentDigest active;
                    const auto activeStatus = assets::ReadModelArtifactGeneration(metadata, active, &reason);
                    if (activeStatus != assets::ModelArtifactGenerationStatus::Success && activeStatus != assets::ModelArtifactGenerationStatus::Missing) return true;
                    assessment.automaticImportSafe = !previous.generatedFiles.empty();
                    return !ValidateGeneratedFileBaselines(project, previous, settings, reader, &reason,
                        UsesLibraryModelStorage(project) && activeStatus == assets::ModelArtifactGenerationStatus::Success);
                }()) { assessment.status = ImportReconciliationStatus::Blocked; assessment.reason = reason; }
                else if (states.Load(metadata.id, state, &reason) != assets::AssetMetadataStatus::Success)
                    assessment.reason = "Import state is missing or invalid.";
                else if (state.sourceReference != entry.reference) assessment.reason = "Source location changed.";
                else
                {
                    ArtifactRecipe inputs;
                    inputs.inputs = state.inputs;
                    ArtifactManifest generation;
                    if (!AreArtifactInputsCurrent(inputs, &reason)) assessment.reason = reason;
                    else if (cache.Find(state.generation, generation, &reason) != ArtifactCacheStatus::Hit)
                        assessment.reason = "Artifact generation is missing or invalid.";
                    else if (generation.recipe.importer != kModelArtifactImporter || generation.recipe.version != kModelArtifactVersion ||
                             generation.recipe.target != kModelArtifactTarget) assessment.reason = "Importer or target version changed.";
                    else
                    {
                        const auto contentInputCount = std::count_if(state.inputs.begin(), state.inputs.end(), [](const auto &input) { return !IsModelPublicationInput(input.identity); });
                        bool current = contentInputCount == generation.recipe.inputs.size();
                        for (const auto &expected : generation.recipe.inputs)
                        {
                            const auto input = std::find_if(state.inputs.begin(), state.inputs.end(), [&](const auto &value)
                            { return value.identity == expected.identity && value.path.lexically_normal() == expected.path.lexically_normal(); });
                            if (input == state.inputs.end() || (!IsModelPublicationInput(expected.identity) && input->digest != expected.digest)) current = false;
                        }
                        content::ContentDigest active;
                        if (UsesLibraryModelStorage(project) &&
                            (assets::ReadModelArtifactGeneration(metadata, active, &reason) != assets::ModelArtifactGenerationStatus::Success || active != state.generation)) current = false;
                        const auto publication = ModelPublishedGeneration(project, generation, assets::GetAssetMetadataPath(project.ResolveAssetReference(entry.reference)));
                        for (const auto &output : publication.outputs)
                        {
                            content::ContentDigest digest;
                            if (!content::HashFileContent(project.GetAssetDirectoryPath() / output.relativePath, digest, &reason) || digest != output.digest)
                            { current = false; break; }
                        }
                        if (current) { assessment.status = ImportReconciliationStatus::Current; assessment.reason = "Inputs and artifacts are current."; }
                        else assessment.reason = "Published native output is missing or changed.";
                    }
                }
                if (assessment.status != ImportReconciliationStatus::NeedsImport) assessment.automaticImportSafe = false;
                candidate.push_back(std::move(assessment));
            }
            std::sort(candidate.begin(), candidate.end(), [](const auto &a, const auto &b) { return a.sourceReference < b.sourceReference; });
            if (stop.stop_requested()) return Fail(errorMessage, "Import reconciliation cancelled.");
            assessments = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { return Fail(errorMessage, exception.what()); }
    }
    bool ReconcileModelImports(const assets::Project &project, std::vector<ImportAssessment> &assessments, std::string *errorMessage, std::stop_token stop)
    {
        return ReconcileModelImportsImpl(project, assessments, errorMessage, stop, nullptr);
    }

    bool ReconcileChangedModelImports(const assets::Project &project, const std::vector<std::filesystem::path> &changedPaths,
                                      std::vector<ImportAssessment> &assessments, std::string *errorMessage, std::stop_token stop)
    {
        if (stop.stop_requested()) return Fail(errorMessage, "Import reconciliation cancelled.");
        if (std::any_of(changedPaths.begin(), changedPaths.end(), [](const auto &path) { return !path.is_absolute(); }))
            return Fail(errorMessage, "Changed import input paths must be absolute.");
        std::vector<std::string> sources;
        bool covered = false;
        // Invalid/missing index data must not suppress authoritative diagnostics.
        const bool indexed = FindAffectedModelImports(project, changedPaths, sources, nullptr, &covered);
        return ReconcileModelImportsImpl(project, assessments, errorMessage, stop, indexed && covered ? &sources : nullptr);
    }

}
