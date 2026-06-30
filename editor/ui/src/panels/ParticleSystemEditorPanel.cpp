#include "PlutoGE/ui/panels/ParticleSystemEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/ui/EditorShell.h"

#include <algorithm>
#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        const char *ShapeName(assets::ParticleShape shape)
        {
            switch (shape)
            {
            case assets::ParticleShape::Sphere:
                return "Sphere";
            case assets::ParticleShape::Box:
                return "Box";
            case assets::ParticleShape::Cone:
                return "Cone";
            case assets::ParticleShape::Point:
            default:
                return "Point";
            }
        }

        std::string AssetDisplayName(std::string reference)
        {
            if (reference.rfind(assets::Project::kProjectAssetScheme, 0) == 0)
            {
                reference.erase(0, assets::Project::kProjectAssetScheme.size());
            }
            else if (reference.rfind(assets::Project::kEngineAssetScheme, 0) == 0)
            {
                reference.erase(0, assets::Project::kEngineAssetScheme.size());
            }
            return reference;
        }
    }

    void ParticleSystemEditorPanel::LoadActiveAsset()
    {
        auto &editorShell = EditorShell::GetInstance();
        m_loadedReference = editorShell.GetActiveParticleSystemAssetReference();
        bool loaded = false;
        m_asset = core::Engine::GetInstance().GetAssetManager().LoadParticleSystemAsset(m_loadedReference, &loaded);
        if (!loaded)
        {
            m_asset = assets::CreateDefaultParticleSystemAsset();
        }
        m_dirty = false;
    }

    void ParticleSystemEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveParticleSystemAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No particle system selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveAsset();
        }

        ImGui::TextWrapped("Particle System: %s", reference.c_str());
        ImGui::Separator();

        if (ImGui::Checkbox("Play On Awake", &m_asset.playOnAwake)) m_dirty = true;
        if (ImGui::Checkbox("Looping", &m_asset.looping)) m_dirty = true;
        if (ImGui::DragFloat("Duration", &m_asset.duration, 0.05f, 0.0001f, 100000.0f, "%.3f")) m_dirty = true;
        if (ImGui::DragInt("Max Particles", &m_asset.maxParticles, 1.0f, 1, 200000)) m_dirty = true;

        ImGui::SeparatorText("Start");
        if (ImGui::DragFloat("Lifetime", &m_asset.startLifetime, 0.05f, 0.0001f, 100000.0f, "%.3f")) m_dirty = true;
        if (ImGui::DragFloat("Speed", &m_asset.startSpeed, 0.05f, 0.0f, 100000.0f, "%.3f")) m_dirty = true;
        if (ImGui::DragFloat("Size", &m_asset.startSize, 0.01f, 0.0f, 100000.0f, "%.3f")) m_dirty = true;
        float color[4] = {m_asset.startColor.r, m_asset.startColor.g, m_asset.startColor.b, m_asset.startColor.a};
        if (ImGui::ColorEdit4("Color", color))
        {
            m_asset.startColor = glm::vec4(color[0], color[1], color[2], color[3]);
            m_dirty = true;
        }
        if (ImGui::DragFloat("Gravity Modifier", &m_asset.gravityModifier, 0.01f, -1000.0f, 1000.0f, "%.3f")) m_dirty = true;

        ImGui::SeparatorText("Emission");
        if (ImGui::DragFloat("Rate Over Time", &m_asset.emissionRateOverTime, 0.1f, 0.0f, 100000.0f, "%.3f")) m_dirty = true;
        if (ImGui::DragFloat("Burst Time", &m_asset.burstTime, 0.05f, 0.0f, 100000.0f, "%.3f")) m_dirty = true;
        if (ImGui::DragInt("Burst Count", &m_asset.burstCount, 1.0f, 0, 200000)) m_dirty = true;

        ImGui::SeparatorText("Shape");
        int simulationSpace = static_cast<int>(m_asset.simulationSpace);
        const char *simulationItems[] = {"Local", "World"};
        if (ImGui::Combo("Simulation Space", &simulationSpace, simulationItems, IM_ARRAYSIZE(simulationItems)))
        {
            m_asset.simulationSpace = simulationSpace == 1 ? assets::ParticleSimulationSpace::World : assets::ParticleSimulationSpace::Local;
            m_dirty = true;
        }

        int shape = static_cast<int>(m_asset.shape);
        const char *shapeItems[] = {"Point", "Sphere", "Box", "Cone"};
        if (ImGui::Combo("Shape", &shape, shapeItems, IM_ARRAYSIZE(shapeItems)))
        {
            m_asset.shape = static_cast<assets::ParticleShape>(std::clamp(shape, 0, 3));
            m_dirty = true;
        }
        ImGui::TextDisabled("Current shape: %s", ShapeName(m_asset.shape));
        if (m_asset.shape == assets::ParticleShape::Box)
        {
            if (ImGui::DragFloat3("Shape Size", &m_asset.shapeSize.x, 0.05f, 0.0f, 100000.0f, "%.3f")) m_dirty = true;
        }
        if (m_asset.shape == assets::ParticleShape::Sphere || m_asset.shape == assets::ParticleShape::Cone)
        {
            if (ImGui::DragFloat("Shape Radius", &m_asset.shapeRadius, 0.05f, 0.0f, 100000.0f, "%.3f")) m_dirty = true;
        }
        if (m_asset.shape == assets::ParticleShape::Cone)
        {
            if (ImGui::SliderFloat("Cone Angle", &m_asset.coneAngle, 0.0f, 89.0f, "%.1f")) m_dirty = true;
        }

        ImGui::SeparatorText("Rendering");
        int renderShape = static_cast<int>(m_asset.renderShape);
        const char *renderShapeItems[] = {"Circle", "Quad"};
        if (ImGui::Combo("Particle Shape", &renderShape, renderShapeItems, IM_ARRAYSIZE(renderShapeItems)))
        {
            m_asset.renderShape = renderShape == 1 ? assets::ParticleRenderShape::Quad : assets::ParticleRenderShape::Circle;
            m_dirty = true;
        }

        auto *project = editorShell.GetProject();
        std::string materialPreview = m_asset.materialAssetReference.empty() ? "Default" : AssetDisplayName(m_asset.materialAssetReference);
        if (ImGui::BeginCombo("Material Asset", materialPreview.c_str()))
        {
            if (ImGui::Selectable("Default", m_asset.materialAssetReference.empty()))
            {
                m_asset.materialAssetReference.clear();
                m_dirty = true;
            }
            if (project)
            {
                for (const auto &entry : project->GetManifest().assetEntries)
                {
                    if (entry.type != assets::ProjectAssetType::Material)
                    {
                        continue;
                    }
                    const bool selected = entry.reference == m_asset.materialAssetReference;
                    const std::string displayName = AssetDisplayName(entry.reference);
                    if (ImGui::Selectable(displayName.c_str(), selected))
                    {
                        m_asset.materialAssetReference = entry.reference;
                        m_dirty = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Save"))
        {
            m_asset.duration = std::max(m_asset.duration, 0.0001f);
            m_asset.maxParticles = std::clamp(m_asset.maxParticles, 1, 200000);
            m_asset.startLifetime = std::max(m_asset.startLifetime, 0.0001f);
            m_asset.startSpeed = std::max(m_asset.startSpeed, 0.0f);
            m_asset.startSize = std::max(m_asset.startSize, 0.0f);
            m_asset.startColor = glm::clamp(m_asset.startColor, glm::vec4(0.0f), glm::vec4(1.0f));
            m_asset.emissionRateOverTime = std::max(m_asset.emissionRateOverTime, 0.0f);
            m_asset.burstTime = std::max(m_asset.burstTime, 0.0f);
            m_asset.burstCount = std::max(m_asset.burstCount, 0);
            m_asset.shapeSize = glm::max(m_asset.shapeSize, glm::vec3(0.0f));
            m_asset.shapeRadius = std::max(m_asset.shapeRadius, 0.0f);
            m_asset.coneAngle = std::clamp(m_asset.coneAngle, 0.0f, 89.0f);

            std::string errorMessage;
            if (core::Engine::GetInstance().GetAssetManager().SaveParticleSystemAsset(reference, m_asset, &errorMessage))
            {
                if (auto *scene = editorShell.GetScene())
                {
                    for (auto *component : scene->GetParticleSystemComponents())
                    {
                        if (component && component->GetParticleSystemAssetReference() == reference)
                        {
                            component->ApplyParticleSystemAsset(m_asset);
                        }
                    }
                }
                m_dirty = false;
                editorShell.MarkProjectDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved particle system: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save particle system." : errorMessage);
            }
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            LoadActiveAsset();
        }
        ImGui::EndDisabled();
    }
}
