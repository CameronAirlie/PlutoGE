#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/render/RenderObjectIdentity.h"
#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/SkinPoseFollow.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace PlutoGE::render
{
    class Mesh;
    class Material;
}

namespace PlutoGE::assets { class AssetManager; }

namespace PlutoGE::scene
{
    struct MeshComponentConfig
    {
        render::Mesh *mesh = nullptr;
        render::Material *material = nullptr;
        std::vector<render::Material *> materials;
    };

    class MeshComponent : public TypedComponent<MeshComponent>
    {
    public:
        MeshComponent(const MeshComponentConfig &config)
            : m_mesh(config.mesh), m_material(config.material), m_materials(config.materials)
        {
            if (m_material && m_materials.empty())
            {
                m_materials.push_back(m_material);
            }

            if (!m_material && !m_materials.empty())
            {
                m_material = m_materials.front();
            }

            RefreshMeshDerivedState();
        }
        ~MeshComponent() override = default;

        void Update(float deltaTime) override;
        bool RequiresFrameUpdate() const override { return false; }
        void SubmitRenderCommands();
        void ResetRenderHistory() { m_previousSubmeshModels.clear(); m_renderCommandCacheDirty = true; }
        void OnEnabledChanged() override { MarkRenderCommandsDirty(); }
        std::uint64_t GetRenderProducerId() const { return m_renderObjectIdentity.Value(); }
        bool NeedsFrameRenderSubmission() const;

        std::vector<Property> Serialize() const override;
        // Whether Serialize() writes an extended inline material, which selects the scene format, without building it.
        [[nodiscard]] bool HasInlineMaterialVersion() const;
        void Deserialize(const std::vector<Property> &properties) override;
        // Accepted linked generations must never resolve through the current global catalog.
        std::shared_ptr<assets::AssetManager> GetRetainedAssetReader() const { return m_privateResources; }
        void RetainAssetReader(std::shared_ptr<assets::AssetManager> manager) { m_privateResources = std::move(manager); }
        void DeserializeWithAssetManager(const std::vector<Property> &properties, std::shared_ptr<assets::AssetManager> manager);

        void SetMesh(render::Mesh *mesh);
        render::Mesh *GetMesh() const { return m_mesh; }
        bool UsesRetainedGeometry(const render::Mesh *baseline) const { return m_mesh == baseline; }
        void NotifyMeshDataChanged() { MarkRenderCommandsDirty(); }
        // A lightmap bake changed: static draws rebuild their commands with their new lightmap rects.
        void NotifyBakedLightingChanged() { MarkRenderCommandsDirty(); }
        // The mesh lightmapped, drawn and baked for this instance. A static mesh whose lightmap UVs can follow scale
        // (built-in shapes) and whose world scale stretches it unevenly gets its own copy unwrapped at that scale, so a
        // cube stretched into a floor keeps an even texel density; otherwise this is GetMesh(). Only scenes with a
        // Baked Lighting component use copies, so scenes without lightmaps keep sharing (and instancing) one mesh.
        render::Mesh *GetLightmapMesh();
        // Script-built geometry (vision cones, trails) owned by this component.
        // Later calls replace it in place; an empty triangle list draws nothing.
        // Clearing restores the mesh the component had before. Not serialized.
        bool SetRuntimeGeometry(render::MeshData data);
        // With submeshes: each index range draws with its material slot (Mesh::ReplaceGeometry). Empty is one
        // submesh over every index, as above.
        bool SetRuntimeGeometry(render::MeshData data, std::vector<render::Submesh> submeshes);
        void ClearRuntimeGeometry();
        bool HasRuntimeGeometry() const { return m_runtimeGeometryActive; }
        void SetStatic(bool isStatic)
        {
            if (m_isStatic == isStatic)
            {
                return;
            }

            m_isStatic = isStatic;
            MarkRenderCommandsDirty();
        }
        bool IsStatic() const { return m_isStatic; }
        void SetVisible(bool visible)
        {
            if (m_visible == visible)
            {
                return;
            }

            m_visible = visible;
            MarkRenderCommandsDirty();
        }
        bool IsVisible() const { return m_visible; }
        // Coarsest-first floor for this mesh's shadow casters. Casters never
        // exceed the visible LOD's detail; a floor of 1 lets dense skinned
        // characters render simplified shadows at every distance.
        static constexpr int MaxMinShadowLod = 8;
        void SetMinShadowLod(int lodIndex)
        {
            lodIndex = std::clamp(lodIndex, 0, MaxMinShadowLod);
            if (m_minShadowLod == lodIndex)
            {
                return;
            }

            m_minShadowLod = lodIndex;
            MarkRenderCommandsDirty();
        }
        int GetMinShadowLod() const { return m_minShadowLod; }
        // How the mesh bends in the scene's wind, as a foliage type does (render/FoliageWind.h, heights in mesh units);
        // off by default. Saved only when on. A foliage proxy's meshes take their type's response when it is promoted
        // (FoliageProxies.h), so a swaying bush keeps swaying as it becomes an entity.
        void SetWind(const render::FoliageWindResponse &wind);
        std::vector<Property> SerializeForInspector() const override;
        const render::FoliageWindResponse &GetWind() const { return m_wind; }
        // Joints of a skinned mesh hidden from cameras (each with everything below it), as JointHiding.h shrinks them:
        // cameras draw the mesh without them while its shadow stays whole (the full pose is submitted shadow-only).
        // Names resolve like bone queries (exact, then humanoid aliases such as "Head"). A first-person body hides its
        // head this way. Ignored for meshes without a skeleton; empty (the default) draws the mesh normally.
        void SetCameraHiddenJoints(std::vector<std::string> joints);
        const std::vector<std::string> &GetCameraHiddenJoints() const { return m_cameraHiddenJoints; }
        // A skinned mesh posed by its parent's skeleton instead of an animator of its own: clothing, armour or hair
        // authored on a character's rig and exported separately. The source is the nearest ancestor with a skinned
        // mesh that does not itself follow; its joints pose this mesh's joints by name (SkinPoseFollow.h), so the
        // follower needs only the joints it is weighted to. Place the follower at its parent's origin. Animators,
        // ragdolls and bone attachments never pick a follower as their character's mesh. Off by default.
        void SetFollowParentSkeleton(bool follow);
        bool GetFollowParentSkeleton() const { return m_followParentSkeleton; }
        // The mesh this one takes its pose from now, or null (not following, no skinned ancestor, no skeleton).
        MeshComponent *FindFollowedSkeletonMesh() const;
        // Joints of this mesh that matched a joint of the followed skeleton at the last draw (0 before the first).
        std::size_t GetFollowedJointCount() const { return m_followMap.matchedJoints; }
        // Skin hidden under clothing (SkinCoverage.h): triangles mostly weighted to these joints are left out of every
        // draw, each joint hiding only the geometry it carries. Names resolve like bone queries. Set by OutfitComponent
        // from what is worn, so it is not saved; empty (the default) draws the whole mesh.
        void SetCoveredJoints(std::vector<std::string> joints);
        const std::vector<std::string> &GetCoveredJoints() const { return m_coveredJoints; }
        // Triangles of the full-detail mesh hidden by the covered joints at the last draw.
        std::size_t GetCoveredTriangleCount() const { return m_coveredTriangles; }
        void SetSubmeshIndex(int submeshIndex)
        {
            if (m_submeshIndex == submeshIndex)
            {
                return;
            }

            m_submeshIndex = submeshIndex;
            MarkRenderCommandsDirty();
        }
        int GetSubmeshIndex() const { return m_submeshIndex; }
        void SetSubmeshRange(int submeshIndex, int submeshCount)
        {
            const int normalizedCount = submeshIndex >= 0 ? std::max(1, submeshCount) : 1;
            if (m_submeshIndex == submeshIndex && m_submeshCount == normalizedCount)
            {
                return;
            }

            m_submeshIndex = submeshIndex;
            m_submeshCount = normalizedCount;
            MarkRenderCommandsDirty();
        }
        int GetSubmeshRangeCount() const { return m_submeshCount; }
        void SetMeshAssetReference(const std::string &meshAssetReference) { m_sourceMeshPath = meshAssetReference; }
        const std::string &GetMeshAssetReference() const { return m_sourceMeshPath; }
        void SetModelObjectIdentity(const std::string &modelAssetId, std::uint64_t localObjectId)
        {
            m_modelAssetId = modelAssetId;
            m_modelObjectId = localObjectId;
        }
        const std::string &GetModelAssetId() const { return m_modelAssetId; }
        std::uint64_t GetModelObjectId() const { return m_modelObjectId; }
        void SetSourceMeshPath(const std::string &sourceMeshPath) { SetMeshAssetReference(sourceMeshPath); }
        const std::string &GetSourceMeshPath() const { return GetMeshAssetReference(); }
        void SetUseGeneratedLods(bool useGeneratedLods) { m_useGeneratedLods = useGeneratedLods; }
        bool GetUseGeneratedLods() const { return m_useGeneratedLods; }
        // Source-imported meshes (a model file referenced directly, as UseGeneratedLods applies to): import them with
        // generated lightmap UVs (MeshImportOptions::generateLightmapUvs), so a static instance can be lightmapped.
        // Models imported through the model pipeline take the model's import setting instead. Applies at the next load.
        void SetGenerateLightmapUvs(bool generate) { m_generateLightmapUvs = generate; }
        bool GetGenerateLightmapUvs() const { return m_generateLightmapUvs; }
        void SetMeshPositionOffset(const glm::vec3 &offset);
        const glm::vec3 &GetMeshPositionOffset() const;
        void SetMeshRotationOffset(const glm::vec3 &offset);
        const glm::vec3 &GetMeshRotationOffset() const;
        glm::mat4 GetMeshOffsetTransform() const;
        // Per-entity compensation for origin edits; never inherited by submeshes.
        const glm::vec3 &GetPivotOffset() const { return m_pivotOffset; }
        void SetPivotOffset(const glm::vec3 &offset)
        {
            if (m_pivotOffset == offset) return;
            m_pivotOffset = offset;
            MarkRenderCommandsDirty();
        }
        void SetSubmeshPositionOffset(size_t submeshIndex, const glm::vec3 &offset);
        glm::vec3 GetSubmeshPositionOffset(size_t submeshIndex) const;
        void SetSubmeshRotationOffset(size_t submeshIndex, const glm::vec3 &offset);
        glm::vec3 GetSubmeshRotationOffset(size_t submeshIndex) const;
        glm::mat4 GetSubmeshOffsetTransform(size_t submeshIndex) const;
        // Imported node transform applied after the submesh offset, matching the
        // pose submitted for rendering (identity for skinned or unparented submeshes).
        glm::mat4 GetSubmeshNodeTransform(size_t submeshIndex);

        void SetMaterial(render::Material *material)
        {
            m_material = material;
            if (m_materials.empty())
            {
                m_materials.push_back(material);
            }
            else
            {
                m_materials[0] = material;
            }
            MarkRenderCommandsDirty();
        }
        render::Material *GetMaterial() const { return m_material; }
        void SetMaterials(const std::vector<render::Material *> &materials)
        {
            m_materials = materials;
            m_material = m_materials.empty() ? nullptr : m_materials.front();
            m_submeshMaterials.clear();
            m_materialAssetReferences.clear();
            m_submeshMaterialAssetReferences.clear();
            MarkRenderCommandsDirty();
        }
        const std::vector<render::Material *> &GetMaterials() const { return m_materials; }
        render::Material *GetMaterialForMaterialSlot(size_t materialSlotIndex) const
        {
            if (materialSlotIndex < m_materials.size() && m_materials[materialSlotIndex])
            {
                return m_materials[materialSlotIndex];
            }

            return m_material;
        }
        void SetMaterialForMaterialSlot(size_t materialSlotIndex, render::Material *material)
        {
            if (materialSlotIndex >= m_materials.size())
            {
                m_materials.resize(materialSlotIndex + 1, nullptr);
            }

            m_materials[materialSlotIndex] = material;
            if (materialSlotIndex == 0 || !m_material)
            {
                m_material = material;
            }
            MarkRenderCommandsDirty();
        }
        void SetMaterialAssetForMaterialSlot(size_t materialSlotIndex, const std::string &materialAssetReference)
        {
            if (materialSlotIndex >= m_materialAssetReferences.size())
            {
                m_materialAssetReferences.resize(materialSlotIndex + 1);
            }

            m_materialAssetReferences[materialSlotIndex] = materialAssetReference;
        }
        const std::string &GetMaterialAssetForMaterialSlot(size_t materialSlotIndex) const;
        render::Material *GetMaterialForSubmesh(size_t submeshIndex) const
        {
            if (submeshIndex < m_submeshMaterials.size() && m_submeshMaterials[submeshIndex])
            {
                return m_submeshMaterials[submeshIndex];
            }

            if (m_mesh && submeshIndex < m_mesh->GetSubmeshCount())
            {
                return GetMaterialForMaterialSlot(m_mesh->GetSubmesh(submeshIndex).materialIndex);
            }

            return m_material;
        }
        void SetMaterialForSubmesh(size_t submeshIndex, render::Material *material)
        {
            if (submeshIndex >= m_submeshMaterials.size())
            {
                m_submeshMaterials.resize(submeshIndex + 1, nullptr);
            }

            m_submeshMaterials[submeshIndex] = material;
            MarkRenderCommandsDirty();
        }
        void SetMaterialAssetForSubmesh(size_t submeshIndex, const std::string &materialAssetReference)
        {
            if (submeshIndex >= m_submeshMaterialAssetReferences.size())
            {
                m_submeshMaterialAssetReferences.resize(submeshIndex + 1);
            }

            m_submeshMaterialAssetReferences[submeshIndex] = materialAssetReference;
        }
        const std::string &GetMaterialAssetForSubmesh(size_t submeshIndex) const
        {
            static const std::string empty;
            return submeshIndex < m_submeshMaterialAssetReferences.size() ? m_submeshMaterialAssetReferences[submeshIndex] : empty;
        }
        bool HasMaterialOverrideForSubmesh(size_t submeshIndex) const
        {
            return submeshIndex < m_submeshMaterials.size() && m_submeshMaterials[submeshIndex] != nullptr;
        }
        render::Material *CreateUniqueMaterialForMaterialSlot(size_t materialSlotIndex);
        // Keeps the instance materials `source` owns (inline and unique materials, and the asset reader behind their
        // textures) alive as long as this component too, so geometry split off another mesh (fracture pieces) can
        // keep drawing with its materials after the source is destroyed.
        void ShareMaterialOwnership(const MeshComponent &source);
        render::Material *CreateUniqueMaterialForSubmesh(size_t submeshIndex);
        // Every material this mesh draws with (each drawn submesh's override, else its slot's material), without
        // duplicates. With makeInstanceOwned, asset-backed or missing materials are first replaced by per-instance
        // copies, so editing the result (a tint, an emission) changes this mesh only and reaches every submesh.
        std::vector<render::Material *> GetDrawnMaterials(bool makeInstanceOwned);
        bool RefreshMaterialAsset(const std::string &materialAssetReference, render::Material *material);
        bool RemapMaterialAsset(const std::string &oldMaterialAssetReference,
                                const std::string &newMaterialAssetReference,
                                render::Material *material);
        Entity *CreateSkeletonAttachmentEntity(std::size_t jointIndex);
        std::size_t CompactSkeletonAttachmentEntities();

    private:
        void DeserializeUsingAssets(const std::vector<Property> &properties, assets::AssetManager &assets, bool allowSourceImport);
        // How many material slots Serialize() writes, and whether a slot's material is the loaded asset it references.
        [[nodiscard]] std::size_t SerializedMaterialSlotCount() const;
        [[nodiscard]] bool IsMaterialSlotAssetBacked(std::size_t materialSlotIndex, const render::Material *material) const;
        void MarkRenderCommandsDirty();
        void RefreshMeshDerivedState();
        void UpdateCachedPreviousModels();
        MeshComponent *FindMeshOffsetSource() const;

        render::Material *CloneInstanceMaterial(render::Material *source);
        assets::AssetManager &ResourceManager() const;
        std::shared_ptr<assets::AssetManager> m_privateResources;
        // Inline retained-instance materials die before the reader supplying
        // their borrowed textures/shaders. Old clones survive slot replacement.
        std::vector<std::shared_ptr<render::Material>> m_privateMaterials;
        std::unique_ptr<render::Mesh> m_runtimeGeometry;
        render::Mesh *m_runtimeGeometrySource = nullptr;
        bool m_runtimeGeometryActive = false;
        render::Mesh *m_mesh = nullptr;
        render::Material *m_material = nullptr;
        std::vector<render::Material *> m_materials;
        std::vector<render::Material *> m_submeshMaterials;
        std::vector<std::string> m_materialAssetReferences;
        std::vector<std::string> m_submeshMaterialAssetReferences;
        std::unordered_map<size_t, glm::mat4> m_previousSubmeshModels;
        // Joints hidden from cameras, resolved against the skeleton they were last resolved for, and the camera's copy
        // of the skinning palette with them collapsed (stable storage: the renderer keys skinned results by it).
        std::vector<std::string> m_cameraHiddenJoints;
        std::vector<int> m_cameraHiddenJointTargets;
        const render::Skeleton *m_cameraHiddenJointsSkeleton = nullptr;
        bool m_cameraHiddenJointsResolved = false;
        std::vector<glm::mat4> m_cameraPalette;
        const std::vector<glm::mat4> *CameraPalette(const std::vector<glm::mat4> &jointMatrices, bool poseChanged);
        // Following a parent's skeleton: the joint map for the skeleton pair it was built for, and this mesh's palette
        // (stable storage, like the camera palette).
        bool m_followParentSkeleton = false;
        SkinPoseFollowMap m_followMap;
        const render::Skeleton *m_followSourceSkeleton = nullptr;
        const render::Skeleton *m_followOwnSkeleton = nullptr;
        std::vector<glm::mat4> m_followPalette;
        const std::vector<glm::mat4> *FollowedPalette(bool &poseChanged);
        // Covered skin: the mesh drawn in place of m_mesh while joints are covered, rebuilt when either changes.
        std::vector<std::string> m_coveredJoints;
        std::unique_ptr<render::Mesh> m_coveredMesh;
        const render::Mesh *m_coveredSource = nullptr;
        std::uint64_t m_coveredSourceRevision = 0;
        std::size_t m_coveredTriangles = 0;
        bool m_coveredDirty = false;
        render::Mesh *DrawnMesh();
        // Scale-fitted lightmap copy (GetLightmapMesh), rebuilt when the source or the quantised scale changes.
        std::unique_ptr<render::Mesh> m_scaleFittedMesh;
        const render::Mesh *m_scaleFittedSource = nullptr;
        std::uint64_t m_scaleFittedSourceRevision = 0;
        glm::ivec3 m_scaleFittedKey{0};
        bool m_isStatic = false;
        bool m_renderCommandCacheDirty = true;
        render::RenderObjectIdentity m_renderObjectIdentity;
        std::uint64_t m_renderSourceRevision = 0;
        bool m_hasCachedRenderCommandModel = false;
        glm::mat4 m_cachedRenderCommandModel = glm::mat4(1.0f);
        std::uint64_t m_cachedOwnerTransformRevision = 0;
        glm::vec3 m_cachedMeshPositionOffset{0.0f};
        glm::vec3 m_cachedMeshRotationOffset{0.0f};
        std::vector<render::RenderCommand> m_cachedRenderCommands;
        std::string m_sourceMeshPath;
        std::string m_modelAssetId;
        std::uint64_t m_modelObjectId = 0;
        glm::vec3 m_meshPositionOffset{0.0f};
        glm::vec3 m_pivotOffset{0.0f};
        glm::vec3 m_meshRotationOffset{0.0f};
        std::vector<glm::vec3> m_submeshPositionOffsets;
        std::vector<glm::vec3> m_submeshRotationOffsets;
        int m_submeshIndex = -1;
        int m_submeshCount = 1;
        bool m_visible = true;
        bool m_useGeneratedLods = false;
        bool m_generateLightmapUvs = false;
        int m_minShadowLod = 0;
        render::FoliageWindResponse m_wind{};
        bool m_hasAnimatedNodeSubmeshes = false;
        std::uint64_t m_lastSubmittedJointPoseRevision = 0;
        bool m_hasSubmittedJointPoseRevision = false;
    };
}
