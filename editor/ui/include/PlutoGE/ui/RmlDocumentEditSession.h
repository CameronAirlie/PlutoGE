#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::ui
{
    // Source is authoritative. Future visual tools submit edits through SetSource,
    // rather than serializing the rendered DOM back over the authored document.
    class RmlDocumentEditSession
    {
    public:
        struct Buffer
        {
            std::filesystem::path path;
            std::string source;
            std::string savedSource;
            bool conflict = false;
            bool IsDirty() const { return source != savedSource; }
        };

        void Open(const std::filesystem::path &document, const std::filesystem::path &assetRoot);
        void SetSource(std::size_t index, std::string source);
        bool Undo();
        bool Redo();
        void RefreshDependencies();
        void PollExternalChanges();
        void Save(); // Rejects conflicting writes; each file is replaced atomically.
        void Reload(); // Explicit discard; reloads the document and its dependencies.
        bool IsDirty() const;
        bool HasConflicts() const;
        const std::vector<std::string> &GetDiagnostics() const { return m_diagnostics; }
        std::uint64_t GetRevision() const { return m_revision; }
        const std::vector<Buffer> &GetBuffers() const { return m_buffers; }
        const std::filesystem::path &GetAssetRoot() const { return m_assetRoot; }
        std::unordered_map<std::string, std::string> GetSourceOverlay() const;

    private:
        struct Edit
        {
            std::filesystem::path path;
            std::size_t offset;
            std::string before, after;
        };
        void Apply(const Edit &edit, bool forward);
        std::filesystem::path Resolve(const std::filesystem::path &base, const std::string &reference) const;
        std::filesystem::path m_assetRoot;
        std::vector<Buffer> m_buffers;
        std::vector<Edit> m_undo, m_redo;
        std::vector<std::string> m_diagnostics;
        std::uint64_t m_revision = 0;
    };
}
