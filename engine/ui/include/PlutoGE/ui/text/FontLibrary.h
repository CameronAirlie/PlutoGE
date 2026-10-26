#pragma once

#include "PlutoGE/ui/text/FontFace.h"
#include "PlutoGE/ui/text/GlyphAtlas.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    // Process-wide font cache and glyph atlas. Fonts are identified by the
    // reference the UI stores (an asset reference or a path); a resolver set
    // by the engine maps references to files.
    class FontLibrary
    {
    public:
        using PathResolver = std::function<std::filesystem::path(const std::string &reference)>;

        static FontLibrary &Get();

        FontLibrary();
        ~FontLibrary();
        FontLibrary(const FontLibrary &) = delete;
        FontLibrary &operator=(const FontLibrary &) = delete;

        void SetPathResolver(PathResolver resolver) { m_resolver = std::move(resolver); }
        // Directories searched for the bundled default font before system fonts.
        void AddFontSearchDirectory(std::filesystem::path directory);

        // Cached; null when the reference cannot be loaded. Failures are cached
        // too, so a missing font is not re-read every frame.
        FontFace *Load(const std::string &reference);
        FontFace *LoadFromMemory(const std::string &name, std::vector<std::byte> data);

        // The project default when set and loadable, otherwise the first
        // available bundled or system font. Null only if none exist.
        FontFace *GetDefaultFont();
        void SetDefaultFontReference(std::string reference);
        // Faces tried, in order, for codepoints missing from a run's font.
        [[nodiscard]] std::span<FontFace *const> GetFallbackFonts();
        void AddFallbackFont(const std::string &reference);

        [[nodiscard]] GlyphAtlas &GetAtlas() { return m_atlas; }
        [[nodiscard]] const std::string &GetLastError() const { return m_lastError; }

    private:
        FontFace *LoadFile(const std::string &key, const std::filesystem::path &path);
        void ResolveFallbacks();

        FT_LibraryRec_ *m_library = nullptr;
        PathResolver m_resolver;
        std::vector<std::filesystem::path> m_searchDirectories;
        std::unordered_map<std::string, std::unique_ptr<FontFace>> m_faces; // null entries record failures
        std::string m_defaultReference;
        FontFace *m_default = nullptr;
        bool m_defaultResolved = false;
        std::vector<std::string> m_fallbackReferences;
        std::vector<FontFace *> m_fallbacks;
        bool m_fallbacksResolved = false;
        std::uint32_t m_nextId = 1;
        GlyphAtlas m_atlas;
        std::string m_lastError;
    };
}
