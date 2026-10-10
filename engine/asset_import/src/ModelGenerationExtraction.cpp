#include "PlutoGE/asset_import/ModelGenerationExtraction.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/AssetMigrationSerialization.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/assets/AssetReferences.h"
#include "PlutoGE/assets/ModelGenerationSnapshot.h"
#include "PlutoGE/platform/FilesystemPaths.h"
#include <fstream>
#include <map>
#include <span>
#include <set>
#include <algorithm>
#include <stdexcept>

namespace PlutoGE::assetimport
{
    ModelGenerationExtractionResult::ModelGenerationExtractionResult() = default;
    ModelGenerationExtractionResult::~ModelGenerationExtractionResult() = default;
    ModelGenerationExtractionResult::ModelGenerationExtractionResult(ModelGenerationExtractionResult &&) noexcept = default;
    ModelGenerationExtractionResult &ModelGenerationExtractionResult::operator=(ModelGenerationExtractionResult &&) noexcept = default;

    namespace
    {
        bool Absent(const std::filesystem::path &path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            return (!error || error == std::errc::no_such_file_or_directory) &&
                status.type() == std::filesystem::file_type::not_found;
        }
        std::string ReadMaterial(const std::filesystem::path &path)
        {
            const auto size = std::filesystem::file_size(path);
            if (size > 16 * 1024 * 1024) throw std::runtime_error("Accepted material exceeds extraction limits.");
            std::ifstream input(path, std::ios::binary);
            std::string bytes(static_cast<std::size_t>(size), '\0');
            if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("Cannot read accepted material for extraction.");
            return bytes;
        }
        struct ExtractedObject
        {
            assets::ModelSubAsset source;
            assets::AssetMetadata metadata;
            std::filesystem::path relative;
            std::string logical;
        };
    }

    bool VerifyModelGenerationExtraction(const assets::Project &project,
        const ModelGenerationExtractionResult &extracted, std::string *errorMessage)
    {
        try
        {
            if (!extracted.publicationLock || extracted.files.empty() || extracted.files.size() != extracted.changedAssets.size() * 2)
                throw std::runtime_error("Authored generation publication requires its retained writer lock and complete file proofs.");
            const auto root=std::filesystem::canonical(project.GetAssetDirectoryPath());
            std::set<std::string> references;
            for (const auto &file : extracted.files)
            {
                if (!file.reference.starts_with(extracted.directoryReference + "/") || !references.insert(file.reference).second)
                    throw std::runtime_error("Authored generation proof has an invalid or duplicate output.");
                const auto path=project.ResolveAssetReference(file.reference);
                const auto canonical=std::filesystem::canonical(path);
                if (!content::IsPathWithinDirectory(canonical, root) || assets::IsAssetInfrastructurePath(project.GetRootDirectory(), canonical))
                    throw std::runtime_error("Authored generation output escapes the project asset root.");
                content::ContentDigest digest;
                if (!content::HashFileContent(path, digest, errorMessage) || digest != file.digest)
                    throw std::runtime_error("Authored generation output changed before scene publication.");
            }
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { if (errorMessage) *errorMessage=exception.what(); return false; }
    }

    bool ExtractStaticModelGeneration(assets::Project &project,
        const assets::StaticModelInstanceState &accepted, const std::string &directoryReference,
        ModelGenerationExtractionResult &output, std::string *errorMessage)
    {
        const auto fail = [&](const std::string &message) { if (errorMessage) *errorMessage = message; return false; };
        try
        {
            if (!assets::Project::IsProjectAssetReference(directoryReference))
                return fail("Generation extraction requires an authored project directory.");
            auto lock = std::make_unique<ProjectImportLock>();
            if (!lock->TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
            const auto root = std::filesystem::canonical(project.GetAssetDirectoryPath());
            const auto relativeText = directoryReference.substr(assets::Project::kProjectAssetScheme.size());
            const auto relative = std::filesystem::path(relativeText);
            if (relative.empty() || relative.has_root_path() || relative.generic_string() != relativeText || relative.lexically_normal() != relative ||
                relative.generic_string().back() == '/') return fail("Extraction directory must be a normalized relative project location.");
            const auto destination = (root / relative).lexically_normal();
            if (!content::IsPathWithinDirectory(destination, root) || assets::IsAssetInfrastructurePath(project.GetRootDirectory(), destination))
                return fail("Generation extraction must remain in an authored asset location.");
            std::filesystem::path parent;
            if (!content::ResolveDirectoryForCreation(destination.parent_path(), parent, errorMessage) ||
                !content::IsPathWithinDirectory(parent, root, true)) return fail("Extraction directory escapes the asset root.");
            if (!Absent(destination)) return fail("Generation extraction directory already exists.");
            if (!ImportFileTransaction::Recover(project.GetRootDirectory(), root, errorMessage)) return false;
            auto scanProject = project;
            const assets::AssetScanOptions scanOptions{.createMissingMetadata=false, .hashContent=false, .collectDependencies=false,
                .allowUnavailableImportedStorage=true};
            assets::AssetDatabase database;
            if (!database.Scan(scanProject, scanOptions, errorMessage)) return false;
            assets::ModelGenerationSnapshot generation;
            const auto &owner = accepted.accepted.layout.sourceAssetId;
            if (!assets::ReadModelGenerationSnapshot(project, owner, accepted.artifactGenerationKey, accepted.packageArtifact,
                    database.GetCatalog(), database.GetStorageMap(), generation, errorMessage)) return false;
            assets::StaticModelGenerationSnapshot baseline;
            if (!assets::PrepareStaticModelGenerationSnapshot(project, generation, baseline, errorMessage) ||
                !assets::ValidateStaticModelInstanceBaseline(accepted, baseline, errorMessage)) return false;
            assets::AssetManager reader(assets::AssetManager::ResourceLifetime::Scoped, nullptr, owner);
            reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory, 5);
            reader.SetAssetSnapshot(generation.catalog, generation.storage);
            reader.SetLogicalReferenceTypes({assets::ProjectAssetType::Mesh, assets::ProjectAssetType::Material,
                assets::ProjectAssetType::Texture, assets::ProjectAssetType::ShaderGraph});
            ModelGenerationExtractionResult candidate;
            candidate.directoryReference = directoryReference;
            std::vector<ExtractedObject> objects;
            std::map<std::string, std::size_t> byLocation;
            std::set<std::string> createdIds;
            for (const auto &object : generation.package.objects)
            {
                if (object.type != assets::ProjectAssetType::Mesh && object.type != assets::ProjectAssetType::Material &&
                    object.type != assets::ProjectAssetType::Texture) return fail("Static unpacking encountered an unsupported owned asset type.");
                std::string source;
                if (!assets::SerializeAssetReference({owner, object.localId}, source, errorMessage)) return false;
                auto [entry, inserted] = byLocation.emplace(object.reference, objects.size());
                if (inserted)
                {
                    ExtractedObject extracted;
                    extracted.source = object;
                    extracted.metadata.id = assets::GenerateAssetId();
                    if (!createdIds.insert(extracted.metadata.id).second || database.GetCatalog()->Find({extracted.metadata.id, 0})) return fail("New authored asset identity already exists; retry extraction.");
                    extracted.metadata.ownership = assets::AssetOwnership::Authored;
                    extracted.metadata.extensionRecords = {"EXTRACTED_FROM\t" + source,
                        "EXTRACTED_GENERATION\t" + content::DigestToHex(accepted.artifactGenerationKey)};
                    const auto extension = std::filesystem::path(object.reference).extension();
                    extracted.relative = relative / ("Object_" + std::to_string(object.localId) + extension.string());
                    if (!assets::SerializeAssetReference({extracted.metadata.id, 0}, extracted.logical, errorMessage)) return false;
                    objects.push_back(std::move(extracted));
                }
                if (objects[entry->second].source.type != object.type) return fail("Shared accepted location has conflicting asset types.");
                candidate.references.emplace(source, objects[entry->second].logical);
            }
            const auto remap = [&](const std::string &reference)
            {
                if (reference.empty()) return reference;
                const auto persisted = reader.PersistAssetPath(reference);
                if (persisted.empty()) throw std::runtime_error("Accepted dependency has no exact retained route.");
                if (const auto found = candidate.references.find(persisted); found != candidate.references.end()) return found->second;
                assets::AssetReference identity;
                if (assets::ParseAssetReference(persisted, identity) && identity.assetId == owner)
                    throw std::runtime_error("Accepted dependency is absent from the extracted package.");
                return persisted;
            };
            ImportFileTransaction transaction(project.GetRootDirectory());
            std::vector<std::filesystem::path> outputs;
            for (const auto &object : objects)
            {
                const auto staged = transaction.GetOutputRoot() / object.relative;
                std::filesystem::create_directories(staged.parent_path());
                const auto original = std::filesystem::path(reader.ResolveAssetPath(object.source.reference));
                if (original.empty()) return fail("Accepted extraction object has no storage route.");
                const auto proof = std::find_if(generation.package.generatedFiles.begin(), generation.package.generatedFiles.end(),
                    [&](const auto &file) { return file.reference == object.source.reference; });
                if (proof == generation.package.generatedFiles.end()) return fail("Accepted extraction object has no content proof.");
                const auto source = transaction.GetOutputRoot() / ".accepted-inputs" /
                    (std::to_string(object.source.localId) + original.extension().string());
                std::filesystem::create_directories(source.parent_path());
                std::filesystem::copy_file(original, source, std::filesystem::copy_options::none);
                content::ContentDigest copied;
                if (!content::HashFileContent(source, copied, errorMessage) || copied != proof->digest)
                    return fail("Copied accepted extraction bytes differ from their generation proof.");
                if (object.source.type == assets::ProjectAssetType::Mesh)
                {
                    render::MeshConfig config;
                    std::vector<std::string> materials;
                    assets::MeshAssetMetadata metadata;
                    if (!reader.LoadMeshAssetData(source.string(), config, materials, metadata, errorMessage)) return false;
                    if (metadata.sourceAssetId != owner || metadata.sourceObjectId != object.source.localId)
                        return fail("Accepted mesh provenance differs from its retained identity.");
                    for (auto &material : materials) material = remap(material);
                    metadata.sourceAssetReference.clear(); metadata.sourceAssetId.clear(); metadata.sourceObjectId=0;
                    if (!reader.SaveMeshAsset(staged.string(), config, materials, errorMessage, metadata)) return false;
                }
                else if (object.source.type == assets::ProjectAssetType::Material)
                {
                    auto bytes = ReadMaterial(source);
                    assets::MigrationReferenceFile plan;
                    plan.reference = object.source.reference;
                    plan.contentHash = content::HashContent(std::as_bytes(std::span(bytes.data(), bytes.size())));
                    const auto scan = assets::ScanAssetReferences(source, {}, root);
                    if (scan.cancelled || !scan.errors.empty()) return fail("Accepted material has unsupported or invalid reference fields.");
                    for (const auto &occurrence : scan.occurrences)
                    {
                        const auto replacement = remap(occurrence.reference);
                        if (replacement != occurrence.reference) plan.mappings.push_back({occurrence.reference, replacement, occurrence.line});
                    }
                    std::string converted;
                    if (!assets::PrepareMaterialReferenceMigration(plan, bytes, converted, errorMessage)) return false;
                    std::ofstream file(staged, std::ios::binary);
                    file.write(converted.data(), static_cast<std::streamsize>(converted.size())); file.close();
                    if (!file) return fail("Cannot stage unpacked material.");
                }
                else std::filesystem::copy_file(source, staged, std::filesystem::copy_options::none);
                if (!assets::SaveAssetMetadata(assets::GetAssetMetadataPath(staged), object.metadata,
                        assets::AssetMetadataWriteMode::CreateOnly, errorMessage)) return false;
                outputs.push_back(object.relative); outputs.push_back(assets::GetAssetMetadataPath(object.relative));
                candidate.changedAssets.push_back("project://" + object.relative.generic_string());
            }
            for (const auto &file : outputs)
            {
                content::ContentDigest digest;
                if (!content::HashFileContent(transaction.GetOutputRoot() / file, digest, errorMessage)) return false;
                candidate.files.push_back({"project://" + file.generic_string(), digest});
            }
            assets::ModelGenerationSnapshot verified;
            if (!assets::ReadModelGenerationSnapshot(project, owner, accepted.artifactGenerationKey, accepted.packageArtifact,
                    database.GetCatalog(), database.GetStorageMap(), verified, errorMessage)) return false;
            if (!Absent(destination)) return fail("Extraction destination appeared during preparation; retry.");
            if (!transaction.Publish(root, outputs, errorMessage) || !database.Scan(scanProject, scanOptions, errorMessage)) return false;
            for (const auto &object : objects)
            {
                const auto *installed = database.GetCatalog()->Find({object.metadata.id, 0});
                if (!installed || installed->ownership != assets::AssetOwnership::Authored ||
                    installed->location != "project://" + object.relative.generic_string()) return fail("Unpacked authored asset failed catalog validation.");
            }
            candidate.catalog = database.GetCatalog(); candidate.storage = database.GetStorageMap();
            if (!transaction.Accept(errorMessage)) return false;
            candidate.publicationLock = std::move(lock);
            output = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception) { return fail(std::string("Cannot extract accepted model generation: ") + exception.what()); }
    }
}
