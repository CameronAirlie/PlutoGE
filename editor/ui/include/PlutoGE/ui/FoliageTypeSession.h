#pragma once

#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/ui/DocumentHistory.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace PlutoGE::scene
{
    class Scene;
}

namespace PlutoGE::ui
{
    // Where a foliage type lives. Project assets are .plutofoliage files; tests implement their own.
    class FoliageTypeStorage
    {
    public:
        virtual ~FoliageTypeStorage() = default;
        [[nodiscard]] virtual std::string GetLabel() const = 0;
        // The asset reference foliage types link to; empty when they cannot link to it.
        [[nodiscard]] virtual std::string GetReference() const { return {}; }
        virtual bool Read(scene::FoliageTypeAsset &asset, std::string &error) = 0;
        virtual bool Write(const scene::FoliageTypeAsset &asset, std::string &error) = 0;
        // Shows unsaved settings on the types linked to this asset; null goes back to the saved ones.
        virtual void Preview(const scene::FoliageTypeAsset *asset) {}
    };

    // A .plutofoliage file, and the linked types in whichever scene `scene` returns (it may return null).
    [[nodiscard]] std::unique_ptr<FoliageTypeStorage> MakeFoliageTypeAssetStorage(std::string reference, std::function<scene::Scene *()> scene);

    // An open foliage type with undo, unsaved-change tracking and a live preview on the types linked to it.
    class FoliageTypeSession
    {
    public:
        // Null with `error` set when the type cannot be read, so a damaged file is never overwritten by accident.
        static std::unique_ptr<FoliageTypeSession> Open(std::unique_ptr<FoliageTypeStorage> storage, std::string &error);
        // Ends any preview, so closing without saving leaves linked types as saved.
        ~FoliageTypeSession();

        [[nodiscard]] std::string GetLabel() const { return m_storage->GetLabel(); }
        [[nodiscard]] std::string GetReference() const { return m_storage->GetReference(); }
        [[nodiscard]] const scene::FoliageTypeAsset &GetAsset() const { return m_asset; }

        // Changes the asset. Changes made while a control is held become one undo step until EndFrame.
        void Modify(const std::function<void(scene::FoliageTypeAsset &)> &edit);
        void EndFrame(bool controlHeld) { m_history.EndFrame(controlHeld); }

        [[nodiscard]] bool CanUndo() const { return m_history.CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return m_history.CanRedo(); }
        void Undo();
        void Redo();

        [[nodiscard]] bool IsDirty() const { return m_asset != m_saved; }
        bool Save(std::string &error);
        void Revert();

        // While on, unsaved changes show on every type linked to the asset.
        void SetLivePreview(bool enabled);
        [[nodiscard]] bool IsLivePreview() const { return m_livePreview; }
        // Holds the preview back without changing the Live setting, such as while the game runs.
        void SetPreviewSuspended(bool suspended);
        [[nodiscard]] bool IsPreviewing() const { return m_previewing; }
        // Shows the preview again on types that lost it: another scene, or scene undo reloading them from the file.
        void ReapplyPreview();

    private:
        explicit FoliageTypeSession(std::unique_ptr<FoliageTypeStorage> storage) : m_storage(std::move(storage)) {}
        void UpdatePreview();

        std::unique_ptr<FoliageTypeStorage> m_storage;
        scene::FoliageTypeAsset m_asset;
        scene::FoliageTypeAsset m_saved;
        DocumentHistory<scene::FoliageTypeAsset> m_history;
        bool m_livePreview = true;
        bool m_previewSuspended = false;
        bool m_previewing = false;
    };
}
