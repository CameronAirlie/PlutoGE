#include "PlutoGE/ui/ModelImportDependenciesInspector.h"
#include <imgui.h>
#include <chrono>
#include <exception>
#include <fstream>
#include <array>

namespace PlutoGE::ui
{
    ModelImportDependenciesInspector::~ModelImportDependenciesInspector() { m_stop.request_stop(); }
    bool ModelImportDependenciesInspector::Refresh(const assets::Project &project,
        const std::string &source, const std::string &owner)
    {
        m_stop.request_stop();
        ++m_revision;
        m_key = project.GetManifestPath().generic_string() + "|" + source + "|" + owner;
        m_inputs.clear(); m_error.clear(); m_generation.clear();
        if (project.GetManifest().assetPipelineVersion < 3)
        { m_error = "Import dependency inspection requires a Library-based project (version 3 or later)."; return false; }
        assetimport::ImportState state;
        const auto status = assetimport::ImportStateStore(project.GetRootDirectory()).Load(owner, state, &m_error);
        if (status != assets::AssetMetadataStatus::Success)
        {
            if (m_error.empty()) m_error = "No accepted import inputs are available. Reimport this source to record them.";
            return false;
        }
        if (state.ownerId != owner || state.sourceReference != source)
        { m_error = "Accepted import inputs belong to a different source. Refresh or reimport this source."; return false; }
        m_generation = content::DigestToHex(state.generation);
        for (auto &input : state.inputs) m_inputs.push_back({std::move(input)});
        return true;
    }
    bool ModelImportDependenciesInspector::Verify()
    {
        Poll();
        if (m_job.valid() || m_inputs.empty()) return false;
        auto inputs = m_inputs;
        const auto revision = m_revision;
        m_stop = std::stop_source{};
        const auto stop = m_stop.get_token();
        try
        {
            m_job = std::async(std::launch::async, [inputs = std::move(inputs), revision, stop]() mutable
            {
                for (auto &row : inputs)
                {
                    if (stop.stop_requested()) break;
                    row.error.clear();
                    std::ifstream file(row.input.path, std::ios::binary);
                    if (!file)
                    { row.status = ImportInputStatus::Unavailable; row.error = "Cannot open input file."; continue; }
                    content::ContentHasher hasher;
                    std::array<char, 64 * 1024> buffer;
                    while (!stop.stop_requested() && (file.read(buffer.data(), buffer.size()) || file.gcount() > 0))
                        hasher.Update(std::as_bytes(std::span(buffer.data(), static_cast<std::size_t>(file.gcount()))));
                    if (stop.stop_requested()) break;
                    if (!file.eof())
                    { row.status = ImportInputStatus::Unavailable; row.error = "Cannot finish reading input file."; }
                    else row.status = hasher.Finalize() == row.input.digest ? ImportInputStatus::Current : ImportInputStatus::Changed;
                }
                return Verification{revision, std::move(inputs)};
            });
        }
        catch (const std::exception &error) { m_error = error.what(); return false; }
        return true;
    }
    bool ModelImportDependenciesInspector::Poll()
    {
        if (!m_job.valid() || m_job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        try
        {
            auto result = m_job.get();
            if (result.revision == m_revision) m_inputs = std::move(result.inputs);
        }
        catch (const std::exception &error) { m_error = error.what(); }
        return true;
    }
    void ModelImportDependenciesInspector::Render(const assets::Project &project,
        const std::string &source, const std::string &owner, bool importing)
    {
        Poll();
        const auto key = project.GetManifestPath().generic_string() + "|" + source + "|" + owner;
        if (key != m_key || (m_wasImporting && !importing)) Refresh(project, source, owner);
        m_wasImporting = importing;
        if (!ImGui::CollapsingHeader("Import Dependencies")) return;
        ImGui::PushID("ImportDependencies");
        ImGui::TextWrapped("Files recorded by the last successful import. These are import inputs, not the list of assets included in a build.");
        ImGui::BeginDisabled(importing || m_job.valid());
        if (ImGui::SmallButton("Refresh Inputs")) Refresh(project, source, owner);
        ImGui::SameLine();
        if (ImGui::SmallButton("Check Input Contents")) Verify();
        ImGui::EndDisabled();
        if (m_job.valid()) ImGui::TextDisabled("Checking input contents...");
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
        if (!m_generation.empty())
        {
            ImGui::Text("Recorded inputs: %zu", m_inputs.size());
            ImGui::TextDisabled("Checks are a snapshot. Check again after external edits.");
            if (ImGui::BeginTable("Inputs", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Input"); ImGui::TableSetupColumn("Path"); ImGui::TableSetupColumn("Content status"); ImGui::TableHeadersRow();
                ImGuiListClipper clipper; clipper.Begin(static_cast<int>(m_inputs.size()));
                while (clipper.Step()) for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                {
                    const auto &row = m_inputs[index];
                    ImGui::PushID(index); ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.input.identity.c_str());
                    ImGui::TableSetColumnIndex(1);
                    const auto utf8 = row.input.path.generic_u8string();
                    const std::string path(reinterpret_cast<const char *>(utf8.data()), utf8.size());
                    ImGui::TextUnformatted(path.c_str());
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
                    if (ImGui::BeginPopupContextItem("CopyInput"))
                    {
                        if (ImGui::MenuItem("Copy path")) ImGui::SetClipboardText(path.c_str());
                        if (ImGui::MenuItem("Copy accepted SHA-256")) ImGui::SetClipboardText(content::DigestToHex(row.input.digest).c_str());
                        ImGui::EndPopup();
                    }
                    ImGui::TableSetColumnIndex(2);
                    const char *status = "Not checked";
                    switch (row.status)
                    {
                    case ImportInputStatus::Current: status = "Matches accepted import"; break;
                    case ImportInputStatus::Changed: status = "Changed - reimport needed"; break;
                    case ImportInputStatus::Unavailable: status = "Missing or unreadable"; break;
                    default: break;
                    }
                    ImGui::TextUnformatted(status);
                    if (!row.error.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", row.error.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::PopID();
    }
}
