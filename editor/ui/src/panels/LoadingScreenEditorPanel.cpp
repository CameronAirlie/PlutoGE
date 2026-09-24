#include "PlutoGE/ui/panels/LoadingScreenEditorPanel.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/core/LoadingScreenSession.h"
#include "PlutoGE/assets/LoadingScreenAsset.h"
#include "PlutoGE/render/Graphics.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::ui
{
    namespace
    {
        template <std::size_t N> void Copy(std::array<char, N> &buffer, const std::string &text)
        { buffer.fill(0); std::copy_n(text.data(), std::min(text.size(), N - 1), buffer.data()); }

        void ReadSource(const std::filesystem::path &path, std::vector<char> &buffer)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot read " + path.string());
            const std::string text((std::istreambuf_iterator<char>(file)), {});
            if (text.size() >= buffer.size()) throw std::runtime_error("Source exceeds the 256 KiB editor limit");
            std::fill(buffer.begin(), buffer.end(), 0);
            std::copy(text.begin(), text.end(), buffer.begin());
        }
        void WriteSource(const std::filesystem::path &path, const char *text)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            file << text;
            file.flush();
            if (!file) throw std::runtime_error("Cannot write " + path.string());
        }
    }

    LoadingScreenEditorPanel::LoadingScreenEditorPanel(const PanelConfig &config)
        : Panel(config), m_rml(256 * 1024), m_rcss(256 * 1024) { Copy(m_name, "Loading"); }
    LoadingScreenEditorPanel::~LoadingScreenEditorPanel() = default;
    void LoadingScreenEditorPanel::ClearPreview()
    {
        if (m_texture.IsValid()) EditorShell::GetInstance().GetPanelManager().UnregisterTexture(m_texture);
        m_texture = {};
        m_preview.reset();
    }
    void LoadingScreenEditorPanel::Shutdown() { ClearPreview(); }
    void LoadingScreenEditorPanel::OnProjectChanged()
    { ClearPreview(); m_reference.clear(); m_dirty = false; m_previewEnabled = false; m_sourcesValid = false; }

    void LoadingScreenEditorPanel::LoadSources()
    {
        m_sourcesValid = false;
        try
        {
            auto *project = EditorShell::GetInstance().GetProject();
            if (!project) return;
            assets::LoadingScreenAsset validation{m_document.data(), m_controller.data()};
            std::ostringstream stream;
            if (!assets::WriteLoadingScreenAsset(stream, validation)) throw std::runtime_error("Select a project RML document");
            auto path = project->ResolveAssetReference(m_document.data());
            ReadSource(path, m_rml);
            // The template uses a same-name stylesheet. Other linked stylesheets
            // can still be authored externally, with Reload applying changes.
            ReadSource(path.replace_extension(".rcss"), m_rcss);
            m_sourcesValid = true;
            m_error.clear();
        }
        catch (const std::exception &e) { m_error = e.what(); }
    }
    void LoadingScreenEditorPanel::Load()
    {
        ClearPreview();
        m_reference = EditorShell::GetInstance().GetActiveLoadingScreenAssetReference();
        m_dirty = false;
        m_error.clear();
        m_sourcesValid = false;
        Copy(m_document, ""); Copy(m_controller, "");
        if (m_reference.empty()) return;
        auto *project = EditorShell::GetInstance().GetProject();
        if (!project) return;
        std::ifstream file(project->ResolveAssetReference(m_reference));
        assets::LoadingScreenAsset asset;
        if (!assets::ReadLoadingScreenAsset(file, asset)) { m_error = "Invalid loading-screen asset"; return; }
        Copy(m_document, asset.document); Copy(m_controller, asset.controller);
        LoadSources();
        m_reloadPreview = true;
    }
    void LoadingScreenEditorPanel::Save()
    {
        try
        {
            auto *project = EditorShell::GetInstance().GetProject();
            if (!project || m_reference.empty()) return;
            std::ostringstream serialized;
            if (!assets::WriteLoadingScreenAsset(serialized, {m_document.data(), m_controller.data()}))
                throw std::runtime_error("Invalid document reference or controller class name");
            if (m_sourcesValid)
            {
                auto path = project->ResolveAssetReference(m_document.data());
                WriteSource(path, m_rml.data());
                WriteSource(path.replace_extension(".rcss"), m_rcss.data());
            }
            WriteSource(project->ResolveAssetReference(m_reference), serialized.str().c_str());
            m_dirty = false; m_reloadPreview = true; m_error.clear();
            project->RefreshAssetRegistry();
        }
        catch (const std::exception &e) { m_error = e.what(); }
    }
    void LoadingScreenEditorPanel::Create()
    {
        try
        {
            auto &shell = EditorShell::GetInstance();
            auto *project = shell.GetProject();
            if (!project) return;
            const std::string name(m_name.data());
            if (name.empty() || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos)
                throw std::runtime_error("Use letters, numbers, underscores or hyphens for the name");
            const auto directory = project->ResolveAssetReference("project://UI/Loading");
            std::filesystem::create_directories(directory);
            const auto assetPath = directory / (name + ".plutoloading");
            const auto rmlPath = directory / (name + ".rml");
            const auto cssPath = directory / (name + ".rcss");
            for (const auto &path : {assetPath, rmlPath, cssPath})
                if (std::filesystem::exists(path)) throw std::runtime_error("An asset with that name already exists");
            const auto font = PanelManager::GetEditorFontPath("Georama-Regular.ttf");
            if (font.empty()) throw std::runtime_error("Editor font is unavailable; cannot create a portable template");
            const auto fontPath = directory / "Georama-Regular.ttf";
            if (!std::filesystem::exists(fontPath)) std::filesystem::copy_file(font, fontPath);
            const std::string rml = "<rml>\n<head><title>Loading</title><link type=\"text/rcss\" href=\"" + name +
                ".rcss\"/></head>\n<body><div id=\"content\"><div id=\"spinner\"/><h1>Loading</h1>"
                "<p id=\"loading-stage\">Preparing</p><p id=\"loading-error\"/></div></body>\n</rml>\n";
            WriteSource(rmlPath, rml.c_str());
            WriteSource(cssPath, R"(@font-face { font-family: LoadingFont; src: url(Georama-Regular.ttf); }
body { width: 100%; height: 100%; margin: 0; background-color: #10151e; color: #dce1eb; font-family: LoadingFont; font-size: 20px; }
#content { position: absolute; top: 30%; width: 100%; text-align: center; }
h1 { font-size: 36px; margin: 18px 0; }
#spinner { width: 32px; height: 32px; margin: 0 auto; border: 4px #384251; border-top-color: #dce1eb; border-radius: 20px; animation: 1.2s linear infinite spin; }
#loading-error { color: #ed9f94; }
@keyframes spin { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
)");
            std::ostringstream asset;
            if (!assets::WriteLoadingScreenAsset(asset, {project->MakeAssetReference(rmlPath), {}}))
                throw std::runtime_error("Could not serialize loading asset");
            WriteSource(assetPath, asset.str().c_str());
            project->RefreshAssetRegistry(); shell.MarkProjectDirty();
            shell.OpenLoadingScreenAsset(project->MakeAssetReference(assetPath));
            m_previewEnabled = true;
            Load();
        }
        catch (const std::exception &e) { m_error = e.what(); }
    }

    void LoadingScreenEditorPanel::PreparePreview()
    {
        auto &shell = EditorShell::GetInstance();
        auto &engine = shell.GetEngine();
        if (!IsOpen() || !m_previewEnabled || engine.IsRuntimeRunning() || m_reference.empty())
        { ClearPreview(); return; }
        if (m_reloadPreview) { ClearPreview(); m_reloadPreview = false; }
        try
        {
            if (!m_preview)
            {
                render::LoadingScreenStyle style;
                style.assetReference = m_reference;
                m_preview = std::make_unique<core::LoadingScreenSession>(engine, style, false);
            }
            core::SceneLoadStatus status;
            status.stage = static_cast<core::SceneLoadStage>(m_stage);
            status.path = "Preview";
            if (m_stage == 5) status.error = "Example loading error";
            if (m_preview->Render(status, 960, 540))
            {
                const EditorTextureDescriptor descriptor{.device = engine.GetRenderDevice(),
                    .texture = m_preview->GetTexture(), .width = 960, .height = 540};
                if (!m_texture.IsValid()) m_texture = shell.GetPanelManager().RegisterTexture(descriptor);
                else shell.GetPanelManager().UpdateTexture(m_texture, descriptor);
            }
            if (!m_preview->GetError().empty()) m_error = m_preview->GetError();
        }
        catch (const std::exception &e) { m_error = e.what(); ClearPreview(); m_previewEnabled = false; }
        if (engine.GetConfig().graphicsApi == render::rhi::GraphicsApi::OpenGL)
        { render::Graphics::ResetStateCache(); render::Graphics::Disable(GL_SCISSOR_TEST); }
    }

    void LoadingScreenEditorPanel::Render()
    {
        auto &shell = EditorShell::GetInstance();
        auto *project = shell.GetProject();
        if (!project) { ImGui::TextDisabled("Open a project to author loading screens."); return; }
        if (m_reference != shell.GetActiveLoadingScreenAssetReference()) Load();
        ImGui::InputText("New asset name", m_name.data(), m_name.size());
        ImGui::SameLine(); if (ImGui::Button("Create screen")) Create();
        if (!m_reference.empty())
        {
            ImGui::SeparatorText(m_reference.c_str());
            if (ImGui::InputText("RML document", m_document.data(), m_document.size()))
            { m_dirty = true; m_sourcesValid = false; }
            ImGui::SameLine(); if (ImGui::Button("Load source")) LoadSources();
            m_dirty |= ImGui::InputText("Controller class (optional)", m_controller.data(), m_controller.size());
            if (ImGui::BeginCombo("Available controllers", "Select a compiled controller"))
            {
                if (ImGui::Selectable("None")) { Copy(m_controller, ""); m_dirty = true; }
                for (const auto &name : shell.GetEngine().GetScriptEngine().GetLoadingScreenClassNames())
                    if (ImGui::Selectable(name.c_str())) { Copy(m_controller, name); m_dirty = true; }
                ImGui::EndCombo();
            }
            if (ImGui::Button(m_dirty ? "Save and reload *" : "Save and reload")) Save();
            ImGui::SameLine(); if (ImGui::Button("Reload from disk")) Load();
            ImGui::SameLine();
            if (ImGui::Button("Use as project loading screen"))
            { project->GetManifest().loadingScreen.assetReference = m_reference; shell.MarkProjectDirty(); }
            ImGui::SameLine();
            if (ImGui::Button("Use engine default"))
            { project->GetManifest().loadingScreen = {}; shell.MarkProjectDirty(); }
            ImGui::Checkbox("Animated preview", &m_previewEnabled);
            ImGui::SameLine(); ImGui::Combo("Stage", &m_stage, "Preparing\0Reading\0Building\0Starting\0Ready\0Failed\0");
            ImGui::TextDisabled("Preview uses saved files. Controllers run during loading; preview runs RCSS animations only.");
            if (m_texture.IsValid())
            {
                const float width = std::min(960.0f, ImGui::GetContentRegionAvail().x);
                ImGui::Image(static_cast<ImTextureID>(shell.GetPanelManager().GetImGuiTextureId(m_texture)),
                    {width, width * 9.0f / 16.0f}, {0, 1}, {1, 0});
            }
            if (m_sourcesValid && ImGui::BeginTabBar("Loading source"))
            {
                if (ImGui::BeginTabItem("RML"))
                { m_dirty |= ImGui::InputTextMultiline("##rml", m_rml.data(), m_rml.size(), {-1, 300}, ImGuiInputTextFlags_AllowTabInput); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("RCSS (same-name stylesheet)"))
                { m_dirty |= ImGui::InputTextMultiline("##rcss", m_rcss.data(), m_rcss.size(), {-1, 300}, ImGuiInputTextFlags_AllowTabInput); ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
    }
}
