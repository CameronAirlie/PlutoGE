#include "PlutoGE/ui/FoliageTypeSession.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/FoliageTypeUsage.h"

#include <filesystem>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        class AssetStorage final : public FoliageTypeStorage
        {
        public:
            AssetStorage(std::string reference, std::function<scene::Scene *()> scene) : m_reference(std::move(reference)), m_scene(std::move(scene)) {}

            std::string GetLabel() const override { return std::filesystem::path(m_reference).filename().string(); }
            std::string GetReference() const override { return m_reference; }

            bool Read(scene::FoliageTypeAsset &asset, std::string &error) override
            {
                // A version 1 file holds no prototype, and reading one keeps what `asset` already has. Starting from a
                // linked type means saving it as version 2 cannot take the mesh away from the types that use it.
                if (auto *scene = m_scene ? m_scene() : nullptr)
                    if (const auto *linked = FindFoliageTypeLinkedTo(*scene, m_reference))
                    {
                        asset = linked->asset;
                        asset.assetReference.clear();
                    }
                return scene::LoadFoliageTypeAsset(Path(), asset, &error);
            }

            bool Write(const scene::FoliageTypeAsset &asset, std::string &error) override
            {
                return scene::SaveFoliageTypeAsset(Path(), asset, &error);
            }

            void Preview(const scene::FoliageTypeAsset *asset) override
            {
                auto *scene = m_scene ? m_scene() : nullptr;
                if (!scene) return;
                if (asset) scene->PreviewFoliageTypeAsset(m_reference, *asset);
                else scene->RefreshFoliageTypeAsset(m_reference);
            }

        private:
            [[nodiscard]] std::string Path() const { return core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(m_reference); }

            std::string m_reference;
            std::function<scene::Scene *()> m_scene;
        };
    }

    std::unique_ptr<FoliageTypeStorage> MakeFoliageTypeAssetStorage(std::string reference, std::function<scene::Scene *()> scene)
    {
        return std::make_unique<AssetStorage>(std::move(reference), std::move(scene));
    }

    std::unique_ptr<FoliageTypeSession> FoliageTypeSession::Open(std::unique_ptr<FoliageTypeStorage> storage, std::string &error)
    {
        std::unique_ptr<FoliageTypeSession> session(new FoliageTypeSession(std::move(storage)));
        if (!session->m_storage->Read(session->m_saved, error)) return nullptr;
        scene::SanitizeFoliageTypeAsset(session->m_saved);
        session->m_asset = session->m_saved;
        return session;
    }

    FoliageTypeSession::~FoliageTypeSession()
    {
        if (m_previewing) m_storage->Preview(nullptr);
    }

    void FoliageTypeSession::Modify(const std::function<void(scene::FoliageTypeAsset &)> &edit)
    {
        auto edited = m_asset;
        edit(edited);
        scene::SanitizeFoliageTypeAsset(edited);
        if (edited == m_asset) return;
        m_history.Record(std::move(m_asset));
        m_asset = std::move(edited);
        UpdatePreview();
    }

    void FoliageTypeSession::Undo()
    {
        if (m_history.Undo(m_asset)) UpdatePreview();
    }

    void FoliageTypeSession::Redo()
    {
        if (m_history.Redo(m_asset)) UpdatePreview();
    }

    bool FoliageTypeSession::Save(std::string &error)
    {
        if (!m_storage->Write(m_asset, error)) return false;
        m_saved = m_asset;
        // Linked types follow the file now, whether or not they were previewing.
        m_previewing = false;
        m_storage->Preview(nullptr);
        return true;
    }

    void FoliageTypeSession::Revert()
    {
        if (!IsDirty()) return;
        m_history.CloseGroup();
        Modify([this](scene::FoliageTypeAsset &asset) { asset = m_saved; });
        m_history.CloseGroup();
    }

    void FoliageTypeSession::SetLivePreview(bool enabled)
    {
        if (m_livePreview == enabled) return;
        m_livePreview = enabled;
        UpdatePreview();
    }

    void FoliageTypeSession::SetPreviewSuspended(bool suspended)
    {
        if (m_previewSuspended == suspended) return;
        m_previewSuspended = suspended;
        UpdatePreview();
    }

    void FoliageTypeSession::ReapplyPreview()
    {
        if (m_previewing) m_storage->Preview(&m_asset);
    }

    void FoliageTypeSession::UpdatePreview()
    {
        const bool preview = m_livePreview && !m_previewSuspended && IsDirty();
        if (preview) m_storage->Preview(&m_asset);
        else if (m_previewing) m_storage->Preview(nullptr);
        m_previewing = preview;
    }
}
