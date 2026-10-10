#include "PlutoGE/ui/panels/RmlDocumentEditorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/ui/SourceTextEditor.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/ui/RmlSourceTools.h"
#include <algorithm>
#include <imgui_internal.h>

namespace PlutoGE::ui
{
    bool RmlDocumentEditorPanel::OwnsKeyboardFocus() const
    {
        if (!IsOpen() || !ImGui::GetCurrentContext()) return false;
        auto *window = ImGui::FindWindowByName(GetName().c_str());
        auto *focused = ImGui::GetCurrentContext()->NavWindow;
        return window && focused && ImGui::IsWindowChildOf(focused, window, true, false);
    }

    void RmlDocumentEditorPanel::RequestOpen(const std::string &reference)
    {
        SetOpen(true);
        m_pendingReference = reference;
        if (m_session.IsDirty()) m_confirmOpen = true;
        else OpenPending();
    }

    void RmlDocumentEditorPanel::OpenPending()
    {
        try
        {
            auto *project = EditorShell::GetInstance().GetProject();
            if (!project || m_pendingReference.empty()) return;
            m_session.Open(project->ResolveAssetReference(m_pendingReference), project->GetAssetDirectoryPath());
            ClearPreview();
            m_previewRevision = 0; m_seenRevision = m_session.GetRevision();
            m_lastEdit = {}; m_error.clear(); m_pendingReference.clear();
        }
        catch (const std::exception &error) { m_error = error.what(); }
    }

    void RmlDocumentEditorPanel::ClearPreview()
    {
        if (m_texture.IsValid()) EditorShell::GetInstance().GetPanelManager().UnregisterTexture(m_texture);
        m_texture = {}; m_preview.Reset(); m_previewRevision = 0;
    }

    void RmlDocumentEditorPanel::Save()
    {
        try
        {
            m_session.Save(); m_error.clear();
            if (auto *project = EditorShell::GetInstance().GetProject()) project->RefreshAssetRegistry();
        }
        catch (const std::exception &error) { m_error = error.what(); }
    }

    void RmlDocumentEditorPanel::PreparePreview()
    {
        if (!IsOpen())
        {
            if (m_session.IsDirty()) { SetOpen(true); m_confirmClose = true; }
            else { ClearPreview(); return; }
        }
        if (m_session.GetBuffers().empty()) return;
        auto &shell = EditorShell::GetInstance();
        auto &engine = shell.GetEngine();
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastPoll > std::chrono::seconds(1))
        {
            m_lastPoll = now;
            try { m_session.PollExternalChanges(); }
            catch (const std::exception &error) { m_error = error.what(); }
        }
        if (m_seenRevision != m_session.GetRevision())
        { m_seenRevision = m_session.GetRevision(); m_lastEdit = now; }
        if (m_previewRevision != m_seenRevision && now - m_lastEdit >= std::chrono::milliseconds(200))
        {
            try { m_session.RefreshDependencies(); }
            catch (const std::exception &error) { m_error = error.what(); }
            m_seenRevision = m_session.GetRevision();
            m_preview.Rebuild(engine, m_session);
            m_previewRevision = m_seenRevision;
        }
        if (m_preview.Render(engine, m_width, m_height, m_background))
        {
            const EditorTextureDescriptor descriptor{.device = engine.GetRenderDevice(),
                .texture = m_preview.GetTexture(), .width = static_cast<unsigned>(m_width), .height = static_cast<unsigned>(m_height)};
            if (m_texture.IsValid()) shell.GetPanelManager().UpdateTexture(m_texture, descriptor);
            else m_texture = shell.GetPanelManager().RegisterTexture(descriptor);
        }
        if (engine.GetConfig().graphicsApi == render::rhi::GraphicsApi::OpenGL)
        { render::Graphics::ResetStateCache(); render::Graphics::Disable(GL_SCISSOR_TEST); }
    }

    void RmlDocumentEditorPanel::Render()
    {
        RenderPendingPrompts();
        ImGui::SetNextItemWidth(340);
        ImGui::InputTextWithHint("##openRml", "project://UI/document.rml", m_openReference.data(), m_openReference.size());
        ImGui::SameLine();
        if (ImGui::Button("Open RML")) RequestOpen(m_openReference.data());
        if (m_session.GetBuffers().empty())
        {
            ImGui::TextDisabled("Open an RML asset from the Content Browser or enter its project reference.");
            if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
            return;
        }

        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_Z))
            { if (ImGui::GetIO().KeyShift) m_session.Redo(); else m_session.Undo(); }
            if (ImGui::IsKeyPressed(ImGuiKey_Y)) m_session.Redo();
        }
        ImGui::TextWrapped("%s%s", m_session.GetBuffers().front().path.generic_string().c_str(), m_session.IsDirty() ? " *" : "");
        if (ImGui::Button("Save all") || (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))) Save();
        ImGui::SameLine(); if (ImGui::Button("Undo")) m_session.Undo();
        ImGui::SameLine(); if (ImGui::Button("Redo")) m_session.Redo();
        ImGui::SameLine(); if (ImGui::Button("Reload from disk")) m_confirmReload = true;
        ImGui::SameLine(); if (ImGui::Button("Rebuild preview")) { m_previewRevision = 0; m_lastEdit = {}; }
        if (m_confirmReload) { ImGui::OpenPopup("Reload UI sources?"); m_confirmReload = false; }
        if (ImGui::BeginPopupModal("Reload UI sources?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Discard all unsaved UI edits and reload from disk?");
            if (ImGui::Button("Discard and reload"))
            {
                try { m_session.Reload(); m_error.clear(); }
                catch (const std::exception &error) { m_error = error.what(); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (m_session.HasConflicts()) ImGui::TextWrapped("Source changed or was removed on disk. Copy any edits you need before discarding and reloading.");
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());

        if (ImGui::BeginTable("UI document workspace", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableNextColumn();
            RenderVisualAuthoring();
            RenderSourceTabs();
            ImGui::TableNextColumn();
            RenderPreviewPane();
            ImGui::EndTable();
        }
    }

    void RmlDocumentEditorPanel::RenderPendingPrompts()
    {
        if (m_confirmClose) { ImGui::OpenPopup("Close UI document?"); m_confirmClose = false; }
        if (ImGui::BeginPopupModal("Close UI document?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save unsaved UI sources before closing?");
            if (ImGui::Button("Save and close"))
            {
                Save();
                if (!m_session.IsDirty()) { SetOpen(false); ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard and close"))
            { m_session = {}; ClearPreview(); SetOpen(false); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Keep editing")) ImGui::CloseCurrentPopup();
            if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::EndPopup();
        }
        if (m_confirmOpen) { ImGui::OpenPopup("Unsaved UI document"); m_confirmOpen = false; }
        if (ImGui::BeginPopupModal("Unsaved UI document", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Save the current document before opening another?");
            if (ImGui::Button("Save and open"))
            {
                Save();
                if (!m_session.IsDirty()) { OpenPending(); ImGui::CloseCurrentPopup(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard and open")) { OpenPending(); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) { m_pendingReference.clear(); ImGui::CloseCurrentPopup(); }
            if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
            ImGui::EndPopup();
        }
    }

    void RmlDocumentEditorPanel::RenderSourceTabs()
    {
        if (ImGui::BeginTabBar("UI sources"))
        {
            const auto count = m_session.GetBuffers().size();
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto &buffer = m_session.GetBuffers()[index];
                const auto label = buffer.path.filename().string() + (buffer.IsDirty() ? " *" : "") + "###" + buffer.path.generic_string();
                if (ImGui::BeginTabItem(label.c_str()))
                {
                    ImGui::TextWrapped("%s", buffer.path.generic_string().c_str());
                    auto source = buffer.source;
                    ImGui::PushID(static_cast<int>(index));
                    if (EditSourceText("##source", source, {-1, std::max(200.0f, ImGui::GetContentRegionAvail().y - 35)}))
                        m_session.SetSource(index, std::move(source));
                    ImGui::PopID();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }

    void RmlDocumentEditorPanel::RenderPreviewPane()
    {
        ImGui::SetNextItemWidth(160);
        if (ImGui::BeginCombo("Resolution", "Custom / presets"))
        {
            if (ImGui::Selectable("1920 x 1080")) { m_width = 1920; m_height = 1080; }
            if (ImGui::Selectable("1280 x 720")) { m_width = 1280; m_height = 720; }
            if (ImGui::Selectable("800 x 600")) { m_width = 800; m_height = 600; }
            if (ImGui::Selectable("390 x 844")) { m_width = 390; m_height = 844; }
            ImGui::EndCombo();
        }
        ImGui::SetNextItemWidth(120); ImGui::InputInt("Width", &m_width);
        ImGui::SetNextItemWidth(120); ImGui::InputInt("Height", &m_height);
        m_width = std::clamp(m_width, 64, 4096); m_height = std::clamp(m_height, 64, 4096);
        ImGui::SetNextItemWidth(180); ImGui::SliderFloat("Zoom", &m_zoom, 0.1f, 2.0f, "%.2fx");
        ImGui::ColorEdit3("Background", m_background.data());
        ImGui::TextDisabled("Preview is isolated; gameplay controllers do not run.");
        if (m_preview.IsStale() || m_previewRevision != m_session.GetRevision()) ImGui::TextDisabled("Preview is outdated.");
        for (const auto &diagnostic : m_session.GetDiagnostics()) ImGui::TextWrapped("%s", diagnostic.c_str());
        for (const auto &diagnostic : m_preview.GetDiagnostics()) ImGui::TextWrapped("%s", diagnostic.c_str());
        if (ImGui::BeginChild("Preview viewport", {0, 0}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            if (m_texture.IsValid())
            {
                const auto origin = ImGui::GetCursorScreenPos();
                ImGui::Image(static_cast<ImTextureID>(EditorShell::GetInstance().GetPanelManager().GetImGuiTextureId(m_texture)),
                    {m_width * m_zoom, m_height * m_zoom}, {0, 1}, {1, 0});
                if (ImGui::IsItemClicked() && m_previewRevision == m_session.GetRevision())
                {
                    const auto mouse = ImGui::GetIO().MousePos;
                    const auto id = m_preview.PickElement((mouse.x - origin.x) / m_zoom, (mouse.y - origin.y) / m_zoom);
                    try
                    {
                        const auto elements = RmlSourceTools::Parse(m_session.GetBuffers().front().source);
                        for (std::size_t i = 0; i < elements.size(); ++i) if (!id.empty() && elements[i].id == id) { m_selectedElement = i; break; }
                    }
                    catch (const std::exception &error) { m_error = error.what(); }
                }
            }
        }
        ImGui::EndChild();
    }

    void RmlDocumentEditorPanel::Shutdown() { ClearPreview(); }
    void RmlDocumentEditorPanel::OnProjectChanged()
    {
        ClearPreview();
        // Project transitions are guarded by the editor's unsaved-change prompt.
        m_session = {}; m_pendingReference.clear(); m_error.clear();
        m_confirmOpen = m_confirmReload = m_confirmClose = false; m_seenRevision = 0;
    }
}
