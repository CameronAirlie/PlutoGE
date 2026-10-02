#include "PlutoGE/ui/RmlDocumentEditSession.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        std::string Read(const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) throw std::runtime_error("Cannot read " + path.string());
            std::string text((std::istreambuf_iterator<char>(stream)), {});
            if (stream.bad()) throw std::runtime_error("Cannot read " + path.string());
            if (text.find('\0') != std::string::npos) throw std::runtime_error("Source contains null bytes: " + path.string());
            return text;
        }

        void WriteAtomic(const std::filesystem::path &path, const std::string &text)
        {
            auto temporary = path;
            temporary += ".pluto-ui-save";
            if (std::filesystem::exists(temporary))
                throw std::runtime_error("Save staging file already exists: " + temporary.string());
            try
            {
                std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
                stream.write(text.data(), static_cast<std::streamsize>(text.size()));
                stream.flush();
                if (!stream) throw std::runtime_error("Cannot save " + path.string());
                stream.close();
                if (!stream) throw std::runtime_error("Cannot close " + temporary.string());
#ifdef _WIN32
                if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Replace source file");
#else
                std::filesystem::rename(temporary, path);
#endif
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                throw;
            }
        }

        std::vector<std::string> LinkedSourceReferences(const std::string &source)
        {
            // Discover stylesheet and native template links, preserving all original source bytes.
            // This is dependency discovery, not a markup serializer.
            static const std::regex comments(R"(<!--[\s\S]*?-->|/\*[\s\S]*?\*/)");
            static const std::regex links(R"rml(<link\b[^>]*\s+href\s*=\s*(?:"([^"]+)"|'([^']+)'|([^\s>]+))[^>]*>)rml", std::regex::icase);
            const auto clean = std::regex_replace(source, comments, "");
            std::vector<std::string> result;
            for (std::sregex_iterator it(clean.begin(), clean.end(), links), end; it != end; ++it)
                for (std::size_t group = 1; group < it->size(); ++group)
                    if ((*it)[group].matched)
                    {
                        auto reference = (*it)[group].str();
                        if (group == 3 && reference.ends_with('/')) reference.pop_back();
                        result.push_back(std::move(reference));
                        break;
                    }
            return result;
        }
    }

    std::filesystem::path RmlDocumentEditSession::Resolve(const std::filesystem::path &base, const std::string &reference) const
    {
        const auto path = std::filesystem::weakly_canonical(reference.starts_with("project://")
            ? m_assetRoot / reference.substr(10) : base / reference);
        const auto relative = path.lexically_relative(m_assetRoot);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            throw std::runtime_error("UI sources must be inside project assets: " + reference);
        return path;
    }

    void RmlDocumentEditSession::Open(const std::filesystem::path &document, const std::filesystem::path &assetRoot)
    {
        RmlDocumentEditSession next;
        next.m_assetRoot = std::filesystem::weakly_canonical(assetRoot);
        const auto path = next.Resolve(next.m_assetRoot, document.generic_string());
        if (path.extension() != ".rml") throw std::runtime_error("Select an .rml document");
        auto source = Read(path);
        next.m_buffers.push_back({path, source, source});
        next.RefreshDependencies();
        next.m_revision = m_revision + 1;
        *this = std::move(next);
    }

    void RmlDocumentEditSession::RefreshDependencies()
    {
        m_diagnostics.clear();
        // Keep detached buffers: removing a link must never discard unsaved edits.
        if (m_buffers.empty()) return;
        for (std::size_t index = 0; index < m_buffers.size(); ++index)
        {
            if (m_buffers[index].path.extension() != ".rml") continue;
            const auto base = m_buffers[index].path.parent_path();
            const auto references = LinkedSourceReferences(m_buffers[index].source);
            for (const auto &reference : references)
            {
                try
                {
                    const auto path = Resolve(base, reference);
                    if (path.extension() != ".rcss" && path.extension() != ".rml") continue;
                    if (std::any_of(m_buffers.begin(), m_buffers.end(), [&](const Buffer &buffer) { return buffer.path == path; })) continue;
                    auto text = Read(path);
                    m_buffers.push_back({path, text, text});
                    ++m_revision;
                }
                catch (const std::exception &error) { m_diagnostics.push_back(error.what()); }
            }
        }
    }

    void RmlDocumentEditSession::SetSource(std::size_t index, std::string source)
    {
        auto &buffer = m_buffers.at(index);
        if (source == buffer.source) return;
        if (source.find('\0') != std::string::npos) throw std::runtime_error("Source contains null bytes");
        // Retain only the changed span, not two full documents per keystroke.
        std::size_t prefix = 0;
        while (prefix < buffer.source.size() && prefix < source.size() && buffer.source[prefix] == source[prefix]) ++prefix;
        std::size_t suffix = 0;
        while (suffix < buffer.source.size() - prefix && suffix < source.size() - prefix &&
            buffer.source[buffer.source.size() - suffix - 1] == source[source.size() - suffix - 1]) ++suffix;
        m_undo.push_back({buffer.path, prefix,
            buffer.source.substr(prefix, buffer.source.size() - prefix - suffix),
            source.substr(prefix, source.size() - prefix - suffix)});
        // Bound retained history; visual commands can use the same edit API.
        if (m_undo.size() > 128) m_undo.erase(m_undo.begin());
        buffer.source = std::move(source);
        m_redo.clear();
        ++m_revision;
    }

    void RmlDocumentEditSession::Apply(const Edit &edit, bool forward)
    {
        for (auto &buffer : m_buffers)
        {
            if (buffer.path != edit.path) continue;
            buffer.source.replace(edit.offset, forward ? edit.before.size() : edit.after.size(), forward ? edit.after : edit.before);
            ++m_revision;
            return;
        }
    }

    bool RmlDocumentEditSession::Undo()
    {
        if (m_undo.empty()) return false;
        Apply(m_undo.back(), false);
        m_redo.push_back(std::move(m_undo.back()));
        m_undo.pop_back();
        return true;
    }

    bool RmlDocumentEditSession::Redo()
    {
        if (m_redo.empty()) return false;
        Apply(m_redo.back(), true);
        m_undo.push_back(std::move(m_redo.back()));
        m_redo.pop_back();
        return true;
    }

    void RmlDocumentEditSession::PollExternalChanges()
    {
        bool changed = false;
        for (auto &buffer : m_buffers)
        {
            if (!std::filesystem::exists(buffer.path)) { buffer.conflict = true; continue; }
            auto disk = Read(buffer.path);
            buffer.conflict = disk != buffer.savedSource && buffer.IsDirty();
            if (disk != buffer.savedSource && !buffer.IsDirty())
            {
                buffer.source = disk; buffer.savedSource = std::move(disk); changed = true; ++m_revision;
            }
        }
        // Old undo snapshots must not overwrite freshly reloaded external edits.
        if (changed) { m_undo.clear(); m_redo.clear(); }
        // A previously missing stylesheet may have appeared without changing
        // any existing source buffer.
        RefreshDependencies();
    }

    void RmlDocumentEditSession::Save()
    {
        PollExternalChanges();
        if (HasConflicts()) throw std::runtime_error("Files changed on disk. Resolve the conflict or discard and reload before saving.");
        for (auto &buffer : m_buffers)
        {
            if (!buffer.IsDirty()) continue;
            if (Read(buffer.path) != buffer.savedSource)
            {
                buffer.conflict = true;
                throw std::runtime_error("File changed during save: " + buffer.path.string());
            }
            WriteAtomic(buffer.path, buffer.source);
            buffer.savedSource = buffer.source;
        }
    }

    void RmlDocumentEditSession::Reload()
    {
        if (!m_buffers.empty()) Open(m_buffers.front().path, m_assetRoot);
    }

    bool RmlDocumentEditSession::IsDirty() const
    { return std::any_of(m_buffers.begin(), m_buffers.end(), [](const Buffer &buffer) { return buffer.IsDirty(); }); }

    bool RmlDocumentEditSession::HasConflicts() const
    { return std::any_of(m_buffers.begin(), m_buffers.end(), [](const Buffer &buffer) { return buffer.conflict; }); }

    std::unordered_map<std::string, std::string> RmlDocumentEditSession::GetSourceOverlay() const
    {
        std::unordered_map<std::string, std::string> result;
        for (const auto &buffer : m_buffers) result.emplace(buffer.path.generic_string(), buffer.source);
        return result;
    }
}
