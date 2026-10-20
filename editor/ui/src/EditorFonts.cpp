#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorIcons.h"

#include <imgui.h>

#include <algorithm>
#include <iostream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::ui::fonts
{
    namespace
    {
        constexpr std::array<EditorFontInfo, static_cast<std::size_t>(EditorFont::Count)> kFonts = {{
            // Variable font; ImGui renders its default Regular (wght 400) instance.
            {EditorFont::Roboto, "Roboto", "Roboto.ttf"},
            {EditorFont::MartianMono, "Martian Mono", "MartianMono-StdRg.ttf"},
            {EditorFont::Georama, "Georama", "Georama-Regular.ttf"},
            {EditorFont::ImGuiDefault, "ImGui Default", nullptr},
        }};
        constexpr const char *kIconFontFile = "fa-solid-900.ttf";

        std::filesystem::path GetExecutableDirectory()
        {
#ifdef _WIN32
            std::array<char, MAX_PATH> modulePath{};
            const DWORD modulePathLength = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size()));
            if (modulePathLength > 0 && modulePathLength < modulePath.size())
            {
                return std::filesystem::path(modulePath.data()).parent_path().lexically_normal();
            }
#endif
            return std::filesystem::current_path();
        }

        void MergeIcons(ImGuiIO &io)
        {
            const auto path = ResolvePath(kIconFontFile);
            if (path.empty())
            {
                std::cerr << "Editor icon font missing: " << kIconFontFile << '\n';
                return;
            }
            ImFontConfig config{};
            config.MergeMode = true;
            config.PixelSnapH = true;
            config.GlyphMinAdvanceX = kBaseSize;
            io.Fonts->AddFontFromFileTTF(path.string().c_str(), kBaseSize, &config, icons::Ranges);
        }

        ImFont *LoadFontFile(ImGuiIO &io, const char *fileName)
        {
            const auto path = ResolvePath(fileName);
            if (path.empty())
            {
                std::cerr << "Editor font missing: " << fileName << '\n';
                return nullptr;
            }
            ImFontConfig config{};
            config.OversampleH = 3;
            config.OversampleV = 2;
            config.PixelSnapH = true;
            return io.Fonts->AddFontFromFileTTF(path.string().c_str(), kBaseSize, &config);
        }

        ImFont *LoadBuiltinFont(ImGuiIO &io)
        {
            ImFontConfig config{};
            config.SizePixels = kBaseSize;
            return io.Fonts->AddFontDefault(&config);
        }
    }

    std::span<const EditorFontInfo> All() { return kFonts; }

    const EditorFontInfo &Info(EditorFont font)
    {
        const auto index = static_cast<std::size_t>(font);
        return index < kFonts.size() ? kFonts[index] : kFonts[static_cast<std::size_t>(kDefaultFont)];
    }

    std::string_view Name(EditorFont font) { return Info(font).name; }

    EditorFont FromName(std::string_view name)
    {
        const auto match = std::ranges::find(kFonts, name, &EditorFontInfo::name);
        return match != kFonts.end() ? match->font : kDefaultFont;
    }

    std::filesystem::path ResolvePath(const char *fileName)
    {
        const std::filesystem::path relativePath = std::filesystem::path("resources/fonts") / fileName;
        const std::array<std::filesystem::path, 4> candidates = {
            GetExecutableDirectory() / relativePath,
            std::filesystem::current_path() / relativePath,
            std::filesystem::current_path() / "editor" / relativePath,
            std::filesystem::current_path() / ".." / ".." / ".." / "editor" / relativePath,
        };

        for (const auto &candidate : candidates)
        {
            std::error_code errorCode;
            if (std::filesystem::exists(candidate, errorCode))
            {
                return candidate.lexically_normal();
            }
        }

        return {};
    }

    void EditorFontLibrary::Load(ImGuiIO &io)
    {
        for (const auto &info : kFonts)
        {
            ImFont *font = info.fileName ? LoadFontFile(io, info.fileName) : LoadBuiltinFont(io);
            if (font)
                MergeIcons(io);
            m_fonts[static_cast<std::size_t>(info.font)] = font;
        }
    }

    ImFont *EditorFontLibrary::Get(EditorFont font) const
    {
        const auto index = static_cast<std::size_t>(font);
        ImFont *loaded = index < m_fonts.size() ? m_fonts[index] : nullptr;
        return loaded ? loaded : m_fonts[static_cast<std::size_t>(EditorFont::ImGuiDefault)];
    }
}
