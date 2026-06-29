#include "PlutoGE/ui/panels/ContentBrowserPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>

#include <imgui.h>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace PlutoGE::ui
{
    namespace
    {
        bool ContainsInsensitive(std::string_view text, std::string_view filter)
        {
            if (filter.empty())
            {
                return true;
            }

            auto lower = [](unsigned char value)
            {
                return static_cast<char>(std::tolower(value));
            };

            std::string haystack(text);
            std::string needle(filter);
            std::transform(haystack.begin(), haystack.end(), haystack.begin(), lower);
            std::transform(needle.begin(), needle.end(), needle.begin(), lower);
            return haystack.find(needle) != std::string::npos;
        }

        std::string DisplayAssetReference(std::string reference)
        {
            if (reference.rfind(assets::Project::kProjectAssetScheme, 0) == 0)
            {
                reference.erase(0, assets::Project::kProjectAssetScheme.size());
            }
            return reference;
        }

        std::string SanitizeAssetFileName(std::string_view text)
        {
            std::string name;
            name.reserve(text.size());
            for (const char rawCharacter : text)
            {
                const unsigned char character = static_cast<unsigned char>(rawCharacter);
                if (std::isalnum(character) != 0 || rawCharacter == '_' || rawCharacter == '-')
                {
                    name.push_back(rawCharacter);
                }
                else if (!name.empty() && name.back() != '_')
                {
                    name.push_back('_');
                }
            }

            while (!name.empty() && name.back() == '_')
            {
                name.pop_back();
            }
            return name;
        }

        std::string BuildEntityNameForMeshReference(const std::string &reference)
        {
            std::string displayName = DisplayAssetReference(reference);
            if (displayName.rfind("engine://", 0) == 0)
            {
                const auto separator = displayName.find_last_of('/');
                return separator == std::string::npos ? displayName : displayName.substr(separator + 1);
            }

            std::filesystem::path path(displayName);
            const auto stem = path.stem().string();
            return stem.empty() ? displayName : stem;
        }

        bool IsSupportedImportedModelReference(const std::string &reference)
        {
            const auto extension = std::filesystem::path(reference).extension().string();
            return extension == ".gltf" || extension == ".glb" || extension == ".fbx" ||
                   extension == ".GLTF" || extension == ".GLB" || extension == ".FBX";
        }

        std::string BrowseSourceModelPath()
        {
#ifdef _WIN32
            OPENFILENAMEA openFileName{};
            char fileName[MAX_PATH] = "";
            openFileName.lStructSize = sizeof(openFileName);
            openFileName.hwndOwner = nullptr;
            openFileName.lpstrFilter = "Model Files\0*.gltf;*.glb;*.fbx\0glTF Files\0*.gltf;*.glb\0FBX Files\0*.fbx\0All Files\0*.*\0";
            openFileName.lpstrFile = fileName;
            openFileName.nMaxFile = MAX_PATH;
            openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            if (!GetOpenFileNameA(&openFileName))
            {
                return {};
            }
            return std::filesystem::path(fileName).lexically_normal().string();
#else
            return {};
#endif
        }

        bool CopySourceModelPackageToAssets(const assets::Project &project,
                                            const std::filesystem::path &sourcePath,
                                            std::filesystem::path &importedSourcePath,
                                            std::string *errorMessage)
        {
            std::error_code errorCode;
            const auto normalizedSourcePath = std::filesystem::weakly_canonical(sourcePath, errorCode);
            const auto sourceFilePath = errorCode ? sourcePath.lexically_normal() : normalizedSourcePath;
            if (!std::filesystem::exists(sourceFilePath))
            {
                if (errorMessage)
                {
                    *errorMessage = "Selected model does not exist.";
                }
                return false;
            }

            if (!IsSupportedImportedModelReference(sourceFilePath.string()))
            {
                if (errorMessage)
                {
                    *errorMessage = "Selected file is not a supported source model. Use .gltf, .glb, or .fbx.";
                }
                return false;
            }

            if (project.IsInAssetDirectory(sourceFilePath))
            {
                importedSourcePath = sourceFilePath;
                return true;
            }

            std::string packageName = SanitizeAssetFileName(sourceFilePath.stem().string());
            if (packageName.empty())
            {
                packageName = "ImportedModel";
            }
            const auto packageDirectory = project.GetAssetDirectoryPath() / "SourceModels" / packageName;
            std::filesystem::create_directories(packageDirectory, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create source model directory: " + errorCode.message();
                }
                return false;
            }

            const auto sourceParent = sourceFilePath.parent_path();
            for (std::filesystem::recursive_directory_iterator iterator(sourceParent, errorCode), end; iterator != end && !errorCode; iterator.increment(errorCode))
            {
                const auto &entry = *iterator;
                if (!entry.is_regular_file(errorCode) || errorCode)
                {
                    errorCode.clear();
                    continue;
                }

                const auto relativePath = std::filesystem::relative(entry.path(), sourceParent, errorCode);
                if (errorCode)
                {
                    break;
                }

                const auto destinationPath = packageDirectory / relativePath;
                std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                if (errorCode)
                {
                    break;
                }

                std::filesystem::copy_file(entry.path(), destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                if (errorCode)
                {
                    break;
                }
            }

            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to copy source model package: " + errorCode.message();
                }
                return false;
            }

            importedSourcePath = packageDirectory / sourceFilePath.filename();
            const auto sourceSize = std::filesystem::file_size(sourceFilePath, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to read selected model size: " + errorCode.message();
                }
                return false;
            }

            const auto importedSize = std::filesystem::file_size(importedSourcePath, errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to read copied model size: " + errorCode.message();
                }
                return false;
            }

            if (importedSize == 0 || importedSize != sourceSize)
            {
                if (errorMessage)
                {
                    *errorMessage = "Copied model file is incomplete. Source size=" + std::to_string(sourceSize) +
                                    " bytes, copied size=" + std::to_string(importedSize) + " bytes.";
                }
                return false;
            }
            return true;
        }

        bool WriteTextureTga(const std::filesystem::path &path,
                             const assetimport::ImportedTextureData &texture,
                             std::string *errorMessage)
        {
            if (texture.width <= 0 || texture.height <= 0 || texture.channels <= 0 || texture.pixels.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Imported texture has no pixel data.";
                }
                return false;
            }

            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            if (errorCode)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to create texture directory: " + errorCode.message();
                }
                return false;
            }

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to write texture asset: " + path.string();
                }
                return false;
            }

            const unsigned char header[18] = {
                0, 0, 2,
                0, 0, 0, 0, 0,
                0, 0, 0, 0,
                static_cast<unsigned char>(texture.width & 0xff),
                static_cast<unsigned char>((texture.width >> 8) & 0xff),
                static_cast<unsigned char>(texture.height & 0xff),
                static_cast<unsigned char>((texture.height >> 8) & 0xff),
                32,
                0x20 | 0x08,
            };
            output.write(reinterpret_cast<const char *>(header), sizeof(header));

            const std::size_t sourceChannels = static_cast<std::size_t>(texture.channels);
            const std::size_t pixelCount = static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height);
            for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
            {
                const std::size_t sourceOffset = pixelIndex * sourceChannels;
                const unsigned char red = texture.pixels[sourceOffset + 0];
                const unsigned char green = sourceChannels > 1 ? texture.pixels[sourceOffset + 1] : red;
                const unsigned char blue = sourceChannels > 2 ? texture.pixels[sourceOffset + 2] : red;
                const unsigned char alpha = sourceChannels > 3 ? texture.pixels[sourceOffset + 3] : 255;
                const unsigned char bgra[4] = {blue, green, red, alpha};
                output.write(reinterpret_cast<const char *>(bgra), sizeof(bgra));
            }

            return output.good();
        }

        std::string SanitizeImportedAssetName(std::string text)
        {
            for (auto &character : text)
            {
                const unsigned char value = static_cast<unsigned char>(character);
                if (std::isalnum(value) == 0 && character != '_' && character != '-')
                {
                    character = '_';
                }
            }
            return text.empty() ? "Asset" : text;
        }

        std::string ImportTextureAsset(const assets::Project &project,
                                       const std::filesystem::path &importDirectory,
                                       const assetimport::ImportedTextureData &texture,
                                       int textureIndex,
                                       std::string *errorMessage)
        {
            const auto textureDirectory = importDirectory / "Textures";
            if (!texture.sourcePath.empty() && std::filesystem::exists(texture.sourcePath))
            {
                const auto sourcePath = std::filesystem::path(texture.sourcePath);
                const auto destinationPath = textureDirectory / sourcePath.filename();
                std::error_code errorCode;
                std::filesystem::create_directories(destinationPath.parent_path(), errorCode);
                if (!errorCode)
                {
                    std::filesystem::copy_file(sourcePath, destinationPath, std::filesystem::copy_options::overwrite_existing, errorCode);
                }
                if (errorCode)
                {
                    if (errorMessage)
                    {
                        *errorMessage = "Failed to copy imported texture: " + errorCode.message();
                    }
                    return {};
                }
                return project.MakeAssetReference(destinationPath);
            }

            const auto texturePath = textureDirectory / ("T_" + std::to_string(textureIndex) + ".tga");
            if (!WriteTextureTga(texturePath, texture, errorMessage))
            {
                return {};
            }
            return project.MakeAssetReference(texturePath);
        }

        render::MaterialConfig BuildGeneratedMaterialConfig(const assetimport::ImportedMaterialData &material,
                                                            const std::vector<std::string> &textureReferences,
                                                            std::deque<render::Texture> &textureHandles)
        {
            render::MaterialConfig config;
            config.color = material.color;
            config.surfaceType = material.surfaceType;
            config.alphaMode = material.alphaMode;
            config.alphaCutoff = material.alphaCutoff;
            config.castsShadow = material.castsShadow;
            config.metallic = material.metallic;
            config.roughness = material.roughness;
            config.transmission = material.transmission;
            config.ior = material.ior;
            config.thickness = material.thickness;
            config.attenuationColor = material.attenuationColor;
            config.attenuationDistance = material.attenuationDistance;
            config.flipNormalY = material.flipNormalY;

            auto assignTexture = [&](int textureIndex) -> render::Texture *
            {
                if (textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= textureReferences.size() || textureReferences[static_cast<std::size_t>(textureIndex)].empty())
                {
                    return nullptr;
                }
                render::TextureConfig textureConfig;
                textureConfig.filePath = textureReferences[static_cast<std::size_t>(textureIndex)];
                textureHandles.emplace_back(textureConfig);
                return &textureHandles.back();
            };

            config.albedoTexture = assignTexture(material.albedoTextureIndex);
            config.normalTexture = assignTexture(material.normalTextureIndex);
            if (auto *packedTexture = assignTexture(material.metallicRoughnessTextureIndex))
            {
                config.roughnessTexture = packedTexture;
                config.roughnessTextureChannel = render::TextureChannel::Green;
                if (material.metallicRoughnessTextureHasMetallicChannel)
                {
                    config.metallicTexture = packedTexture;
                    config.metallicTextureChannel = render::TextureChannel::Blue;
                }
            }
            return config;
        }

        std::vector<render::Material *> LoadMaterialReferences(core::Engine &engine, const std::vector<std::string> &materialReferences)
        {
            std::vector<render::Material *> materials;
            materials.reserve(materialReferences.size());
            for (const auto &materialReference : materialReferences)
            {
                auto *material = materialReference.empty()
                                     ? engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference))
                                     : engine.GetAssetManager().LoadMaterialAsset(materialReference);
                materials.push_back(material);
            }
            if (materials.empty())
            {
                materials.push_back(engine.GetAssetManager().LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)));
            }
            return materials;
        }

        std::string FindSiblingAnimationAssetReference(const assets::Project *project, const std::string &meshReference)
        {
            if (!project || meshReference.empty() || assets::Project::IsEngineAssetReference(meshReference))
            {
                return {};
            }

            const auto meshPath = project->ResolveAssetReference(meshReference);
            const auto expectedAnimationPath = meshPath.parent_path() / (meshPath.stem().string() + ".plutoanim");
            if (std::filesystem::exists(expectedAnimationPath))
            {
                return project->MakeAssetReference(expectedAnimationPath);
            }

            for (const auto &asset : project->GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::Animation)
                {
                    continue;
                }

                const auto animationPath = project->ResolveAssetReference(asset.reference);
                if (animationPath.parent_path() == meshPath.parent_path())
                {
                    return asset.reference;
                }
            }
            return {};
        }

        void AttachAnimationAsset(scene::Entity &entity, const std::string &animationReference)
        {
            if (animationReference.empty())
            {
                return;
            }

            auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
            if (!animationComponent)
            {
                animationComponent = entity.CreateComponent<scene::AnimationComponent>();
            }

            animationComponent->SetAnimationAssetReference(animationReference);
        }

        void AttachImportedAnimations(scene::Entity &entity,
                                      const std::string &sourceReference,
                                      const core::ImportedRenderMeshAsset &importedMeshAsset)
        {
            if (!importedMeshAsset.animations || importedMeshAsset.animations->empty())
            {
                return;
            }

            auto *animationComponent = entity.GetComponent<scene::AnimationComponent>();
            if (!animationComponent)
            {
                animationComponent = entity.CreateComponent<scene::AnimationComponent>();
            }

            animationComponent->SetClipsFromImportedAnimations(*importedMeshAsset.animations);
            animationComponent->SetSourceAnimationPath(sourceReference);
        }

        std::string MakeClipAssetFileName(const std::filesystem::path &sourcePath, const render::AnimationClip &clip, std::size_t clipIndex)
        {
            std::string clipName = SanitizeAssetFileName(clip.name);
            if (clipName.empty())
            {
                clipName = "Clip_" + std::to_string(clipIndex);
            }
            return sourcePath.stem().string() + "_" + clipName + "_" + std::to_string(clipIndex) + ".plutoclip";
        }

        bool SaveImportedAnimationClips(const assets::Project &project,
                                        const std::filesystem::path &clipDirectory,
                                        const std::filesystem::path &sourcePath,
                                        const std::vector<render::AnimationClip> &clips,
                                        std::vector<std::string> &clipReferences,
                                        std::string *errorMessage)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            for (std::size_t clipIndex = 0; clipIndex < clips.size(); ++clipIndex)
            {
                const auto clipPath = clipDirectory / MakeClipAssetFileName(sourcePath, clips[clipIndex], clipIndex);
                const std::string clipReference = project.MakeAssetReference(clipPath);
                if (!assetManager.SaveAnimationClipAsset(clipReference, clips[clipIndex], errorMessage))
                {
                    return false;
                }
                if (std::find(clipReferences.begin(), clipReferences.end(), clipReference) == clipReferences.end())
                {
                    clipReferences.push_back(clipReference);
                }
            }
            return true;
        }

        bool EnsureAnimationAssetUsesClipReferences(const assets::Project &project,
                                                   const std::string &animationReference,
                                                   std::vector<std::string> &clipReferences,
                                                   std::string *errorMessage)
        {
            auto &assetManager = core::Engine::GetInstance().GetAssetManager();
            if (assetManager.LoadAnimationClipReferences(animationReference, clipReferences))
            {
                return true;
            }

            std::vector<render::AnimationClip> embeddedClips;
            if (!assetManager.LoadAnimationAsset(animationReference, embeddedClips))
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to load animation asset: " + animationReference;
                }
                return false;
            }

            const auto animationPath = project.ResolveAssetReference(animationReference);
            const auto clipDirectory = animationPath.parent_path() / "Clips";
            if (!SaveImportedAnimationClips(project, clipDirectory, animationPath, embeddedClips, clipReferences, errorMessage))
            {
                return false;
            }

            return assetManager.SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage);
        }

        bool AddClipsFromSourceModelToAnimationAsset(const assets::Project &project,
                                                    const std::string &animationReference,
                                                    const std::filesystem::path &sourcePath,
                                                    std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            assetimport::ImportedMeshSourceAsset importedSourceAsset;
            try
            {
                importedSourceAsset = engine.GetMeshImporter().ImportMeshSourceAsset(sourcePath.string());
            }
            catch (const std::exception &exception)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import animation source '" + sourcePath.string() + "': " + exception.what();
                }
                return false;
            }

            if (importedSourceAsset.animations.empty())
            {
                if (errorMessage)
                {
                    *errorMessage = "Source model contained no animation clips: " + sourcePath.string();
                }
                return false;
            }

            std::vector<std::string> clipReferences;
            if (!EnsureAnimationAssetUsesClipReferences(project, animationReference, clipReferences, errorMessage))
            {
                return false;
            }

            const auto animationPath = project.ResolveAssetReference(animationReference);
            const auto clipDirectory = animationPath.parent_path() / "Clips";
            if (!SaveImportedAnimationClips(project, clipDirectory, sourcePath, importedSourceAsset.animations, clipReferences, errorMessage))
            {
                return false;
            }

            return engine.GetAssetManager().SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage);
        }

        render::Mesh *GetOrCreateIsolatedSubmeshRuntimeMesh(const std::string &reference,
                                                            int submeshIndex,
                                                            int submeshCount,
                                                            const render::Mesh &sourceMesh);

        bool ConfigureMeshComponentForReference(scene::Entity &entity,
                                                const std::string &reference,
                                                int submeshIndex,
                                                int submeshCount,
                                                int materialSlot,
                                                std::string *errorMessage)
        {
            auto &engine = core::Engine::GetInstance();
            auto *meshComponent = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{});

            if (auto *mesh = engine.GetAssetManager().LoadMeshAsset(reference))
            {
                meshComponent->SetMesh(mesh);
                meshComponent->SetSourceMeshPath(reference);
                const auto &materialReferences = engine.GetAssetManager().GetMeshAssetMaterialReferences(reference);
                const auto materials = LoadMaterialReferences(engine, materialReferences);
                meshComponent->SetMaterials(materials);
                for (size_t materialSlotIndex = 0; materialSlotIndex < materialReferences.size(); ++materialSlotIndex)
                {
                    meshComponent->SetMaterialAssetForMaterialSlot(materialSlotIndex, materialReferences[materialSlotIndex]);
                }
                if (submeshIndex >= 0)
                {
                    meshComponent->SetSubmeshRange(submeshIndex, submeshCount);
                    if (static_cast<size_t>(submeshIndex) < mesh->GetSubmeshCount())
                    {
                        const auto materialSlot = static_cast<size_t>(mesh->GetSubmesh(static_cast<size_t>(submeshIndex)).materialIndex);
                        if (materialSlot < materials.size())
                        {
                            meshComponent->SetMaterialForSubmesh(static_cast<size_t>(submeshIndex), materials[materialSlot]);
                        }
                    }
                }
                else
                {
                    meshComponent->CreateSubmeshChildEntities();
                }
                AttachAnimationAsset(entity, FindSiblingAnimationAssetReference(EditorShell::GetInstance().GetProject(), reference));
                return true;
            }

            const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(reference);
            if (!engine.GetMeshImporter().SupportsFileType(resolvedPath))
            {
                if (errorMessage)
                {
                    *errorMessage = "Unsupported mesh format for model subassets: " + reference;
                }
                return false;
            }

            auto importedMeshAsset = engine.ImportMeshAsset(resolvedPath);
            if (!importedMeshAsset.mesh)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import mesh asset: " + reference;
                }
                return false;
            }

            meshComponent->SetMesh(importedMeshAsset.mesh);
            meshComponent->SetMaterials(importedMeshAsset.materials);
            meshComponent->SetSourceMeshPath(reference);
            if (submeshIndex >= 0)
            {
                if (static_cast<size_t>(submeshIndex) < importedMeshAsset.mesh->GetSubmeshCount())
                {
                    const auto &submesh = importedMeshAsset.mesh->GetSubmesh(static_cast<size_t>(submeshIndex));
                    const uint32_t resolvedMaterialSlot = materialSlot >= 0
                                                              ? static_cast<uint32_t>(materialSlot)
                                                              : submesh.materialIndex;
                    if (resolvedMaterialSlot < importedMeshAsset.materials.size())
                    {
                        if (auto *isolatedMesh = GetOrCreateIsolatedSubmeshRuntimeMesh(reference, submeshIndex, submeshCount, *importedMeshAsset.mesh))
                        {
                            meshComponent->SetMesh(isolatedMesh);
                        }
                        meshComponent->SetMaterials(importedMeshAsset.materials);
                        meshComponent->SetMaterialForSubmesh(static_cast<size_t>(submeshIndex),
                                                             importedMeshAsset.materials[resolvedMaterialSlot]);
                    }
                    meshComponent->SetSubmeshRange(submeshIndex, submeshCount);
                    if (!submesh.name.empty())
                    {
                        entity.SetName(submesh.name);
                    }
                }
            }
            else
            {
                meshComponent->CreateSubmeshChildEntities();
            }

            AttachImportedAnimations(entity, reference, importedMeshAsset);
            return true;
        }

        bool ImportSourceModelAsset(const assets::Project &project, const assets::ProjectAssetEntry &asset, std::string *errorMessage)
        {
            if (asset.type != assets::ProjectAssetType::SourceModel)
            {
                return false;
            }

            auto &engine = core::Engine::GetInstance();
            const auto sourcePath = project.ResolveAssetReference(asset.reference);
            assetimport::ImportedMeshSourceAsset importedSourceAsset;
            try
            {
                importedSourceAsset = engine.GetMeshImporter().ImportMeshSourceAsset(sourcePath.string());
            }
            catch (const std::exception &exception)
            {
                if (errorMessage)
                {
                    *errorMessage = "Failed to import source model '" + asset.reference + "': " + exception.what();
                }
                return false;
            }

            const bool hasMesh = !importedSourceAsset.meshData.vertices.empty() && !importedSourceAsset.meshData.indices.empty();
            const bool hasAnimations = !importedSourceAsset.animations.empty();
            const bool hasTextures = !importedSourceAsset.textures.empty();
            const bool hasAuthoredMaterials = importedSourceAsset.materials.size() > 1;
            if (!hasMesh && !hasAnimations && !hasTextures && !hasAuthoredMaterials)
            {
                if (errorMessage)
                {
                    *errorMessage = "Source model contained no mesh, materials, textures, or animations: " + asset.reference;
                }
                return false;
            }

            const auto importDirectory = project.GetAssetDirectoryPath() / "Imported" / sourcePath.stem();
            const std::string meshReference = project.MakeAssetReference(importDirectory / (sourcePath.stem().string() + ".plutomesh"));
            std::vector<std::string> textureReferences;
            if (!importedSourceAsset.textures.empty())
            {
                textureReferences.reserve(importedSourceAsset.textures.size());
                for (std::size_t textureIndex = 0; textureIndex < importedSourceAsset.textures.size(); ++textureIndex)
                {
                    const auto &texture = importedSourceAsset.textures[textureIndex];
                    textureReferences.push_back(ImportTextureAsset(project, importDirectory, texture, static_cast<int>(textureIndex), errorMessage));
                    if (textureReferences.back().empty() && (!texture.sourcePath.empty() || !texture.pixels.empty()))
                    {
                        return false;
                    }
                }
            }

            std::vector<std::string> materialReferences;
            if (hasMesh ? !importedSourceAsset.materials.empty() : hasAuthoredMaterials)
            {
                materialReferences.reserve(importedSourceAsset.materials.size());
                std::deque<render::Texture> textureHandles;
                for (size_t materialIndex = 0; materialIndex < importedSourceAsset.materials.size(); ++materialIndex)
                {
                    const std::string materialName = "M_" + sourcePath.stem().string() + "_" + std::to_string(materialIndex);
                    const std::string materialReference = project.MakeAssetReference(importDirectory / (materialName + ".plutomaterial"));
                    std::string materialError;
                    const auto materialConfig = BuildGeneratedMaterialConfig(importedSourceAsset.materials[materialIndex], textureReferences, textureHandles);
                    if (!engine.GetAssetManager().SaveMaterialAsset(materialReference, materialConfig, &materialError))
                    {
                        if (errorMessage)
                        {
                            *errorMessage = materialError.empty() ? "Failed to save imported material." : materialError;
                        }
                        return false;
                    }
                    materialReferences.push_back(materialReference);
                }
            }

            if (hasMesh)
            {
                render::MeshConfig meshConfig;
                meshConfig.data = importedSourceAsset.meshData;
                meshConfig.submeshes = importedSourceAsset.submeshes;
                meshConfig.hasLightmapUvs = importedSourceAsset.hasLightmapUvs;
                meshConfig.skeleton = importedSourceAsset.skeleton;
                meshConfig.animationNodes = importedSourceAsset.animationNodes;
                meshConfig.animations = importedSourceAsset.animations;

                if (!engine.GetAssetManager().SaveMeshAsset(meshReference, meshConfig, materialReferences, errorMessage))
                {
                    return false;
                }
            }

            if (hasAnimations)
            {
                const std::string animationReference = project.MakeAssetReference(importDirectory / (sourcePath.stem().string() + ".plutoanim"));
                std::vector<std::string> clipReferences;
                if (!SaveImportedAnimationClips(project, importDirectory / "Clips", sourcePath, importedSourceAsset.animations, clipReferences, errorMessage))
                {
                    return false;
                }
                if (!engine.GetAssetManager().SaveAnimationAssetReferences(animationReference, clipReferences, errorMessage))
                {
                    return false;
                }
            }

            return true;
        }

        bool ImportExternalSourceModelIntoAssets(assets::Project &project, std::string *importedReference, std::string *errorMessage)
        {
            const std::string selectedPath = BrowseSourceModelPath();
            if (selectedPath.empty())
            {
                return false;
            }

            std::filesystem::path importedSourcePath;
            if (!CopySourceModelPackageToAssets(project, selectedPath, importedSourcePath, errorMessage))
            {
                return false;
            }

            const std::string sourceReference = project.MakeAssetReference(importedSourcePath);
            assets::ProjectAssetEntry sourceAsset{};
            sourceAsset.reference = sourceReference;
            sourceAsset.type = assets::ProjectAssetType::SourceModel;
            std::error_code errorCode;
            sourceAsset.size = std::filesystem::file_size(importedSourcePath, errorCode);

            if (!ImportSourceModelAsset(project, sourceAsset, errorMessage))
            {
                return false;
            }

            if (importedReference)
            {
                *importedReference = sourceReference;
            }
            return true;
        }

        render::Mesh *GetOrCreateIsolatedSubmeshRuntimeMesh(const std::string &reference,
                                                            int submeshIndex,
                                                            int submeshCount,
                                                            const render::Mesh &sourceMesh)
        {
            if (submeshIndex < 0 || static_cast<size_t>(submeshIndex) >= sourceMesh.GetSubmeshCount())
            {
                return nullptr;
            }

            static std::unordered_map<std::string, std::unique_ptr<render::Mesh>> isolatedMeshes;
            const int normalizedCount = std::max(1, submeshCount);
            const size_t submeshEnd = std::min(static_cast<size_t>(submeshIndex + normalizedCount), sourceMesh.GetSubmeshCount());
            const std::string key = reference + "#submesh:" + std::to_string(submeshIndex) + "+" + std::to_string(submeshEnd - static_cast<size_t>(submeshIndex));
            const auto cached = isolatedMeshes.find(key);
            if (cached != isolatedMeshes.end())
            {
                return cached->second.get();
            }

            std::vector<render::Submesh> submeshes(submeshEnd);
            for (size_t index = static_cast<size_t>(submeshIndex); index < submeshEnd; ++index)
            {
                submeshes[index] = sourceMesh.GetSubmesh(index);
            }

            render::MeshConfig config;
            config.data = sourceMesh.GetMeshData();
            config.submeshes = std::move(submeshes);
            config.hasLightmapUvs = sourceMesh.HasLightmapUvs();
            config.skeleton = sourceMesh.GetSkeleton();
            config.animationNodes = sourceMesh.GetAnimationNodes();
            config.animations = sourceMesh.GetAnimations();
            auto mesh = std::unique_ptr<render::Mesh>(render::Mesh::FromConfig(std::move(config)));
            auto *meshPtr = mesh.get();
            isolatedMeshes.emplace(key, std::move(mesh));
            return meshPtr;
        }
    }

    bool InstantiateMeshAssetIntoScene(std::string reference, scene::Entity *parent, int submeshIndex, int submeshCount, int materialSlot)
    {
        if (reference.empty() || assets::Project::GetAssetTypeForReference(reference) != assets::ProjectAssetType::Mesh)
        {
            return false;
        }

        auto &editorShell = EditorShell::GetInstance();
        auto *scene = editorShell.GetEngine().GetScene();
        if (!scene)
        {
            return false;
        }

        scene::Entity *createdEntity = nullptr;
        std::string errorMessage;
        editorShell.ExecuteSceneEdit(submeshIndex >= 0 ? "Instantiate Mesh Subasset" : "Instantiate Mesh Asset",
                                     [scene, parent, reference, submeshIndex, submeshCount, materialSlot, &createdEntity, &errorMessage]()
                                     {
                                         auto entity = std::make_unique<scene::Entity>(scene::EntityConfig{
                                             .name = BuildEntityNameForMeshReference(reference),
                                         });
                                         createdEntity = scene->AddEntity(std::move(entity), parent);
                                         if (!createdEntity || !ConfigureMeshComponentForReference(*createdEntity, reference, submeshIndex, submeshCount, materialSlot, &errorMessage))
                                         {
                                             if (createdEntity)
                                             {
                                                 scene->RemoveEntity(createdEntity);
                                                 createdEntity = nullptr;
                                             }
                                             return;
                                         }
                                     });

        if (createdEntity)
        {
            editorShell.SetSelectedEntity(createdEntity);
            editorShell.MarkSceneDirty();
            return true;
        }

        if (!errorMessage.empty())
        {
            editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
        }
        return false;
    }

    void ContentBrowserPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        auto *project = editorShell.GetProject();
        if (!project)
        {
            ImGui::TextDisabled("No project loaded.");
            return;
        }

        ImGui::SetNextItemWidth(240.0f);
        ImGui::InputText("Filter", m_filterBuffer.data(), m_filterBuffer.size());
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
        {
            project->RefreshAssetRegistry();
            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Refreshed project assets.");
        }
        ImGui::SameLine();
        if (ImGui::Button("Import Model"))
        {
            std::string importedReference;
            std::string errorMessage;
            if (ImportExternalSourceModelIntoAssets(*project, &importedReference, &errorMessage))
            {
                project->RefreshAssetRegistry();
                editorShell.MarkProjectDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Imported model into assets: " + importedReference);
            }
            else if (!errorMessage.empty())
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Material"))
        {
            m_newMaterialNameBuffer.fill('\0');
            ImGui::OpenPopup("Create Material Asset");
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Shader Graph"))
        {
            m_newShaderGraphNameBuffer.fill('\0');
            ImGui::OpenPopup("Create Shader Graph Asset");
        }
        ImGui::SameLine();
        if (ImGui::Button("Create Anim Graph"))
        {
            m_newAnimationGraphNameBuffer.fill('\0');
            ImGui::OpenPopup("Create Animation Graph Asset");
        }

        if (ImGui::BeginPopupModal("Create Material Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newMaterialNameBuffer.data(), m_newMaterialNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newMaterialNameBuffer.data());
            if (!sanitizedName.empty())
            {
                ImGui::TextDisabled("Creates Materials/%s.plutomaterial", sanitizedName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a material name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto materialPath = project->GetAssetDirectoryPath() / "Materials" / (sanitizedName + ".plutomaterial");
                const std::string reference = project->MakeAssetReference(materialPath);
                render::MaterialConfig config;
                config.color = glm::vec4(0.82f, 0.84f, 0.88f, 1.0f);
                config.metallic = 0.0f;
                config.roughness = 0.55f;

                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    editorShell.OpenMaterialAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created material: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create material." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Shader Graph Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newShaderGraphNameBuffer.data(), m_newShaderGraphNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newShaderGraphNameBuffer.data());
            if (!sanitizedName.empty())
            {
                ImGui::TextDisabled("Creates Shaders/%s.plutoshadergraph", sanitizedName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter a shader graph name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto graphPath = project->GetAssetDirectoryPath() / "Shaders" / (sanitizedName + ".plutoshadergraph");
                const std::string reference = project->MakeAssetReference(graphPath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveShaderGraphAsset(reference, render::CreateDefaultShaderGraph(), &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    editorShell.OpenShaderGraphAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created shader graph: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create shader graph." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Create Animation Graph Asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::InputText("Name", m_newAnimationGraphNameBuffer.data(), m_newAnimationGraphNameBuffer.size());
            const std::string sanitizedName = SanitizeAssetFileName(m_newAnimationGraphNameBuffer.data());
            if (!sanitizedName.empty())
            {
                ImGui::TextDisabled("Creates AnimGraphs/%s.plutoanimgraph", sanitizedName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Enter an animation graph name.");
            }

            ImGui::BeginDisabled(sanitizedName.empty());
            if (ImGui::Button("Create"))
            {
                const auto graphPath = project->GetAssetDirectoryPath() / "AnimGraphs" / (sanitizedName + ".plutoanimgraph");
                const std::string reference = project->MakeAssetReference(graphPath);
                std::string errorMessage;
                if (core::Engine::GetInstance().GetAssetManager().SaveAnimationGraphAsset(reference, assets::CreateDefaultAnimationGraphAsset(), &errorMessage))
                {
                    project->RefreshAssetRegistry();
                    editorShell.OpenAnimationGraphAsset(reference);
                    editorShell.MarkProjectDirty();
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "Created animation graph: " + reference);
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to create animation graph." : errorMessage);
                }
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::TextDisabled("Assets: %zu", project->GetManifest().assetEntries.size());
        ImGui::Separator();

        const auto &assets = project->GetManifest().assetEntries;
        const std::string_view filter(m_filterBuffer.data());
        bool registryChanged = m_cachedProject != project || m_cachedAssetReferences.size() != assets.size();
        if (!registryChanged)
        {
            for (std::size_t index = 0; index < assets.size(); ++index)
            {
                if (m_cachedAssetReferences[index] != assets[index].reference)
                {
                    registryChanged = true;
                    break;
                }
            }
        }

        if (registryChanged || m_cachedFilter != filter)
        {
            m_cachedProject = project;
            m_cachedFilter = filter;
            m_cachedAssetReferences.clear();
            m_filteredAssetIndices.clear();
            m_filteredAssetDisplayNames.clear();
            m_cachedAssetReferences.reserve(assets.size());
            m_filteredAssetIndices.reserve(assets.size());
            m_filteredAssetDisplayNames.reserve(assets.size());

            for (int index = 0; index < static_cast<int>(assets.size()); ++index)
            {
                const auto &asset = assets[static_cast<std::size_t>(index)];
                m_cachedAssetReferences.push_back(asset.reference);
                std::string displayName = std::string("[") + std::string(assets::Project::GetAssetTypeName(asset.type)) + "] " + DisplayAssetReference(asset.reference);
                if (ContainsInsensitive(displayName, filter))
                {
                    m_filteredAssetIndices.push_back(index);
                    m_filteredAssetDisplayNames.push_back(std::move(displayName));
                }
            }
            m_hasExpandedMesh = false;
        }

        ImGui::BeginChild("Assets", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 3.0f), true);
        bool hasExpandedMesh = false;
        auto renderAssetRow = [&](int filteredIndex)
        {
            const int index = m_filteredAssetIndices[static_cast<std::size_t>(filteredIndex)];
            const auto &asset = assets[static_cast<std::size_t>(index)];
            const auto &displayName = m_filteredAssetDisplayNames[static_cast<std::size_t>(filteredIndex)];

            const bool selected = m_selectedAssetIndex == index;
            const bool canExpandMesh = asset.type == assets::ProjectAssetType::Mesh &&
                                       !assets::Project::IsEngineAssetReference(asset.reference);
            bool rowActivated = false;
            bool treeOpen = false;
            ImGui::PushID(index);
            if (canExpandMesh)
            {
                const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                 ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                 ImGuiTreeNodeFlags_SpanAvailWidth |
                                                 (selected ? ImGuiTreeNodeFlags_Selected : 0);
                treeOpen = ImGui::TreeNodeEx("AssetTreeNode", flags, "%s", displayName.c_str());
                hasExpandedMesh = hasExpandedMesh || treeOpen;
                rowActivated = ImGui::IsItemClicked();
            }
            else
            {
                rowActivated = ImGui::Selectable(displayName.c_str(), selected);
            }

            if (rowActivated)
            {
                m_selectedAssetIndex = index;
            }

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                const auto resolvedPath = project->ResolveAssetReference(asset.reference);
                if (resolvedPath.extension() == ".plutoscene")
                {
                    editorShell.OpenSceneFromPath(resolvedPath);
                }
                else if (resolvedPath.extension() == ".plutoprefab")
                {
                    editorShell.OpenSceneFromPath(resolvedPath);
                }
                else if (asset.type == assets::ProjectAssetType::Material)
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
                else if (asset.type == assets::ProjectAssetType::Mesh)
                {
                    editorShell.OpenMeshAsset(asset.reference);
                }
                else if (asset.type == assets::ProjectAssetType::ShaderGraph)
                {
                    editorShell.OpenShaderGraphAsset(asset.reference);
                }
                else if (asset.type == assets::ProjectAssetType::AnimationGraph)
                {
                    editorShell.OpenAnimationGraphAsset(asset.reference);
                }
            }

            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kContentBrowserAssetDragDropPayload,
                                          asset.reference.c_str(),
                                          asset.reference.size() + 1);
                ImGui::TextUnformatted(displayName.c_str());
                ImGui::EndDragDropSource();
            }

            if (canExpandMesh && treeOpen)
            {
                try
                {
                    auto *mesh = editorShell.GetEngine().GetAssetManager().LoadMeshAsset(asset.reference);
                    if (!mesh || mesh->GetSubmeshCount() == 0)
                    {
                        ImGui::TextDisabled("No mesh children.");
                    }
                    else
                    {
                        for (size_t submeshIndex = 0; submeshIndex < mesh->GetSubmeshCount();)
                        {
                            const auto &submesh = mesh->GetSubmesh(submeshIndex);
                            const std::string submeshName = submesh.name.empty()
                                                                ? std::string("Mesh ") + std::to_string(submeshIndex)
                                                                : submesh.name;
                            size_t groupEnd = submeshIndex + 1;
                            while (groupEnd < mesh->GetSubmeshCount())
                            {
                                const auto &nextSubmesh = mesh->GetSubmesh(groupEnd);
                                const std::string nextName = nextSubmesh.name.empty()
                                                                 ? std::string("Mesh ") + std::to_string(groupEnd)
                                                                 : nextSubmesh.name;
                                if (nextName != submeshName)
                                {
                                    break;
                                }
                                ++groupEnd;
                            }

                            std::string slotSummary;
                            uint32_t indexCount = 0;
                            for (size_t groupedIndex = submeshIndex; groupedIndex < groupEnd; ++groupedIndex)
                            {
                                const auto &groupedSubmesh = mesh->GetSubmesh(groupedIndex);
                                if (!slotSummary.empty())
                                {
                                    slotSummary += ", ";
                                }
                                slotSummary += std::to_string(groupedSubmesh.materialIndex);
                                indexCount += groupedSubmesh.indexCount;
                            }

                            const int groupCount = static_cast<int>(groupEnd - submeshIndex);
                            const std::string submeshDisplayName = submeshName +
                                                                   (groupCount > 1 ? " [" + std::to_string(groupCount) + " parts, slots " + slotSummary + "]"
                                                                                   : " [Slot " + slotSummary + "]");
                            ImGui::PushID(static_cast<int>(submeshIndex));
                            ImGui::Selectable(submeshDisplayName.c_str(), false);
                            if (ImGui::BeginDragDropSource())
                            {
                                ContentBrowserMeshSubassetPayload payload{};
                                strncpy_s(payload.sourceReference, asset.reference.c_str(), _TRUNCATE);
                                payload.submeshIndex = static_cast<int>(submeshIndex);
                                payload.submeshCount = groupCount;
                                payload.materialSlot = static_cast<int>(submesh.materialIndex);
                                ImGui::SetDragDropPayload(kContentBrowserMeshSubassetDragDropPayload,
                                                          &payload,
                                                          sizeof(payload));
                                ImGui::Text("%s", submeshDisplayName.c_str());
                                ImGui::TextDisabled("Material slots %s, %u indices", slotSummary.c_str(), indexCount);
                                ImGui::EndDragDropSource();
                            }
                            ImGui::SameLine();
                            ImGui::TextDisabled("slots %s, %u indices", slotSummary.c_str(), indexCount);
                            ImGui::PopID();
                            submeshIndex = groupEnd;
                        }
                    }
                }
                catch (const std::exception &exception)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", exception.what());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        };

        if (m_hasExpandedMesh)
        {
            for (int filteredIndex = 0; filteredIndex < static_cast<int>(m_filteredAssetIndices.size()); ++filteredIndex)
            {
                renderAssetRow(filteredIndex);
            }
        }
        else
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(m_filteredAssetIndices.size()));
            while (clipper.Step())
            {
                for (int filteredIndex = clipper.DisplayStart; filteredIndex < clipper.DisplayEnd; ++filteredIndex)
                {
                    renderAssetRow(filteredIndex);
                }
            }
        }
        m_hasExpandedMesh = hasExpandedMesh;
        ImGui::EndChild();

        if (m_selectedAssetIndex >= 0 && m_selectedAssetIndex < static_cast<int>(assets.size()))
        {
            const auto &asset = assets[static_cast<std::size_t>(m_selectedAssetIndex)];
            const auto resolvedPath = project->ResolveAssetReference(asset.reference);
            const std::string typeName(assets::Project::GetAssetTypeName(asset.type));
            ImGui::Text("Type: %s", typeName.c_str());
            ImGui::TextWrapped("Reference: %s", asset.reference.c_str());
            if (assets::Project::IsEngineAssetReference(asset.reference))
            {
                ImGui::TextWrapped("Engine Asset");
            }
            else
            {
                ImGui::TextWrapped("Path: %s", resolvedPath.string().c_str());
            }

            if (asset.type == assets::ProjectAssetType::Material)
            {
                if (ImGui::Button("Open Material"))
                {
                    editorShell.OpenMaterialAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::Animation)
            {
                if (ImGui::Button("Add Clips From Model"))
                {
                    const std::string selectedPath = BrowseSourceModelPath();
                    if (!selectedPath.empty())
                    {
                        std::string errorMessage;
                        if (AddClipsFromSourceModelToAnimationAsset(*project, asset.reference, selectedPath, &errorMessage))
                        {
                            project->RefreshAssetRegistry();
                            editorShell.MarkProjectDirty();
                            editorShell.Log(EditorShell::ConsoleSeverity::Info, "Added animation clips to: " + asset.reference);
                        }
                        else
                        {
                            editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                            errorMessage.empty() ? "Failed to add animation clips." : errorMessage);
                        }
                    }
                }
            }
            else if (asset.type == assets::ProjectAssetType::Mesh)
            {
                if (ImGui::Button("Open Mesh"))
                {
                    editorShell.OpenMeshAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::ShaderGraph)
            {
                if (ImGui::Button("Open Shader Graph"))
                {
                    editorShell.OpenShaderGraphAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::AnimationGraph)
            {
                if (ImGui::Button("Open Animation Graph"))
                {
                    editorShell.OpenAnimationGraphAsset(asset.reference);
                }
            }
            else if (asset.type == assets::ProjectAssetType::SourceModel)
            {
                if (ImGui::Button("Import Model"))
                {
                    std::string errorMessage;
                    if (ImportSourceModelAsset(*project, asset, &errorMessage))
                    {
                        project->RefreshAssetRegistry();
                        editorShell.MarkProjectDirty();
                        editorShell.Log(EditorShell::ConsoleSeverity::Info, "Imported model assets: " + asset.reference);
                    }
                    else
                    {
                        editorShell.Log(EditorShell::ConsoleSeverity::Error,
                                        errorMessage.empty() ? "Failed to import model." : errorMessage);
                    }
                }
            }
        }
    }
}
