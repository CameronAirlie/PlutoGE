#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"

#include <imgui.h>
#include <cstdio>

namespace PlutoGE::ui
{
    void EditorShell::RenderViewportBookmarks()
    {
        if (!m_showViewportBookmarks) return;
        ImGui::SetNextWindowSize(ImVec2(440, 430), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport Bookmarks", &m_showViewportBookmarks))
        {
            ImGui::End();
            return;
        }
        if (!m_project)
        {
            m_bookmarkPath.clear();
            ImGui::TextUnformatted("Open a project to save named views.");
            ImGui::End();
            return;
        }
        const auto path = m_project->GetRootDirectory() / ".plutoge-editor" /
                          (m_project->GetManifestPath().filename().string() + ".bookmarks");
        if (m_bookmarkPath != path)
        {
            m_bookmarkPath = path;
            m_viewportBookmarks.clear();
            m_selectedBookmark = -1;
            m_bookmarkName.fill(0);
            m_bookmarksLoaded = ViewportBookmarks::Load(path, m_viewportBookmarks, m_bookmarkError);
        }
        if (!m_bookmarksLoaded)
        {
            ImGui::TextWrapped("%s", m_bookmarkError.c_str());
            if (ImGui::Button("Retry loading"))
                m_bookmarksLoaded = ViewportBookmarks::Load(path, m_viewportBookmarks, m_bookmarkError);
            ImGui::End();
            return;
        }
        const auto capture = [&]() {
            return ViewportBookmark{.name = m_bookmarkName.data(), .position = m_editorCamera.position,
                .yaw = m_editorCamera.yawDegrees, .pitch = m_editorCamera.pitchDegrees,
                .fov = m_editorCamera.camera.GetFOV(), .nearPlane = m_editorCamera.camera.GetNearPlane(),
                .farPlane = m_editorCamera.camera.GetFarPlane(), .moveSpeed = m_editorCamera.moveSpeed,
                .orthographic = m_editorCamera.orthographic, .orthographicSize = m_editorCamera.orthographicSize};
        };
        const auto save = [&](std::vector<ViewportBookmark> next) {
            if (!ViewportBookmarks::Save(path, next, m_bookmarkError)) return false;
            m_viewportBookmarks = std::move(next);
            return true;
        };

        ImGui::InputText("Name", m_bookmarkName.data(), m_bookmarkName.size());
        ImGui::BeginDisabled(IsRuntimeExportProject());
        if (ImGui::Button("Save Current View"))
        {
            auto next = m_viewportBookmarks;
            next.push_back(capture());
            if (save(std::move(next))) m_selectedBookmark = static_cast<int>(m_viewportBookmarks.size()) - 1;
        }
        ImGui::EndDisabled();
        ImGui::BeginChild("Saved views", ImVec2(0, 190), ImGuiChildFlags_Borders);
        for (std::size_t i = 0; i < m_viewportBookmarks.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(m_viewportBookmarks[i].name.c_str(), m_selectedBookmark == static_cast<int>(i)))
            {
                m_selectedBookmark = static_cast<int>(i);
                std::snprintf(m_bookmarkName.data(), m_bookmarkName.size(), "%s", m_viewportBookmarks[i].name.c_str());
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        const bool selected = m_selectedBookmark >= 0 && static_cast<std::size_t>(m_selectedBookmark) < m_viewportBookmarks.size();
        ImGui::BeginDisabled(!selected);
        if (ImGui::Button("Recall") && selected)
        {
            const auto &b = m_viewportBookmarks[m_selectedBookmark];
            m_editorCamera.position = b.position;
            m_editorCamera.yawDegrees = b.yaw;
            m_editorCamera.pitchDegrees = b.pitch;
            m_editorCamera.camera.SetFOV(b.fov);
            m_editorCamera.camera.SetNearPlane(b.nearPlane);
            m_editorCamera.camera.SetFarPlane(b.farPlane);
            m_editorCamera.moveSpeed = b.moveSpeed;
            m_editorCamera.speedAdjustment = 1;
            m_editorCamera.orthographic = b.orthographic;
            m_editorCamera.orthographicSize = b.orthographicSize;
            m_editorCamera.hasPerspectivePosition = false;
        }
        ImGui::BeginDisabled(IsRuntimeExportProject());
        ImGui::SameLine();
        if (ImGui::Button("Replace View") && selected)
        {
            auto next = m_viewportBookmarks;
            auto replacement = capture();
            replacement.name = next[m_selectedBookmark].name;
            next[m_selectedBookmark] = std::move(replacement);
            save(std::move(next));
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename") && selected)
        {
            auto next = m_viewportBookmarks;
            next[m_selectedBookmark].name = m_bookmarkName.data();
            save(std::move(next));
        }
        if (ImGui::Button("Delete") && selected)
        {
            auto next = m_viewportBookmarks;
            next.erase(next.begin() + m_selectedBookmark);
            if (save(std::move(next)))
            {
                m_selectedBookmark = -1;
                m_bookmarkName.fill(0);
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        ImGui::TextWrapped("Views save automatically as editor data in this project. Recall does not change scene cameras.");
        if (!m_bookmarkError.empty()) ImGui::TextWrapped("%s", m_bookmarkError.c_str());
        ImGui::End();
    }
}
