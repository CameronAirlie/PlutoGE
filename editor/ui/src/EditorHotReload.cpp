// Reloads textures, materials and sounds edited outside the editor, while editing and during play.

#include "PlutoGE/ui/EditorShell.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/audio/AudioSystem.h"

namespace PlutoGE::ui
{
    void EditorShell::PollAssetHotReload()
    {
        if (!m_project || IsRuntimeExportProject())
            return;
        const auto now = AssetFileWatch::Clock::now();
        const auto root = m_project->GetAssetDirectoryPath();
        if (root != m_assetWatchRoot)
        {
            if (m_assetWatchFuture.valid())
                m_assetWatchFuture.wait();
            m_assetWatchFuture = {};
            m_assetWatch.Reset();
            m_assetWatchRoot = root;
            m_nextAssetScan = {};
        }

        if (m_assetWatchFuture.valid() && m_assetWatchFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            std::vector<std::string> changed;
            try
            {
                changed = m_assetWatch.Observe(m_assetWatchFuture.get(), now);
            }
            catch (const std::exception &error)
            {
                Log(ConsoleSeverity::Warning, "Asset change scan: " + std::string(error.what()));
            }
            m_nextAssetScan = now + std::chrono::seconds(1);
            if (!changed.empty())
                ReloadChangedAssets(changed);
        }

        if (!m_assetWatchFuture.valid() && now >= m_nextAssetScan)
            m_assetWatchFuture = std::async(std::launch::async, [root] { return AssetFileWatch::Capture(root); });
    }

    void EditorShell::ReloadChangedAssets(const std::vector<std::string> &paths)
    {
        auto &assets = m_engine.GetAssetManager();
        std::vector<std::string> textureAndMaterialReferences;
        std::vector<std::string> names;
        for (const auto &path : paths)
        {
            const auto kind = ClassifyHotReloadPath(path);
            if (!kind)
                continue;
            names.push_back(std::filesystem::path(path).filename().string());
            if (*kind == HotReloadKind::Audio)
                m_engine.GetAudioSystem().ForgetClip(path);
            else
                textureAndMaterialReferences.push_back(m_project->MakeAssetReference(path));
        }
        if (!textureAndMaterialReferences.empty())
        {
            // Textures update in place; materials keep their pointers and rebind, so scene objects need no change.
            assets.RefreshImportedAssets(textureAndMaterialReferences);
            if (m_scene)
                for (const auto &reference : textureAndMaterialReferences)
                    if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Material)
                        m_scene->RefreshMaterialAsset(reference, assets.LoadMaterialAsset(reference));
        }
        if (names.empty())
            return;
        std::string list;
        for (std::size_t i = 0; i < names.size() && i < 5; ++i)
            list += (i ? ", " : "") + names[i];
        if (names.size() > 5)
            list += ", +" + std::to_string(names.size() - 5) + " more";
        Log(ConsoleEntry{.severity = ConsoleSeverity::Info, .source = ConsoleSource::Import, .text = "Reloaded changed assets: " + list});
    }
}
