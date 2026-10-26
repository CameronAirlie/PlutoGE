#include "PlutoGE/scene/ui/UIThemeAssets.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/ui/UIThemeFormat.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr auto kRecheckInterval = std::chrono::milliseconds(500);
    }

    UIThemeAssets &UIThemeAssets::Get()
    {
        static UIThemeAssets assets;
        return assets;
    }

    void UIThemeAssets::SetPathResolver(PathResolver resolver)
    {
        m_resolver = std::move(resolver);
        Invalidate();
    }

    const UIThemeAssets::Entry &UIThemeAssets::Load(std::string_view reference)
    {
        const std::string key(reference);
        auto [found, inserted] = m_cache.try_emplace(key);
        Refresh(key, found->second, inserted);
        return found->second.entry;
    }

    void UIThemeAssets::Refresh(const std::string &reference, Cached &cached, bool force)
    {
        if (cached.live)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - cached.checkedAt < kRecheckInterval)
            return;
        cached.checkedAt = now;

        std::filesystem::path path;
        if (m_resolver)
            path = m_resolver(reference);
        else
            path = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
        std::error_code error;
        const auto writeTime = std::filesystem::last_write_time(path, error);
        if (error)
        {
            if (cached.entry.theme || force)
            {
                cached.entry = {nullptr, {"Theme '" + reference + "' was not found"}};
                ++m_revision;
            }
            return;
        }
        if (!force && cached.entry.theme && writeTime == cached.writeTime)
            return;

        std::ifstream file(path, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        Entry entry;
        entry.theme = std::make_shared<UITheme>(ParseUITheme(text.str(), &entry.errors));
        for (auto &message : entry.errors)
            message = std::filesystem::path(reference).filename().string() + ": " + message;
        cached.entry = std::move(entry);
        cached.writeTime = writeTime;
        ++m_revision;
    }

    void UIThemeAssets::SetLiveTheme(std::string_view reference, UITheme theme)
    {
        auto &cached = m_cache[std::string(reference)];
        cached.entry = {std::make_shared<UITheme>(std::move(theme)), {}};
        cached.live = true;
        ++m_revision;
    }

    void UIThemeAssets::ClearLiveTheme(std::string_view reference)
    {
        if (const auto found = m_cache.find(std::string(reference)); found != m_cache.end() && found->second.live)
        {
            m_cache.erase(found);
            ++m_revision;
        }
    }

    void UIThemeAssets::Invalidate()
    {
        std::erase_if(m_cache, [](const auto &item) { return !item.second.live; });
        ++m_revision;
    }

    void UIThemeAssets::AppendLayers(std::string_view reference, std::vector<std::shared_ptr<const UITheme>> &layers,
                                     std::vector<std::string> &errors)
    {
        std::vector<std::string> visiting;
        AppendLayers(reference, layers, errors, visiting);
    }

    void UIThemeAssets::AppendLayers(const std::shared_ptr<const UITheme> &theme, std::vector<std::shared_ptr<const UITheme>> &layers,
                                     std::vector<std::string> &errors)
    {
        if (!theme)
            return;
        std::vector<std::string> visiting;
        AppendBases(*theme, layers, errors, visiting);
        layers.push_back(theme);
    }

    void UIThemeAssets::AppendLayers(std::string_view reference, std::vector<std::shared_ptr<const UITheme>> &layers,
                                     std::vector<std::string> &errors, std::vector<std::string> &visiting)
    {
        const std::string key(reference);
        if (std::find(visiting.begin(), visiting.end(), key) != visiting.end())
        {
            errors.push_back("Theme '" + key + "' is based on itself");
            return;
        }
        const auto &entry = Load(reference);
        errors.insert(errors.end(), entry.errors.begin(), entry.errors.end());
        // A theme shared by two bases is layered once, at its first position.
        if (!entry.theme || std::find(layers.begin(), layers.end(), entry.theme) != layers.end())
            return;
        visiting.push_back(key);
        const auto theme = entry.theme; // AppendBases may reload and replace the entry
        AppendBases(*theme, layers, errors, visiting);
        visiting.pop_back();
        layers.push_back(theme);
    }

    void UIThemeAssets::AppendBases(const UITheme &theme, std::vector<std::shared_ptr<const UITheme>> &layers,
                                    std::vector<std::string> &errors, std::vector<std::string> &visiting)
    {
        for (const auto &base : theme.baseThemes)
            AppendLayers(base, layers, errors, visiting);
    }
}
