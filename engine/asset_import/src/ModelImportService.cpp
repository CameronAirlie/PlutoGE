#include "PlutoGE/assets/AssetPathPolicy.h"
#include "ModelCacheRestore.h"
#include "ModelGenerationPublication.h"
#include "PlutoGE/assets/ModelActiveGeneration.h"
#include "PlutoGE/assets/ModelArtifactStorage.h"
#include "PlutoGE/assets/ModelNodeCorrespondence.h"
#include "PlutoGE/assets/ModelHierarchyAsset.h"
#include "ModelOutputProvenance.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ModelImportService.h"
#include "PlutoGE/asset_import/ModelSourceSnapshot.h"
#include "PlutoGE/asset_import/ProjectImportLock.h"
#include "PlutoGE/asset_import/ImportedTextureWriter.h"
#include "PlutoGE/asset_import/ModelArtifactSettings.h"
#include "PlutoGE/asset_import/ImportFileTransaction.h"
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/MaterialAssetSerialization.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/assets/ModelSourcePackage.h"
#include "PlutoGE/import/MeshImporter.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace PlutoGE::assetimport
{
    namespace
    {
        std::string LogicalReference(const assets::AssetReference &reference)
        {
            std::string text, error;
            if (!assets::SerializeAssetReference(reference, text, &error)) throw std::runtime_error(error);
            return text;
        }

        bool PrepareImportReader(const assets::Project &project, assets::AssetManager &reader, std::string *errorMessage)
        {
            reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory);
            auto inventory = project;
            assets::AssetDatabase database;
            if (!database.Scan(inventory, assets::AssetScanOptions{.createMissingMetadata=false, .hashContent=false, .collectDependencies=false, .allowUnavailableImportedStorage=true}, errorMessage)) return false;
            reader.SetAssetSnapshot(database.GetCatalog(), database.GetStorageMap());
            return true;
        }

        bool ReadEffectiveSettings(const assets::Project &project, const assets::AssetMetadata &metadata,
                                   const assets::ModelAsset *previous, assets::AssetManager &reader,
                                   assets::ModelImportSettings &settings, std::string *errorMessage)
        {
            const auto status = assets::ReadModelImportSettings(metadata, settings, errorMessage);
            if (status != assets::ModelImportSettingsStatus::Success && status != assets::ModelImportSettingsStatus::Missing) return false;
            if (project.GetManifest().assetPipelineVersion < 2 && status == assets::ModelImportSettingsStatus::Success)
            {
                if (errorMessage) *errorMessage = "Persisted import settings require project format version 2.";
                return false;
            }
            if (!settings.nodeAliases.empty() && project.GetManifest().assetPipelineVersion < 5)
            {
                if (errorMessage) *errorMessage = "Reviewed node repairs require project format version 5.";
                return false;
            }
            if (status == assets::ModelImportSettingsStatus::Missing && previous &&
                project.GetManifest().assetPipelineVersion >= 2 && previous->importerVersion >= 2)
            {
                if (errorMessage) *errorMessage = "Persistent model correspondence is missing from source metadata. Restore it before reimporting.";
                return false;
            }
            if (status == assets::ModelImportSettingsStatus::Success && previous &&
                project.GetManifest().assetPipelineVersion >= 2 && previous->importerVersion >= 2 &&
                !ValidateModelSourceCorrespondence(*previous, settings, errorMessage)) return false;
            if (status == assets::ModelImportSettingsStatus::Missing && previous)
                for (const auto &object : previous->objects)
                    if (object.type == assets::ProjectAssetType::Mesh)
                    {
                        settings.meshOptions = reader.GetMeshAssetMetadata(object.reference).importOptions;
                        break;
                    }
            return true;
        }

        std::mutex ImportPublicationMutex;

        std::string SafeName(std::string_view text)
        {
            std::string name;
            for (const unsigned char character : text)
            {
                if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                    (character >= '0' && character <= '9') || character == '_' || character == '-')
                    name += static_cast<char>(character);
                else if (!name.empty() && name.back() != '_') name += '_';
            }
            while (!name.empty() && name.back() == '_') name.pop_back();
            return name;
        }

        bool WriteTextureTga(const std::filesystem::path &path, const ImportedTextureData &texture, std::string *errorMessage)
        {
            std::ofstream output(path, std::ios::binary);
            if (!output || !WriteImportedTextureTga(output, texture, errorMessage)) return false;
            output.close();
            if (!output)
            {
                if (errorMessage) *errorMessage = "Cannot finish texture output: " + path.string();
                return false;
            }
            return true;
        }

    }

    bool ModelImportService::ReadOptions(const assets::Project &project, std::string_view sourceReference,
                                         MeshImportOptions &options, std::string *errorMessage) const
    {
        try
        {
            if (!assets::Project::IsProjectAssetReference(sourceReference) ||
                assets::Project::GetAssetTypeForReference(sourceReference) != assets::ProjectAssetType::Model)
                throw std::runtime_error("A project model source is required.");
            const auto source = project.ResolveAssetReference(sourceReference);
            const auto relative = source.lexically_relative(project.GetAssetDirectoryPath());
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                throw std::runtime_error("Model source escapes the asset directory.");
            if (assets::IsAssetInfrastructurePath(project.GetRootDirectory(), source)) throw std::runtime_error("Engine infrastructure cannot be imported as a source asset.");
            if (!std::filesystem::is_regular_file(source)) throw std::runtime_error("Model source does not exist.");
            assets::AssetMetadata metadata;
            const auto metadataStatus = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(source), metadata, errorMessage);
            if (metadataStatus != assets::AssetMetadataStatus::Success && metadataStatus != assets::AssetMetadataStatus::Missing) return false;
            assets::ModelAsset previous;
            const auto manifest = assets::FindModelManifestPath(project, std::string(sourceReference));
            const auto packageStatus = assets::ReadModelSourcePackage(metadata, previous, errorMessage);
            if (packageStatus != assets::ModelSourcePackageStatus::Success && packageStatus != assets::ModelSourcePackageStatus::Missing) return false;
            const bool hasPrevious = packageStatus == assets::ModelSourcePackageStatus::Success || std::filesystem::exists(manifest);
            if (packageStatus == assets::ModelSourcePackageStatus::Missing && hasPrevious && !assets::LoadModelAsset(manifest.string(), previous, errorMessage)) return false;
            assets::AssetManager reader;
            reader.SetProjectContext(project.GetRootDirectory().string(), project.GetManifest().assetDirectory);
            assets::ModelImportSettings settings;
            if (!ReadEffectiveSettings(project, metadata, hasPrevious ? &previous : nullptr, reader, settings, errorMessage)) return false;
            options = settings.meshOptions;
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
    }

    bool ModelImportService::Import(assets::Project &project, const ModelImportRequest &request,
                                    ModelImportResult &result, std::string *errorMessage) const
    {
        // Legacy package publication changes multiple files; serialize all such
        // writers until generation-based cache publication replaces this path.
        std::lock_guard lock(ImportPublicationMutex);
        try
        {
            auto progress = [&](std::string_view stage)
            {
                if (request.stop.stop_requested()) throw std::runtime_error("Model import cancelled.");
                if (request.progress) request.progress(stage);
                if (request.stop.stop_requested()) throw std::runtime_error("Model import cancelled.");
            };
            progress("Validating source");
            if (!assets::Project::IsProjectAssetReference(request.sourceReference) ||
                assets::Project::GetAssetTypeForReference(request.sourceReference) != assets::ProjectAssetType::Model)
                throw std::runtime_error("A project model source is required.");
            const auto sourcePath = project.ResolveAssetReference(request.sourceReference);
            const auto assetRoot = project.GetAssetDirectoryPath();
            ProjectImportLock projectLock;
            if (!projectLock.TryAcquire(project.GetRootDirectory(), errorMessage)) return false;
            if (!ImportFileTransaction::Recover(project.GetRootDirectory(), assetRoot, errorMessage)) return false;
            const auto sourceRelative = sourcePath.lexically_relative(assetRoot);
            if (sourceRelative.empty() || sourceRelative.is_absolute()) throw std::runtime_error("Source is outside the asset root.");
            for (const auto &part : sourceRelative)
                if (part == "..") throw std::runtime_error("Source is outside the asset root.");
            auto projectReference = [&](const std::filesystem::path &path)
            {
                const auto relative = path.lexically_normal().lexically_relative(assetRoot.lexically_normal());
                if (relative.empty() || relative.is_absolute()) throw std::runtime_error("Invalid project artifact location.");
                for (const auto &part : relative)
                    if (part == "..") throw std::runtime_error("Artifact escapes the asset root.");
                const auto utf8 = relative.generic_u8string();
                return std::string(assets::Project::kProjectAssetScheme) +
                       std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size());
            };
            if (assets::IsAssetInfrastructurePath(project.GetRootDirectory(), sourcePath)) throw std::runtime_error("Engine infrastructure cannot be imported as a source asset.");
            if (!std::filesystem::is_regular_file(sourcePath)) throw std::runtime_error("Model source does not exist.");
            assets::AssetMetadata sourceMetadata;
            const auto metadataPath = assets::GetAssetMetadataPath(sourcePath);
            const auto metadataStatus = assets::LoadAssetMetadata(metadataPath, sourceMetadata, errorMessage);
            if (metadataStatus == assets::AssetMetadataStatus::Missing)
            {
                sourceMetadata.id = assets::GenerateAssetId();
                if (!assets::SaveAssetMetadata(metadataPath, sourceMetadata, assets::AssetMetadataWriteMode::CreateOnly, errorMessage))
                    return false;
            }
            else if (metadataStatus != assets::AssetMetadataStatus::Success) return false;
            std::vector<ArtifactInput> authoredInputs;
            ArtifactInput sourceSettings{.identity = "source-metadata", .path = metadataPath};
            if (!content::HashFileContent(metadataPath, sourceSettings.digest, errorMessage)) return false;
            authoredInputs.push_back(std::move(sourceSettings));
            const auto sourceHash = assets::AssetDatabase::HashFile(sourcePath);
            assets::AssetManager reader;
            if (!PrepareImportReader(project, reader, errorMessage)) return false;
            assets::ModelAsset previous;
            auto previousManifest = assets::FindModelManifestPath(project, request.sourceReference);
            const auto packageStatus = assets::ReadModelSourcePackage(sourceMetadata, previous, errorMessage);
            if (packageStatus != assets::ModelSourcePackageStatus::Success && packageStatus != assets::ModelSourcePackageStatus::Missing) return false;
            bool hasPrevious = packageStatus == assets::ModelSourcePackageStatus::Success || assets::LoadModelAsset(previousManifest.string(), previous);
            if (!hasPrevious && project.GetManifest().assetPipelineVersion >= 2)
                for (std::filesystem::recursive_directory_iterator iterator(assetRoot), end; iterator != end; ++iterator)
                {
                    const auto &entry = *iterator;
                    if (assets::IsAssetInfrastructurePath(project.GetRootDirectory(), entry.path()))
                    {
                        iterator.disable_recursion_pending();
                        continue;
                    }
                    if (!entry.is_regular_file() || entry.path().extension() != ".plutomodel") continue;
                    assets::ModelAsset candidate;
                    if (!assets::LoadModelAsset(entry.path().string(), candidate) || candidate.sourceAssetId != sourceMetadata.id) continue;
                    if (hasPrevious) throw std::runtime_error("Multiple prior model manifests require correspondence repair.");
                    previousManifest = entry.path();
                    previous = std::move(candidate);
                    hasPrevious = true;
                }
            if (hasPrevious && !previous.sourceAssetId.empty() && previous.sourceAssetId != sourceMetadata.id)
                throw std::runtime_error("Model output package belongs to another source identity.");
            std::unordered_set<std::string> previousGeneratedMaterials;
            std::unordered_set<std::string> previousOutputs;
            std::vector<std::string> previousBindings;
            if (hasPrevious)
            {
                assets::ModelHierarchyArtifact hierarchyArtifact;
                const auto hierarchyStatus = assets::ReadModelHierarchyArtifact(previous, hierarchyArtifact, errorMessage);
                if (hierarchyStatus != assets::ModelHierarchyArtifactStatus::Success && hierarchyStatus != assets::ModelHierarchyArtifactStatus::Missing) return false;
                if (hierarchyStatus == assets::ModelHierarchyArtifactStatus::Success) previousOutputs.insert(hierarchyArtifact.reference);
                previousOutputs.insert(projectReference(previousManifest));
                for (const auto &object : previous.objects)
                {
                    previousOutputs.insert(projectReference(project.ResolveAssetReference(object.reference)));
                    if (object.type == assets::ProjectAssetType::Material)
                    {
                        previousGeneratedMaterials.insert(object.reference);
                        previousGeneratedMaterials.insert(LogicalReference({sourceMetadata.id, object.localId}));
                    }
                    if (object.type == assets::ProjectAssetType::Mesh && previousBindings.empty())
                    {
                        const auto overridePath = std::filesystem::path(project.ResolveAssetReference(object.reference)).concat(".materials");
                        if (std::filesystem::is_regular_file(overridePath))
                        {
                            ArtifactInput overrideInput{.identity = "material-overrides/" + object.reference, .path = overridePath};
                            if (!content::HashFileContent(overridePath, overrideInput.digest, errorMessage)) return false;
                            authoredInputs.push_back(std::move(overrideInput));
                        }
                        previousBindings = reader.GetMeshAssetMaterialReferences(object.reference);
                    }
                }
            }

            if (UsesLibraryModelStorage(project) && hasPrevious && previousBindings.empty())
            {
                for (const auto &object : previous.objects)
                    if (object.type == assets::ProjectAssetType::Material)
                        previousBindings.push_back(LogicalReference({sourceMetadata.id, object.localId}));
                for (const auto &object : previous.objects)
                    if (object.type == assets::ProjectAssetType::Mesh)
                    {
                        std::ifstream input(reader.GetMeshAssetMaterialOverridePath(object.reference));
                        if (input)
                        {
                            std::vector<std::string> overrides;
                            std::string line;
                            while (std::getline(input, line))
                            {
                                if (!line.empty() && line.back() == '\r') line.pop_back();
                                overrides.push_back(std::move(line));
                            }
                            if (overrides.size() != previousBindings.size())
                                throw std::runtime_error("Authored material override count conflicts with the source package.");
                            previousBindings = std::move(overrides);
                        }
                        break;
                    }
            }
            const bool persistentSettings = project.GetManifest().assetPipelineVersion >= 2;
            assets::ModelImportSettings settings;
            if (!ReadEffectiveSettings(project, sourceMetadata, hasPrevious ? &previous : nullptr, reader, settings, errorMessage)) return false;
            if (request.options) settings.meshOptions = *request.options;
            const auto importOptions = settings.meshOptions;
            if (hasPrevious && !ValidateGeneratedFileBaselines(project, previous, settings, reader, errorMessage, UsesLibraryModelStorage(project) && std::any_of(sourceMetadata.extensionRecords.begin(), sourceMetadata.extensionRecords.end(), [](const auto &record) { return record.starts_with("MODEL_ARTIFACT\t"); }))) return false;
            if (hasPrevious && !request.forceReimport)
            {
                const auto restored = RestoreCachedModel({project, request, sourceMetadata, importOptions, previous,
                    previousManifest, previousBindings, previousOutputs, authoredInputs, reader.GetAssetCatalog(), progress}, result, errorMessage);
                if (restored == ModelCacheRestoreStatus::Restored) return true;
                if (restored == ModelCacheRestoreStatus::Failed) return false;
            }
            if (persistentSettings) assets::BeginModelObjectImport(settings);
            progress("Parsing model");
            ModelSourceSnapshot snapshot;
            if (!ReadModelSourceSnapshot(sourcePath, importOptions, snapshot, errorMessage)) return false;
            snapshot.inputs.insert(snapshot.inputs.end(), authoredInputs.begin(), authoredInputs.end());
            auto &imported = snapshot.imported;
            std::vector<assets::ModelNodeIdentity> nodeIdentities;
            if (UsesLibraryModelStorage(project) &&
                !assets::ReconcileModelNodeIdentities(imported.hierarchy, settings, nodeIdentities, errorMessage)) return false;
            const bool hasMesh = !imported.meshData.vertices.empty() && !imported.meshData.indices.empty();
            if (!hasMesh && imported.animations.empty() && imported.textures.empty() && imported.materials.size() <= 1)
                throw std::runtime_error("Source contains no importable objects.");

            ImportFileTransaction transaction(project.GetRootDirectory());
            assets::AssetManager writer;
            writer.SetProjectContext(transaction.GetOutputRoot().string(), ".");
            const auto importDirectory = assets::GetModelArtifactDirectory(project, request.sourceReference);
            std::vector<std::filesystem::path> outputs;
            auto stage = [&](const std::filesystem::path &destination)
            {
                const auto relative = destination.lexically_relative(assetRoot);
                if (relative.empty() || relative.is_absolute()) throw std::runtime_error("Invalid import output location.");
                for (const auto &part : relative)
                    if (part == "..") throw std::runtime_error("Import output escapes asset root.");
                const auto staged = transaction.GetOutputRoot() / relative;
                std::filesystem::create_directories(staged.parent_path());
                outputs.push_back(relative);
                return staged;
            };
            assets::ModelAsset model{
                .sourceReference = request.sourceReference,
                .sourceAssetId = sourceMetadata.id,
                .sourceContentHash = sourceHash,
                .importerVersion = kModelArtifactVersion,
            };
            std::set<std::uint64_t> objectIds;
            auto addObject = [&](assets::ProjectAssetType type, const std::string &name, const std::string &reference,
                                 const std::string &sourceKey)
            {
                auto id = assets::MakeModelSubAssetId(type, name);
                if (persistentSettings)
                {
                    std::uint64_t legacyId = 0;
                    for (const auto &object : previous.objects)
                        if (object.type == type && (object.name == name || type == assets::ProjectAssetType::Mesh))
                        {
                            if (legacyId != 0) throw std::runtime_error("Ambiguous legacy model correspondence: " + name);
                            legacyId = object.localId;
                        }
                    id = assets::ResolveModelObjectId(settings, sourceKey, legacyId, errorMessage);
                    if (id == 0) throw std::runtime_error(errorMessage ? *errorMessage : "Cannot assign model object identity.");
                }
                if (!objectIds.insert(id).second)
                    throw std::runtime_error("Ambiguous imported object name: " + name + ". Rename duplicate source objects before importing.");
                model.objects.push_back({id, type, name, reference});
                return id;
            };
            auto dependencyReference = [&](std::uint64_t id, const std::string &location)
            {
                return persistentSettings ? LogicalReference({sourceMetadata.id, id}) : location;
            };
            std::vector<std::string> textureReferences;
            for (std::size_t index = 0; index < imported.textures.size(); ++index)
            {
                progress("Writing textures");
                const auto &texture = imported.textures[index];
                std::filesystem::path destination;
                if (!texture.sourcePath.empty() && std::filesystem::is_regular_file(texture.sourcePath))
                {
                    const auto texturePath = std::filesystem::path(texture.sourcePath);
                    std::error_code relativeError;
                    const auto relative = std::filesystem::relative(texturePath, importDirectory, relativeError);
                    bool inPackage = !relativeError && !relative.empty() && !relative.is_absolute();
                    for (const auto &part : relative) if (part == "..") inPackage = false;
                    if (inPackage) destination = texturePath;
                    else
                    {
                        destination = importDirectory / "Textures" / ("T_" + std::to_string(index) + "_" + texturePath.filename().string());
                        std::filesystem::copy_file(texturePath, stage(destination));
                    }
                }
                else
                {
                    destination = importDirectory / "Textures" / ("T_" + std::to_string(index) + ".tga");
                    if (!WriteTextureTga(stage(destination), texture, errorMessage)) return false;
                }
                const auto reference = projectReference(destination);
                textureReferences.push_back(reference);
                const auto name = texture.sourcePath.empty() ? "Texture " + std::to_string(index) : std::filesystem::path(texture.sourcePath).stem().string();
                const auto textureKey = GetModelTextureSourceKey(sourcePath, texture, index);
                const auto textureId = addObject(assets::ProjectAssetType::Texture, name, reference, textureKey);
                textureReferences.back() = dependencyReference(textureId, reference);
            }
            auto textureReference = [&](int index)
            {
                return index >= 0 && static_cast<std::size_t>(index) < textureReferences.size() ? textureReferences[index] : std::string{};
            };
            std::vector<std::string> materialReferences;
            std::vector<std::uint64_t> materialIds;
            if (hasMesh || imported.materials.size() > 1)
            {
                for (std::size_t index = 0; index < imported.materials.size(); ++index)
                {
                    progress("Writing materials");
                    const auto &material = imported.materials[index];
                    render::MaterialConfig config;
                    config.color = material.color;
                    config.surfaceType = material.surfaceType;
                    config.alphaMode = material.alphaMode;
                    config.alphaCutoff = material.alphaCutoff;
                    config.castsShadow = material.castsShadow;
                    config.twoSided = material.twoSided;
                    config.metallic = material.metallic;
                    config.roughness = material.roughness;
                    config.emission = material.emission;
                    config.emissionTexCoord = material.emissionTexCoord;
                    config.subsurface = material.subsurface;
                    config.subsurfaceColor = material.subsurfaceColor;
                    config.subsurfaceRadius = material.subsurfaceRadius;
                    config.transmission = material.transmission;
                    config.ior = material.ior;
                    config.thickness = material.thickness;
                    config.attenuationColor = material.attenuationColor;
                    config.attenuationDistance = material.attenuationDistance;
                    config.flipNormalY = material.flipNormalY;
                    assets::MaterialTextureReferences textures{
                        .albedo = textureReference(material.albedoTextureIndex),
                        .normal = textureReference(material.normalTextureIndex),
                        .emission = textureReference(material.emissionTextureIndex),
                    };
                    textures.roughness = textureReference(material.metallicRoughnessTextureIndex);
                    if (!textures.roughness.empty())
                    {
                        config.roughnessTextureChannel = render::TextureChannel::Green;
                        if (material.metallicRoughnessTextureHasMetallicChannel)
                        {
                            textures.metallic = textures.roughness;
                            config.metallicTextureChannel = render::TextureChannel::Blue;
                        }
                    }
                    const auto name = "M_" + sourcePath.stem().string() + "_" + std::to_string(index);
                    const auto destination = importDirectory / (name + ".plutomaterial");
                    std::ofstream output(stage(destination));
                    if (!assets::WriteMaterialAsset(output, config, textures, errorMessage)) return false;
                    output.close();
                    if (!output) throw std::runtime_error("Cannot finish material output.");
                    const auto reference = projectReference(destination);
                    materialReferences.push_back(reference);
                    const auto materialId = addObject(assets::ProjectAssetType::Material, name, reference, "material/slot/" + std::to_string(index));
                    materialReferences.back() = dependencyReference(materialId, reference);
                    materialIds.push_back(materialId);
                    for (const auto &remap : settings.materialRemaps)
                        if (remap.materialLocalId == materialId)
                        {
                            const auto target = remap.engineMaterial.empty() ? reader.ResolveStableAssetId(remap.authoredMaterial.assetId) : remap.engineMaterial;
                            if (remap.authoredMaterial.localObjectId != 0 || assets::Project::GetAssetTypeForReference(target) != assets::ProjectAssetType::Material)
                                throw std::runtime_error("Authored material remap cannot be resolved.");
                            materialReferences.back() = persistentSettings && remap.engineMaterial.empty()
                                ? LogicalReference(remap.authoredMaterial) : target;
                        }
                }
            }
            if (previousBindings.size() == materialReferences.size())
                for (std::size_t index = 0; index < materialReferences.size(); ++index)
                {
                    const bool explicitRemap = std::any_of(settings.materialRemaps.begin(), settings.materialRemaps.end(),
                        [&](const auto &remap) { return remap.materialLocalId == materialIds[index]; });
                    if (explicitRemap || previousBindings[index].empty() || previousGeneratedMaterials.contains(previousBindings[index])) continue;
                    materialReferences[index] = previousBindings[index];
                    if (persistentSettings)
                    {
                        assets::ModelMaterialRemap remap;
                        remap.materialLocalId = materialIds[index];
                        if (assets::Project::IsEngineAssetReference(previousBindings[index])) remap.engineMaterial = previousBindings[index];
                        else
                        {
                            assets::AssetReference bindingIdentity;
                            const bool logical = assets::ParseAssetReference(previousBindings[index], bindingIdentity);
                            if (logical && bindingIdentity.localObjectId != 0)
                                throw std::runtime_error("Extract imported materials before assigning them as authored overrides.");
                            const auto materialPath = reader.ResolveAssetPath(previousBindings[index]);
                            if (assets::Project::GetAssetTypeForReference(materialPath) != assets::ProjectAssetType::Material ||
                                !std::filesystem::is_regular_file(materialPath) || reader.IsImportedAsset(previousBindings[index]))
                                throw std::runtime_error("Authored material override cannot be resolved.");
                            assets::AssetMetadata materialMetadata;
                            const auto status = assets::LoadAssetMetadata(assets::GetAssetMetadataPath(materialPath), materialMetadata, errorMessage);
                            if (status == assets::AssetMetadataStatus::Missing)
                            {
                                materialMetadata.id = assets::GenerateAssetId();
                                if (!assets::SaveAssetMetadata(assets::GetAssetMetadataPath(materialPath), materialMetadata,
                                                              assets::AssetMetadataWriteMode::CreateOnly, errorMessage)) return false;
                            }
                            else if (status != assets::AssetMetadataStatus::Success) return false;
                            remap.authoredMaterial = {materialMetadata.id, 0};
                        }
                        if (remap.engineMaterial.empty()) materialReferences[index] = LogicalReference(remap.authoredMaterial);
                        settings.materialRemaps.push_back(std::move(remap));
                    }
                }
            if (hasMesh)
            {
                progress("Writing mesh");
                const auto name = sourcePath.stem().string();
                const auto destination = importDirectory / (name + ".plutomesh");
                const auto reference = projectReference(destination);
                const auto id = addObject(assets::ProjectAssetType::Mesh, name, reference, "mesh/main");
                render::MeshConfig config;
                config.data = std::move(imported.meshData);
                config.submeshes = std::move(imported.submeshes);
                config.hasLightmapUvs = imported.hasLightmapUvs;
                config.skeleton = std::move(imported.skeleton);
                config.animationNodes = std::move(imported.animationNodes);
                config.animations = imported.animations;
                assets::MeshAssetMetadata metadata{
                    .sourceAssetReference = request.sourceReference,
                    .sourceAssetId = sourceMetadata.id,
                    .sourceObjectId = id,
                    .importOptions = importOptions,
                };
                const auto staged = stage(destination);
                if (!writer.SaveMeshAsset(staged.string(), config, materialReferences, errorMessage, metadata)) return false;
                const auto overridePath = std::filesystem::path(destination).concat(".materials");
                if (std::filesystem::is_regular_file(overridePath))
                {
                    const bool wasRead = std::any_of(authoredInputs.begin(), authoredInputs.end(),
                        [&](const auto &input) { return input.path == overridePath; });
                    if (!wasRead) throw std::runtime_error("Material overrides appeared during import or belong to another output package: " + overridePath.string());
                    std::ofstream overrides(stage(overridePath));
                    for (const auto &binding : materialReferences) overrides << binding << '\n';
                    overrides.close();
                    if (!overrides) throw std::runtime_error("Cannot finish material override output.");
                    previousOutputs.insert(projectReference(overridePath));
                }
            }
            if (!imported.animations.empty())
            {
                std::vector<std::string> clipReferences;
                for (std::size_t index = 0; index < imported.animations.size(); ++index)
                {
                    progress("Writing animation clips");
                    const auto &clip = imported.animations[index];
                    auto name = SafeName(clip.name);
                    if (name.empty()) name = "Clip_" + std::to_string(index);
                    const auto destination = importDirectory / "Clips" /
                        (sourcePath.stem().string() + "_" + name + "_" + std::to_string(index) + ".plutoclip");
                    const auto reference = projectReference(destination);
                    if (!writer.SaveAnimationClipAsset(stage(destination).string(), clip, errorMessage)) return false;
                    const auto clipId = addObject(assets::ProjectAssetType::AnimationClip, clip.name, reference, "clip/" + clip.name);
                    clipReferences.push_back(dependencyReference(clipId, reference));
                }
                const auto destination = importDirectory / (sourcePath.stem().string() + ".plutoanim");
                if (!writer.SaveAnimationAssetReferences(stage(destination).string(), clipReferences, errorMessage)) return false;
                addObject(assets::ProjectAssetType::Animation, sourcePath.stem().string(), projectReference(destination), "animation/set");
            }
            const auto manifestPath = assets::GetModelManifestPath(project, request.sourceReference);
            if (hasPrevious) model.extensionRecords = previous.extensionRecords;
            if (UsesLibraryModelStorage(project))
            {
                assets::ModelHierarchyAsset hierarchy{sourceMetadata.id, {}, imported.hierarchy, std::move(nodeIdentities)};
                for (const auto &object : model.objects)
                    if (object.type == assets::ProjectAssetType::Mesh)
                    { hierarchy.meshReference = LogicalReference({sourceMetadata.id, object.localId}); break; }
                const auto path = importDirectory / (sourcePath.stem().string() + ".plutomodelhierarchy");
                const auto staged = stage(path);
                assets::ModelHierarchyArtifact descriptor{projectReference(path)};
                if (!assets::SaveModelHierarchyAsset(staged, hierarchy, errorMessage) ||
                    !content::HashFileContent(staged, descriptor.digest, errorMessage) ||
                    !assets::WriteModelHierarchyArtifact(model, descriptor, errorMessage)) return false;
            }
            if (persistentSettings)
                for (const auto &relative : outputs)
                {
                    if (std::none_of(model.objects.begin(), model.objects.end(), [&](const auto &object)
                        { return object.reference == projectReference(assetRoot / relative); })) continue;
                    assets::ModelGeneratedFile baseline{projectReference(assetRoot / relative)};
                    if (!content::HashFileContent(transaction.GetOutputRoot() / relative, baseline.digest, errorMessage)) return false;
                    model.generatedFiles.push_back(std::move(baseline));
                }
            if (!assets::SaveModelAsset(stage(manifestPath).string(), model, errorMessage)) return false;
            if (persistentSettings)
            {
                sourceMetadata.importerVersion = model.importerVersion;
                if (!assets::WriteModelImportSettings(sourceMetadata, settings, errorMessage) ||
                    !assets::WriteModelSourcePackage(sourceMetadata, model, errorMessage)) return false;
                std::string bytes;
                if (!assets::SerializeAssetMetadata(sourceMetadata, bytes, errorMessage)) return false;
                std::ofstream metadataOutput(stage(metadataPath), std::ios::binary);
                metadataOutput << bytes;
                metadataOutput.close();
                if (!metadataOutput) throw std::runtime_error("Cannot finish source import metadata.");
                previousOutputs.insert(projectReference(metadataPath));
            }
            // Validate logical object uniqueness before any existing artifact is touched.
            assets::AssetCatalog validation;
            std::vector<assets::AssetObjectDescriptor> descriptors;
            for (const auto &object : model.objects)
                descriptors.push_back({{model.sourceAssetId, object.localId}, object.type, assets::AssetOwnership::Imported, object.name, object.reference});
            if (!validation.Replace(std::move(descriptors), errorMessage)) return false;
            for (const auto &relative : outputs)
            {
                const auto destination = assetRoot / relative;
                if (std::filesystem::exists(destination) && !previousOutputs.contains(projectReference(destination)))
                    throw std::runtime_error("Import would overwrite content without generated provenance: " + destination.string());
            }
            progress("Publishing artifacts");
            if (assets::AssetDatabase::HashFile(sourcePath) != sourceHash)
                throw std::runtime_error("Model source changed during import; retry after saving completes.");
            ArtifactRecipe inputValidation;
            inputValidation.inputs = snapshot.inputs;
            if (!AreArtifactInputsCurrent(inputValidation, errorMessage)) return false;
            progress("Caching artifacts");
            ArtifactRecipe recipe{
                .importer = std::string(kModelArtifactImporter),
                .version = kModelArtifactVersion,
                .target = std::string(kModelArtifactTarget),
                .inputs = snapshot.inputs,
            };
            // Authored sidecars and overrides are publication preconditions.
            // Their canonical semantics are already represented by settings;
            // rewritten bytes must not feed back into the next generation key.
            std::erase_if(recipe.inputs, [](const auto &input) { return IsModelPublicationInput(input.identity); });
            if (!ComputeModelArtifactSettings(sourceMetadata, importOptions, request.sourceReference, materialReferences,
                                              outputs, recipe.settings, errorMessage, UsesLibraryModelStorage(project))) return false;
            if (UsesLibraryModelStorage(project))
            {
                content::ContentDigest key;
                if (!ComputeArtifactKey(recipe, key, errorMessage) ||
                    !assets::WriteModelArtifactGeneration(sourceMetadata, key, errorMessage)) return false;
                assets::ModelGeneratedFile packageArtifact{projectReference(manifestPath)};
                if (!content::HashFileContent(transaction.GetOutputRoot() / manifestPath.lexically_relative(assetRoot), packageArtifact.digest, errorMessage) ||
                    !assets::WriteActiveModelPackageArtifact(sourceMetadata, packageArtifact, errorMessage)) return false;
                std::string bytes;
                if (!assets::SerializeAssetMetadata(sourceMetadata, bytes, errorMessage)) return false;
                std::ofstream output(transaction.GetOutputRoot() / metadataPath.lexically_relative(assetRoot), std::ios::binary | std::ios::trunc);
                output << bytes;
                output.close();
                if (!output) throw std::runtime_error("Cannot finish active model generation metadata.");
            }
            // All reads from the previous generation are complete and staged.
            // Release this operation's snapshot before an exclusive corrupt-cache
            // repair; external scene/catalog readers must continue to block it.
            reader.ClearProjectContext();
            ArtifactCache cache(project.GetRootDirectory() / "Library" / "Artifacts");
            ArtifactManifest generation;
            if (!cache.Store(recipe, transaction.GetOutputRoot(), outputs, generation, errorMessage)) return false;
            cache.RememberRequestGeneration("model/" + sourceMetadata.id, generation.key);
            // A progress callback can edit inputs, so recheck immediately before
            // publication even when cache storage verified the same snapshot.
            if (!AreArtifactInputsCurrent(inputValidation, errorMessage)) return false;
            const auto publication = ModelPublishedGeneration(project, generation, metadataPath);
            std::vector<std::filesystem::path> publishedOutputs;
            for (const auto &output : publication.outputs) publishedOutputs.push_back(output.relativePath);
            if (!transaction.Publish(assetRoot, publishedOutputs, errorMessage)) return false;
            progress("Validating asset database");
            assets::AssetDatabase database;
            // Publication needs identities, ownership and verified storage.
            // Dependency inventories and legacy content hashes are computed by
            // reconciliation/cooking, not repeatedly for unrelated assets here.
            if (!database.Scan(project, assets::AssetScanOptions{.hashContent=false, .collectDependencies=false}, errorMessage)) return false;
            std::vector<std::string> changedAssets;
            for (const auto &relative : outputs) changedAssets.push_back(projectReference(assetRoot / relative));
            ModelImportResult candidate{database.GetCatalog(), UsesLibraryModelStorage(project) ? request.sourceReference : projectReference(manifestPath), false, std::move(changedAssets), sourceMetadata.id, database.GetStorageMap()};
            candidate.artifactGenerationKey = generation.key;
            if (!FindModelPackageArtifact(generation, candidate.packageArtifact, errorMessage)) return false;
            progress("Import complete");
            ImportState acceptedState;
            if (!CaptureImportState(sourceMetadata.id, request.sourceReference, generation, authoredInputs, acceptedState, errorMessage)) return false;
            if (!ValidateModelGenerationPublication(project, generation, metadataPath, errorMessage)) return false;
            if (!transaction.Accept(errorMessage)) return false;
            RememberAcceptedImport(project, acceptedState);
            result = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &exception)
        {
            if (errorMessage) *errorMessage = std::string("Model import failed: ") + exception.what();
            return false;
        }
    }
}
