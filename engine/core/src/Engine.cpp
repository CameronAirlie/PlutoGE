#include "PlutoGE/core/Engine.h"

#include "PlutoGE/scene/Scene.h"

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

    ImportedRenderMeshAsset Engine::ImportMeshAsset(const std::string &filePath)
    {
        ImportedRenderMeshAsset importedRenderMeshAsset;

        const auto normalizedPath = NormalizePath(filePath);
        const auto importedMeshAsset = m_meshImporter.ImportMeshAsset(normalizedPath);
        importedRenderMeshAsset.mesh = importedMeshAsset.mesh;
        if (!importedMeshAsset.mesh)
        {
            return importedRenderMeshAsset;
        }

        auto cachedMaterials = m_importedMaterialCache.find(normalizedPath);
        if (cachedMaterials == m_importedMaterialCache.end())
        {
            std::vector<std::unique_ptr<render::Material>> importedMaterials;
            importedMaterials.reserve(importedMeshAsset.materials ? importedMeshAsset.materials->size() : 0);

            const auto loadImportedTexture = [this, &importedMeshAsset](int textureIndex) -> render::Texture *
            {
                if (!importedMeshAsset.textures || textureIndex < 0 || textureIndex >= static_cast<int>(importedMeshAsset.textures->size()))
                {
                    return nullptr;
                }

                const auto &texture = importedMeshAsset.textures->at(textureIndex);
                if (!texture.pixels.empty() && texture.width > 0 && texture.height > 0 && texture.channels > 0)
                {
                    return m_textureManager.LoadTextureFromMemory(
                        texture.cacheKey,
                        texture.pixels.data(),
                        texture.width,
                        texture.height,
                        texture.channels);
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

    render::Mesh *Engine::ImportMesh(const std::string &filePath)
    {
        return m_meshImporter.ImportMesh(filePath);
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