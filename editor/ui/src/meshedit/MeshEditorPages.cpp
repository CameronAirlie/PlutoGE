#include "PlutoGE/ui/meshedit/MeshEditorViews.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/meshedit/MeshAssetSession.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdint>
#include <string>

namespace PlutoGE::ui::meshedit
{
    namespace
    {
        void MutedText(const char *text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::TextMuted));
            ImGui::TextWrapped("%s", text);
            ImGui::PopStyleColor();
        }

        void WarningText(const std::string &text)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
            ImGui::TextWrapped("%s  %s", icons::Warning, text.c_str());
            ImGui::PopStyleColor();
        }

        std::string WithSeparators(std::uint64_t value)
        {
            std::string digits = std::to_string(value);
            for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
                digits.insert(static_cast<std::size_t>(at), ",");
            return digits;
        }

        std::uint64_t CountTriangles(const render::MeshConfig &mesh, std::size_t lod)
        {
            std::uint64_t indices = 0;
            for (const auto &submesh : mesh.submeshes)
            {
                if (lod < submesh.lods.size())
                    indices += submesh.lods[lod].indexCount;
                else if (lod == 0)
                    indices += submesh.indexCount;
            }
            return indices / 3;
        }

        void ValueRow(const char *label, const std::string &value, const char *description = nullptr)
        {
            widgets::SettingsRow(label, description);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(value.c_str());
        }

        void ReadOnlyBanner(const MeshAssetSession &session)
        {
            if (!session.IsReadOnly())
                return;
            widgets::BeginCard("##ReadOnly");
            ImGui::TextColored(GetEditorColor(EditorColor::Warning), "%s", icons::Lock);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", session.GetReadOnlyReason().c_str());
            widgets::EndCard();
            ImGui::Spacing();
        }
    }

    void DrawOverviewPage(const MeshAssetSession &session, const HumanoidRigAnalysisCache &rig, MeshEditorPage &page)
    {
        const auto &mesh = session.GetMesh();
        widgets::PageTitle(session.GetLabel().c_str(), session.GetReference().empty() ? nullptr : session.GetReference().c_str());
        ReadOnlyBanner(session);

        widgets::SectionHeading("Geometry");
        widgets::BeginCard("##GeometryCard");
        if (widgets::BeginSettingsTable("##GeometryTable"))
        {
            ValueRow("Vertices", WithSeparators(mesh.data.vertices.size()));
            ValueRow("Triangles", WithSeparators(CountTriangles(mesh, 0)), "At full detail (LOD 0).");
            ValueRow("Submeshes", std::to_string(mesh.submeshes.size()), "One draw each; every submesh uses one material slot.");
            ValueRow("Detail levels", std::to_string(session.GetLodCount()));
            ValueRow("Lightmap UVs", mesh.hasLightmapUvs ? "Yes" : "No");
            widgets::EndSettingsTable();
        }
        widgets::EndCard();

        // Bounds from the vertices, so they match what the editor shows.
        glm::vec3 minimum(FLT_MAX), maximum(-FLT_MAX);
        for (const auto &vertex : mesh.data.vertices)
        {
            const glm::vec3 position(vertex.position[0], vertex.position[1], vertex.position[2]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        if (!mesh.data.vertices.empty())
        {
            const glm::vec3 size = maximum - minimum;
            const glm::vec3 centre = (minimum + maximum) * 0.5f;
            char text[96];
            widgets::SectionHeading("Bounds");
            widgets::BeginCard("##BoundsCard");
            if (widgets::BeginSettingsTable("##BoundsTable"))
            {
                std::snprintf(text, sizeof(text), "%.3f  x  %.3f  x  %.3f", size.x, size.y, size.z);
                ValueRow("Size", text, "Width, height and depth in mesh units.");
                std::snprintf(text, sizeof(text), "%.3f,  %.3f,  %.3f", centre.x, centre.y, centre.z);
                ValueRow("Centre", text);
                widgets::EndSettingsTable();
            }
            widgets::EndCard();
        }

        widgets::SectionHeading("Skeleton");
        widgets::BeginCard("##SkeletonCard");
        if (widgets::BeginSettingsTable("##SkeletonTable"))
        {
            const auto &skeleton = session.GetSkeleton();
            ValueRow("Joints", skeleton.joints.empty() ? "None" : std::to_string(skeleton.joints.size()));
            ValueRow("Embedded clips", std::to_string(mesh.animations.size()));
            if (!skeleton.joints.empty())
            {
                widgets::SettingsRow("Humanoid map", "Lets clips made for other characters play on this mesh.");
                const auto &status = rig.status;
                if (skeleton.humanoidBoneMappings.empty())
                    widgets::Pill("Not set up", GetEditorColor(EditorColor::TextMuted));
                else if (status.IsReady())
                    widgets::Pill("Ready", GetEditorColor(EditorColor::Success));
                else
                    widgets::Pill("Needs attention", GetEditorColor(EditorColor::Warning));
                ImGui::SameLine();
                if (ImGui::SmallButton("Open"))
                    page = MeshEditorPage::HumanoidRig;
            }
            widgets::EndSettingsTable();
        }
        widgets::EndCard();
    }

    void DrawMaterialsPage(MeshAssetSession &session, const assets::Project *project)
    {
        const auto &mesh = session.GetMesh();
        const auto &materials = session.GetEdits().materialReferences;
        widgets::PageTitle("Materials", "The material each part of the mesh draws with. Drop a material from the Content Browser onto a slot.");
        ReadOnlyBanner(session);
        if (materials.empty())
        {
            MutedText("This mesh has no material slots.");
            return;
        }

        widgets::BeginCard("##MaterialsCard");
        ImGui::BeginDisabled(session.IsReadOnly());
        if (widgets::BeginSettingsTable("##MaterialsTable"))
        {
            for (std::size_t slot = 0; slot < materials.size(); ++slot)
            {
                ImGui::PushID(static_cast<int>(slot));
                std::size_t parts = 0;
                std::uint64_t indices = 0;
                for (const auto &submesh : mesh.submeshes)
                    if (submesh.materialIndex == slot)
                    {
                        ++parts;
                        indices += submesh.indexCount;
                    }
                const std::string label = "Slot " + std::to_string(slot);
                const std::string description = parts == 0 ? "Not used by any submesh."
                                                           : std::to_string(parts) + (parts == 1 ? " submesh, " : " submeshes, ") +
                                                                 WithSeparators(indices / 3) + " triangles";
                widgets::SettingsRow(label.c_str(), description.c_str());
                const bool hasMaterial = !materials[slot].empty();
                ImGui::SetNextItemWidth(hasMaterial ? -(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x) : -FLT_MIN);
                std::string reference = materials[slot];
                if (RenderProjectAssetPicker("##Material", project, assets::ProjectAssetType::Material, reference,
                                             {.noneLabel = "None", .includeEngineAssets = true}))
                    session.Modify([&](MeshAssetEdits &edits) { edits.materialReferences[slot] = reference; });
                if (hasMaterial)
                {
                    ImGui::SameLine();
                    ImGui::EndDisabled(); // opening works on read-only meshes too
                    if (widgets::IconButton("##OpenMaterial", icons::OpenExternal, "Open in the Material Editor"))
                        EditorShell::GetInstance().OpenMaterialAsset(materials[slot]);
                    ImGui::BeginDisabled(session.IsReadOnly());
                }
                ImGui::PopID();
            }
            widgets::EndSettingsTable();
        }
        ImGui::EndDisabled();
        widgets::EndCard();
    }

    void DrawLevelOfDetailPage(MeshAssetSession &session, std::string &error)
    {
        const auto &mesh = session.GetMesh();
        const auto &edits = session.GetEdits();
        widgets::PageTitle("Level of Detail", "Simpler versions of the mesh drawn when it is small on screen, and how they are imported.");
        ReadOnlyBanner(session);

        const std::uint64_t fullDetail = CountTriangles(mesh, 0);
        if (mesh.submeshes.size() > 500)
            WarningText("Many draws: " + std::to_string(mesh.submeshes.size()) + " submeshes (500 or fewer recommended).");
        if (fullDetail > 1'000'000)
            WarningText("Dense mesh: " + WithSeparators(fullDetail) + " triangles at full detail.");
        if (session.GetLodCount() <= 1 && fullDetail > 100'000)
            WarningText("This large mesh has no simpler levels. Turn on Generate LODs below and reimport.");

        widgets::SectionHeading("Levels");
        widgets::BeginCard("##LevelsCard");
        ImGui::BeginDisabled(session.IsReadOnly());
        if (widgets::BeginSettingsTable("##LevelsTable"))
        {
            for (std::size_t lod = 0; lod < session.GetLodCount(); ++lod)
            {
                ImGui::PushID(static_cast<int>(lod));
                const std::uint64_t triangles = CountTriangles(mesh, lod);
                const std::string label = "LOD " + std::to_string(lod);
                std::string description = WithSeparators(triangles) + " triangles";
                if (lod > 0 && fullDetail > 0)
                    description += " (" + std::to_string(static_cast<int>(100.0 * static_cast<double>(triangles) / static_cast<double>(fullDetail) + 0.5)) + "%)";
                widgets::SettingsRow(label.c_str(), description.c_str());
                if (lod == 0)
                {
                    ImGui::AlignTextToFramePadding();
                    MutedText("Full detail, used up close.");
                }
                else
                {
                    float radius = edits.lodMaxScreenRadii[lod];
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::DragFloat("##MaxScreenRadius", &radius, 1.0f, 1.0f, 2000.0f, "Up to %.0f px"))
                        session.Modify([&](MeshAssetEdits &changed) { changed.lodMaxScreenRadii[lod] = (std::max)(1.0f, radius); });
                    ImGui::SetItemTooltip("Draw this level while the mesh's on-screen radius is at most this many pixels.");
                }
                ImGui::PopID();
            }
            widgets::EndSettingsTable();
        }
        ImGui::EndDisabled();
        widgets::EndCard();

        widgets::SectionHeading("Import", "Applied when the source model is imported again.");
        widgets::BeginCard("##ImportCard");
        ImGui::BeginDisabled(session.IsReadOnly());
        if (widgets::BeginSettingsTable("##ImportTable"))
        {
            const auto toggle = [&](const char *id, const char *label, const char *description, bool assetimport::MeshImportOptions::*field)
            {
                widgets::SettingsRow(label, description);
                bool value = edits.importOptions.*field;
                if (widgets::ToggleSwitch(id, &value))
                    session.Modify([&](MeshAssetEdits &changed) { changed.importOptions.*field = value; });
            };
            toggle("##GenerateLods", "Generate LODs", "Build simpler levels from the full-detail mesh.", &assetimport::MeshImportOptions::generateLods);
            toggle("##VertexCache", "Optimise vertex cache", "Reorder triangles so the GPU reuses more transformed vertices.",
                   &assetimport::MeshImportOptions::optimizeVertexCache);
            toggle("##Overdraw", "Optimise overdraw", "Reorder triangles so fewer hidden pixels are shaded.", &assetimport::MeshImportOptions::optimizeOverdraw);
            widgets::SettingsRow("Source model", session.CanReimport() ? nullptr : "Reimporting needs the model this mesh came from.");
            ImGui::AlignTextToFramePadding();
            const std::string source = session.GetSourcePath();
            MutedText(source.empty() ? "Not found" : source.c_str());
            widgets::EndSettingsTable();
        }
        ImGui::EndDisabled();
        ImGui::Spacing();
        ImGui::BeginDisabled(!session.CanReimport());
        if (widgets::PrimaryButton("Reimport With These Settings"))
        {
            error.clear();
            if (session.Reimport(error))
                EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Info, "Reimported mesh with its import settings: " + session.GetReference());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("Keeps the humanoid map and materials. Save to keep the result.");
        widgets::EndCard();
    }
}
