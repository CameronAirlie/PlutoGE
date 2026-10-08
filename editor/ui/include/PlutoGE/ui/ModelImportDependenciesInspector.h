#pragma once
#include "PlutoGE/asset_import/ImportState.h"
#include <future>
#include <stop_token>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    enum class ImportInputStatus { Unchecked, Current, Changed, Unavailable };
    struct ImportInputInspection
    {
        assetimport::ArtifactInput input;
        ImportInputStatus status = ImportInputStatus::Unchecked;
        std::string error;
    };
    // Accepted inputs are a historical snapshot, separate from runtime dependencies.
    // Verification owns copied paths and runs without touching the project or UI.
    class ModelImportDependenciesInspector
    {
    public:
        ~ModelImportDependenciesInspector();
        bool Refresh(const assets::Project &project, const std::string &source, const std::string &owner);
        bool Verify();
        bool Poll();
        void Render(const assets::Project &project, const std::string &source, const std::string &owner, bool importing);
        const auto &Inputs() const { return m_inputs; }
        const auto &Error() const { return m_error; }
    private:
        struct Verification { std::size_t revision; std::vector<ImportInputInspection> inputs; };
        std::stop_source m_stop;
        std::future<Verification> m_job;
        std::vector<ImportInputInspection> m_inputs;
        std::string m_key, m_error, m_generation;
        std::size_t m_revision = 0;
        bool m_wasImporting = false;
    };
}
