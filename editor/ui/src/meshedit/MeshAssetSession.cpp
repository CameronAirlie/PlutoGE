#include "PlutoGE/ui/meshedit/MeshAssetSession.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <filesystem>
#include <limits>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kFullDetailRadius = std::numeric_limits<float>::max();

        render::MeshConfig BuildMeshConfigFromMesh(const render::Mesh &mesh)
        {
            render::MeshConfig config;
            config.data = mesh.GetMeshData();
            config.hasLightmapUvs = mesh.HasLightmapUvs();
            config.skeleton = mesh.GetSkeleton();
            config.animationNodes = mesh.GetAnimationNodes();
            config.animations = mesh.GetAnimations();
            config.submeshes.reserve(mesh.GetSubmeshCount());
            for (size_t submeshIndex = 0; submeshIndex < mesh.GetSubmeshCount(); ++submeshIndex)
                config.submeshes.push_back(mesh.GetSubmesh(submeshIndex));
            return config;
        }

        // Every submesh gets an explicit LOD 0 so thresholds can be edited uniformly.
        void EnsureBaseLodRanges(render::MeshConfig &config)
        {
            for (auto &submesh : config.submeshes)
            {
                if (!submesh.lods.empty())
                    continue;
                submesh.lods.push_back(render::Submesh::LodRange{
                    .indexOffset = submesh.indexOffset,
                    .indexCount = submesh.indexCount,
                    .minDistanceFactor = 0.0f,
                    .maxScreenRadiusPixels = kFullDetailRadius,
                });
            }
        }

        std::size_t RequiredMaterialSlots(const render::MeshConfig &config)
        {
            std::size_t slots = 0;
            for (const auto &submesh : config.submeshes)
                slots = (std::max)(slots, static_cast<std::size_t>(submesh.materialIndex) + 1);
            return slots;
        }

        std::filesystem::path FindSourceModelForMesh(const assets::Project &project, const std::string &meshReference)
        {
            const auto meshPath = project.ResolveAssetReference(meshReference);
            const auto meshStem = meshPath.stem().string();
            for (const auto &asset : project.GetManifest().assetEntries)
            {
                if (asset.type != assets::ProjectAssetType::Model)
                    continue;
                const auto sourcePath = project.ResolveAssetReference(asset.reference);
                if (sourcePath.stem() == meshPath.stem())
                    return sourcePath;
            }

            const auto guessedSourceDirectory = project.GetAssetDirectoryPath() / "SourceModels" / meshStem;
            for (const auto *extension : {".gltf", ".glb", ".fbx"})
            {
                const auto candidate = guessedSourceDirectory / (meshStem + extension);
                if (std::filesystem::exists(candidate))
                    return candidate;
            }
            return {};
        }

        void RefreshMeshAssetInstances(scene::Entity &entity, const std::string &meshReference, render::Mesh *mesh)
        {
            for (auto *meshComponent : entity.GetComponents<scene::MeshComponent>())
            {
                if (meshComponent && meshComponent->GetMeshAssetReference() == meshReference)
                {
                    meshComponent->SetMesh(mesh);
                    // SetMesh invalidates cached commands because a reloaded asset has a new pointer. Keep the
                    // refresh robust if asset loading later starts updating meshes in place.
                    meshComponent->NotifyMeshDataChanged();
                }
            }
            for (auto *child : entity.GetChildren())
                if (child)
                    RefreshMeshAssetInstances(*child, meshReference, mesh);
        }

        class AssetStorage final : public MeshAssetStorage
        {
        public:
            AssetStorage(core::Engine &engine, const assets::Project *project, std::string reference)
                : m_engine(engine), m_project(project), m_reference(std::move(reference))
            {
            }

            std::string GetLabel() const override { return std::filesystem::path(m_reference).filename().string(); }
            std::string GetReference() const override { return m_reference; }

            std::string GetReadOnlyReason() const override
            {
                if (assets::Project::IsEngineAssetReference(m_reference))
                    return "Engine meshes are read-only.";
                if (m_engine.GetAssetManager().IsImportedAsset(m_reference))
                    return "This mesh is imported from a model. Extract an authored copy to edit it.";
                return {};
            }

            bool Read(MeshAssetContent &content, std::string &error) override
            {
                auto &assets = m_engine.GetAssetManager();
                const auto *mesh = assets.LoadMeshAsset(m_reference);
                if (!mesh)
                {
                    error = "Could not load " + m_reference + ".";
                    return false;
                }
                content.config = BuildMeshConfigFromMesh(*mesh);
                content.materialReferences = assets.GetMeshAssetMaterialReferences(m_reference);
                content.metadata = assets.GetMeshAssetMetadata(m_reference);
                return true;
            }

            bool Write(const MeshAssetContent &content, std::string &error) override
            {
                auto &assets = m_engine.GetAssetManager();
                if (!assets.SaveMeshAsset(m_reference, content.config, content.materialReferences, &error, content.metadata))
                {
                    if (error.empty())
                        error = "Could not save " + m_reference + ".";
                    return false;
                }
                RefreshOpenScene();
                return true;
            }

            std::string GetSourcePath() const override
            {
                if (!m_project || !GetReadOnlyReason().empty())
                    return {};
                const auto resolved = m_engine.GetAssetManager().ResolveMeshAssetSourcePath(m_reference);
                return resolved.empty() ? FindSourceModelForMesh(*m_project, m_reference).string() : resolved;
            }

            bool Reimport(const assetimport::MeshImportOptions &options, render::MeshConfig &config, std::string &sourceReference,
                          std::string &error) override
            {
                const auto sourcePath = GetSourcePath();
                if (sourcePath.empty())
                {
                    error = "This mesh has no matching source model to reimport.";
                    return false;
                }
                const auto imported = m_engine.ImportMeshAsset(sourcePath, options);
                if (!imported.mesh)
                {
                    error = "Could not import " + sourcePath + ".";
                    return false;
                }
                config = BuildMeshConfigFromMesh(*imported.mesh);
                sourceReference = m_project ? m_project->MakeAssetReference(sourcePath) : std::string{};
                return true;
            }

        private:
            void RefreshOpenScene()
            {
                auto *scene = m_engine.GetScene();
                if (!scene)
                    return;
                auto *mesh = m_engine.GetAssetManager().LoadMeshAsset(m_reference);
                if (!mesh)
                    return;
                for (auto *root : scene->GetRootEntities())
                    if (root)
                        RefreshMeshAssetInstances(*root, m_reference, mesh);
                scene->MarkShadowLightsDirty();
            }

            core::Engine &m_engine;
            const assets::Project *m_project;
            std::string m_reference;
        };
    }

    std::unique_ptr<MeshAssetStorage> MakeMeshAssetStorage(core::Engine &engine, const assets::Project *project, std::string reference)
    {
        return std::make_unique<AssetStorage>(engine, project, std::move(reference));
    }

    std::unique_ptr<MeshAssetSession> MeshAssetSession::Open(std::unique_ptr<MeshAssetStorage> storage, std::string &error)
    {
        std::unique_ptr<MeshAssetSession> session(new MeshAssetSession(std::move(storage)));
        if (!session->Load(error))
            return nullptr;
        return session;
    }

    std::unique_ptr<MeshAssetSession> MeshAssetSession::OpenAsset(const std::string &reference, std::string &error)
    {
        auto &shell = EditorShell::GetInstance();
        return Open(MakeMeshAssetStorage(shell.GetEngine(), shell.GetProject(), reference), error);
    }

    bool MeshAssetSession::Load(std::string &error)
    {
        MeshAssetContent content;
        if (!m_storage->Read(content, error))
            return false;
        if (content.config.data.vertices.empty() || content.config.data.indices.empty())
        {
            error = "The mesh has no geometry.";
            return false;
        }
        m_content = std::move(content);
        EnsureBaseLodRanges(m_content.config);
        // Older files can bind fewer slots than the submeshes use; show every slot.
        m_content.materialReferences.resize((std::max)(m_content.materialReferences.size(), RequiredMaterialSlots(m_content.config)));
        TakeEditsFromContent();
        m_saved = m_edits;
        m_geometryChanged = false;
        m_history = DocumentHistory<MeshAssetEdits>();
        ++m_geometryRevision;
        Changed();
        return true;
    }

    void MeshAssetSession::TakeEditsFromContent()
    {
        const auto &config = m_content.config;
        m_edits.materialReferences = m_content.materialReferences;
        m_edits.humanoidBoneMappings = config.skeleton.humanoidBoneMappings;
        m_edits.importOptions = m_content.metadata.importOptions;

        std::size_t lodCount = 1;
        for (const auto &submesh : config.submeshes)
            lodCount = (std::max)(lodCount, submesh.lods.size());
        m_edits.lodMaxScreenRadii.assign(lodCount, kFullDetailRadius);
        for (std::size_t lod = 1; lod < lodCount; ++lod)
        {
            const auto found = std::ranges::find_if(config.submeshes, [&](const auto &submesh) { return lod < submesh.lods.size(); });
            if (found != config.submeshes.end())
                m_edits.lodMaxScreenRadii[lod] = found->lods[lod].maxScreenRadiusPixels;
        }
    }

    void MeshAssetSession::Modify(const std::function<void(MeshAssetEdits &)> &edit)
    {
        if (IsReadOnly())
            return;
        auto edited = m_edits;
        edit(edited);
        if (edited == m_edits)
            return;
        m_history.Record(std::move(m_edits));
        m_edits = std::move(edited);
        Changed();
    }

    void MeshAssetSession::ModifyRig(const std::function<void(render::Skeleton &)> &edit)
    {
        Modify([&](MeshAssetEdits &edits)
               {
                   // The joints come from the mesh; only the map is part of the edit.
                   render::Skeleton skeleton;
                   skeleton.joints = m_content.config.skeleton.joints;
                   skeleton.humanoidBoneMappings = edits.humanoidBoneMappings;
                   edit(skeleton);
                   edits.humanoidBoneMappings = std::move(skeleton.humanoidBoneMappings);
               });
    }

    void MeshAssetSession::Undo()
    {
        if (m_history.Undo(m_edits))
            Changed();
    }

    void MeshAssetSession::Redo()
    {
        if (m_history.Redo(m_edits))
            Changed();
    }

    bool MeshAssetSession::Save(std::string &error)
    {
        if (IsReadOnly())
        {
            error = GetReadOnlyReason();
            return false;
        }
        if (!m_storage->Write(m_content, error))
            return false;
        m_saved = m_edits;
        m_geometryChanged = false;
        ++m_revision;
        return true;
    }

    bool MeshAssetSession::Revert(std::string &error)
    {
        if (!IsDirty())
            return true;
        if (m_geometryChanged)
            return Load(error); // the saved geometry is only in the file
        m_history.CloseGroup();
        Modify([this](MeshAssetEdits &edits) { edits = m_saved; });
        m_history.CloseGroup();
        return true;
    }

    bool MeshAssetSession::Reimport(std::string &error)
    {
        if (!CanReimport())
        {
            error = IsReadOnly() ? GetReadOnlyReason() : "This mesh has no matching source model to reimport.";
            return false;
        }
        render::MeshConfig config;
        std::string sourceReference;
        if (!m_storage->Reimport(m_edits.importOptions, config, sourceReference, error))
            return false;

        // Keep the authored map: a reimport of the same model has the same joints.
        config.skeleton.humanoidBoneMappings = m_edits.humanoidBoneMappings;
        m_content.config = std::move(config);
        EnsureBaseLodRanges(m_content.config);
        if (m_content.metadata.sourceAssetReference.empty())
            m_content.metadata.sourceAssetReference = sourceReference;

        // The new geometry decides how many material slots and LOD levels there are.
        const auto materials = std::move(m_edits.materialReferences);
        const auto importOptions = m_edits.importOptions;
        TakeEditsFromContent();
        m_edits.importOptions = importOptions;
        m_edits.materialReferences = materials;
        m_edits.materialReferences.resize(RequiredMaterialSlots(m_content.config));

        // Undo steps refer to the old geometry's slots and levels.
        m_history = DocumentHistory<MeshAssetEdits>();
        m_geometryChanged = true;
        ++m_geometryRevision;
        Changed();
        return true;
    }

    void MeshAssetSession::Changed()
    {
        auto &config = m_content.config;
        m_content.materialReferences = m_edits.materialReferences;
        m_content.metadata.importOptions = m_edits.importOptions;
        config.skeleton.humanoidBoneMappings = m_edits.humanoidBoneMappings;
        for (auto &submesh : config.submeshes)
            for (std::size_t lod = 0; lod < submesh.lods.size() && lod < m_edits.lodMaxScreenRadii.size(); ++lod)
                submesh.lods[lod].maxScreenRadiusPixels = lod == 0 ? kFullDetailRadius : (std::max)(1.0f, m_edits.lodMaxScreenRadii[lod]);
        ++m_revision;
    }
}
