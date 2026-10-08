#include "PlutoGE/assets/ModelArtifactStorage.h"
#include <algorithm>
#include <map>
#include <set>

namespace PlutoGE::assets
{
    namespace
    {
        bool IsGenerationRecord(std::string_view record)
        { return record.substr(0, record.find('\t')) == "MODEL_ARTIFACT"; }
        bool Fail(std::string *error, const std::string &message)
        { if (error) *error = message; return false; }
        bool RelativeReference(std::string_view reference, std::filesystem::path &path)
        {
            if (!Project::IsProjectAssetReference(reference)) return false;
            path = std::filesystem::path(reference.substr(Project::kProjectAssetScheme.size()));
            return !path.empty() && !path.has_root_path() &&
                std::none_of(path.begin(), path.end(), [](const auto &part) { return part == ".." || part == "." || part.empty(); });
        }
    }

    ModelArtifactGenerationStatus ReadModelArtifactGeneration(const AssetMetadata &metadata, content::ContentDigest &generation,
                                                               std::string *errorMessage)
    {
        const std::string *record = nullptr;
        for (const auto &line : metadata.extensionRecords)
            if (IsGenerationRecord(line))
            {
                if (record) { Fail(errorMessage, "Duplicate active model artifact generation."); return ModelArtifactGenerationStatus::Invalid; }
                record = &line;
            }
        if (!record) { if (errorMessage) errorMessage->clear(); return ModelArtifactGenerationStatus::Missing; }
        const auto first = record->find('\t');
        const auto second = first == std::string::npos ? std::string::npos : record->find('\t', first + 1);
        if (second == std::string::npos || record->find('\t', second + 1) != std::string::npos)
        { Fail(errorMessage, "Malformed active model artifact generation."); return ModelArtifactGenerationStatus::Invalid; }
        if (std::string_view(*record).substr(first + 1, second - first - 1) != "1")
        { Fail(errorMessage, "Unsupported active model artifact generation version."); return ModelArtifactGenerationStatus::UnsupportedVersion; }
        content::ContentDigest candidate;
        if (!content::ParseContentDigest(std::string_view(*record).substr(second + 1), candidate))
        { Fail(errorMessage, "Invalid active model artifact digest."); return ModelArtifactGenerationStatus::Invalid; }
        generation = candidate;
        if (errorMessage) errorMessage->clear();
        return ModelArtifactGenerationStatus::Success;
    }

    bool WriteModelArtifactGeneration(AssetMetadata &metadata, const content::ContentDigest &generation, std::string *errorMessage)
    {
        content::ContentDigest previous;
        const auto status = ReadModelArtifactGeneration(metadata, previous, errorMessage);
        if (status != ModelArtifactGenerationStatus::Success && status != ModelArtifactGenerationStatus::Missing) return false;
        auto candidate = metadata;
        const auto record = "MODEL_ARTIFACT\t1\t" + content::DigestToHex(generation);
        // Derived publication state always follows authored settings/package
        // records, including after those codecs rewrite their owned sections.
        std::erase_if(candidate.extensionRecords, IsGenerationRecord);
        candidate.extensionRecords.push_back(record);
        std::string encoded;
        if (!SerializeAssetMetadata(candidate, encoded, errorMessage)) return false;
        metadata = std::move(candidate);
        return true;
    }

    bool BuildModelArtifactStorage(const Project &project, const AssetMetadata &metadata, const ModelAsset &package,
                                   std::vector<ImportedAssetStorage> &entries, std::string *errorMessage)
    {
        content::ContentDigest generation;
        if (ReadModelArtifactGeneration(metadata, generation, errorMessage) != ModelArtifactGenerationStatus::Success)
            return Fail(errorMessage, "Model has no valid active artifact generation.");
        if (metadata.id.empty() || metadata.id != package.sourceAssetId)
            return Fail(errorMessage, "Active model artifact owner conflicts with source metadata.");
        std::filesystem::path sourceRelative;
        if (!RelativeReference(package.sourceReference, sourceRelative)) return Fail(errorMessage, "Invalid model source location.");
        std::string validated;
        if (!SerializeModelAsset(package, validated, errorMessage)) return false;
        std::map<std::string, content::ContentDigest> baselines;
        for (const auto &file : package.generatedFiles)
        {
            std::filesystem::path relative;
            if (!RelativeReference(file.reference, relative) || !baselines.emplace(file.reference, file.digest).second ||
                std::none_of(package.objects.begin(), package.objects.end(), [&](const auto &object) { return object.reference == file.reference; }))
                return Fail(errorMessage, "Invalid source-owned artifact baseline: " + file.reference);
        }
        std::vector<ImportedAssetStorage> candidate;
        std::set<std::string> locations;
        for (const auto &object : package.objects)
        {
            std::filesystem::path relative;
            if (!RelativeReference(object.reference, relative)) return Fail(errorMessage, "Invalid imported artifact location: " + object.reference);
            const auto baseline = baselines.find(object.reference);
            if (baseline == baselines.end())
            {
                // Ordinary external/shared textures remain source assets.
                if (object.type == ProjectAssetType::Texture) continue;
                return Fail(errorMessage, "Imported object has no durable artifact baseline: " + object.reference);
            }
            if (!locations.insert(object.reference).second) continue;
            candidate.push_back({object.reference, project.GetRootDirectory() / "Library/Artifacts" /
                content::DigestToHex(generation) / "Files" / relative, baseline->second});
        }
        entries = std::move(candidate);
        if (errorMessage) errorMessage->clear();
        return true;
    }
}
