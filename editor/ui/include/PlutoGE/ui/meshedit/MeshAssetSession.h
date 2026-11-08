#pragma once

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/ui/DocumentHistory.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::core
{
    class Engine;
}

namespace PlutoGE::ui
{
    // Everything a mesh asset file holds.
    struct MeshAssetContent
    {
        render::MeshConfig config;
        std::vector<std::string> materialReferences;
        assets::MeshAssetMetadata metadata;
    };

    // The parts of a mesh asset the editor changes. Small, so undo keeps whole copies; the geometry stays in the
    // session and only a reimport replaces it.
    struct MeshAssetEdits
    {
        std::vector<std::string> materialReferences;
        std::vector<render::HumanoidBoneMapping> humanoidBoneMappings;
        // Per LOD level: the largest on-screen radius, in pixels, that still draws it. Level 0 always draws up close.
        std::vector<float> lodMaxScreenRadii;
        assetimport::MeshImportOptions importOptions;

        bool operator==(const MeshAssetEdits &) const = default;
    };

    // Where a mesh asset lives. Project assets go through the AssetManager; tests implement their own.
    class MeshAssetStorage
    {
    public:
        virtual ~MeshAssetStorage() = default;
        [[nodiscard]] virtual std::string GetLabel() const = 0;
        [[nodiscard]] virtual std::string GetReference() const { return {}; }
        // Why the asset cannot be saved ("Engine meshes are read-only."), empty when it can.
        [[nodiscard]] virtual std::string GetReadOnlyReason() const { return {}; }
        virtual bool Read(MeshAssetContent &content, std::string &error) = 0;
        virtual bool Write(const MeshAssetContent &content, std::string &error) = 0;

        // The model file a reimport reads, empty when there is none.
        [[nodiscard]] virtual std::string GetSourcePath() const { return {}; }
        // Imports the source model again with `options`. Fills the new geometry and the source's asset reference.
        virtual bool Reimport(const assetimport::MeshImportOptions &options, render::MeshConfig &config, std::string &sourceReference,
                              std::string &error)
        {
            error = "This mesh has no source model to reimport.";
            return false;
        }
    };

    // A project or engine mesh asset. `project` finds the source model for reimports and may be null.
    [[nodiscard]] std::unique_ptr<MeshAssetStorage> MakeMeshAssetStorage(core::Engine &engine, const assets::Project *project, std::string reference);

    // An open mesh asset with undo and unsaved-change tracking. Edits change MeshAssetEdits; GetMesh() always shows
    // them applied, so views and the rig analysis read one consistent mesh.
    class MeshAssetSession
    {
    public:
        // Null with `error` set when the mesh cannot be read.
        static std::unique_ptr<MeshAssetSession> Open(std::unique_ptr<MeshAssetStorage> storage, std::string &error);
        // Opens a mesh asset reference ("project://Characters/Hero.plutomesh").
        static std::unique_ptr<MeshAssetSession> OpenAsset(const std::string &reference, std::string &error);

        [[nodiscard]] std::string GetLabel() const { return m_storage->GetLabel(); }
        [[nodiscard]] std::string GetReference() const { return m_storage->GetReference(); }
        [[nodiscard]] std::string GetReadOnlyReason() const { return m_storage->GetReadOnlyReason(); }
        [[nodiscard]] bool IsReadOnly() const { return !GetReadOnlyReason().empty(); }

        [[nodiscard]] const render::MeshConfig &GetMesh() const { return m_content.config; }
        [[nodiscard]] const render::Skeleton &GetSkeleton() const { return m_content.config.skeleton; }
        [[nodiscard]] const MeshAssetEdits &GetEdits() const { return m_edits; }
        [[nodiscard]] std::size_t GetLodCount() const { return m_edits.lodMaxScreenRadii.size(); }

        // Changes the asset. Changes made while a control is held become one undo step until EndFrame.
        void Modify(const std::function<void(MeshAssetEdits &)> &edit);
        // Changes the humanoid map through the skeleton it belongs to, for the render:: rig helpers.
        void ModifyRig(const std::function<void(render::Skeleton &)> &edit);
        void EndFrame(bool controlHeld) { m_history.EndFrame(controlHeld); }

        [[nodiscard]] bool CanUndo() const { return m_history.CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return m_history.CanRedo(); }
        void Undo();
        void Redo();

        [[nodiscard]] bool IsDirty() const { return m_geometryChanged || m_edits != m_saved; }
        bool Save(std::string &error);
        // Discards unsaved changes, reimported geometry included.
        bool Revert(std::string &error);

        [[nodiscard]] bool CanReimport() const { return !IsReadOnly() && !m_storage->GetSourcePath().empty(); }
        [[nodiscard]] std::string GetSourcePath() const { return m_storage->GetSourcePath(); }
        // Replaces the geometry with a fresh import using the edited import options. Keeps the humanoid map and
        // material choices. Not undoable: Revert returns to the saved file.
        bool Reimport(std::string &error);

        // Increments on every change, for views that cache derived data.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }
        // Increments only when the geometry or skeleton is replaced (open, reimport, revert of a reimport).
        [[nodiscard]] std::uint64_t GetGeometryRevision() const { return m_geometryRevision; }

    private:
        explicit MeshAssetSession(std::unique_ptr<MeshAssetStorage> storage) : m_storage(std::move(storage)) {}
        bool Load(std::string &error);
        void TakeEditsFromContent();
        void Changed();

        std::unique_ptr<MeshAssetStorage> m_storage;
        MeshAssetContent m_content; // geometry with m_edits applied
        MeshAssetEdits m_edits;
        MeshAssetEdits m_saved;
        DocumentHistory<MeshAssetEdits> m_history;
        bool m_geometryChanged = false;
        std::uint64_t m_revision = 1;
        std::uint64_t m_geometryRevision = 1;
    };
}
