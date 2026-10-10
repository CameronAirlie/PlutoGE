#include "PlutoGE/assets/ModelActiveGeneration.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/AssetReferences.h"
#include <algorithm>
#include <array>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view Record = "MODEL_PACKAGE_ARTIFACT";
        bool Known(std::string_view line) { return line.substr(0, line.find('\t')) == Record; }
        bool Fail(std::string *error, const char *message)
        { if (error) *error = message; return false; }
        bool Validate(const AssetMetadata &metadata, const ModelGeneratedFile &artifact, std::string *error)
        {
            if (!Project::IsProjectAssetReference(artifact.reference) ||
                NormalizeAssetReference(artifact.reference) != artifact.reference ||
                artifact.reference.find_first_of("\t\r\n") != std::string::npos)
                return Fail(error, "Invalid active model package location.");
            const std::filesystem::path relative(artifact.reference.substr(Project::kProjectAssetScheme.size()));
            if (relative.empty() || relative.has_root_path() || relative.extension() != ".plutomodel" ||
                std::any_of(relative.begin(), relative.end(), [](const auto &part) { return part == "." || part == ".." || part.empty(); }))
                return Fail(error, "Invalid active model package location.");
            ModelAsset package;
            std::string bytes;
            if (ReadModelSourcePackage(metadata, package, error) != ModelSourcePackageStatus::Success ||
                !SerializeModelAsset(package, bytes, error)) return false;
            if (content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size()))) != artifact.digest)
                return Fail(error, "Active model package proof differs from persistent source metadata.");
            return true;
        }
    }
    bool ReadActiveModelPackageArtifact(const AssetMetadata &metadata, ModelGeneratedFile &output, std::string *error)
    {
        const std::string *record = nullptr;
        for (const auto &line : metadata.extensionRecords) if (Known(line))
        {
            if (record) return Fail(error, "Duplicate active model package proof.");
            record = &line;
        }
        if (!record) return Fail(error, "Active model package proof is missing; reimport the source.");
        std::array<std::string_view, 5> fields;
        std::string_view remaining(*record);
        for (std::size_t i = 0; i < fields.size(); ++i)
        {
            const auto at = remaining.find('\t');
            if ((i + 1 == fields.size()) != (at == std::string_view::npos))
                return Fail(error, "Malformed active model package proof.");
            fields[i] = remaining.substr(0, at);
            if (at != std::string_view::npos) remaining.remove_prefix(at + 1);
        }
        content::ContentDigest active, recorded;
        ModelGeneratedFile candidate;
        if (fields[1] != "1" ||
            ReadModelArtifactGeneration(metadata, active, error) != ModelArtifactGenerationStatus::Success ||
            !content::ParseContentDigest(fields[2], recorded) || recorded != active ||
            !content::ParseContentDigest(fields[3], candidate.digest))
            return Fail(error, "Unsupported or inconsistent active model package proof.");
        candidate.reference = fields[4];
        if (!Validate(metadata, candidate, error)) return false;
        output = std::move(candidate);
        if (error) error->clear();
        return true;
    }
    bool WriteActiveModelPackageArtifact(AssetMetadata &metadata, const ModelGeneratedFile &artifact, std::string *error)
    {
        content::ContentDigest generation;
        if (ReadModelArtifactGeneration(metadata, generation, error) != ModelArtifactGenerationStatus::Success ||
            !Validate(metadata, artifact, error)) return false;
        bool found = false;
        for (const auto &record : metadata.extensionRecords) if (Known(record))
        {
            if (found || !record.starts_with("MODEL_PACKAGE_ARTIFACT\t1\t"))
                return Fail(error, "Duplicate or unsupported active model package proof.");
            found = true;
        }
        auto candidate = metadata;
        std::erase_if(candidate.extensionRecords, Known);
        candidate.extensionRecords.push_back(std::string(Record) + "\t1\t" + content::DigestToHex(generation) +
            "\t" + content::DigestToHex(artifact.digest) + "\t" + artifact.reference);
        std::string encoded;
        if (!SerializeAssetMetadata(candidate, encoded, error)) return false;
        metadata = std::move(candidate);
        if (error) error->clear();
        return true;
    }
    bool ReadActiveModelGenerationSnapshot(const Project &project, const std::string &sourceAssetId,
        const std::shared_ptr<const AssetCatalog> &currentCatalog,
        const std::shared_ptr<const AssetStorageMap> &currentStorage,
        ModelGenerationSnapshot &output, std::string *error)
    {
        const auto *source = currentCatalog ? currentCatalog->Find({sourceAssetId, 0}) : nullptr;
        if (!source || source->type != ProjectAssetType::Model || !Project::IsProjectAssetReference(source->location))
            return Fail(error, "Active model source identity is unavailable.");
        AssetMetadata metadata;
        if (LoadAssetMetadata(GetAssetMetadataPath(project.ResolveAssetReference(source->location)), metadata, error) != AssetMetadataStatus::Success)
            return false;
        if (metadata.id != sourceAssetId) return Fail(error, "Active model source metadata has a different identity.");
        content::ContentDigest generation;
        ModelGeneratedFile artifact;
        if (ReadModelArtifactGeneration(metadata, generation, error) != ModelArtifactGenerationStatus::Success ||
            !ReadActiveModelPackageArtifact(metadata, artifact, error)) return false;
        return ReadModelGenerationSnapshot(project, sourceAssetId, generation, artifact,
            currentCatalog, currentStorage, output, error);
    }
}
