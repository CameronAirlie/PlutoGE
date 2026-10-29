#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"

#include <cctype>

namespace PlutoGE::ui
{
    namespace
    {
        class AssetStorage final : public PostProcessPresetStorage
        {
        public:
            AssetStorage(assets::AssetManager &assets, std::string reference) : m_assets(assets), m_reference(std::move(reference)) {}

            std::string GetLabel() const override { return std::filesystem::path(m_reference).filename().string(); }
            std::string GetReference() const override { return m_reference; }

            bool Read(assets::PostProcessPresetAsset &preset, std::string &error) override
            {
                // The saved file, not a preview another session left behind.
                m_assets.ClearPostProcessPresetPreview(m_reference);
                bool loaded = false;
                preset = m_assets.LoadPostProcessPresetAsset(m_reference, &loaded);
                if (!loaded)
                    error = "Could not read " + m_reference + ". It may be missing or written by a newer version.";
                return loaded;
            }

            bool Write(const assets::PostProcessPresetAsset &preset, std::string &error) override
            {
                return m_assets.SavePostProcessPresetAsset(m_reference, preset, &error);
            }

            void Preview(const assets::PostProcessPresetAsset *preset) override
            {
                if (preset)
                    m_assets.SetPostProcessPresetPreview(m_reference, *preset);
                else
                    m_assets.ClearPostProcessPresetPreview(m_reference);
            }

        private:
            assets::AssetManager &m_assets;
            std::string m_reference;
        };

        std::string SanitizeFileName(const std::string &name)
        {
            std::string result;
            for (const char character : name)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) || character == '_' || character == '-' || character == ' ')
                    result += character;
            }
            const auto first = result.find_first_not_of(' ');
            const auto last = result.find_last_not_of(' ');
            return first == std::string::npos ? std::string{} : result.substr(first, last - first + 1);
        }
    }

    std::unique_ptr<PostProcessPresetStorage> MakePostProcessPresetAssetStorage(assets::AssetManager &assets, std::string reference)
    {
        return std::make_unique<AssetStorage>(assets, std::move(reference));
    }

    std::string CreatePostProcessPresetAsset(const assets::Project &project, assets::AssetManager &assets, const std::string &name,
                                             const assets::PostProcessPresetAsset &preset, std::string &error,
                                             const std::filesystem::path &directory)
    {
        const std::string baseName = SanitizeFileName(name);
        if (baseName.empty())
        {
            error = "Give the preset a name using letters, numbers, spaces, - or _.";
            return {};
        }
        const auto folder = directory.empty() ? project.GetAssetDirectoryPath() / "PostProcessing" : directory;
        auto path = folder / (baseName + ".plutopostprocess");
        for (int suffix = 2; std::filesystem::exists(path); ++suffix)
            path = folder / (baseName + " " + std::to_string(suffix) + ".plutopostprocess");
        std::string reference = project.MakeAssetReference(path);
        if (!assets.SavePostProcessPresetAsset(reference, preset, &error))
            return {};
        return reference;
    }

    std::unique_ptr<PostProcessPresetSession> PostProcessPresetSession::Open(std::unique_ptr<PostProcessPresetStorage> storage, std::string &error)
    {
        std::unique_ptr<PostProcessPresetSession> session(new PostProcessPresetSession(std::move(storage)));
        if (!session->m_storage->Read(session->m_saved, error))
            return nullptr;
        session->m_preset = session->m_saved;
        return session;
    }

    std::unique_ptr<PostProcessPresetSession> PostProcessPresetSession::OpenAsset(const std::string &reference, std::string &error)
    {
        return Open(MakePostProcessPresetAssetStorage(core::Engine::GetInstance().GetAssetManager(), reference), error);
    }

    PostProcessPresetSession::~PostProcessPresetSession()
    {
        if (m_previewing)
            m_storage->Preview(nullptr);
    }

    void PostProcessPresetSession::Modify(const std::function<void(assets::PostProcessPresetAsset &)> &edit)
    {
        auto edited = m_preset;
        edit(edited);
        if (edited == m_preset)
            return;
        m_history.Record(std::move(m_preset));
        m_preset = std::move(edited);
        Changed();
    }

    void PostProcessPresetSession::Undo()
    {
        if (m_history.Undo(m_preset))
            Changed();
    }

    void PostProcessPresetSession::Redo()
    {
        if (m_history.Redo(m_preset))
            Changed();
    }

    bool PostProcessPresetSession::Save(std::string &error)
    {
        if (!m_storage->Write(m_preset, error))
            return false;
        m_saved = m_preset;
        // Saving replaces any preview with the saved file.
        m_previewing = false;
        m_storage->Preview(nullptr);
        return true;
    }

    void PostProcessPresetSession::Revert()
    {
        if (!IsDirty())
            return;
        m_history.CloseGroup();
        Modify([this](assets::PostProcessPresetAsset &preset) { preset = m_saved; });
        m_history.CloseGroup();
    }

    void PostProcessPresetSession::SetLivePreview(bool enabled)
    {
        if (m_livePreview == enabled)
            return;
        m_livePreview = enabled;
        Changed();
    }

    void PostProcessPresetSession::Changed()
    {
        ++m_revision;
        const bool preview = m_livePreview && IsDirty();
        if (preview)
            m_storage->Preview(&m_preset);
        else if (m_previewing)
            m_storage->Preview(nullptr);
        m_previewing = preview;
    }
}
