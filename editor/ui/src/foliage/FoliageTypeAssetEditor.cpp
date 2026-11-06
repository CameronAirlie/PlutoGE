#include "PlutoGE/ui/FoliageTypeAssetEditor.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/SceneWind.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/FoliageTypeSections.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    void FoliageTypeAssetEditor::Load(const std::string &reference)
    {
        m_reference = reference;
        m_draft = {};
        m_error.clear();
        const auto path = core::Engine::GetInstance().GetAssetManager().ResolveAssetPath(reference);
        scene::LoadFoliageTypeAsset(path, m_draft, &m_error);
        if (!m_error.empty()) m_error += " Saving will replace it with the values below.";
        m_saved = m_draft;
    }

    void FoliageTypeAssetEditor::Render(const std::string &reference)
    {
        if (m_reference != reference) Load(reference);

        auto &shell = EditorShell::GetInstance();
        auto &assets = core::Engine::GetInstance().GetAssetManager();

        ImGui::SeparatorText("Foliage Type");
        FoliageTypeSectionContext context;
        context.project = shell.GetProject();
        if (m_meshReference != m_draft.sourceMeshPath)
        {
            m_meshReference = m_draft.sourceMeshPath;
            m_mesh = m_meshReference.empty() ? nullptr : assets.LoadMeshAsset(m_meshReference);
        }
        context.mesh = m_mesh;
        context.sceneHasWind = !render::BuildSceneWind(shell.GetScene()).Calm();
        DrawFoliageTypeSections(m_draft, context);
        scene::SanitizeFoliageTypeAsset(m_draft);

        ImGui::Spacing();
        const bool changed = m_draft != m_saved;
        ImGui::BeginDisabled(!changed);
        if (ImGui::Button("Save"))
        {
            if (scene::SaveFoliageTypeAsset(assets.ResolveAssetPath(reference), m_draft, &m_error))
            {
                m_saved = m_draft;
                m_error.clear();
                std::size_t updated = 0;
                if (auto *scene = shell.GetScene()) updated = scene->RefreshFoliageTypeAsset(reference);
                shell.MarkProjectDirty();
                if (updated > 0)
                {
                    shell.MarkSceneDirty();
                    shell.Log(EditorShell::ConsoleSeverity::Info,
                              "Saved " + reference + " and updated " + std::to_string(updated) + " foliage type(s) in the scene.");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert"))
        {
            m_draft = m_saved;
            m_error.clear();
        }
        ImGui::EndDisabled();
        if (changed)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Unsaved changes");
        }
        if (!m_error.empty()) ImGui::TextWrapped("%s", m_error.c_str());
    }
}
