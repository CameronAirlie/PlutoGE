#include "PlutoGE/ui/AssetReferenceSearchPanel.h"
#include "PlutoGE/assets/AssetReferences.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <imgui.h>

namespace PlutoGE::ui
{
    struct AssetReferenceSearchPanel::Task
    {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::shared_ptr<const assets::AssetReferenceQuery> result;
        bool force = false;
        // Declared last so it joins before the state accessed by the worker dies.
        std::jthread worker;

        Task(std::filesystem::path root, std::string target)
            : worker([this, root = std::move(root), target = std::move(target)](std::stop_token stop) {
                assets::AssetReferenceIndex index;
                bool forceRefresh = false;
                while (!stop.stop_requested())
                {
                    auto query = std::make_shared<assets::AssetReferenceQuery>();
                    try { *query = index.Query(root, target, stop, forceRefresh); }
                    catch (const std::exception &error) { query->errors.push_back(error.what()); }
                    if (stop.stop_requested() || query->cancelled) break;
                    std::unique_lock lock(mutex);
                    result = std::move(query);
                    wake.wait_for(lock, stop, std::chrono::seconds(1), [this] { return force; });
                    forceRefresh = force;
                    force = false;
                }
            }) {}

        std::shared_ptr<const assets::AssetReferenceQuery> Snapshot()
        {
            std::lock_guard lock(mutex);
            return result;
        }

        void Refresh()
        {
            std::lock_guard lock(mutex);
            result.reset();
            force = true;
            wake.notify_all();
        }
    };

    AssetReferenceSearchPanel::AssetReferenceSearchPanel() = default;
    AssetReferenceSearchPanel::~AssetReferenceSearchPanel() = default;

    void AssetReferenceSearchPanel::Open(const std::filesystem::path &assetRoot, std::string target)
    {
        m_task.reset();
        m_root = assetRoot;
        m_target = std::move(target);
        m_open = true;
        m_task = std::make_unique<Task>(m_root, m_target);
    }

    void AssetReferenceSearchPanel::Close()
    {
        m_open = false;
        m_task.reset();
    }

    void AssetReferenceSearchPanel::Render(const std::filesystem::path &assetRoot,
                                         const std::function<void(const std::string &)> &reveal,
                                         const std::function<void(const std::string &)> &open)
    {
        if (!m_open || assetRoot != m_root) { Close(); return; }
        ImGui::SetNextWindowSize(ImVec2(750, 440), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Asset References", &m_open)) { ImGui::End(); return; }
        ImGui::TextWrapped("References to: %s", m_target.c_str());
        ImGui::TextWrapped("Saved assets only. Save scene and asset edits to include them. Results refresh automatically as files change.");
        if (ImGui::Button("Rescan all")) m_task->Refresh();
        ImGui::SameLine();
        if (ImGui::Button("Close")) m_open = false;
        auto result = m_task->Snapshot();
        if (!result) ImGui::TextUnformatted("Scanning assets...");
        else
        {
            ImGui::Text("%zu owner(s), %zu files scanned", result->owners.size(), result->scannedFiles);
            if (!result->errors.empty())
            {
                ImGui::TextWrapped("Results are incomplete: %zu scan issue(s).", result->errors.size());
                if (ImGui::TreeNode("Scan issues"))
                {
                    for (const auto &error : result->errors) ImGui::TextWrapped("%s", error.c_str());
                    ImGui::TreePop();
                }
            }
            if (result->owners.empty()) ImGui::TextUnformatted("No matching saved references were found.");
            if (ImGui::BeginTable("Owners", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                                  ImVec2(0, ImGui::GetContentRegionAvail().y)))
            {
                ImGui::TableSetupColumn("Owner");
                ImGui::TableSetupColumn("First line", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 45);
                ImGui::TableSetupColumn("Navigate", ImGuiTableColumnFlags_WidthFixed, 155);
                ImGui::TableHeadersRow();
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(result->owners.size()));
                while (clipper.Step())
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto &owner = result->owners[static_cast<std::size_t>(i)];
                        ImGui::PushID(i);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(owner.reference.c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", owner.reference.c_str());
                        ImGui::TableNextColumn();
                        if (owner.firstLine) ImGui::Text("%zu", owner.firstLine);
                        else ImGui::TextUnformatted("Binary");
                        ImGui::TableNextColumn(); ImGui::Text("%zu", owner.occurrences);
                        ImGui::TableNextColumn();
                        if (ImGui::SmallButton("Show in Browser")) reveal(owner.reference);
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Open")) open(owner.reference);
                        ImGui::PopID();
                    }
                ImGui::EndTable();
            }
        }
        ImGui::End();
        if (!m_open) m_task.reset();
    }
}
