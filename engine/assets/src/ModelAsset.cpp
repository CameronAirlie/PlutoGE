#include "PlutoGE/platform/ContentPack.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <stdexcept>

#include <fstream>
#include <algorithm>
#include <sstream>
#include <charconv>
#include <set>
#include <limits>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kHeader = "PLUTOMODEL\t1";

        void SetError(std::string *output, std::string message)
        {
            if (output) *output = std::move(message);
        }
    }

    std::uint64_t MakeModelSubAssetId(ProjectAssetType type, std::string_view name)
    {
        std::uint64_t hash = 14695981039346656037ull;
        const auto typeValue = static_cast<std::uint32_t>(type);
        for (std::size_t byte = 0; byte < sizeof(typeValue); ++byte)
        {
            hash ^= static_cast<unsigned char>((typeValue >> (byte * 8)) & 0xffu);
            hash *= 1099511628211ull;
        }
        for (const unsigned char character : name)
        {
            hash ^= character;
            hash *= 1099511628211ull;
        }
        return hash == 0 ? 1 : hash;
    }

    std::filesystem::path GetModelArtifactDirectory(const Project &project, std::string_view sourceReference)
    {
        const auto sourcePath = project.ResolveAssetReference(sourceReference);
        if (sourcePath.empty()) return {};
        AssetMetadata metadata;
        std::string error;
        const auto status = LoadAssetMetadata(GetAssetMetadataPath(sourcePath), metadata, &error);
        if (status != AssetMetadataStatus::Success && status != AssetMetadataStatus::Missing)
            throw std::runtime_error(error);
        std::filesystem::path selected;
        for (const auto &record : metadata.extensionRecords)
        {
            if (!record.starts_with("MODEL_OUTPUT_DIRECTORY\t")) continue;
            constexpr std::string_view prefix = "MODEL_OUTPUT_DIRECTORY\t1\tproject://";
            if (!selected.empty() || project.GetManifest().assetPipelineVersion < 3 || !record.starts_with(prefix))
                throw std::runtime_error("Unsupported or duplicate model output directory setting.");
            const auto relative = std::filesystem::u8path(record.substr(prefix.size()));
            if (relative.empty() || relative.has_root_path() || record.find('\t', prefix.size()) != std::string::npos || record.find_first_of("\r\n\0", 0, 3) != std::string::npos)
                throw std::runtime_error("Invalid model output directory setting.");
            for (const auto &part : relative)
                if (part == "." || part == ".." || part.empty()) throw std::runtime_error("Model output directory contains traversal.");
            const auto root = std::filesystem::canonical(project.GetAssetDirectoryPath());
            const auto requested = root / relative;
            if (IsAssetInfrastructurePath(std::filesystem::canonical(project.GetRootDirectory()), requested) ||
                !content::ResolveDirectoryForCreation(requested, selected, &error) ||
                !content::IsPathWithinDirectory(selected, root))
                throw std::runtime_error("Model output directory escapes the authored asset root.");
        }
        return selected.empty() ? sourcePath.parent_path() : selected;
    }

    std::filesystem::path GetModelManifestPath(const Project &project, std::string_view sourceReference)
    {
        const auto sourcePath = project.ResolveAssetReference(sourceReference);
        if (sourcePath.empty()) return {};
        return GetModelArtifactDirectory(project, sourceReference) /
               (sourcePath.stem().string() + ".plutomodel");
    }

    std::filesystem::path FindModelManifestPath(const Project &project, std::string_view sourceReference)
    {
        const auto canonicalPath = GetModelManifestPath(project, sourceReference);
        std::error_code error;
        if (content::IsRegularFile(canonicalPath, error)) return canonicalPath;

        const auto sourcePath = project.ResolveAssetReference(sourceReference);
        if (sourcePath.empty()) return canonicalPath;
        const auto legacyPath = project.GetAssetDirectoryPath() / "Imported" / sourcePath.stem() /
                                (sourcePath.stem().string() + ".plutomodel");
        error.clear();
        if (!content::IsRegularFile(legacyPath, error)) return canonicalPath;
        ModelAsset legacy;
        if (!LoadModelAsset(legacyPath.string(), legacy)) return legacyPath;
        AssetMetadata sourceMetadata;
        const auto metadataStatus = LoadAssetMetadata(GetAssetMetadataPath(sourcePath), sourceMetadata);
        if (!legacy.sourceAssetId.empty() && metadataStatus == AssetMetadataStatus::Success)
            return legacy.sourceAssetId == sourceMetadata.id ? legacyPath : canonicalPath;
        if (!legacy.sourceReference.empty() && legacy.sourceReference != sourceReference)
        {
            error.clear();
            if (!std::filesystem::equivalent(project.ResolveAssetReference(legacy.sourceReference), sourcePath, error)) return canonicalPath;
        }
        return legacyPath;
    }

    bool ResolveModelPlacementMesh(const Project &project, std::string_view reference,
                                   std::string &meshReference, std::string &materialBindingReference,
                                   std::string *error)
    {
        if (error) error->clear();
        meshReference.clear(); materialBindingReference.clear();
        if (Project::GetAssetTypeForReference(reference) != ProjectAssetType::Model)
        { SetError(error, "Surface placement requires a model asset."); return false; }
        ModelAsset model;
        if (!LoadModelSourcePackage(project, reference, model, error))
        { SetError(error, "Import the model before placing it in a scene."); return false; }
        const auto object = std::find_if(model.objects.begin(), model.objects.end(), [](const auto &entry)
            { return entry.type == ProjectAssetType::Mesh; });
        if (object == model.objects.end())
        { SetError(error, "The imported model contains no mesh object."); return false; }
        materialBindingReference = object->reference;
        meshReference = object->reference;
        if (!model.sourceReference.empty())
        {
            auto authored = project.ResolveAssetReference(model.sourceReference);
            authored.replace_extension(".plutomesh");
            std::error_code ec;
            if (content::IsRegularFile(authored, ec)) meshReference = project.MakeAssetReference(authored);
        }
        return true;
    }

    bool ParseModelAsset(std::string_view text, ModelAsset &asset, std::string *errorMessage)
    {
        const auto fail = [&](const char *message) { SetError(errorMessage, message); return false; };
        if (text.size() > 16 * 1024 * 1024 || text.find('\0') != std::string_view::npos)
            return fail("Model manifest exceeds its size limit or contains a null byte.");
        std::istringstream input{std::string(text)};
        std::string line;
        const auto readLine = [&]()
        {
            if (!std::getline(input, line)) return false;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        };
        if (!readLine() || line != kHeader) return fail("Invalid or unsupported model manifest header.");
        ModelAsset candidate;
        std::set<std::string> scalars, generatedReferences;
        std::set<std::uint64_t> objectIds;
        while (readLine())
        {
            if (line.find('\r') != std::string::npos) return fail("Invalid carriage return inside a model manifest field.");
            std::vector<std::string> fields;
            std::size_t start = 0;
            for (;;)
            {
                const auto end = line.find('\t', start);
                fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
            const auto &key = fields.front();
            if (key == "SOURCE" || key == "SOURCE_ID" || key == "SOURCE_HASH" || key == "IMPORTER_VERSION")
            {
                if (fields.size() != 2 || !scalars.insert(key).second) return fail("Malformed or duplicate model manifest field.");
                if (key == "SOURCE") candidate.sourceReference = fields[1];
                else if (key == "SOURCE_ID") candidate.sourceAssetId = fields[1];
                else
                {
                    std::uint64_t value = 0;
                    const auto parsed = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), value);
                    if (parsed.ec != std::errc{} || parsed.ptr != fields[1].data() + fields[1].size()) return fail("Invalid model manifest numeric field.");
                    if (key == "SOURCE_HASH") candidate.sourceContentHash = value;
                    else
                    {
                        if (value == 0 || value > std::numeric_limits<std::uint32_t>::max()) return fail("Invalid model importer version.");
                        candidate.importerVersion = static_cast<std::uint32_t>(value);
                    }
                }
            }
            else if (key == "OBJECT")
            {
                if (fields.size() != 5 || fields[4].empty()) return fail("Malformed model object record.");
                ModelSubAsset object;
                const auto parsed = std::from_chars(fields[1].data(), fields[1].data() + fields[1].size(), object.localId);
                if (parsed.ec != std::errc{} || parsed.ptr != fields[1].data() + fields[1].size() ||
                    object.localId == 0 || !objectIds.insert(object.localId).second) return fail("Invalid or duplicate model object ID.");
                object.type = Project::ParseAssetTypeName(fields[2]);
                if ((object.type == ProjectAssetType::Unknown && fields[2] != Project::GetAssetTypeName(ProjectAssetType::Unknown)) || object.type == ProjectAssetType::Count) return fail("Unsupported model object type.");
                object.name = fields[3];
                object.reference = fields[4];
                candidate.objects.push_back(std::move(object));
            }
            else if (key == "OUTPUT_HASH")
            {
                ModelGeneratedFile generated;
                if (fields.size() != 3 || fields[1].empty() || !generatedReferences.insert(fields[1]).second ||
                    !content::ParseContentDigest(fields[2], generated.digest)) return fail("Invalid generated file baseline.");
                generated.reference = fields[1];
                candidate.generatedFiles.push_back(std::move(generated));
            }
            else candidate.extensionRecords.push_back(line);
        }
        if (!scalars.contains("SOURCE")) return fail("Model manifest source record is required.");
        if (!candidate.generatedFiles.empty() && candidate.sourceAssetId.empty())
            return fail("Generated file baselines require an explicit source identity.");
        asset = std::move(candidate);
        if (errorMessage) errorMessage->clear();
        return true;
    }

    bool SerializeModelAsset(const ModelAsset &asset, std::string &text, std::string *errorMessage)
    {
        const auto field = [](std::string_view value)
        { return value.find_first_of("\t\r\n\0", 0, 4) == std::string_view::npos; };
        if (!field(asset.sourceReference) || !field(asset.sourceAssetId))
        { SetError(errorMessage, "Model manifest source fields contain unsupported control characters."); return false; }
        std::ostringstream output;
        output << kHeader << '\n' << "SOURCE\t" << asset.sourceReference << '\n'
               << "SOURCE_ID\t" << asset.sourceAssetId << '\n'
               << "SOURCE_HASH\t" << asset.sourceContentHash << '\n'
               << "IMPORTER_VERSION\t" << asset.importerVersion << '\n';
        for (const auto &object : asset.objects)
        {
            if (!field(object.name) || !field(object.reference))
            { SetError(errorMessage, "Model object fields contain unsupported control characters."); return false; }
            output << "OBJECT\t" << object.localId << '\t' << Project::GetAssetTypeName(object.type) << '\t'
                   << object.name << '\t' << object.reference << '\n';
        }
        for (const auto &generated : asset.generatedFiles)
        {
            if (!field(generated.reference)) { SetError(errorMessage, "Invalid generated file reference."); return false; }
            output << "OUTPUT_HASH\t" << generated.reference << '\t' << content::DigestToHex(generated.digest) << '\n';
        }
        for (const auto &record : asset.extensionRecords)
        {
            if (record.find_first_of("\r\n\0", 0, 3) != std::string::npos)
            { SetError(errorMessage, "Model extension records must contain a single line."); return false; }
            output << record << '\n';
        }
        auto bytes = output.str();
        ModelAsset validated;
        if (!ParseModelAsset(bytes, validated, errorMessage)) return false;
        text = std::move(bytes);
        return true;
    }
    bool SaveModelAsset(const std::string &path, const ModelAsset &asset, std::string *errorMessage)
    {
        std::string bytes;
        if (!SerializeModelAsset(asset, bytes, errorMessage)) return false;
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) { SetError(errorMessage, "Failed to finish model asset: " + path); return false; }
        return true;
    }
    bool LoadModelAsset(const std::string &path, ModelAsset &asset, std::string *errorMessage)
    {
        std::error_code error;
        const auto size = content::FileSize(path, error);
        if (error || size > 16 * 1024 * 1024) { SetError(errorMessage, "Cannot read bounded model manifest: " + path); return false; }
        content::InputFile input(path, std::ios::binary);
        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
        { SetError(errorMessage, "Cannot finish reading model manifest: " + path); return false; }
        return ParseModelAsset(bytes, asset, errorMessage);
    }
}
