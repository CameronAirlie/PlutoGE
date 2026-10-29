#pragma once

#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/ui/DocumentHistory.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace PlutoGE::assets
{
    class AssetManager;
    class Project;
}

namespace PlutoGE::ui
{
    // Where a preset lives. Project assets go through the AssetManager; tests implement their own.
    class PostProcessPresetStorage
    {
    public:
        virtual ~PostProcessPresetStorage() = default;
        [[nodiscard]] virtual std::string GetLabel() const = 0;
        // The asset reference cameras use for this preset; empty when cameras cannot refer to it.
        [[nodiscard]] virtual std::string GetReference() const { return {}; }
        virtual bool Read(assets::PostProcessPresetAsset &preset, std::string &error) = 0;
        virtual bool Write(const assets::PostProcessPresetAsset &preset, std::string &error) = 0;
        // Shows unsaved settings on the cameras using the preset; null goes back to the saved ones.
        virtual void Preview(const assets::PostProcessPresetAsset *preset) {}
    };

    [[nodiscard]] std::unique_ptr<PostProcessPresetStorage> MakePostProcessPresetAssetStorage(assets::AssetManager &assets,
                                                                                             std::string reference);

    // Writes `preset` as a new .plutopostprocess in the project's PostProcessing folder (or `directory`), named
    // after `name` and made unique. Returns its reference, or empty with `error` set.
    [[nodiscard]] std::string CreatePostProcessPresetAsset(const assets::Project &project, assets::AssetManager &assets,
                                                           const std::string &name, const assets::PostProcessPresetAsset &preset,
                                                           std::string &error, const std::filesystem::path &directory = {});

    // An open preset with undo, unsaved-change tracking and an optional live preview on every camera using it.
    class PostProcessPresetSession
    {
    public:
        // Null with `error` set when the preset cannot be read.
        static std::unique_ptr<PostProcessPresetSession> Open(std::unique_ptr<PostProcessPresetStorage> storage, std::string &error);
        // Opens a project asset reference ("project://PostProcessing/Main.plutopostprocess").
        static std::unique_ptr<PostProcessPresetSession> OpenAsset(const std::string &reference, std::string &error);
        // Ends any preview, so closing without saving leaves cameras as saved.
        ~PostProcessPresetSession();

        [[nodiscard]] std::string GetLabel() const { return m_storage->GetLabel(); }
        [[nodiscard]] std::string GetReference() const { return m_storage->GetReference(); }
        [[nodiscard]] const assets::PostProcessPresetAsset &GetPreset() const { return m_preset; }

        // Changes the preset. Changes made while a control is held become one undo step until EndFrame.
        void Modify(const std::function<void(assets::PostProcessPresetAsset &)> &edit);
        void EndFrame(bool controlHeld) { m_history.EndFrame(controlHeld); }

        [[nodiscard]] bool CanUndo() const { return m_history.CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return m_history.CanRedo(); }
        void Undo();
        void Redo();

        [[nodiscard]] bool IsDirty() const { return m_preset != m_saved; }
        bool Save(std::string &error);
        void Revert();

        // While on, unsaved changes show on every camera using the preset.
        void SetLivePreview(bool enabled);
        [[nodiscard]] bool IsLivePreview() const { return m_livePreview; }

        // Increments on every change, for views that cache derived data.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }

    private:
        explicit PostProcessPresetSession(std::unique_ptr<PostProcessPresetStorage> storage) : m_storage(std::move(storage)) {}
        void Changed();

        std::unique_ptr<PostProcessPresetStorage> m_storage;
        assets::PostProcessPresetAsset m_preset;
        assets::PostProcessPresetAsset m_saved;
        DocumentHistory<assets::PostProcessPresetAsset> m_history;
        bool m_livePreview = true;
        bool m_previewing = false;
        std::uint64_t m_revision = 1;
    };
}
