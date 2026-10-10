#pragma once

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/AssetCatalog.h"
#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/assets/ParticleSystemAsset.h"
#include "PlutoGE/assets/SurfaceResponseAsset.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/ShaderGraph.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class Texture;
    enum class TextureColorSpace : std::uint8_t;
    class Mesh;
    class Material;
    class Shader;
    struct MaterialConfig;
    struct MeshConfig;
    struct AnimationClip;
    struct ShaderSource;
    struct ShaderGraph;
}

namespace PlutoGE::assets
{
    struct MeshAssetMetadata
    {
        std::string sourceAssetReference;
        std::string sourceAssetId;
        std::uint64_t sourceObjectId = 0;
        assetimport::MeshImportOptions importOptions;
    };

    class AssetManager
    {
    public:
        // Scoped readers own resources loaded into their caches; application readers preserve
        // the legacy borrowed-resource lifetime across cache invalidation.
        enum class ResourceLifetime { Application, Scoped };
        // sharedAssets must outlive a scoped reader. Only retainedSourceAssetId
        // stays private; external authored assets use the shared application reader.
        explicit AssetManager(ResourceLifetime lifetime = ResourceLifetime::Application,
            AssetManager *sharedAssets = nullptr, std::string retainedSourceAssetId = {});
        ~AssetManager();
        AssetManager(const AssetManager &) = delete;
        AssetManager &operator=(const AssetManager &) = delete;

        std::string GetAssetPath(const std::string &relativePath) const;
        std::string ResolveAssetPath(const std::string &assetPath) const;
        std::string ResolveMeshAssetSourcePath(const std::string &assetReference);
        std::string PersistAssetPath(const std::string &filePath) const;
        std::string GetStableAssetId(const std::string &assetReference) const;
        std::string ResolveStableAssetId(const std::string &assetId, const std::string &fallbackReference = {}) const;
        std::string ResolveModelObject(const std::string &modelAssetId, std::uint64_t localId) const;
        // Call on the resource-owning thread after publishing a database scan.
        // Publish both snapshots on the owning thread; preserve borrowed materials
        // when an unambiguous identity moves to another physical generation.
        void SetAssetSnapshot(std::shared_ptr<const AssetCatalog> catalog,
            std::shared_ptr<const AssetStorageMap> storage);
        void SetAssetCatalog(std::shared_ptr<const AssetCatalog> catalog);
        std::shared_ptr<const AssetCatalog> GetAssetCatalog() const { return m_catalog; }
        std::shared_ptr<const AssetStorageMap> GetAssetStorageMap() const { return m_storage; }
        // Install a database-validated snapshot alongside its catalog on the owning thread.
        // Project owner calls this only after explicitly saving a format conversion.
        void SetProjectAssetPipelineVersion(std::uint32_t version) { m_assetPipelineVersion = version; }
        void SetAssetStorageMap(std::shared_ptr<const AssetStorageMap> storage) { m_storage = std::move(storage); }
        // Catalog ownership applies to logical IDs and physical location aliases.
        bool IsImportedAsset(const std::string &reference) const;
        // The project format owner opts in only types whose readers support IDs.
        // Context changes reset this list to preserve legacy writer behavior.
        void SetLogicalReferenceTypes(const std::vector<ProjectAssetType> &types);
        bool LoadAssetCatalog(const std::string &path, std::string *errorMessage = nullptr);
        std::string ResolveAssetReference(const AssetReference &reference) const;

        render::Texture *LoadTexture(const char *filePath);
        render::Texture *LoadTexture(const char *filePath, render::TextureColorSpace colorSpace);
        render::Mesh *LoadMeshAsset(const std::string &assetReference);
        const std::vector<std::string> &GetMeshAssetMaterialReferences(const std::string &assetReference);
        bool ReplaceMeshAssetMaterialReference(const std::string &meshAssetReference,
                                               const std::string &oldMaterialReference,
                                               const std::string &newMaterialReference,
                                               std::string *errorMessage = nullptr);
        const MeshAssetMetadata &GetMeshAssetMetadata(const std::string &assetReference);
        // Authored bindings stay in Assets even when geometry lives in Library.
        std::filesystem::path GetMeshAssetMaterialOverridePath(const std::string &assetReference) const;
        // CPU-only decoding; failure leaves every output unchanged.
        bool LoadMeshAssetData(const std::string &reference, render::MeshConfig &config,
                               std::vector<std::string> &materialReferences, MeshAssetMetadata &metadata,
                               std::string *errorMessage = nullptr) const;
        bool SaveMeshAsset(const std::string &assetReference,
                           const render::MeshConfig &config,
                           const std::vector<std::string> &materialReferences,
                           std::string *errorMessage = nullptr,
                           const MeshAssetMetadata &metadata = {});
        bool LoadAnimationAsset(const std::string &assetReference, std::vector<render::AnimationClip> &clips) const;
        bool LoadAnimationClipAsset(const std::string &assetReference, render::AnimationClip &clip) const;
        bool LoadAnimationClipReferences(const std::string &assetReference, std::vector<std::string> &clipReferences) const;
        bool SaveAnimationAssetReferences(const std::string &assetReference,
                                          const std::vector<std::string> &clipReferences,
                                          std::string *errorMessage = nullptr);
        bool SaveAnimationClipAsset(const std::string &assetReference,
                                    const render::AnimationClip &clip,
                                    std::string *errorMessage = nullptr);
        // IO-free lookup; useful for distinguishing borrowed assets from instance clones.
        render::Material *FindLoadedMaterialAsset(const std::string &assetReference) const;
        render::Material *LoadMaterialAsset(const std::string &assetReference);
        // Reread saved assets while preserving Material pointers held by scenes.
        void ReloadMaterialAssets();
        // Resource-owning thread only. Existing mesh/texture borrowers remain
        // alive; subsequent loads and metadata reads use the published files.
        void RefreshImportedAssets(const std::vector<std::string> &references);
        bool SaveMaterialAsset(const std::string &assetReference, const render::MaterialConfig &config, std::string *errorMessage = nullptr);
        render::ShaderGraph LoadShaderGraphAsset(const std::string &assetReference, bool *loaded = nullptr);
        bool SaveShaderGraphAsset(const std::string &assetReference, const render::ShaderGraph &graph, std::string *errorMessage = nullptr);
        AnimationGraphAsset LoadAnimationGraphAsset(const std::string &assetReference, bool *loaded = nullptr);
        bool SaveAnimationGraphAsset(const std::string &assetReference, const AnimationGraphAsset &graph, std::string *errorMessage = nullptr);
        SurfaceResponseAsset LoadSurfaceResponseAsset(const std::string &reference, bool *loaded = nullptr);
        bool SaveSurfaceResponseAsset(const std::string &reference, const SurfaceResponseAsset &asset, std::string *error = nullptr);
        ParticleSystemAsset LoadParticleSystemAsset(const std::string &assetReference, bool *loaded = nullptr);
        bool SaveParticleSystemAsset(const std::string &assetReference, const ParticleSystemAsset &asset, std::string *errorMessage = nullptr);
        PostProcessPresetAsset LoadPostProcessPresetAsset(const std::string &assetReference, bool *loaded = nullptr);
        bool SavePostProcessPresetAsset(const std::string &assetReference, const PostProcessPresetAsset &asset, std::string *errorMessage = nullptr);
        bool ResolveMaterialShaderGraph(render::MaterialConfig &config, std::string *errorMessage = nullptr);
        // Scoped scene-owned overrides borrow graph resources from this reader.
        // Weak registration refreshes external graph dependencies without ownership cycles.
        void RegisterInstanceMaterial(const std::shared_ptr<render::Material> &material);
        render::Shader *CompileShaderGraphAsset(const std::string &assetReference, std::string *errorMessage = nullptr);
        render::Material *CreateDefaultMaterial();
        render::Material *CreateDefaultShadedMaterial();

        std::string GetAssetDirectory() const { return m_assetDirectory; }
        void SetAssetDirectory(const std::string &directory) { m_assetDirectory = directory; }
        std::string GetProjectRootDirectory() const { return m_projectRootDirectory; }
        std::string GetProjectAssetDirectory() const { return m_projectAssetDirectory; }
        void SetProjectContext(const std::string &projectRootDirectory, const std::string &projectAssetDirectory = "Assets", std::uint32_t assetPipelineVersion = 1);
        void ClearProjectContext();
        std::uint32_t GetAssetPipelineVersion() const { return m_assetPipelineVersion; }

    private:
        render::Material *LoadMaterialAsset(const std::string &assetReference, bool reload);
        struct OwnedResources;
        std::unique_ptr<OwnedResources> m_ownedResources;
        // Resource-owning thread only. Scoped readers unregister before teardown.
        std::unordered_set<AssetManager *> m_scopedReaders;
        render::Texture *LoadTextureResource(const char *path);
        render::Texture *LoadTextureResource(const char *path, render::TextureColorSpace colorSpace);
        bool UsesSharedAssets(const std::string &reference) const;
        void RebuildRetainedAliases();
        const std::string *FindRetainedAlias(const std::string &reference) const;
        bool IsUnresolvedRetainedReference(const std::string &reference) const;
        void RefreshInstanceMaterialShaders();
        struct ModelResolutionCache
        {
            std::filesystem::file_time_type modified;
            std::uintmax_t size = 0;
            std::unordered_map<std::uint64_t, std::string> objects;
        };
        std::shared_ptr<const AssetCatalog> m_catalog;
        std::shared_ptr<const AssetStorageMap> m_storage;
        std::uint32_t m_assetPipelineVersion = 1;
        std::unordered_set<ProjectAssetType> m_logicalReferenceTypes;
        std::string PersistLogicalReference(const std::string &reference) const;
        std::string PersistDependencyReference(const std::string &reference) const;
        mutable std::unordered_map<std::string, std::string> m_stableIdReferenceCache;
        mutable std::unordered_map<std::string, ModelResolutionCache> m_modelResolutionCache;
        std::string m_assetDirectory = "assets/"; // Base directory for assets
        std::string m_projectRootDirectory;
        std::string m_projectAssetDirectory = "Assets";
        std::unordered_map<std::string, render::Texture *> m_textureCache;   // Cache for loaded textures
        std::unordered_map<std::string, render::Mesh *> m_meshCache;
        // Old mesh borrowers survive cache invalidation; so must their generation.
        std::unordered_map<render::Mesh *, std::shared_ptr<const ArtifactGenerationLock>> m_meshGenerationLeases;
        std::unordered_map<std::string, std::vector<std::string>> m_meshMaterialReferenceCache;
        std::unordered_map<std::string, MeshAssetMetadata> m_meshMetadataCache;
        std::unordered_map<std::string, render::Material *> m_materialCache; // Cache for loaded materials
        std::unordered_map<std::string, render::ShaderGraph> m_shaderGraphCache;
        std::unordered_map<std::string, AnimationGraphAsset> m_animationGraphCache;
        std::unordered_map<std::string, std::pair<bool, SurfaceResponseAsset>> m_surfaceResponseCache;
        std::unordered_map<std::string, ParticleSystemAsset> m_particleSystemCache;
        std::unordered_map<std::string, PostProcessPresetAsset> m_postProcessPresetCache;
        std::unordered_map<std::string, std::pair<std::uint64_t, render::Shader *>> m_shaderGraphShaderCache;

        void RefreshCachedMaterialsForShaderGraph(const std::string &shaderGraphReference);
        std::string ResolveMaterialTexturePath(const std::string &texturePath) const;
        std::string PersistMaterialTexturePath(const std::string &texturePath) const;
    };
}
