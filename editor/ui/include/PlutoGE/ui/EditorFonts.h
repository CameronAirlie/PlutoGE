#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>

struct ImFont;
struct ImGuiIO;

// Editor (ImGui) text fonts bundled under editor/resources/fonts/.
namespace PlutoGE::ui::fonts
{
    // Fonts are rasterized at kBaseSize; the user font size scales from it.
    inline constexpr float kBaseSize = 15.0f;
    inline constexpr float kMinSize = 10.0f;
    inline constexpr float kMaxSize = 24.0f;

    enum class EditorFont
    {
        Roboto,
        MartianMono,
        Georama,
        ImGuiDefault,
        Count,
    };

    inline constexpr EditorFont kDefaultFont = EditorFont::Roboto;

    struct EditorFontInfo
    {
        EditorFont font;
        std::string_view name;    // Display name, also persisted in the project manifest.
        const char *fileName;     // Bundled file, or nullptr for ImGui's built-in font.
    };

    // All selectable editor fonts, in menu order.
    [[nodiscard]] std::span<const EditorFontInfo> All();
    [[nodiscard]] const EditorFontInfo &Info(EditorFont font);
    [[nodiscard]] std::string_view Name(EditorFont font);
    // Unknown or empty names resolve to kDefaultFont.
    [[nodiscard]] EditorFont FromName(std::string_view name);

    // Locates a file in the bundled fonts directory; empty when it is missing.
    [[nodiscard]] std::filesystem::path ResolvePath(const char *fileName);

    // Owns the ImFont handles for every editor font in an ImGui font atlas.
    class EditorFontLibrary
    {
    public:
        // Adds every editor font, each with the icon font merged in.
        void Load(ImGuiIO &io);
        // Falls back to ImGui's built-in font when the requested file failed to load.
        [[nodiscard]] ImFont *Get(EditorFont font) const;

    private:
        std::array<ImFont *, static_cast<std::size_t>(EditorFont::Count)> m_fonts{};
    };
}
