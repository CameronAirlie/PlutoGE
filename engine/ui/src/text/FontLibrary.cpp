#include "PlutoGE/ui/text/FontLibrary.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        std::filesystem::path ExecutableDirectory()
        {
            std::filesystem::path executable;
#ifdef _WIN32
            std::wstring path(32768, L'\0');
            const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (size && size < path.size())
            {
                path.resize(size);
                executable = path;
            }
#elif defined(__APPLE__)
            std::uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string path(size, '\0');
            if (_NSGetExecutablePath(path.data(), &size) == 0)
                executable = path.c_str();
#else
            std::error_code error;
            executable = std::filesystem::read_symlink("/proc/self/exe", error);
#endif
            return executable.empty() ? std::filesystem::path{} : executable.parent_path();
        }

        std::vector<std::byte> ReadFile(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
                return {};
            const auto size = static_cast<std::size_t>(stream.tellg());
            std::vector<std::byte> data(size);
            stream.seekg(0);
            stream.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(size));
            return stream ? data : std::vector<std::byte>{};
        }

        std::vector<std::filesystem::path> SystemFontDirectories()
        {
            std::vector<std::filesystem::path> directories;
#ifdef _WIN32
            if (const char *windows = std::getenv("WINDIR"))
                directories.emplace_back(std::filesystem::path(windows) / "Fonts");
            directories.emplace_back("C:/Windows/Fonts");
#elif defined(__APPLE__)
            directories.emplace_back("/System/Library/Fonts/Supplemental");
            directories.emplace_back("/Library/Fonts");
#else
            directories.emplace_back("/usr/share/fonts/truetype/dejavu");
            directories.emplace_back("/usr/share/fonts/TTF");
            directories.emplace_back("/usr/share/fonts/dejavu");
#endif
            return directories;
        }

        // Preferred UI faces, best first: the bundled editor font, then
        // common system sans-serif faces.
        constexpr const char *kDefaultFontFiles[] = {
            "Roboto.ttf", "segoeui.ttf", "arial.ttf", "Arial.ttf", "DejaVuSans.ttf", "Helvetica.ttc"};
        // Wide-coverage system faces used when a glyph is missing.
        constexpr const char *kFallbackFontFiles[] = {
            "seguisym.ttf", "msyh.ttc", "YuGothR.ttc", "malgun.ttf", "Nirmala.ttf", "DejaVuSans.ttf"};
    }

    FontLibrary &FontLibrary::Get()
    {
        static FontLibrary library;
        return library;
    }

    FontLibrary::FontLibrary()
    {
        if (FT_Init_FreeType(&m_library) != 0)
        {
            m_library = nullptr;
            m_lastError = "FreeType failed to initialize.";
            return;
        }
        FT_Int spread = FontFace::kSdfSpread;
        FT_Property_Set(m_library, "sdf", "spread", &spread);
        FT_Property_Set(m_library, "bsdf", "spread", &spread);
        // Fonts shipped beside the executable (the editor deploys resources/fonts).
        if (const auto directory = ExecutableDirectory(); !directory.empty())
        {
            m_searchDirectories.push_back(directory / "resources" / "fonts");
            m_searchDirectories.push_back(directory / "Fonts");
        }
    }

    FontLibrary::~FontLibrary()
    {
        // Faces must close before the library that created them.
        m_faces.clear();
        if (m_library)
            FT_Done_FreeType(m_library);
    }

    void FontLibrary::AddFontSearchDirectory(std::filesystem::path directory)
    {
        m_searchDirectories.push_back(std::move(directory));
        m_defaultResolved = false;
    }

    FontFace *FontLibrary::LoadFile(const std::string &key, const std::filesystem::path &path)
    {
        if (const auto found = m_faces.find(key); found != m_faces.end())
            return found->second.get();
        std::unique_ptr<FontFace> face;
        auto data = m_reader ? m_reader(path) : ReadFile(path);
        if (data.empty())
            m_lastError = "Could not read font file " + path.string();
        else
            face = FontFace::Load(m_library, std::move(data), key, m_nextId++, &m_lastError);
        return m_faces.emplace(key, std::move(face)).first->second.get();
    }

    FontFace *FontLibrary::Load(const std::string &reference)
    {
        if (reference.empty())
            return nullptr;
        if (const auto found = m_faces.find(reference); found != m_faces.end())
            return found->second.get();
        std::filesystem::path path = m_resolver ? m_resolver(reference) : std::filesystem::path{};
        if (path.empty())
            path = reference;
        return LoadFile(reference, path);
    }

    FontFace *FontLibrary::LoadFromMemory(const std::string &name, std::vector<std::byte> data)
    {
        auto face = FontFace::Load(m_library, std::move(data), name, m_nextId++, &m_lastError);
        auto *result = face.get();
        m_faces[name] = std::move(face);
        return result;
    }

    void FontLibrary::SetDefaultFontReference(std::string reference)
    {
        m_defaultReference = std::move(reference);
        m_defaultResolved = false;
    }

    FontFace *FontLibrary::GetDefaultFont()
    {
        if (m_defaultResolved)
            return m_default;
        m_defaultResolved = true;
        m_default = m_defaultReference.empty() ? nullptr : Load(m_defaultReference);
        if (m_default)
            return m_default;

        auto directories = m_searchDirectories;
        for (auto &directory : SystemFontDirectories())
            directories.push_back(std::move(directory));
        for (const char *file : kDefaultFontFiles)
            for (const auto &directory : directories)
            {
                const auto path = directory / file;
                std::error_code error;
                if (!std::filesystem::exists(path, error))
                    continue;
                if ((m_default = LoadFile(path.generic_string(), path)))
                    return m_default;
            }
        return nullptr;
    }

    void FontLibrary::AddFallbackFont(const std::string &reference)
    {
        m_fallbackReferences.push_back(reference);
        m_fallbacksResolved = false;
    }

    void FontLibrary::ResolveFallbacks()
    {
        m_fallbacks.clear();
        for (const auto &reference : m_fallbackReferences)
            if (auto *face = Load(reference))
                m_fallbacks.push_back(face);
        for (const char *file : kFallbackFontFiles)
            for (const auto &directory : SystemFontDirectories())
            {
                const auto path = directory / file;
                std::error_code error;
                if (!std::filesystem::exists(path, error))
                    continue;
                if (auto *face = LoadFile(path.generic_string(), path))
                {
                    m_fallbacks.push_back(face);
                    break;
                }
            }
        m_fallbacksResolved = true;
    }

    std::span<FontFace *const> FontLibrary::GetFallbackFonts()
    {
        // Loaded on first need: the wide-coverage faces are large files.
        if (!m_fallbacksResolved)
            ResolveFallbacks();
        return m_fallbacks;
    }
}
