#pragma once

#include "PlutoGE/scene/ui/UITheme.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::scene
{
    // Loaded .plutotheme assets, shared by every theme component. Files are
    // re-read when they change on disk, and the theme editor can show unsaved
    // edits everywhere by publishing a live theme for a reference. Main
    // thread only, like the rest of the UI.
    class UIThemeAssets
    {
    public:
        using PathResolver = std::function<std::filesystem::path(std::string_view reference)>;

        static UIThemeAssets &Get();

        // Defaults to the engine asset manager; tests point it at files directly.
        void SetPathResolver(PathResolver resolver);

        struct Entry
        {
            std::shared_ptr<const UITheme> theme; // null when the file is missing
            std::vector<std::string> errors;
        };
        // The theme for an asset reference. Changed files are noticed within
        // half a second.
        [[nodiscard]] const Entry &Load(std::string_view reference);

        // Shows `theme` instead of the file until cleared (unsaved editor edits).
        void SetLiveTheme(std::string_view reference, UITheme theme);
        void ClearLiveTheme(std::string_view reference);
        // Forgets cached files so the next Load reads them again.
        void Invalidate();

        // Changes whenever any theme this cache returned may have changed.
        [[nodiscard]] std::uint64_t GetRevision() const { return m_revision; }

        // Appends `reference` and, before it, the themes it is based on (each
        // once, lowest priority first). Missing files and cycles go to `errors`.
        void AppendLayers(std::string_view reference, std::vector<std::shared_ptr<const UITheme>> &layers,
                          std::vector<std::string> &errors);
        // The same for an already-loaded theme, e.g. a component's local styles.
        void AppendLayers(const std::shared_ptr<const UITheme> &theme, std::vector<std::shared_ptr<const UITheme>> &layers,
                          std::vector<std::string> &errors);

    private:
        struct Cached
        {
            Entry entry;
            std::filesystem::file_time_type writeTime{};
            std::chrono::steady_clock::time_point checkedAt{};
            bool live = false;
        };

        void Refresh(const std::string &reference, Cached &cached, bool force);
        void AppendLayers(std::string_view reference, std::vector<std::shared_ptr<const UITheme>> &layers,
                          std::vector<std::string> &errors, std::vector<std::string> &visiting);
        void AppendBases(const UITheme &theme, std::vector<std::shared_ptr<const UITheme>> &layers,
                         std::vector<std::string> &errors, std::vector<std::string> &visiting);

        PathResolver m_resolver;
        std::unordered_map<std::string, Cached> m_cache;
        std::uint64_t m_revision = 1;
    };
}
