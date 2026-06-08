#include "PlutoGE/ui/panels/MaterialEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>

#include <imgui.h>

namespace PlutoGE::ui
{
    void MaterialEditorPanel::LoadActiveMaterial()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveMaterialAssetReference();
        m_loadedReference = reference;
        m_dirty = false;

        auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(reference);
        if (!material)
        {
            m_color = glm::vec4(1.0f);
            m_metallic = 0.0f;
            m_roughness = 0.55f;
            m_flipNormalY = false;
            return;
        }

        const auto &config = material->GetConfig();
        m_color = config.color;
        m_metallic = config.metallic;
        m_roughness = config.roughness;
        m_flipNormalY = config.flipNormalY;
    }

    void MaterialEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveMaterialAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No material selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveMaterial();
        }

        const bool engineMaterial = assets::Project::IsEngineAssetReference(reference);
        ImGui::TextWrapped("Material: %s", reference.c_str());
        if (engineMaterial)
        {
            ImGui::TextDisabled("Engine material assets are read-only.");
        }

        ImGui::Separator();
        if (engineMaterial)
        {
            ImGui::BeginDisabled();
        }

        float color[4] = {m_color.r, m_color.g, m_color.b, m_color.a};
        if (ImGui::ColorEdit4("Color", color))
        {
            m_color = glm::vec4(color[0], color[1], color[2], color[3]);
            m_dirty = true;
        }

        if (ImGui::SliderFloat("Metallic", &m_metallic, 0.0f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }

        if (ImGui::SliderFloat("Roughness", &m_roughness, 0.04f, 1.0f, "%.2f"))
        {
            m_dirty = true;
        }

        if (ImGui::Checkbox("Flip Normal Y", &m_flipNormalY))
        {
            m_dirty = true;
        }

        if (engineMaterial)
        {
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        ImGui::BeginDisabled(engineMaterial || !m_dirty);
        if (ImGui::Button("Save"))
        {
            render::MaterialConfig config;
            config.color = m_color;
            config.metallic = std::clamp(m_metallic, 0.0f, 1.0f);
            config.roughness = std::clamp(m_roughness, 0.04f, 1.0f);
            config.flipNormalY = m_flipNormalY;

            std::string errorMessage;
            if (core::Engine::GetInstance().GetAssetManager().SaveMaterialAsset(reference, config, &errorMessage))
            {
                m_dirty = false;
                editorShell.MarkSceneDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved material: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save material." : errorMessage);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            LoadActiveMaterial();
        }
        ImGui::EndDisabled();
    }
}
