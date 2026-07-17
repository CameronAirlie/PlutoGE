#include "PlutoGE/core/Engine.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <type_traits>

#include "PlutoGE/platform/Window.h"

namespace PlutoGE::core
{
    namespace
    {
        using ImportClock = std::chrono::steady_clock;

        std::string NormalizePath(const std::string &filePath)
        {
            return std::filesystem::absolute(std::filesystem::path(filePath)).lexically_normal().string();
        }

        bool IsMeshFinalizeProfilingEnabled()
        {
            static const bool enabled = []()
            {
#ifdef _WIN32
                char *value = nullptr;
                size_t valueLength = 0;
                const errno_t result = _dupenv_s(&value, &valueLength, "PLUTOGE_PROFILE_MESH_FINALIZE");
                const bool isEnabled = result == 0 && value != nullptr && value[0] != '\0' && value[0] != '0';
                std::free(value);
                return isEnabled;
#else
                const char *value = std::getenv("PLUTOGE_PROFILE_MESH_FINALIZE");
                return value != nullptr && value[0] != '\0' && value[0] != '0';
#endif
            }();
            return enabled;
        }

        double ElapsedMilliseconds(ImportClock::time_point startTime)
        {
            return std::chrono::duration<double, std::milli>(ImportClock::now() - startTime).count();
        }

        struct MeshFinalizeProfile
        {
            bool enabled = false;
            std::string filePath;
            bool materialCacheHit = false;
            double textureCopyMs = 0.0;
            double textureResolveMs = 0.0;
            double materialCreateMs = 0.0;
            size_t textureRequests = 0;
            size_t textureResolveHits = 0;
            size_t memoryTextureUploads = 0;
            size_t fileTextureLoads = 0;

            ~MeshFinalizeProfile()
            {
                if (!enabled)
                {
                    return;
                }

                std::cerr
                    << "Mesh finalize profile for '" << filePath << "': "
                    << "materialCacheHit=" << (materialCacheHit ? "yes" : "no") << ", "
                    << "textureCopy=" << textureCopyMs << "ms, "
                    << "textureResolve=" << textureResolveMs << "ms, "
                    << "materialCreate=" << materialCreateMs << "ms, "
                    << "textureRequests=" << textureRequests << ", "
                    << "resolvedFromLocalCache=" << textureResolveHits << ", "
                    << "memoryUploads=" << memoryTextureUploads << ", "
                    << "fileLoads=" << fileTextureLoads
                    << std::endl;
            }
        };

        bool IsFutureReady(std::future<assetimport::ImportedMeshSourceAsset> &future)
        {
            return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }

        void HashCombine(uint64_t &seed, uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6ull) + (seed >> 2ull);
        }

        template <typename T>
        void HashValue(uint64_t &seed, const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            uint64_t hash = 1469598103934665603ull;
            const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
            for (size_t index = 0; index < sizeof(T); ++index)
            {
                hash ^= bytes[index];
                hash *= 1099511628211ull;
            }
            HashCombine(seed, hash);
        }

        void HashString(uint64_t &seed, const std::string &value)
        {
            uint64_t hash = 1469598103934665603ull;
            for (const char character : value)
            {
                hash ^= static_cast<unsigned char>(character);
                hash *= 1099511628211ull;
            }
            HashCombine(seed, hash);
        }

        uint64_t BuildImportedMaterialFingerprint(const assetimport::ImportedMeshAsset &importedMeshAsset)
        {
            uint64_t fingerprint = 1469598103934665603ull;
            const size_t materialCount = importedMeshAsset.materials ? importedMeshAsset.materials->size() : 0;
            HashValue(fingerprint, materialCount);
            if (importedMeshAsset.materials)
            {
                for (const auto &material : *importedMeshAsset.materials)
                {
                    HashValue(fingerprint, material.color.r);
                    HashValue(fingerprint, material.color.g);
                    HashValue(fingerprint, material.color.b);
                    HashValue(fingerprint, material.color.a);
                    HashValue(fingerprint, material.surfaceType);
                    HashValue(fingerprint, material.alphaMode);
                    HashValue(fingerprint, material.alphaCutoff);
                    HashValue(fingerprint, material.castsShadow);
                    HashValue(fingerprint, material.metallic);
                    HashValue(fingerprint, material.roughness);
                    HashValue(fingerprint, material.emission.r);
                    HashValue(fingerprint, material.emission.g);
                    HashValue(fingerprint, material.emission.b);
                    HashValue(fingerprint, material.transmission);
                    HashValue(fingerprint, material.ior);
                    HashValue(fingerprint, material.thickness);
                    HashValue(fingerprint, material.attenuationColor.r);
                    HashValue(fingerprint, material.attenuationColor.g);
                    HashValue(fingerprint, material.attenuationColor.b);
                    HashValue(fingerprint, material.attenuationDistance);
                    HashValue(fingerprint, material.albedoTextureIndex);
                    HashValue(fingerprint, material.normalTextureIndex);
                    HashValue(fingerprint, material.metallicRoughnessTextureIndex);
                    HashValue(fingerprint, material.metallicRoughnessTextureHasMetallicChannel);
                    HashValue(fingerprint, material.flipNormalY);
                }
            }

            const size_t textureCount = importedMeshAsset.textures ? importedMeshAsset.textures->size() : 0;
            HashValue(fingerprint, textureCount);
            if (importedMeshAsset.textures)
            {
                for (const auto &texture : *importedMeshAsset.textures)
                {
                    HashString(fingerprint, texture.cacheKey);
                    HashString(fingerprint, texture.sourcePath);
                    HashValue(fingerprint, texture.width);
                    HashValue(fingerprint, texture.height);
                    HashValue(fingerprint, texture.channels);
                    HashValue(fingerprint, texture.pixels.size());
                }
            }

            return fingerprint;
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

        m_textureManager.SetWindow(&m_window);

        render::RendererConfig rendererConfig;
        rendererConfig.window = &m_window;
        if (!m_renderer.Initialize(rendererConfig))
        {
            std::cerr << "Failed to initialize renderer." << std::endl;
            m_textureManager.SetWindow(nullptr);
            m_window.Close();
            return false;
        }

        if (!m_audioSystem.Initialize())
        {
            std::cerr << "Failed to initialize audio system." << std::endl;
        }

        m_scriptEngine.Initialize();

        m_isInitialized = true;
        return true;
    }

    ImportedRenderMeshAsset Engine::BuildImportedRenderMeshAsset(const std::string &normalizedPath, const assetimport::ImportedMeshAsset &importedMeshAsset)
    {
        MeshFinalizeProfile profile;
        profile.enabled = IsMeshFinalizeProfilingEnabled();
        profile.filePath = normalizedPath;

        ImportedRenderMeshAsset importedRenderMeshAsset;
        importedRenderMeshAsset.mesh = importedMeshAsset.mesh;
        importedRenderMeshAsset.animations = importedMeshAsset.animations;
        if (!importedMeshAsset.mesh)
        {
            return importedRenderMeshAsset;
        }

        // LRU cache for imported materials
        constexpr size_t kMaxMaterialCacheSize = 8;
        const uint64_t materialFingerprint = BuildImportedMaterialFingerprint(importedMeshAsset);
        auto cachedMaterials = m_importedMaterialCache.find(normalizedPath);
        if (cachedMaterials != m_importedMaterialCache.end() && cachedMaterials->second.fingerprint != materialFingerprint)
        {
            auto retiredNode = m_importedMaterialCache.extract(cachedMaterials);
            m_retiredImportedMaterialCache.push_back(std::move(retiredNode.mapped()));
            if (m_retiredImportedMaterialCache.size() > kMaxMaterialCacheSize)
            {
                m_retiredImportedMaterialCache.erase(m_retiredImportedMaterialCache.begin());
            }
            cachedMaterials = m_importedMaterialCache.end();
        }

        if (cachedMaterials == m_importedMaterialCache.end())
        {
            // Evict oldest if over limit
            if (m_importedMaterialCache.size() >= kMaxMaterialCacheSize)
            {
                auto retiredNode = m_importedMaterialCache.extract(m_importedMaterialCache.begin());
                m_retiredImportedMaterialCache.push_back(std::move(retiredNode.mapped()));
                if (m_retiredImportedMaterialCache.size() > kMaxMaterialCacheSize)
                {
                    m_retiredImportedMaterialCache.erase(m_retiredImportedMaterialCache.begin());
                }
            }
            std::vector<std::unique_ptr<render::Material>> importedMaterials;
            importedMaterials.reserve(importedMeshAsset.materials ? importedMeshAsset.materials->size() : 0);

            auto *importedTextures = importedMeshAsset.textures;
            std::vector<render::Texture *> resolvedTextures(importedTextures ? importedTextures->size() : 0, nullptr);

            const auto loadImportedTexture = [this, importedTextures, &resolvedTextures, &profile](int textureIndex) -> render::Texture *
            {
                profile.textureRequests += 1;
                if (!importedTextures || textureIndex < 0 || textureIndex >= static_cast<int>(importedTextures->size()))
                {
                    return nullptr;
                }

                if (resolvedTextures[textureIndex] != nullptr)
                {
                    profile.textureResolveHits += 1;
                    return resolvedTextures[textureIndex];
                }

                const auto textureResolveStart = ImportClock::now();
                auto &texture = (*importedTextures)[textureIndex];
                if (auto *cachedTexture = m_textureManager.FindTexture(texture.cacheKey))
                {
                    profile.textureResolveHits += 1;
                    profile.textureResolveMs += ElapsedMilliseconds(textureResolveStart);
                    resolvedTextures[textureIndex] = cachedTexture;
                    return cachedTexture;
                }

                if (!texture.pixels.empty() && texture.width > 0 && texture.height > 0 && texture.channels > 0)
                {
                    auto *tex = m_textureManager.LoadTextureFromMemory(
                        texture.cacheKey,
                        texture.pixels.data(),
                        texture.width,
                        texture.height,
                        texture.channels);
                    profile.textureResolveMs += ElapsedMilliseconds(textureResolveStart);
                    profile.memoryTextureUploads += 1;
                    resolvedTextures[textureIndex] = tex;
                    return tex;
                }
                if (!texture.sourcePath.empty())
                {
                    auto *tex = m_textureManager.LoadTextureFromFile(texture.sourcePath.c_str());
                    profile.textureResolveMs += ElapsedMilliseconds(textureResolveStart);
                    profile.fileTextureLoads += 1;
                    resolvedTextures[textureIndex] = tex;
                    return tex;
                }
                profile.textureResolveMs += ElapsedMilliseconds(textureResolveStart);
                return nullptr;
            };

            if (importedMeshAsset.materials)
            {
                const auto materialCreateStart = ImportClock::now();
                for (const auto &material : *importedMeshAsset.materials)
                {
                    render::MaterialConfig config;
                    config.color = material.color;
                    config.surfaceType = material.surfaceType;
                    config.alphaMode = material.alphaMode;
                    config.alphaCutoff = material.alphaCutoff;
                    config.castsShadow = material.castsShadow;
                    config.metallic = material.metallic;
                    config.roughness = material.roughness;
                    config.emission = material.emission;
                    config.transmission = material.transmission;
                    config.ior = material.ior;
                    config.thickness = material.thickness;
                    config.attenuationColor = material.attenuationColor;
                    config.attenuationDistance = material.attenuationDistance;
                    config.albedoTexture = loadImportedTexture(material.albedoTextureIndex);
                    config.normalTexture = loadImportedTexture(material.normalTextureIndex);
                    config.flipNormalY = material.flipNormalY;
                    if (material.metallicRoughnessTextureIndex >= 0)
                    {
                        auto *packedTexture = loadImportedTexture(material.metallicRoughnessTextureIndex);
                        config.roughnessTexture = packedTexture;
                        config.roughnessTextureChannel = render::TextureChannel::Green;
                        if (material.metallicRoughnessTextureHasMetallicChannel)
                        {
                            config.metallicTexture = packedTexture;
                            config.metallicTextureChannel = render::TextureChannel::Blue;
                        }
                    }
                    importedMaterials.push_back(std::make_unique<render::Material>(config));
                }
                profile.materialCreateMs = ElapsedMilliseconds(materialCreateStart);
            }
            ImportedMaterialCacheEntry cacheEntry;
            cacheEntry.fingerprint = materialFingerprint;
            cacheEntry.materials = std::move(importedMaterials);
            cachedMaterials = m_importedMaterialCache.emplace(normalizedPath, std::move(cacheEntry)).first;
        }
        else
        {
            profile.materialCacheHit = true;
        }

        importedRenderMeshAsset.materials.reserve(cachedMaterials->second.materials.size());
        for (const auto &material : cachedMaterials->second.materials)
        {
            importedRenderMeshAsset.materials.push_back(material.get());
        }
        return importedRenderMeshAsset;
    }

    ImportedRenderMeshAsset Engine::FinalizeImportedMeshAsset(const std::string &filePath, assetimport::ImportedMeshSourceAsset importedMeshSourceAsset, const assetimport::MeshImportOptions &options)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto importedMeshAsset = m_meshImporter.FinalizeImportedMeshAsset(normalizedPath, std::move(importedMeshSourceAsset), options);
        return BuildImportedRenderMeshAsset(normalizedPath, importedMeshAsset);
    }

    ImportedRenderMeshAsset Engine::ImportMeshAsset(const std::string &filePath, const assetimport::MeshImportOptions &options)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto importedMeshAsset = m_meshImporter.ImportMeshAsset(normalizedPath, options);
        return BuildImportedRenderMeshAsset(normalizedPath, importedMeshAsset);
    }

    ImportedRenderMeshAsset Engine::GenerateMeshAssetLods(const std::string &filePath, const assetimport::MeshImportOptions &options)
    {
        const auto normalizedPath = NormalizePath(filePath);
        const auto importedMeshAsset = m_meshImporter.GenerateMeshLods(normalizedPath, options);
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
                        meshComponent->SetSourceMeshPath(job.normalizedPath);
                        if (importedRenderMeshAsset.animations && !importedRenderMeshAsset.animations->empty())
                        {
                            auto *animationComponent = entity->GetComponent<scene::AnimationComponent>();
                            if (!animationComponent)
                            {
                                animationComponent = entity->CreateComponent<scene::AnimationComponent>();
                            }

                            animationComponent->SetClipsFromImportedAnimations(*importedRenderMeshAsset.animations);
                            animationComponent->SetSourceAnimationPath(job.normalizedPath);
                        }
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

    void Engine::SetScene(scene::Scene *scene)
    {
        if (m_scene == scene)
        {
            return;
        }

        if (m_isRuntimeRunning && m_scene)
        {
            m_scene->StopRuntime();
        }

        // Scene updates may already have submitted commands containing pointers
        // into the outgoing scene. Never carry those across a scene transition.
        m_renderer.ClearRenderCommands();
        m_scene = scene;

        if (m_isRuntimeRunning && m_scene)
        {
            m_scene->StartRuntime();
        }
    }

    bool Engine::RequestSceneLoad(std::string sceneAssetReference)
    {
        if (!m_isRuntimeRunning || sceneAssetReference.empty())
        {
            return false;
        }

        m_pendingSceneLoadRequest = std::move(sceneAssetReference);
        return true;
    }

    std::optional<std::string> Engine::ConsumeSceneLoadRequest()
    {
        auto request = std::move(m_pendingSceneLoadRequest);
        m_pendingSceneLoadRequest.reset();
        return request;
    }

    void Engine::StartRuntime()
    {
        if (m_isRuntimeRunning)
        {
            return;
        }

        m_isRuntimeRunning = true;
        if (m_scene)
        {
            m_scene->StartRuntime();
        }
    }

    void Engine::StopRuntime()
    {
        if (!m_isRuntimeRunning)
        {
            return;
        }

        if (m_scene)
        {
            m_scene->StopRuntime();
        }

        m_isRuntimeRunning = false;
        m_pendingSceneLoadRequest.reset();
    }

    void Engine::Shutdown()
    {
        StopRuntime();
        m_scriptEngine.Shutdown();
        m_audioSystem.Shutdown();
        m_renderer.Shutdown();
        m_textureManager.SetWindow(nullptr);
        m_window.Close();
        m_isInitialized = false;
    }
}
