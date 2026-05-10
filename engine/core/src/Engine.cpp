#include "PlutoGE/core/Engine.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"

#include <chrono>
#include <filesystem>
#include <iostream>

#include "PlutoGE/platform/Window.h"

namespace PlutoGE::core
{
    namespace
    {
        std::string NormalizePath(const std::string &filePath)
        {
            return std::filesystem::absolute(std::filesystem::path(filePath)).lexically_normal().string();
        }

        bool IsFutureReady(std::future<assetimport::ImportedMeshSourceAsset> &future)
        {
            return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }
    }

    bool Engine::Initialize(const EngineConfig &config)
    {
        m_config = config;

        if (!m_window.Create(m_config.windowConfig))
        {
            std::cerr << "Failed to create window." << std::endl;
            return false;
        }

        render::RendererConfig rendererConfig;
        rendererConfig.window = &m_window;
        if (!m_renderer.Initialize(rendererConfig))
        {
            std::cerr << "Failed to initialize renderer." << std::endl;
            return false;
        }

        m_scriptEngine.Initialize();

        m_isInitialized = true;
        return true;
    }

    ImportedRenderMeshAsset Engine::BuildImportedRenderMeshAsset(const std::string &normalizedPath, const assetimport::ImportedMeshAsset &importedMeshAsset)
    {
        ImportedRenderMeshAsset importedRenderMeshAsset;
        importedRenderMeshAsset.mesh = importedMeshAsset.mesh;
        if (!importedMeshAsset.mesh)
        {
            return importedRenderMeshAsset;
        }

        // LRU cache for imported materials
        constexpr size_t kMaxMaterialCacheSize = 8;
        auto cachedMaterials = m_importedMaterialCache.find(normalizedPath);
        if (cachedMaterials == m_importedMaterialCache.end())
        {
            // Evict oldest if over limit
            if (m_importedMaterialCache.size() >= kMaxMaterialCacheSize)
            {
                m_importedMaterialCache.erase(m_importedMaterialCache.begin());
            }
            std::vector<std::unique_ptr<render::Material>> importedMaterials;
            importedMaterials.reserve(importedMeshAsset.materials ? importedMeshAsset.materials->size() : 0);

            // Copy textures so we can clear pixel buffers after upload
            std::vector<assetimport::ImportedTextureData> tempTextures;
            if (importedMeshAsset.textures)
            {
                tempTextures = *importedMeshAsset.textures;
            }

            const auto loadImportedTexture = [this, &tempTextures](int textureIndex) -> render::Texture *
            {
                if (textureIndex < 0 || textureIndex >= static_cast<int>(tempTextures.size()))
                {
                    return nullptr;
                }
                auto &texture = tempTextures[textureIndex];
                if (!texture.pixels.empty() && texture.width > 0 && texture.height > 0 && texture.channels > 0)
                {
                    auto *tex = m_textureManager.LoadTextureFromMemory(
                        texture.cacheKey,
                        texture.pixels.data(),
                        texture.width,
                        texture.height,
                        texture.channels);
                    // Clear pixel buffer after upload
                    std::vector<unsigned char>().swap(texture.pixels);
                    return tex;
                }
                if (!texture.sourcePath.empty())
                {
                    return m_textureManager.LoadTextureFromFile(texture.sourcePath.c_str());
                }
                return nullptr;
            };

            if (importedMeshAsset.materials)
            {
                for (const auto &material : *importedMeshAsset.materials)
                {
                    render::MaterialConfig config;
                    config.color = material.color;
                    config.metallic = material.metallic;
                    config.roughness = material.roughness;
                    config.albedoTexture = loadImportedTexture(material.albedoTextureIndex);
                    config.normalTexture = loadImportedTexture(material.normalTextureIndex);
                    config.flipNormalY = material.flipNormalY;
                    if (material.metallicRoughnessTextureIndex >= 0)
                    {
                        auto *packedTexture = loadImportedTexture(material.metallicRoughnessTextureIndex);
                        config.metallicTexture = packedTexture;
                        config.metallicTextureChannel = render::TextureChannel::Blue;
                        config.roughnessTexture = packedTexture;
                        config.roughnessTextureChannel = render::TextureChannel::Green;
                    }
                    importedMaterials.push_back(std::make_unique<render::Material>(config));
                }
            }
            cachedMaterials = m_importedMaterialCache.emplace(normalizedPath, std::move(importedMaterials)).first;
        }
        importedRenderMeshAsset.materials.reserve(cachedMaterials->second.size());
        for (const auto &material : cachedMaterials->second)
        {
            importedRenderMeshAsset.materials.push_back(material.get());
        }
        return importedRenderMeshAsset;
    }

    ImportedRenderMeshAsset Engine::FinalizeImportedMeshAsset(const std::string &filePath, assetimport::ImportedMeshSourceAsset importedMeshSourceAsset)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto importedMeshAsset = m_meshImporter.FinalizeImportedMeshAsset(normalizedPath, std::move(importedMeshSourceAsset));
        return BuildImportedRenderMeshAsset(normalizedPath, importedMeshAsset);
    }

    ImportedRenderMeshAsset Engine::ImportMeshAsset(const std::string &filePath)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto importedMeshAsset = m_meshImporter.ImportMeshAsset(normalizedPath);
        return BuildImportedRenderMeshAsset(normalizedPath, importedMeshAsset);
    }

    render::Mesh *Engine::ImportMesh(const std::string &filePath)
    {
        return m_meshImporter.ImportMesh(filePath);
    }

    void Engine::QueueMeshImport(scene::EntityID entityId, const std::string &filePath)
    {
        const auto normalizedPath = NormalizePath(filePath);
        if (m_pendingMeshImports.find(entityId) != m_pendingMeshImports.end())
        {
            return;
        }

        m_meshImportErrors.erase(entityId);
        m_pendingMeshImports.emplace(entityId, PendingMeshImportJob{
                                                   .entityId = entityId,
                                                   .normalizedPath = normalizedPath,
                                                   .future = std::async(std::launch::async, [normalizedPath]()
                                                                        {
                                                                        assetimport::MeshImporter importer;
                                                                        return importer.ImportMeshSourceAsset(normalizedPath); }),
                                               });
    }

    void Engine::UpdateAsyncMeshImports()
    {
        for (auto iterator = m_pendingMeshImports.begin(); iterator != m_pendingMeshImports.end();)
        {
            auto &job = iterator->second;
            if (!IsFutureReady(job.future))
            {
                ++iterator;
                continue;
            }

            try
            {
                auto importedRenderMeshAsset = FinalizeImportedMeshAsset(job.normalizedPath, job.future.get());
                if (!importedRenderMeshAsset.mesh)
                {
                    m_meshImportErrors[job.entityId] = "Mesh import finished without creating a mesh.";
                }
                else if (!m_scene)
                {
                    m_meshImportErrors[job.entityId] = "No active scene to receive the imported mesh.";
                }
                else
                {
                    auto *entity = m_scene->FindEntityByID(job.entityId);
                    if (!entity)
                    {
                        m_meshImportErrors[job.entityId] = "The target entity no longer exists.";
                    }
                    else if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
                    {
                        meshComponent->SetMesh(importedRenderMeshAsset.mesh);
                        meshComponent->SetMaterials(importedRenderMeshAsset.materials);
                    }
                    else
                    {
                        m_meshImportErrors[job.entityId] = "The target entity no longer has a mesh component.";
                    }
                }
            }
            catch (const std::exception &exception)
            {
                m_meshImportErrors[job.entityId] = exception.what();
            }

            iterator = m_pendingMeshImports.erase(iterator);
        }
    }

    MeshImportStatus Engine::GetMeshImportStatus(scene::EntityID entityId) const
    {
        MeshImportStatus status;
        const auto pendingImport = m_pendingMeshImports.find(entityId);
        if (pendingImport != m_pendingMeshImports.end())
        {
            status.pending = true;
            status.filePath = pendingImport->second.normalizedPath;
        }

        const auto importError = m_meshImportErrors.find(entityId);
        if (importError != m_meshImportErrors.end())
        {
            status.errorMessage = importError->second;
        }

        return status;
    }

    void Engine::Run()
    {
        auto previousFrame = std::chrono::steady_clock::now();

        while (m_isInitialized && (!m_window.IsOpen() || !m_window.ShouldClose()))
        {
            const auto currentFrame = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(currentFrame - previousFrame).count();
            previousFrame = currentFrame;

            if (m_window.IsOpen())
            {
                m_window.PollEvents();
            }

            if (m_scene)
            {
                m_scene->Update(deltaTime);
            }
        }
    }

    void Engine::Shutdown()
    {
        m_scriptEngine.Shutdown();
        m_renderer.Shutdown();
        m_window.Close();
        m_isInitialized = false;
    }
}