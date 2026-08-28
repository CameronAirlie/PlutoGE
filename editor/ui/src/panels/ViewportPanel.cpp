#include "PlutoGE/ui/panels/ViewportPanel.h"

// Editor selection access is validated by EditorShell before panel use.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/SpatialUpscaler.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/TexturePainter.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/UISystem.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/AudioEnvironmentVolumeComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/NavigationSystem.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanBootstrap.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include <ImGuizmo.h>
#include <iostream>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <limits>
#include <optional>
#include <memory>
#include <filesystem>
#include <fstream>
#include <unordered_set>

#include <imgui.h>
#include <glad/glad.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace PlutoGE::ui
{
    ViewportPanel::ViewportPanel(const ViewportPanelConfig &config)
        : Panel(config), m_config(config)
    {
    }

    namespace
    {
        constexpr int kDefaultViewportWidth = 1280;
        constexpr int kDefaultViewportHeight = 720;
        constexpr int kResizeDebounceFrames = 2;
        constexpr float kMinRenderScale = 0.5f;
        constexpr float kMaxRenderScale = 2.0f;
        constexpr float kRayEpsilon = 0.0001f;
        constexpr float kHomogeneousWEpsilon = 0.00000001f;
        constexpr float kTriangleDeterminantEpsilon = 0.00000001f;
        constexpr float kTriangleDistanceEpsilon = 0.000001f;
        constexpr const char *kDebugViewLabels[] = {
            "Post Process",
            "Quadrants",
            "Position",
            "Normal",
            "Albedo",
            "Depth",
            "Shadow Cascades",
            "Shadow Mask Raw",
            "Shadow Mask Filtered",
            "LOD",
        };

        std::string ReadRhiShaderText(const char *fileName)
        {
            std::ifstream input(std::filesystem::path(PLUTO_RHI_SHADER_DIR) / fileName, std::ios::binary);
            if (!input)
                return {};
            return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        }

        std::vector<std::uint32_t> ReadRhiShaderBinary(const char *fileName)
        {
            std::ifstream input(std::filesystem::path(PLUTO_RHI_SHADER_DIR) / fileName, std::ios::binary | std::ios::ate);
            if (!input)
                return {};
            const auto byteSize = input.tellg();
            if (byteSize <= 0 || byteSize % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0)
                return {};
            std::vector<std::uint32_t> words(static_cast<std::size_t>(byteSize) / sizeof(std::uint32_t));
            input.seekg(0);
            input.read(reinterpret_cast<char *>(words.data()), byteSize);
            return input ? words : std::vector<std::uint32_t>{};
        }

        struct PickRay
        {
            glm::vec3 origin{0.0f};
            glm::vec3 direction{0.0f, 0.0f, -1.0f};
        };

        struct ProjectedPoint
        {
            ImVec2 screen;
            bool visible = false;
        };

        struct PickDebugInfo
        {
            bool sceneMissing = false;
            bool rayBuilt = false;
            std::size_t totalEntities = 0;
            std::size_t inactiveEntities = 0;
            std::size_t pickableMeshEntities = 0;
            std::size_t pickableTerrainEntities = 0;
            std::size_t emptyMeshEntities = 0;
            std::size_t submeshesTested = 0;
            std::size_t submeshesRejectedByBounds = 0;
            std::size_t boundsHits = 0;
            std::size_t terrainHits = 0;
            std::size_t triangleTests = 0;
            std::size_t triangleHits = 0;
            std::size_t approximateHits = 0;
            scene::Entity *selectedEntity = nullptr;
            float selectedDistance = std::numeric_limits<float>::max();
            std::string selectedSource;
            std::string rayFailureReason;
        };

        struct TexturePaintState
        {
            bool enabled = false;
            float radiusPixels = 32.0f;
            float opacity = 0.65f;
            glm::vec4 color{1.0f};
            render::Texture *texture = nullptr;
            render::Texture *brushTexture = nullptr;
            std::string brushTextureReference;
            float brushTextureScale = 8.0f;
            std::unique_ptr<render::TexturePainter> painter;
            bool strokeActive = false;
        };

        TexturePaintState g_texturePaint;

        const char *GetTerrainPaintModeLabel(scene::TerrainPaintMode mode)
        {
            switch (mode)
            {
            case scene::TerrainPaintMode::Lower:
                return "Lower";
            case scene::TerrainPaintMode::Smooth:
                return "Smooth";
            case scene::TerrainPaintMode::Flatten:
                return "Flatten";
            case scene::TerrainPaintMode::Raise:
            default:
                return "Raise";
            }
        }

        void RenderTerrainPaintToolbar(scene::TerrainComponent &terrainComponent)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Terrain Paint");
            ImGui::SameLine();

            const char *currentMode = GetTerrainPaintModeLabel(terrainComponent.GetPaintMode());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::BeginCombo("Mode##TerrainPaint", currentMode))
            {
                constexpr scene::TerrainPaintMode modes[] = {
                    scene::TerrainPaintMode::Raise,
                    scene::TerrainPaintMode::Lower,
                    scene::TerrainPaintMode::Smooth,
                    scene::TerrainPaintMode::Flatten,
                };
                for (auto mode : modes)
                {
                    const bool selected = terrainComponent.GetPaintMode() == mode;
                    if (ImGui::Selectable(GetTerrainPaintModeLabel(mode), selected))
                    {
                        terrainComponent.SetPaintMode(mode);
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SameLine();
            float radius = terrainComponent.GetBrushRadius();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Radius##TerrainPaint", &radius, 0.05f, 0.05f, 512.0f, "%.2f"))
            {
                terrainComponent.SetBrushRadius(radius);
            }

            ImGui::SameLine();
            float strength = terrainComponent.GetBrushStrength();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Strength##TerrainPaint", &strength, 0.05f, 0.0f, 128.0f, "%.2f"))
            {
                terrainComponent.SetBrushStrength(strength);
            }

            if (terrainComponent.GetPaintMode() == scene::TerrainPaintMode::Flatten)
            {
                ImGui::SameLine();
                float flattenHeight = terrainComponent.GetFlattenHeight();
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::DragFloat("Height##TerrainPaint", &flattenHeight, 0.05f, 0.0f, terrainComponent.GetHeightScale(), "%.2f"))
                {
                    terrainComponent.SetFlattenHeight(flattenHeight);
                }
            }
        }

        void RenderFoliagePaintToolbar(scene::FoliageComponent &foliageComponent)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Foliage Paint");
            ImGui::SameLine();

            bool enabled = foliageComponent.IsPaintEnabled();
            if (ImGui::Checkbox("Enable##FoliagePaint", &enabled))
            {
                foliageComponent.SetPaintEnabled(enabled);
            }

            ImGui::SameLine();
            scene::FoliageBrushMode brushMode = foliageComponent.GetBrushMode();
            ImGui::SetNextItemWidth(80.0f);
            if (ImGui::Combo("Brush Mode##FoliagePaint", reinterpret_cast<int *>(&brushMode), "Add\0Remove\0"))
            {
                foliageComponent.SetBrushMode(brushMode);
            }

            ImGui::SameLine();
            float radius = foliageComponent.GetBrushRadius();
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Radius##FoliagePaint", &radius, 0.05f, 0.05f, 512.0f, "%.2f"))
            {
                foliageComponent.SetBrushRadius(radius);
            }

            ImGui::SameLine();
            int density = foliageComponent.GetDensity();
            ImGui::SetNextItemWidth(90.0f);
            if (ImGui::DragInt("Density##FoliagePaint", &density, 1.0f, 1, 100))
            {
                foliageComponent.SetDensity(density);
            }

            ImGui::SameLine();
            float minScale = foliageComponent.GetMinScale();
            float maxScale = foliageComponent.GetMaxScale();
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::DragFloatRange2("Scale##FoliagePaint", &minScale, &maxScale, 0.01f, 0.01f, 50.0f, "%.2f", "%.2f"))
            {
                foliageComponent.SetScaleRange(minScale, maxScale);
            }
        }

        void RenderTexturePaintToolbar(render::Texture *texture)
        {
            ImGui::Separator();
            ImGui::TextUnformatted("Texture Paint");
            ImGui::SameLine();
            ImGui::Checkbox("Enable##TexturePaint", &g_texturePaint.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(95.0f);
            ImGui::DragFloat("Radius px##TexturePaint", &g_texturePaint.radiusPixels, 1.0f, 1.0f, 2048.0f, "%.0f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::SliderFloat("Opacity##TexturePaint", &g_texturePaint.opacity, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine();
            ImGui::ColorEdit4("Color##TexturePaint", &g_texturePaint.color.x,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine();
            const std::string brushLabel = g_texturePaint.brushTextureReference.empty() ? "Drop paint texture" : g_texturePaint.brushTextureReference;
            ImGui::Button(brushLabel.c_str(), ImVec2(150.0f, 0.0f));
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
                {
                    if (payload->Data && payload->DataSize > 1)
                    {
                        const auto *data = static_cast<const char *>(payload->Data);
                        const std::string reference(data, data + payload->DataSize - 1);
                        if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Texture)
                        {
                            if (auto *project = EditorShell::GetInstance().GetProject())
                            {
                                const auto path = project->ResolveAssetReference(reference);
                                g_texturePaint.brushTexture = render::Texture::LoadFromFile(path.string().c_str());
                                g_texturePaint.brushTextureReference = reference;
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(80.0f);
            ImGui::DragFloat("Tiling##TexturePaint", &g_texturePaint.brushTextureScale, 0.1f, 0.1f, 256.0f, "%.1f");
            ImGui::SameLine();
            if (ImGui::SmallButton("Save##TexturePaint") && g_texturePaint.painter && g_texturePaint.texture == texture)
                g_texturePaint.painter->Save();
        }

        void CollectEntitiesRecursive(scene::Entity *entity, std::vector<scene::Entity *> &entities)
        {
            if (!entity)
            {
                return;
            }

            entities.push_back(entity);
            for (auto *child : entity->GetChildren())
            {
                CollectEntitiesRecursive(child, entities);
            }
        }

        std::optional<float> IntersectBoundsDistance(const render::MeshBounds &bounds, const glm::vec3 &origin, const glm::vec3 &direction)
        {
            const glm::vec3 offset = origin - bounds.center;
            const float b = glm::dot(offset, direction);
            const float c = glm::dot(offset, offset) - bounds.radius * bounds.radius;
            if (c <= 0.0f)
            {
                return 0.0f;
            }

            if (b > 0.0f)
            {
                return std::nullopt;
            }

            const float discriminant = b * b - c;
            if (discriminant < 0.0f)
            {
                return std::nullopt;
            }

            return std::max(0.0f, -b - std::sqrt(discriminant));
        }

        ProjectedPoint ProjectWorldPoint(const glm::vec3 &point,
                                         const render::CameraData &cameraData,
                                         const ImVec2 &viewportMin,
                                         const ImVec2 &viewportSize)
        {
            const glm::vec4 clip = cameraData.projection * cameraData.view * glm::vec4(point, 1.0f);
            if (clip.w <= kRayEpsilon)
            {
                return {};
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.z < -1.0f || ndc.z > 1.0f)
            {
                return {};
            }

            return ProjectedPoint{
                .screen = ImVec2(
                    viewportMin.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                    viewportMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * viewportSize.y),
                .visible = true,
            };
        }

        bool ClipScreenLineToRect(ImVec2 &a, ImVec2 &b, const ImVec2 &min, const ImVec2 &max)
        {
            enum OutCode
            {
                Inside = 0,
                Left = 1,
                Right = 2,
                Bottom = 4,
                Top = 8,
            };

            const auto computeOutCode = [&](const ImVec2 &point)
            {
                int code = Inside;
                if (point.x < min.x)
                {
                    code |= Left;
                }
                else if (point.x > max.x)
                {
                    code |= Right;
                }

                if (point.y < min.y)
                {
                    code |= Top;
                }
                else if (point.y > max.y)
                {
                    code |= Bottom;
                }

                return code;
            };

            int codeA = computeOutCode(a);
            int codeB = computeOutCode(b);
            while (true)
            {
                if ((codeA | codeB) == 0)
                {
                    return true;
                }

                if ((codeA & codeB) != 0)
                {
                    return false;
                }

                const int outsideCode = codeA != 0 ? codeA : codeB;
                ImVec2 clippedPoint = outsideCode == codeA ? a : b;
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;

                if ((outsideCode & Top) != 0)
                {
                    if (std::abs(dy) <= kRayEpsilon)
                    {
                        return false;
                    }
                    clippedPoint.x = a.x + dx * (min.y - a.y) / dy;
                    clippedPoint.y = min.y;
                }
                else if ((outsideCode & Bottom) != 0)
                {
                    if (std::abs(dy) <= kRayEpsilon)
                    {
                        return false;
                    }
                    clippedPoint.x = a.x + dx * (max.y - a.y) / dy;
                    clippedPoint.y = max.y;
                }
                else if ((outsideCode & Right) != 0)
                {
                    if (std::abs(dx) <= kRayEpsilon)
                    {
                        return false;
                    }
                    clippedPoint.y = a.y + dy * (max.x - a.x) / dx;
                    clippedPoint.x = max.x;
                }
                else if ((outsideCode & Left) != 0)
                {
                    if (std::abs(dx) <= kRayEpsilon)
                    {
                        return false;
                    }
                    clippedPoint.y = a.y + dy * (min.x - a.x) / dx;
                    clippedPoint.x = min.x;
                }

                if (outsideCode == codeA)
                {
                    a = clippedPoint;
                    codeA = computeOutCode(a);
                }
                else
                {
                    b = clippedPoint;
                    codeB = computeOutCode(b);
                }
            }
        }

        void DrawWorldLine(ImDrawList *drawList,
                           const glm::vec3 &a,
                           const glm::vec3 &b,
                           const render::CameraData &cameraData,
                           const ImVec2 &viewportMin,
                           const ImVec2 &viewportSize,
                           ImU32 color,
                           float thickness = 1.5f)
        {
            glm::vec3 clippedA = a;
            glm::vec3 clippedB = b;
            const auto clipToDepthRange = [&](float minDepth, float maxDepth)
            {
                const glm::vec3 viewA = glm::vec3(cameraData.view * glm::vec4(clippedA, 1.0f));
                const glm::vec3 viewB = glm::vec3(cameraData.view * glm::vec4(clippedB, 1.0f));
                float depthA = -viewA.z;
                float depthB = -viewB.z;

                if ((depthA < minDepth && depthB < minDepth) || (depthA > maxDepth && depthB > maxDepth))
                {
                    return false;
                }

                const auto clipAgainstPlane = [&](float planeDepth, bool keepGreater)
                {
                    const bool aInside = keepGreater ? depthA >= planeDepth : depthA <= planeDepth;
                    const bool bInside = keepGreater ? depthB >= planeDepth : depthB <= planeDepth;
                    if (aInside && bInside)
                    {
                        return true;
                    }

                    const float denominator = depthB - depthA;
                    if (std::abs(denominator) <= kRayEpsilon)
                    {
                        return false;
                    }

                    const float t = glm::clamp((planeDepth - depthA) / denominator, 0.0f, 1.0f);
                    const glm::vec3 clippedPoint = glm::mix(clippedA, clippedB, t);
                    if (!aInside)
                    {
                        clippedA = clippedPoint;
                        depthA = planeDepth;
                    }
                    else
                    {
                        clippedB = clippedPoint;
                        depthB = planeDepth;
                    }
                    return true;
                };

                return clipAgainstPlane(minDepth, true) && clipAgainstPlane(maxDepth, false);
            };

            if (!clipToDepthRange(std::max(cameraData.nearPlane, 0.001f), std::max(cameraData.farPlane, cameraData.nearPlane + 0.001f)))
            {
                return;
            }

            const auto projectedA = ProjectWorldPoint(clippedA, cameraData, viewportMin, viewportSize);
            const auto projectedB = ProjectWorldPoint(clippedB, cameraData, viewportMin, viewportSize);
            if (!projectedA.visible || !projectedB.visible)
            {
                return;
            }

            ImVec2 screenA = projectedA.screen;
            ImVec2 screenB = projectedB.screen;
            const ImVec2 viewportMax(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y);
            if (!ClipScreenLineToRect(screenA, screenB, viewportMin, viewportMax))
            {
                return;
            }

            drawList->AddLine(screenA, screenB, color, thickness);
        }

        void DrawWorldCircle(ImDrawList *drawList,
                             const glm::vec3 &center,
                             const glm::vec3 &axisA,
                             const glm::vec3 &axisB,
                             const render::CameraData &cameraData,
                             const ImVec2 &viewportMin,
                             const ImVec2 &viewportSize,
                             ImU32 color)
        {
            constexpr int kSegmentCount = 48;
            glm::vec3 previousPoint = center + axisA;
            for (int segmentIndex = 1; segmentIndex <= kSegmentCount; ++segmentIndex)
            {
                const float angle = (static_cast<float>(segmentIndex) / static_cast<float>(kSegmentCount)) * glm::two_pi<float>();
                const glm::vec3 point = center + std::cos(angle) * axisA + std::sin(angle) * axisB;
                DrawWorldLine(drawList, previousPoint, point, cameraData, viewportMin, viewportSize, color, 1.0f);
                previousPoint = point;
            }
        }

        void DrawWorldArc(ImDrawList *drawList,
                          const glm::vec3 &center,
                          const glm::vec3 &axisA,
                          const glm::vec3 &axisB,
                          float startAngle,
                          float endAngle,
                          const render::CameraData &cameraData,
                          const ImVec2 &viewportMin,
                          const ImVec2 &viewportSize,
                          ImU32 color)
        {
            constexpr int kSegmentCount = 24;
            glm::vec3 previousPoint = center + std::cos(startAngle) * axisA + std::sin(startAngle) * axisB;
            for (int segmentIndex = 1; segmentIndex <= kSegmentCount; ++segmentIndex)
            {
                const float t = static_cast<float>(segmentIndex) / static_cast<float>(kSegmentCount);
                const float angle = glm::mix(startAngle, endAngle, t);
                const glm::vec3 point = center + std::cos(angle) * axisA + std::sin(angle) * axisB;
                DrawWorldLine(drawList, previousPoint, point, cameraData, viewportMin, viewportSize, color, 1.0f);
                previousPoint = point;
            }
        }

        void DrawWireBox(ImDrawList *drawList,
                         const glm::mat4 &transform,
                         const render::CameraData &cameraData,
                         const ImVec2 &viewportMin,
                         const ImVec2 &viewportSize,
                         ImU32 color,
                         float thickness = 1.5f)
        {
            glm::vec3 corners[8];
            for (int cornerIndex = 0; cornerIndex < 8; ++cornerIndex)
            {
                const glm::vec3 localCorner(
                    (cornerIndex & 1) ? 0.5f : -0.5f,
                    (cornerIndex & 2) ? 0.5f : -0.5f,
                    (cornerIndex & 4) ? 0.5f : -0.5f);
                corners[cornerIndex] = glm::vec3(transform * glm::vec4(localCorner, 1.0f));
            }

            constexpr int kEdges[12][2] = {
                {0, 1},
                {1, 3},
                {3, 2},
                {2, 0},
                {4, 5},
                {5, 7},
                {7, 6},
                {6, 4},
                {0, 4},
                {1, 5},
                {2, 6},
                {3, 7},
            };
            for (const auto &edge : kEdges)
            {
                DrawWorldLine(drawList, corners[edge[0]], corners[edge[1]], cameraData, viewportMin, viewportSize, color, thickness);
            }
        }

        void DrawCameraShape(ImDrawList *drawList,
                             const scene::Entity &entity,
                             const scene::CameraComponent &cameraComponent,
                             const render::CameraData &cameraData,
                             const ImVec2 &viewportMin,
                             const ImVec2 &viewportSize)
        {
            const auto *camera = cameraComponent.GetCamera();
            if (!camera)
            {
                return;
            }

            const glm::mat4 transform = entity.GetWorldTransform();
            const glm::vec3 origin = entity.GetWorldPosition();
            const glm::vec3 right = glm::normalize(glm::vec3(transform[0]));
            const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
            const glm::vec3 forward = -glm::normalize(glm::vec3(transform[2]));
            const float nearPlane = std::max(camera->GetNearPlane(), 0.001f);
            const float farPlane = std::max(camera->GetFarPlane(), nearPlane + 0.001f);
            const float tanHalfFov = std::tan(glm::radians(camera->GetFOV()) * 0.5f);
            constexpr float kPreviewAspect = 16.0f / 9.0f;

            const auto buildFrustumCorners = [&](float distance)
            {
                const float halfHeight = tanHalfFov * distance;
                const float halfWidth = halfHeight * kPreviewAspect;
                const glm::vec3 center = origin + forward * distance;
                return std::array<glm::vec3, 4>{
                    center - right * halfWidth - up * halfHeight,
                    center + right * halfWidth - up * halfHeight,
                    center + right * halfWidth + up * halfHeight,
                    center - right * halfWidth + up * halfHeight,
                };
            };

            const auto nearCorners = buildFrustumCorners(nearPlane);
            const auto farCorners = buildFrustumCorners(farPlane);
            const ImU32 color = IM_COL32(80, 180, 255, 230);

            for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
            {
                DrawWorldLine(drawList, nearCorners[cornerIndex], farCorners[cornerIndex], cameraData, viewportMin, viewportSize, color);
            }

            DrawWorldLine(drawList, nearCorners[0], nearCorners[1], cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, nearCorners[1], nearCorners[2], cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, nearCorners[2], nearCorners[3], cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, nearCorners[3], nearCorners[0], cameraData, viewportMin, viewportSize, color);

            DrawWorldLine(drawList, farCorners[0], farCorners[1], cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, farCorners[1], farCorners[2], cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, farCorners[2], farCorners[3], cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, farCorners[3], farCorners[0], cameraData, viewportMin, viewportSize, color);
        }

        void DrawLightShape(ImDrawList *drawList,
                            const scene::Entity &entity,
                            const scene::LightComponent &lightComponent,
                            const render::CameraData &cameraData,
                            const ImVec2 &viewportMin,
                            const ImVec2 &viewportSize)
        {
            const auto &light = lightComponent.GetLight();
            const glm::vec3 center = entity.GetWorldPosition();
            const ImU32 color = IM_COL32(255, 216, 92, 230);
            if (light.type == scene::LightType::Point)
            {
                const float radius = std::max(light.range, 0.1f);
                DrawWorldCircle(drawList, center, glm::vec3(radius, 0.0f, 0.0f), glm::vec3(0.0f, radius, 0.0f), cameraData, viewportMin, viewportSize, color);
                DrawWorldCircle(drawList, center, glm::vec3(radius, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, radius), cameraData, viewportMin, viewportSize, color);
                DrawWorldCircle(drawList, center, glm::vec3(0.0f, radius, 0.0f), glm::vec3(0.0f, 0.0f, radius), cameraData, viewportMin, viewportSize, color);
                return;
            }

            const glm::vec3 direction = glm::dot(light.direction, light.direction) > 0.0001f ? glm::normalize(light.direction) : glm::vec3(0.0f, -1.0f, 0.0f);
            if (light.type == scene::LightType::Directional)
            {
                DrawWorldLine(drawList, center - direction * 0.75f, center + direction * 1.5f, cameraData, viewportMin, viewportSize, color, 2.0f);
                return;
            }

            const glm::vec3 referenceUp = std::abs(direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            const glm::vec3 tangent = glm::normalize(glm::cross(referenceUp, direction));
            const glm::vec3 bitangent = glm::normalize(glm::cross(direction, tangent));
            const float range = std::max(light.range, 0.1f);
            const float radius = range * 0.28f;
            const glm::vec3 coneCenter = center + direction * range;
            DrawWorldCircle(drawList, coneCenter, tangent * radius, bitangent * radius, cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, center, coneCenter + tangent * radius, cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, center, coneCenter - tangent * radius, cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, center, coneCenter + bitangent * radius, cameraData, viewportMin, viewportSize, color);
            DrawWorldLine(drawList, center, coneCenter - bitangent * radius, cameraData, viewportMin, viewportSize, color);
        }

        glm::vec3 SafeNormalizedAxis(const glm::vec3 &axis, const glm::vec3 &fallback)
        {
            return glm::dot(axis, axis) > 0.000001f ? glm::normalize(axis) : fallback;
        }

        void DrawColliderShape(ImDrawList *drawList,
                               const scene::Entity &entity,
                               const scene::ColliderComponent &colliderComponent,
                               const render::CameraData &cameraData,
                               const ImVec2 &viewportMin,
                               const ImVec2 &viewportSize)
        {
            const ImU32 color = colliderComponent.IsTrigger()
                                    ? IM_COL32(255, 185, 80, 230)
                                    : IM_COL32(116, 232, 255, 230);
            const glm::mat4 worldTransform = entity.GetWorldTransform();
            const glm::vec3 worldScale = entity.GetWorldScale();
            if (colliderComponent.GetShape() == scene::ColliderShape::Box)
            {
                const glm::mat4 colliderTransform =
                    worldTransform *
                    glm::translate(glm::mat4(1.0f), colliderComponent.GetCenter()) *
                    glm::scale(glm::mat4(1.0f), colliderComponent.GetSize());
                DrawWireBox(drawList, colliderTransform, cameraData, viewportMin, viewportSize, color, 1.75f);
                return;
            }

            const glm::vec3 center = glm::vec3(worldTransform * glm::vec4(colliderComponent.GetCenter(), 1.0f));
            const glm::vec3 right = SafeNormalizedAxis(glm::vec3(worldTransform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 up = SafeNormalizedAxis(glm::vec3(worldTransform[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::vec3 forward = SafeNormalizedAxis(glm::vec3(worldTransform[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            const float radius = std::max(colliderComponent.GetScaledRadius(worldScale), 0.0001f);

            if (colliderComponent.GetShape() == scene::ColliderShape::Sphere)
            {
                DrawWorldCircle(drawList, center, right * radius, up * radius, cameraData, viewportMin, viewportSize, color);
                DrawWorldCircle(drawList, center, right * radius, forward * radius, cameraData, viewportMin, viewportSize, color);
                DrawWorldCircle(drawList, center, up * radius, forward * radius, cameraData, viewportMin, viewportSize, color);
                return;
            }

            const float height = std::max(colliderComponent.GetScaledHeight(worldScale), radius * 2.0f);
            const float cylinderHalfHeight = std::max((height * 0.5f) - radius, 0.0f);
            const glm::vec3 topCenter = center + up * cylinderHalfHeight;
            const glm::vec3 bottomCenter = center - up * cylinderHalfHeight;

            DrawWorldCircle(drawList, topCenter, right * radius, forward * radius, cameraData, viewportMin, viewportSize, color);
            DrawWorldCircle(drawList, bottomCenter, right * radius, forward * radius, cameraData, viewportMin, viewportSize, color);
            DrawWorldArc(drawList, topCenter, right * radius, up * radius, 0.0f, glm::pi<float>(), cameraData, viewportMin, viewportSize, color);
            DrawWorldArc(drawList, bottomCenter, right * radius, up * radius, glm::pi<float>(), glm::two_pi<float>(), cameraData, viewportMin, viewportSize, color);
            DrawWorldArc(drawList, topCenter, forward * radius, up * radius, 0.0f, glm::pi<float>(), cameraData, viewportMin, viewportSize, color);
            DrawWorldArc(drawList, bottomCenter, forward * radius, up * radius, glm::pi<float>(), glm::two_pi<float>(), cameraData, viewportMin, viewportSize, color);

            DrawWorldLine(drawList, topCenter + right * radius, bottomCenter + right * radius, cameraData, viewportMin, viewportSize, color, 1.75f);
            DrawWorldLine(drawList, topCenter - right * radius, bottomCenter - right * radius, cameraData, viewportMin, viewportSize, color, 1.75f);
            DrawWorldLine(drawList, topCenter + forward * radius, bottomCenter + forward * radius, cameraData, viewportMin, viewportSize, color, 1.75f);
            DrawWorldLine(drawList, topCenter - forward * radius, bottomCenter - forward * radius, cameraData, viewportMin, viewportSize, color, 1.75f);
        }

        void DrawEditorDebugShapes(scene::Scene *scene,
                                   scene::Entity *selectedEntity,
                                   const render::CameraData &cameraData,
                                   const ImVec2 &viewportMin,
                                   const ImVec2 &viewportSize)
        {
            if (!scene || !selectedEntity)
            {
                return;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            auto *drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(viewportMin,
                                   ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
                                   true);
            for (auto *entity : entities)
            {
                if (!entity || entity != selectedEntity || !entity->IsActive())
                {
                    continue;
                }

                if (auto *iblCaptureComponent = entity->GetComponent<scene::IblCaptureComponent>())
                {
                    if (iblCaptureComponent->IsEnabled())
                    {
                        const ImU32 color = iblCaptureComponent->GetCaptureTexture()
                                                ? IM_COL32(118, 236, 170, 230)
                                                : IM_COL32(118, 236, 170, 120);
                        DrawWireBox(drawList, iblCaptureComponent->GetVolumeTransform(), cameraData, viewportMin, viewportSize, color, 1.75f);
                    }
                }

                if (auto *audioVolume = entity->GetComponent<scene::AudioEnvironmentVolumeComponent>();
                    audioVolume && audioVolume->IsEnabled())
                {
                    const ImU32 color = IM_COL32(190, 110, 255, 235);
                    const glm::mat4 world = entity->GetWorldTransform();
                    if (audioVolume->GetShape() == scene::AudioEnvironmentShape::Box)
                        DrawWireBox(drawList, world * glm::scale(glm::mat4(1.0f), audioVolume->GetSize()), cameraData, viewportMin, viewportSize, color, 1.75f);
                    else
                    {
                        const glm::vec3 center = entity->GetWorldPosition();
                        const float radius = audioVolume->GetRadius();
                        const glm::vec3 right=glm::vec3(world[0])*radius, up=glm::vec3(world[1])*radius, forward=glm::vec3(world[2])*radius;
                        DrawWorldCircle(drawList,center,right,up,cameraData,viewportMin,viewportSize,color);
                        DrawWorldCircle(drawList,center,right,forward,cameraData,viewportMin,viewportSize,color);
                        DrawWorldCircle(drawList,center,up,forward,cameraData,viewportMin,viewportSize,color);
                    }
                }

                if (auto *cloudComponent = entity->GetComponent<scene::VolumetricCloudComponent>())
                {
                    if (cloudComponent->IsEnabled())
                    {
                        const glm::mat4 volumeTransform = entity->GetWorldTransform() *
                            glm::scale(glm::mat4(1.0f), cloudComponent->GetSize());
                        DrawWireBox(drawList, volumeTransform, cameraData, viewportMin, viewportSize,
                                    IM_COL32(175, 150, 255, 220), 1.75f);
                    }
                }

                if (auto *cameraComponent = entity->GetComponent<scene::CameraComponent>())
                {
                    if (cameraComponent->IsEnabled())
                    {
                        DrawCameraShape(drawList, *entity, *cameraComponent, cameraData, viewportMin, viewportSize);
                    }
                }

                if (auto *lightComponent = entity->GetComponent<scene::LightComponent>())
                {
                    if (lightComponent->IsEnabled())
                    {
                        DrawLightShape(drawList, *entity, *lightComponent, cameraData, viewportMin, viewportSize);
                    }
                }

                if (auto *colliderComponent = entity->GetComponent<scene::ColliderComponent>())
                {
                    if (colliderComponent->IsEnabled())
                    {
                        DrawColliderShape(drawList, *entity, *colliderComponent, cameraData, viewportMin, viewportSize);
                    }
                }
            }
            drawList->PopClipRect();
        }

        bool IntersectTriangle(const glm::vec3 &origin,
                               const glm::vec3 &direction,
                               const glm::vec3 &v0,
                               const glm::vec3 &v1,
                               const glm::vec3 &v2,
                               float &distance)
        {
            const glm::vec3 edge1 = v1 - v0;
            const glm::vec3 edge2 = v2 - v0;
            const glm::vec3 p = glm::cross(direction, edge2);
            const float determinant = glm::dot(edge1, p);
            if (std::abs(determinant) <= kTriangleDeterminantEpsilon)
            {
                return false;
            }

            const float inverseDeterminant = 1.0f / determinant;
            const glm::vec3 t = origin - v0;
            const float u = glm::dot(t, p) * inverseDeterminant;
            if (u < 0.0f || u > 1.0f)
            {
                return false;
            }

            const glm::vec3 q = glm::cross(t, edge1);
            const float v = glm::dot(direction, q) * inverseDeterminant;
            if (v < 0.0f || u + v > 1.0f)
            {
                return false;
            }

            const float hitDistance = glm::dot(edge2, q) * inverseDeterminant;
            if (hitDistance <= kTriangleDistanceEpsilon)
            {
                return false;
            }

            distance = hitDistance;
            return true;
        }

        scene::AnimationComponent *FindAnimationComponent(scene::Entity *entity)
        {
            for (auto *current = entity; current != nullptr; current = current->GetParent())
            {
                if (auto *animationComponent = current->GetComponent<scene::AnimationComponent>())
                {
                    return animationComponent;
                }
            }

            return nullptr;
        }

        glm::mat4 ComputeAnimationNodeBindMatrix(const std::vector<render::AnimationNode> &nodes, int nodeIndex)
        {
            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
            {
                return glm::mat4(1.0f);
            }

            std::vector<int> chain;
            for (int currentNodeIndex = nodeIndex;
                 currentNodeIndex >= 0 && currentNodeIndex < static_cast<int>(nodes.size());
                 currentNodeIndex = nodes[static_cast<size_t>(currentNodeIndex)].parentNodeIndex)
            {
                chain.push_back(currentNodeIndex);
            }

            glm::mat4 transform(1.0f);
            for (auto iterator = chain.rbegin(); iterator != chain.rend(); ++iterator)
            {
                transform *= nodes[static_cast<size_t>(*iterator)].localBindTransform;
            }
            return transform;
        }

        glm::mat4 ComputePickSubmeshTransform(scene::Entity &entity,
                                              scene::MeshComponent &meshComponent,
                                              size_t submeshIndex,
                                              const render::Submesh &submesh,
                                              scene::AnimationComponent *animationComponent)
        {
            glm::mat4 transform = entity.GetWorldTransform() *
                                  meshComponent.GetMeshOffsetTransform() *
                                  meshComponent.GetSubmeshOffsetTransform(submeshIndex);
            if (submesh.animatedNodeIndex >= 0)
            {
                render::Mesh *mesh = meshComponent.GetMesh();
                if (mesh)
                {
                    transform *= animationComponent && animationComponent->GetClipCount() > 0
                                     ? animationComponent->GetNodeMatrix(mesh->GetAnimationNodes(), submesh.animatedNodeIndex)
                                     : ComputeAnimationNodeBindMatrix(mesh->GetAnimationNodes(), submesh.animatedNodeIndex);
                }
            }

            return transform;
        }

        struct MeshUvHit
        {
            glm::vec2 uv{0.0f};
            glm::vec3 worldPosition{0.0f};
            render::Texture *texture = nullptr;
            float worldDistance = std::numeric_limits<float>::max();
            std::size_t submeshIndex = 0;
        };

        std::optional<MeshUvHit> RaycastMeshUv(scene::Entity &entity,
                                               scene::MeshComponent &meshComponent,
                                               const PickRay &ray)
        {
            auto *mesh = meshComponent.GetMesh();
            if (!mesh)
                return std::nullopt;
            const auto &data = mesh->GetMeshData();
            if (data.indices.size() < 3 || data.vertices.empty())
                return std::nullopt;

            std::optional<MeshUvHit> closest;
            auto *animation = FindAnimationComponent(&entity);
            const std::size_t submeshCount = mesh->GetSubmeshCount();
            const std::size_t begin = meshComponent.GetSubmeshIndex() >= 0 ? static_cast<std::size_t>(meshComponent.GetSubmeshIndex()) : 0;
            const std::size_t end = meshComponent.GetSubmeshIndex() >= 0
                                        ? std::min(begin + static_cast<std::size_t>(std::max(1, meshComponent.GetSubmeshRangeCount())), submeshCount)
                                        : submeshCount;
            for (std::size_t submeshIndex = begin; submeshIndex < end; ++submeshIndex)
            {
                const auto &submesh = mesh->GetSubmesh(submeshIndex);
                auto *material = meshComponent.GetMaterialForSubmesh(submeshIndex);
                auto *texture = material ? material->GetConfig().albedoTexture : nullptr;
                if (submesh.indexCount < 3 || submesh.indexOffset + submesh.indexCount > data.indices.size())
                    continue;

                const glm::mat4 world = ComputePickSubmeshTransform(entity, meshComponent, submeshIndex, submesh, animation);
                const glm::mat4 inverseWorld = glm::inverse(world);
                const glm::vec3 localOrigin(inverseWorld * glm::vec4(ray.origin, 1.0f));
                glm::vec3 localDirection(inverseWorld * glm::vec4(ray.direction, 0.0f));
                if (glm::dot(localDirection, localDirection) <= kRayEpsilon)
                    continue;
                localDirection = glm::normalize(localDirection);
                if (!IntersectBoundsDistance(submesh.bounds, localOrigin, localDirection))
                    continue;

                const std::size_t indexEnd = submesh.indexOffset + submesh.indexCount;
                for (std::size_t index = submesh.indexOffset; index + 2 < indexEnd; index += 3)
                {
                    const unsigned int indices[] = {data.indices[index], data.indices[index + 1], data.indices[index + 2]};
                    if (indices[0] >= data.vertices.size() || indices[1] >= data.vertices.size() || indices[2] >= data.vertices.size())
                        continue;
                    const auto &a = data.vertices[indices[0]];
                    const auto &b = data.vertices[indices[1]];
                    const auto &c = data.vertices[indices[2]];
                    const glm::vec3 p0(a.position[0], a.position[1], a.position[2]);
                    const glm::vec3 p1(b.position[0], b.position[1], b.position[2]);
                    const glm::vec3 p2(c.position[0], c.position[1], c.position[2]);
                    float distance = 0.0f;
                    if (!IntersectTriangle(localOrigin, localDirection, p0, p1, p2, distance))
                        continue;
                    const glm::vec3 localHit = localOrigin + localDirection * distance;
                    const glm::vec3 worldHit(world * glm::vec4(localHit, 1.0f));
                    const float worldDistance = glm::length(worldHit - ray.origin);
                    if (closest && worldDistance >= closest->worldDistance)
                        continue;

                    const glm::vec3 v0 = p1 - p0;
                    const glm::vec3 v1 = p2 - p0;
                    const glm::vec3 v2 = localHit - p0;
                    const float d00 = glm::dot(v0, v0);
                    const float d01 = glm::dot(v0, v1);
                    const float d11 = glm::dot(v1, v1);
                    const float d20 = glm::dot(v2, v0);
                    const float d21 = glm::dot(v2, v1);
                    const float denominator = d00 * d11 - d01 * d01;
                    if (std::abs(denominator) <= kTriangleDeterminantEpsilon)
                        continue;
                    const float baryB = (d11 * d20 - d01 * d21) / denominator;
                    const float baryC = (d00 * d21 - d01 * d20) / denominator;
                    const float baryA = 1.0f - baryB - baryC;
                    const glm::vec2 uv0(a.uv[0], a.uv[1]);
                    const glm::vec2 uv1(b.uv[0], b.uv[1]);
                    const glm::vec2 uv2(c.uv[0], c.uv[1]);
                    closest = MeshUvHit{uv0 * baryA + uv1 * baryB + uv2 * baryC, worldHit, texture, worldDistance, submeshIndex};
                }
            }
            return closest;
        }

        render::Texture *CreatePrivatePaintTexture(scene::Entity &entity,
                                                   scene::MeshComponent *meshComponent,
                                                   scene::TerrainComponent *terrainComponent,
                                                   std::size_t submeshIndex,
                                                   render::Texture *source)
        {
            auto *project = EditorShell::GetInstance().GetProject();
            if (!project)
                return nullptr;
            const auto directory = project->GetAssetDirectoryPath() / "PaintedTextures";
            std::error_code error;
            std::filesystem::create_directories(directory, error);
            if (error)
                return nullptr;
            const std::string suffix = meshComponent ? "_mesh_" + std::to_string(submeshIndex) : "_terrain";
            const auto output = directory / ("entity_" + std::to_string(entity.GetID()) + suffix + ".png");
            if (!source)
            {
                constexpr int defaultResolution = 2048;
                static const std::vector<unsigned char> whitePixels(
                    static_cast<std::size_t>(defaultResolution) * defaultResolution * 4, 255);
                const std::string cacheKey = "texture-paint-base://" + std::to_string(entity.GetID()) + suffix;
                source = core::Engine::GetInstance().GetTextureManager().LoadTextureFromMemory(
                    cacheKey, whitePixels.data(), defaultResolution, defaultResolution, 4);
            }
            if (!source)
                return nullptr;
            render::TexturePainter sourceCopy(*source);
            if (!sourceCopy.IsValid() || !sourceCopy.Save(output.string()))
                return nullptr;
            auto *privateTexture = render::Texture::LoadFromFile(output.string().c_str());
            if (!privateTexture)
                return nullptr;

            if (meshComponent)
            {
                auto *uniqueMaterial = meshComponent->CreateUniqueMaterialForSubmesh(submeshIndex);
                if (!uniqueMaterial)
                    return nullptr;
                uniqueMaterial->SetAlbedoTexture(privateTexture);
                meshComponent->SetMaterialAssetForSubmesh(submeshIndex, {});
            }
            else if (terrainComponent)
            {
                terrainComponent->SetPaintedAlbedoPath(output.string());
                privateTexture = terrainComponent->GetMaterial()->GetConfig().albedoTexture;
            }
            project->RefreshAssetRegistry();
            return privateTexture;
        }

        std::optional<PickRay> BuildPickRay(const render::CameraData &cameraData,
                                            const ImVec2 &viewportMin,
                                            const ImVec2 &viewportSize,
                                            std::string *failureReason = nullptr)
        {
            if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
            {
                if (failureReason)
                {
                    *failureReason = "viewport-size";
                }
                return std::nullopt;
            }

            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            const float normalizedX = (mousePosition.x - viewportMin.x) / viewportSize.x;
            const float normalizedY = (mousePosition.y - viewportMin.y) / viewportSize.y;
            if (normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f || normalizedY > 1.0f)
            {
                if (failureReason)
                {
                    std::ostringstream message;
                    message << "mouse-outside normalized=(" << normalizedX << "," << normalizedY << ")";
                    *failureReason = message.str();
                }
                return std::nullopt;
            }

            const float clipX = normalizedX * 2.0f - 1.0f;
            const float clipY = 1.0f - normalizedY * 2.0f;
            const glm::mat4 inverseView = glm::inverse(cameraData.view);
            const glm::mat4 inverseViewProjection = inverseView * glm::inverse(cameraData.projection);
            glm::vec4 cameraWorldPosition = inverseView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            if (std::abs(cameraWorldPosition.w) <= kHomogeneousWEpsilon)
            {
                if (failureReason)
                {
                    std::ostringstream message;
                    message << "camera-w w=" << cameraWorldPosition.w;
                    *failureReason = message.str();
                }
                return std::nullopt;
            }

            cameraWorldPosition /= cameraWorldPosition.w;

            glm::vec4 endpointA = inverseViewProjection * glm::vec4(clipX, clipY, -1.0f, 1.0f);
            glm::vec4 endpointB = inverseViewProjection * glm::vec4(clipX, clipY, 1.0f, 1.0f);
            if (std::abs(endpointA.w) <= kHomogeneousWEpsilon || std::abs(endpointB.w) <= kHomogeneousWEpsilon)
            {
                if (failureReason)
                {
                    *failureReason = "unproject-w";
                }
                return std::nullopt;
            }
            endpointA /= endpointA.w;
            endpointB /= endpointB.w;

            const glm::vec3 cameraPosition(cameraWorldPosition);
            const glm::vec3 cameraForward = -glm::normalize(glm::vec3(inverseView[2]));
            const bool orthographic = std::abs(cameraData.projection[3][3] - 1.0f) < 0.0001f;
            glm::vec3 origin = cameraPosition;
            glm::vec3 direction = cameraForward;
            if (orthographic)
            {
                const glm::vec3 worldA(endpointA);
                const glm::vec3 worldB(endpointB);
                const glm::vec3 offsetA = worldA - cameraPosition;
                const glm::vec3 offsetB = worldB - cameraPosition;
                origin = glm::dot(offsetA, offsetA) < glm::dot(offsetB, offsetB) ? worldA : worldB;
            }
            else
            {
                direction = glm::normalize(glm::vec3(endpointA) - cameraPosition);
                if (glm::dot(direction, cameraForward) < 0.0f)
                {
                    direction = -direction;
                }
            }
            if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
            {
                if (failureReason)
                {
                    std::ostringstream message;
                    message << "nonfinite-direction dir=(" << direction.x << "," << direction.y << "," << direction.z << ")";
                    *failureReason = message.str();
                }
                return std::nullopt;
            }

            return PickRay{.origin = origin, .direction = direction};
        }

        scene::Entity *PickEntity(scene::Scene *scene,
                                  const render::CameraData &cameraData,
                                  const ImVec2 &viewportMin,
                                  const ImVec2 &viewportSize,
                                  PickDebugInfo *debugInfo = nullptr,
                                  std::size_t *selectedSubmeshIndex = nullptr)
        {
            constexpr std::size_t kMaxExactPickTrianglesPerSubmesh = 250000;
            constexpr std::size_t kNoSubmesh = std::numeric_limits<std::size_t>::max();

            if (selectedSubmeshIndex)
            {
                *selectedSubmeshIndex = kNoSubmesh;
            }

            if (!scene)
            {
                if (debugInfo)
                {
                    debugInfo->sceneMissing = true;
                }
                return nullptr;
            }

            std::string rayFailureReason;
            const auto ray = BuildPickRay(cameraData, viewportMin, viewportSize, &rayFailureReason);
            if (!ray.has_value())
            {
                if (debugInfo)
                {
                    debugInfo->rayFailureReason = std::move(rayFailureReason);
                }
                return nullptr;
            }
            if (debugInfo)
            {
                debugInfo->rayBuilt = true;
            }

            std::vector<scene::Entity *> entities;
            for (auto *rootEntity : scene->GetRootEntities())
            {
                CollectEntitiesRecursive(rootEntity, entities);
            }

            scene::Entity *selectedEntity = nullptr;
            float selectedDistance = std::numeric_limits<float>::max();
            for (auto *entity : entities)
            {
                if (debugInfo)
                {
                    ++debugInfo->totalEntities;
                }
                if (!entity || !entity->IsActive())
                {
                    if (debugInfo)
                    {
                        ++debugInfo->inactiveEntities;
                    }
                    continue;
                }

                auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                auto *terrainComponent = entity->GetComponent<scene::TerrainComponent>();
                const bool hasPickableMesh = meshComponent && meshComponent->IsEnabled() && meshComponent->IsVisible() && meshComponent->GetMesh();
                const bool hasPickableTerrain = terrainComponent && terrainComponent->IsEnabled();
                if (debugInfo)
                {
                    if (hasPickableMesh)
                    {
                        ++debugInfo->pickableMeshEntities;
                    }
                    if (hasPickableTerrain)
                    {
                        ++debugInfo->pickableTerrainEntities;
                    }
                }
                if (!hasPickableMesh && !hasPickableTerrain)
                {
                    continue;
                }

                if (hasPickableTerrain)
                {
                    glm::vec3 hitPoint{0.0f};
                    if (terrainComponent->Raycast(ray->origin, ray->direction, hitPoint))
                    {
                        if (debugInfo)
                        {
                            ++debugInfo->terrainHits;
                        }
                        const float worldDistance = glm::length(hitPoint - ray->origin);
                        if (worldDistance < selectedDistance)
                        {
                            selectedDistance = worldDistance;
                            selectedEntity = entity;
                            if (selectedSubmeshIndex)
                            {
                                *selectedSubmeshIndex = kNoSubmesh;
                            }
                            if (debugInfo)
                            {
                                debugInfo->selectedSource = "terrain";
                            }
                        }
                    }
                }

                if (!hasPickableMesh)
                {
                    continue;
                }

                render::Mesh *mesh = meshComponent->GetMesh();
                const auto &meshData = mesh->GetMeshData();
                if (meshData.vertices.empty() || meshData.indices.size() < 3)
                {
                    if (debugInfo)
                    {
                        ++debugInfo->emptyMeshEntities;
                    }
                    continue;
                }

                scene::AnimationComponent *animationComponent = FindAnimationComponent(entity);
                const bool useApproximateSubmeshPick = mesh->HasSkeleton() && animationComponent && animationComponent->GetClipCount() > 0;

                const size_t meshSubmeshCount = std::max<size_t>(mesh->GetSubmeshCount(), 1);
                const size_t submeshBegin = meshComponent->GetSubmeshIndex() >= 0 ? static_cast<size_t>(meshComponent->GetSubmeshIndex()) : 0;
                const size_t submeshEnd = meshComponent->GetSubmeshIndex() >= 0 ? std::min(submeshBegin + static_cast<size_t>(std::max(1, meshComponent->GetSubmeshRangeCount())), meshSubmeshCount) : meshSubmeshCount;

                for (size_t submeshIndex = submeshBegin; submeshIndex < submeshEnd; ++submeshIndex)
                {
                    if (debugInfo)
                    {
                        ++debugInfo->submeshesTested;
                    }
                    const auto &submesh = submeshIndex < mesh->GetSubmeshCount() ? mesh->GetSubmesh(submeshIndex) : render::Submesh{};
                    if (submesh.indexCount < 3 ||
                        submesh.indexOffset + submesh.indexCount > meshData.indices.size())
                    {
                        continue;
                    }

                    const glm::mat4 submeshWorldTransform = ComputePickSubmeshTransform(*entity, *meshComponent, submeshIndex, submesh, animationComponent);
                    const glm::mat4 inverseSubmeshWorldTransform = glm::inverse(submeshWorldTransform);
                    glm::vec3 localOrigin = glm::vec3(inverseSubmeshWorldTransform * glm::vec4(ray->origin, 1.0f));
                    glm::vec3 localDirection = glm::vec3(inverseSubmeshWorldTransform * glm::vec4(ray->direction, 0.0f));
                    const float directionLengthSquared = glm::dot(localDirection, localDirection);
                    if (directionLengthSquared <= kRayEpsilon)
                    {
                        continue;
                    }
                    localDirection = glm::normalize(localDirection);

                    const auto boundsDistance = IntersectBoundsDistance(submesh.bounds, localOrigin, localDirection);
                    if (!boundsDistance.has_value())
                    {
                        if (debugInfo)
                        {
                            ++debugInfo->submeshesRejectedByBounds;
                        }
                        continue;
                    }
                    if (debugInfo)
                    {
                        ++debugInfo->boundsHits;
                    }
                    const glm::vec3 localBoundsHitPoint = localOrigin + localDirection * *boundsDistance;
                    const glm::vec3 worldBoundsHitPoint = glm::vec3(submeshWorldTransform * glm::vec4(localBoundsHitPoint, 1.0f));
                    const float worldBoundsDistance = glm::length(worldBoundsHitPoint - ray->origin);
                    const std::size_t triangleCount = submesh.indexCount / 3;
                    if (useApproximateSubmeshPick || triangleCount > kMaxExactPickTrianglesPerSubmesh)
                    {
                        // Bounds are only a broad-phase test for exact meshes. They
                        // become the final hit solely when exact triangle picking is
                        // intentionally disabled for animated or enormous submeshes.
                        if (worldBoundsDistance < selectedDistance)
                        {
                            selectedDistance = worldBoundsDistance;
                            selectedEntity = entity;
                            if (selectedSubmeshIndex)
                            {
                                *selectedSubmeshIndex = submeshIndex;
                            }
                            if (debugInfo)
                            {
                                ++debugInfo->approximateHits;
                                debugInfo->selectedSource = "approx";
                            }
                        }
                        continue;
                    }

                    const std::size_t indexEnd = submesh.indexOffset + submesh.indexCount;
                    for (std::size_t index = submesh.indexOffset; index + 2 < indexEnd; index += 3)
                    {
                        const auto firstIndex = meshData.indices[index];
                        const auto secondIndex = meshData.indices[index + 1];
                        const auto thirdIndex = meshData.indices[index + 2];
                        if (firstIndex >= meshData.vertices.size() ||
                            secondIndex >= meshData.vertices.size() ||
                            thirdIndex >= meshData.vertices.size())
                        {
                            continue;
                        }

                        const auto &firstVertex = meshData.vertices[firstIndex];
                        const auto &secondVertex = meshData.vertices[secondIndex];
                        const auto &thirdVertex = meshData.vertices[thirdIndex];

                        const glm::vec3 v0(firstVertex.position[0], firstVertex.position[1], firstVertex.position[2]);
                        const glm::vec3 v1(secondVertex.position[0], secondVertex.position[1], secondVertex.position[2]);
                        const glm::vec3 v2(thirdVertex.position[0], thirdVertex.position[1], thirdVertex.position[2]);

                        float localDistance = 0.0f;
                        if (debugInfo)
                        {
                            ++debugInfo->triangleTests;
                        }
                        if (!IntersectTriangle(localOrigin, localDirection, v0, v1, v2, localDistance))
                        {
                            continue;
                        }
                        if (debugInfo)
                        {
                            ++debugInfo->triangleHits;
                        }

                        const glm::vec3 localHitPoint = localOrigin + localDirection * localDistance;
                        const glm::vec3 worldHitPoint = glm::vec3(submeshWorldTransform * glm::vec4(localHitPoint, 1.0f));
                        const float worldDistance = glm::length(worldHitPoint - ray->origin);
                        if (worldDistance < selectedDistance)
                        {
                            selectedDistance = worldDistance;
                            selectedEntity = entity;
                            if (selectedSubmeshIndex)
                            {
                                *selectedSubmeshIndex = submeshIndex;
                            }
                            if (debugInfo)
                            {
                                debugInfo->selectedSource = "triangle";
                            }
                        }
                    }
                }
            }

            if (debugInfo)
            {
                debugInfo->selectedEntity = selectedEntity;
                debugInfo->selectedDistance = selectedDistance;
            }
            return selectedEntity;
        }

        std::string FormatPickDebugMessage(const PickDebugInfo &debugInfo, const ImVec2 &mousePosition, const ImVec2 &viewportMin, const ImVec2 &viewportSize)
        {
            std::ostringstream message;
            message << "[Pick] mouse=(" << mousePosition.x << "," << mousePosition.y << ")"
                    << " viewportMin=(" << viewportMin.x << "," << viewportMin.y << ")"
                    << " viewportSize=(" << viewportSize.x << "," << viewportSize.y << ")";

            if (debugInfo.sceneMissing)
            {
                message << " blocked=no-scene";
                return message.str();
            }

            if (!debugInfo.rayBuilt)
            {
                message << " blocked=ray-not-built";
                if (!debugInfo.rayFailureReason.empty())
                {
                    message << " reason=" << debugInfo.rayFailureReason;
                }
                return message.str();
            }

            message << " entities=" << debugInfo.totalEntities
                    << " inactive=" << debugInfo.inactiveEntities
                    << " pickableMesh=" << debugInfo.pickableMeshEntities
                    << " pickableTerrain=" << debugInfo.pickableTerrainEntities
                    << " emptyMesh=" << debugInfo.emptyMeshEntities
                    << " submeshes=" << debugInfo.submeshesTested
                    << " boundsHits=" << debugInfo.boundsHits
                    << " boundsRejects=" << debugInfo.submeshesRejectedByBounds
                    << " terrainHits=" << debugInfo.terrainHits
                    << " approxHits=" << debugInfo.approximateHits
                    << " triangleTests=" << debugInfo.triangleTests
                    << " triangleHits=" << debugInfo.triangleHits;
            if (!debugInfo.rayFailureReason.empty())
            {
                message << " note=\"" << debugInfo.rayFailureReason << "\"";
            }

            if (debugInfo.selectedEntity)
            {
                message << " selected=\"" << debugInfo.selectedEntity->GetName() << "\""
                        << " id=" << debugInfo.selectedEntity->GetID()
                        << " source=" << (debugInfo.selectedSource.empty() ? "unknown" : debugInfo.selectedSource)
                        << " distance=" << debugInfo.selectedDistance;
            }
            else
            {
                message << " selected=<none>";
            }

            return message.str();
        }

        void ApplyWorldTransformToEntity(scene::Entity &entity, const glm::mat4 &worldTransform)
        {
            glm::mat4 localTransform = worldTransform;
            if (auto *parent = entity.GetParent())
            {
                localTransform = glm::inverse(parent->GetWorldTransform()) * worldTransform;
            }

            const glm::vec3 translation(localTransform[3]);

            glm::vec3 basisX(localTransform[0]);
            glm::vec3 basisY(localTransform[1]);
            glm::vec3 basisZ(localTransform[2]);

            glm::vec3 scale(glm::length(basisX), glm::length(basisY), glm::length(basisZ));
            if (scale.x <= std::numeric_limits<float>::epsilon() ||
                scale.y <= std::numeric_limits<float>::epsilon() ||
                scale.z <= std::numeric_limits<float>::epsilon())
            {
                return;
            }

            basisX /= scale.x;
            basisY /= scale.y;
            basisZ /= scale.z;

            if (glm::dot(glm::cross(basisX, basisY), basisZ) < 0.0f)
            {
                scale.x = -scale.x;
                basisX = -basisX;
            }

            glm::mat4 rotationMatrix(1.0f);
            rotationMatrix[0] = glm::vec4(glm::normalize(basisX), 0.0f);
            rotationMatrix[1] = glm::vec4(glm::normalize(basisY), 0.0f);
            rotationMatrix[2] = glm::vec4(glm::normalize(basisZ), 0.0f);

            float rotationX = 0.0f;
            float rotationY = 0.0f;
            float rotationZ = 0.0f;
            glm::extractEulerAngleXYZ(rotationMatrix, rotationX, rotationY, rotationZ);
            const glm::vec3 rotationDegrees = glm::degrees(glm::vec3(rotationX, rotationY, rotationZ));

            entity.SetPosition(translation);
            entity.SetRotation(rotationDegrees);
            entity.SetScale(scale);
        }

        glm::vec3 ExtractRotationDegrees(const glm::mat4 &transform)
        {
            glm::vec3 basisX = glm::normalize(glm::vec3(transform[0]));
            glm::vec3 basisY = glm::normalize(glm::vec3(transform[1]));
            glm::vec3 basisZ = glm::normalize(glm::vec3(transform[2]));
            if (glm::dot(glm::cross(basisX, basisY), basisZ) < 0.0f)
            {
                basisX = -basisX;
            }

            glm::mat4 rotationMatrix(1.0f);
            rotationMatrix[0] = glm::vec4(basisX, 0.0f);
            rotationMatrix[1] = glm::vec4(basisY, 0.0f);
            rotationMatrix[2] = glm::vec4(basisZ, 0.0f);
            float rotationX = 0.0f;
            float rotationY = 0.0f;
            float rotationZ = 0.0f;
            glm::extractEulerAngleXYZ(rotationMatrix, rotationX, rotationY, rotationZ);
            return glm::degrees(glm::vec3(rotationX, rotationY, rotationZ));
        }

        glm::mat4 BuildSplinePointFrame(const scene::SplineComponent &spline, std::size_t pointIndex)
        {
            const auto &points = spline.GetPoints();
            const std::size_t lastIndex = points.size() - 1;
            const std::size_t previousIndex = pointIndex > 0 ? pointIndex - 1 : (spline.IsClosed() ? lastIndex : pointIndex);
            const std::size_t nextIndex = pointIndex < lastIndex ? pointIndex + 1 : (spline.IsClosed() ? 0 : pointIndex);

            glm::vec3 tangent = points[nextIndex].position - points[previousIndex].position;
            if (glm::dot(tangent, tangent) <= 0.000001f)
            {
                tangent = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            tangent = glm::normalize(tangent);

            glm::vec3 defaultRight = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
            if (glm::dot(defaultRight, defaultRight) <= 0.000001f)
            {
                defaultRight = glm::cross(tangent, glm::vec3(0.0f, 0.0f, 1.0f));
            }
            defaultRight = glm::normalize(defaultRight);

            glm::mat4 frame(1.0f);
            frame[0] = glm::vec4(-defaultRight, 0.0f);
            frame[1] = glm::vec4(glm::normalize(glm::cross(tangent, -defaultRight)), 0.0f);
            frame[2] = glm::vec4(tangent, 0.0f);
            return frame;
        }

        int DrawAndPickSplineControlPoints(scene::Entity &entity,
                                           scene::SplineComponent &spline,
                                           int selectedPointIndex,
                                           const render::CameraData &cameraData,
                                           const ImVec2 &viewportMin,
                                           const ImVec2 &viewportSize,
                                           bool pickPoint)
        {
            const auto &points = spline.GetPoints();
            if (points.empty())
            {
                return -1;
            }

            auto *drawList = ImGui::GetWindowDrawList();
            const glm::mat4 worldTransform = entity.GetWorldTransform();
            std::vector<glm::vec3> worldPoints;
            worldPoints.reserve(points.size());
            for (const auto &point : points)
            {
                worldPoints.push_back(glm::vec3(worldTransform * glm::vec4(point.position, 1.0f)));
            }

            drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);
            for (std::size_t index = 1; index < worldPoints.size(); ++index)
            {
                DrawWorldLine(drawList, worldPoints[index - 1], worldPoints[index], cameraData, viewportMin, viewportSize,
                              IM_COL32(255, 196, 64, 190), 1.5f);
            }
            if (spline.IsClosed() && worldPoints.size() > 2)
            {
                DrawWorldLine(drawList, worldPoints.back(), worldPoints.front(), cameraData, viewportMin, viewportSize,
                              IM_COL32(255, 196, 64, 190), 1.5f);
            }

            int pickedIndex = -1;
            float closestDistanceSquared = 12.0f * 12.0f;
            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            for (std::size_t index = 0; index < worldPoints.size(); ++index)
            {
                const ProjectedPoint projected = ProjectWorldPoint(worldPoints[index], cameraData, viewportMin, viewportSize);
                if (!projected.visible)
                {
                    continue;
                }

                const bool selected = static_cast<int>(index) == selectedPointIndex;
                drawList->AddCircleFilled(projected.screen, selected ? 7.0f : 5.0f,
                                          selected ? IM_COL32(255, 232, 128, 255) : IM_COL32(255, 172, 32, 235));
                drawList->AddCircle(projected.screen, selected ? 8.0f : 6.0f, IM_COL32(32, 24, 12, 255), 0, 1.5f);
                const std::string pointLabel = std::to_string(index);
                drawList->AddText(ImVec2(projected.screen.x + 9.0f, projected.screen.y - 8.0f),
                                  IM_COL32(255, 232, 180, 255), pointLabel.c_str());

                if (pickPoint)
                {
                    const float dx = mousePosition.x - projected.screen.x;
                    const float dy = mousePosition.y - projected.screen.y;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared <= closestDistanceSquared)
                    {
                        closestDistanceSquared = distanceSquared;
                        pickedIndex = static_cast<int>(index);
                    }
                }
            }
            drawList->PopClipRect();
            return pickedIndex;
        }

        void FrameSelectedEntity(EditorShell &editorShell)
        {
            auto *selectedEntity = editorShell.GetSelectedEntity();
            if (!selectedEntity)
            {
                return;
            }

            auto &camera = editorShell.GetEditorCamera();
            glm::mat4 cameraTransform = glm::translate(glm::mat4(1.0f), camera.position);
            cameraTransform = glm::rotate(cameraTransform, glm::radians(camera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            cameraTransform = glm::rotate(cameraTransform, glm::radians(camera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 forward = glm::normalize(-glm::vec3(cameraTransform[2]));

            float radius = 2.5f;
            if (auto *meshComponent = selectedEntity->GetComponent<scene::MeshComponent>())
            {
                if (auto *mesh = meshComponent->GetMesh())
                {
                    const glm::vec3 worldScale = selectedEntity->GetWorldScale();
                    const float maxScale = std::max(worldScale.x, std::max(worldScale.y, worldScale.z));
                    radius = std::max(mesh->GetBounds().radius * maxScale, 0.5f);
                }
            }

            camera.position = selectedEntity->GetWorldPosition() - forward * std::max(radius * 2.5f, 2.0f);
        }

        void FrameSceneOrthographic(EditorShell &editorShell, const ImVec2 &viewportSize)
        {
            auto *scene = editorShell.GetEngine().GetScene();
            auto &camera = editorShell.GetEditorCamera();
            if (!scene)
            {
                camera.orthographic = true;
                return;
            }

            if (!camera.orthographic)
            {
                camera.perspectivePosition = camera.position;
                camera.hasPerspectivePosition = true;
            }

            glm::vec3 boundsMin(std::numeric_limits<float>::max());
            glm::vec3 boundsMax(-std::numeric_limits<float>::max());
            bool hasBounds = false;
            const auto includePoint = [&](const glm::vec3 &point)
            {
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
                hasBounds = true;
            };
            const auto visitEntity = [&](scene::Entity *entity, const auto &self) -> void
            {
                if (!entity || !entity->IsActive())
                {
                    return;
                }

                includePoint(entity->GetWorldPosition());
                if (auto *meshComponent = entity->GetComponent<scene::MeshComponent>())
                {
                    if (auto *mesh = meshComponent->GetMesh())
                    {
                        const auto &bounds = mesh->GetBounds();
                        const glm::mat4 worldTransform = entity->GetWorldTransform();
                        const glm::vec3 center(worldTransform * glm::vec4(bounds.center, 1.0f));
                        const glm::vec3 scale = entity->GetWorldScale();
                        const float radius = bounds.radius * std::max(std::abs(scale.x), std::max(std::abs(scale.y), std::abs(scale.z)));
                        includePoint(center - glm::vec3(radius));
                        includePoint(center + glm::vec3(radius));
                    }
                }
                for (auto *child : entity->GetChildren())
                {
                    self(child, self);
                }
            };
            for (auto *root : scene->GetRootEntities())
            {
                visitEntity(root, visitEntity);
            }

            if (!hasBounds)
            {
                boundsMin = glm::vec3(-10.0f);
                boundsMax = glm::vec3(10.0f);
            }

            const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
            const glm::vec3 halfExtents = glm::max((boundsMax - boundsMin) * 0.5f, glm::vec3(1.0f));
            glm::mat4 cameraTransform = glm::rotate(glm::mat4(1.0f), glm::radians(camera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            cameraTransform = glm::rotate(cameraTransform, glm::radians(camera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 right = glm::normalize(glm::vec3(cameraTransform[0]));
            const glm::vec3 up = glm::normalize(glm::vec3(cameraTransform[1]));
            const glm::vec3 forward = glm::normalize(-glm::vec3(cameraTransform[2]));
            const float horizontalExtent = glm::dot(glm::abs(right), halfExtents);
            const float verticalExtent = glm::dot(glm::abs(up), halfExtents);
            const float depthExtent = glm::dot(glm::abs(forward), halfExtents);
            const float aspect = viewportSize.y > 1.0f ? viewportSize.x / viewportSize.y : 1.0f;
            camera.orthographicSize = std::max(verticalExtent, horizontalExtent / std::max(aspect, 0.01f)) * 1.15f;

            const float cameraDistance = depthExtent + std::max(camera.orthographicSize * 2.0f, 100.0f);
            camera.position = center - forward * cameraDistance;
            camera.camera.SetFarPlane(std::max(camera.camera.GetFarPlane(), cameraDistance + depthExtent + 100.0f));
            camera.orthographic = true;
        }

    }

    const char *ViewportPanel::GetDebugViewLabel(render::PostProcessDebugView debugView)
    {
        return kDebugViewLabels[static_cast<int>(debugView)];
    }

    void ViewportPanel::SetEditorCameraData(const render::CameraData &cameraData)
    {
        m_editorCameraData = cameraData;
        m_hasEditorCameraData = true;
    }

    void ViewportPanel::ClearEditorCameraData()
    {
        m_hasEditorCameraData = false;
    }

    void ViewportPanel::Initialize()
    {
        m_renderScale = glm::clamp(m_config.initialRenderScale, kMinRenderScale, kMaxRenderScale);
        m_upscaleSharpness = glm::clamp(m_config.initialUpscaleSharpness, 0.0f, 1.0f);

        auto renderConfig = render::RenderTargetConfig{
            .width = kDefaultViewportWidth,
            .height = kDefaultViewportHeight,
            .clearColor = m_config.clearColor,
        };
        m_renderTarget = new render::RenderTarget(renderConfig);
        if (!m_renderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize RenderTarget in ViewportPanel" << std::endl;
        }

        renderConfig.width = std::max(1, static_cast<int>(std::lround(static_cast<float>(kDefaultViewportWidth) * m_renderScale)));
        renderConfig.height = std::max(1, static_cast<int>(std::lround(static_cast<float>(kDefaultViewportHeight) * m_renderScale)));
        m_scaledRenderTarget = new render::RenderTarget(renderConfig);
        m_upscaler = std::make_unique<render::SpatialUpscaler>();

        if (m_config.editorViewport)
        {
            const auto vulkanInfo = render::rhi::vulkan::ProbeVulkanDevice();
            m_vulkanAvailable = vulkanInfo.available;
            m_vulkanStatus = vulkanInfo.available
                                  ? "Vulkan available: " + vulkanInfo.deviceName
                                  : "Vulkan unavailable: " + vulkanInfo.error;
            std::cout << "Viewport RHI: " << m_vulkanStatus << std::endl;
            try
            {
                // During the editor migration Vulkan renders off-screen and
                // readback is uploaded to an OpenGL texture consumed by the
                // existing ImGui backend. Native Vulkan presentation replaces
                // this bridge once the editor window is created with NO_API.
                if (m_config.graphicsApi == render::rhi::GraphicsApi::Vulkan && m_vulkanAvailable)
                {
                    m_rhiDevice = std::make_unique<render::rhi::vulkan::VulkanDevice>();
                    m_activeRhiVulkan = true;
                }
                else
                {
                    m_rhiDevice = std::make_unique<render::rhi::opengl::OpenGLDevice>();
                    m_activeRhiVulkan = false;
                }
                m_basicRenderer = std::make_unique<render::BasicRenderer>();
                render::BasicRendererShaderPackage shaders;
                shaders.vertex.glsl = ReadRhiShaderText("BasicLit.vertex.glsl");
                shaders.vertex.spirv = ReadRhiShaderBinary("BasicLit.vertex.spv");
                shaders.fragment.glsl = ReadRhiShaderText("BasicLit.fragment.glsl");
                shaders.fragment.spirv = ReadRhiShaderBinary("BasicLit.fragment.spv");
                if (!m_basicRenderer->Initialize(*m_rhiDevice, shaders))
                {
                    m_basicRenderer.reset();
                    m_rhiDevice.reset();
                    m_useRhiPreview = false;
                }
                else
                {
                    std::cout << "Viewport RHI active preview backend: "
                              << (m_activeRhiVulkan ? "Vulkan (OpenGL readback bridge)" : "OpenGL")
                              << std::endl;
                }
            }
            catch (const std::exception &error)
            {
                std::cerr << "Failed to initialize viewport RHI preview: " << error.what() << std::endl;
                m_basicRenderer.reset();
                m_rhiDevice.reset();
                m_useRhiPreview = false;
            }
        }
    }

    void ViewportPanel::Render()
    {
        m_isViewportHovered = false;
        m_isViewportFocused = false;
        m_viewportSize = glm::vec2(0.0f);

        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        const ImVec2 panelSize = ImGui::GetContentRegionAvail();
        const int newWidth = static_cast<int>(panelSize.x);
        const int newHeight = static_cast<int>(panelSize.y);

        if (newWidth <= 0 || newHeight <= 0)
        {
            return;
        }

        if (newWidth != m_pendingWidth || newHeight != m_pendingHeight)
        {
            m_pendingWidth = newWidth;
            m_pendingHeight = newHeight;
            m_resizeStableFrames = 0;
        }
        if ((newWidth != m_renderTarget->GetWidth() || newHeight != m_renderTarget->GetHeight()) && ++m_resizeStableFrames >= kResizeDebounceFrames)
        {
            if (!m_renderTarget->Resize(newWidth, newHeight))
            {
                std::cerr << "Failed to resize RenderTarget in ViewportPanel" << std::endl;
                return;
            }
        }

        const int scaledWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(newWidth) * m_renderScale)));
        const int scaledHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(newHeight) * m_renderScale)));
        if (m_scaledRenderTarget && (scaledWidth != m_scaledRenderTarget->GetWidth() || scaledHeight != m_scaledRenderTarget->GetHeight()) &&
            m_resizeStableFrames >= kResizeDebounceFrames && !m_scaledRenderTarget->Resize(scaledWidth, scaledHeight))
        {
            std::cerr << "Failed to resize scaled RenderTarget in ViewportPanel" << std::endl;
            return;
        }

        const std::uint64_t displayedTexture = m_useRhiPreview && m_rhiViewportTexture != 0
                                                   ? m_rhiViewportTexture
                                                   : m_renderTarget->GetColorTextureID();
        ImTextureID texId = (ImTextureID)(uintptr_t)displayedTexture;
        ImVec2 imageSize = ImVec2(panelSize.x, panelSize.y);
        const bool displayingVulkanReadback = m_useRhiPreview && m_activeRhiVulkan && m_rhiViewportTexture != 0;
        ImGui::Image(texId, imageSize,
                     displayingVulkanReadback ? ImVec2(0, 0) : ImVec2(0, 1),
                     displayingVulkanReadback ? ImVec2(1, 1) : ImVec2(1, 0));
        const ImVec2 viewportMin = ImGui::GetItemRectMin();
        const ImVec2 viewportMax = ImGui::GetItemRectMax();
        if (m_useRhiPreview)
        {
            const std::string rhiLabel = std::string(m_activeRhiVulkan ? "Vulkan" : "OpenGL") +
                                         " RHI | commands " + std::to_string(m_rhiSceneCommandCount) +
                                         " | draws " + std::to_string(m_rhiDrawCount) +
                                         (m_activeRhiVulkan ? " | changed pixels " + std::to_string(m_rhiChangedPixelCount) : "");
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(viewportMin.x + 10.0f, viewportMax.y - ImGui::GetTextLineHeightWithSpacing() - 8.0f),
                IM_COL32(120, 220, 255, 255), rhiLabel.c_str());
        }
        m_viewportMin = glm::vec2(viewportMin.x, viewportMin.y);
        m_viewportSize = glm::vec2(imageSize.x, imageSize.y);
        const ImVec2 mousePosition = ImGui::GetIO().MousePos;
        const bool mouseInsideViewport =
            mousePosition.x >= viewportMin.x && mousePosition.x <= viewportMax.x &&
            mousePosition.y >= viewportMin.y && mousePosition.y <= viewportMax.y;
        m_isViewportHovered = mouseInsideViewport &&
                              ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem |
                                                     ImGuiHoveredFlags_RootAndChildWindows);
        // Alt + left-drag is reserved for trackpad camera navigation. Do not
        // turn the start of that gesture into a scene selection/edit click.
        const bool viewportClicked = mouseInsideViewport && !ImGui::GetIO().KeyAlt &&
                                     ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        m_isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (m_config.editorViewport && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
            {
                const std::string reference(static_cast<const char *>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
                const auto assetType = assets::Project::GetAssetTypeForReference(reference);
                if (assetType == assets::ProjectAssetType::Material)
                {
                    auto &editorShell = EditorShell::GetInstance();
                    auto &engine = editorShell.GetEngine();
                    auto *material = engine.GetAssetManager().LoadMaterialAsset(reference);
                    auto &editorCamera = editorShell.GetEditorCamera();
                    const glm::mat4 cameraTransform =
                        glm::translate(glm::mat4(1.0f), editorCamera.position) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(editorCamera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(editorCamera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
                    const render::CameraData cameraData = editorCamera.camera.GetCameraDataForTransform(
                        cameraTransform, m_renderTarget->GetWidth(), m_renderTarget->GetHeight());

                    std::size_t submeshIndex = std::numeric_limits<std::size_t>::max();
                    auto *entity = PickEntity(engine.GetScene(), cameraData, viewportMin, imageSize, nullptr, &submeshIndex);
                    auto *meshComponent = entity ? entity->GetComponent<scene::MeshComponent>() : nullptr;
                    if (material && meshComponent &&
                        meshComponent->GetMesh() &&
                        submeshIndex < meshComponent->GetMesh()->GetSubmeshCount())
                    {
                        editorShell.ExecuteSceneEdit(
                            "Assign Material",
                            [entity, meshComponent, submeshIndex, material, reference]()
                            {
                                meshComponent->SetMaterialForSubmesh(submeshIndex, material);
                                meshComponent->SetMaterialAssetForSubmesh(submeshIndex, reference);
                                entity->AddPrefabOverride(
                                    "Component:MeshComponent:SubmeshOverrides." +
                                    std::to_string(submeshIndex) + ".MaterialAsset");
                            });
                        editorShell.SetSelectedEntity(entity);
                    }
                    else if (!material)
                    {
                        editorShell.Log(EditorShell::ConsoleSeverity::Error, "Failed to load material: " + reference);
                    }
                }
                else if (assetType == assets::ProjectAssetType::Prefab)
                {
                    auto *scene = EditorShell::GetInstance().GetEngine().GetScene();
                    std::string errorMessage;
                    scene::Entity *createdEntity = nullptr;
                    if (scene)
                    {
                        EditorShell::GetInstance().ExecuteSceneEdit("Instantiate Prefab",
                                                                    [scene, reference, &createdEntity, &errorMessage]()
                                                                    {
                                                                        createdEntity = scene::Prefab::Instantiate(*scene, reference, nullptr, &errorMessage);
                                                                    });
                    }

                    if (createdEntity)
                    {
                        EditorShell::GetInstance().SetSelectedEntity(createdEntity);
                    }
                    else if (!errorMessage.empty())
                    {
                        EditorShell::GetInstance().Log(EditorShell::ConsoleSeverity::Error, errorMessage);
                    }
                }
                else if (assetType == assets::ProjectAssetType::Mesh)
                {
                    InstantiateMeshAssetIntoScene(reference, nullptr);
                }
                else if (assetType == assets::ProjectAssetType::Model)
                {
                    InstantiateModelAssetIntoScene(reference, nullptr);
                }
            }
            ImGui::EndDragDropTarget();
        }

        bool controlsHovered = false;
        if (m_panelControlsEnabled)
        {
            controlsHovered = RenderViewportSettingsOverlay(viewportMin, imageSize);
            if (m_config.editorViewport)
            {
                controlsHovered = RenderViewSelectionGizmo(viewportMin, imageSize) || controlsHovered;
            }
            m_isViewportHovered = m_isViewportHovered && !controlsHovered;
        }

        if (m_config.editorViewport)
        {
            RenderEditorOverlays(viewportMin, imageSize, viewportClicked, controlsHovered);
        }
    }

    bool ViewportPanel::RenderViewportSettingsOverlay(const ImVec2 &viewportMin, const ImVec2 &viewportSize)
    {
        const bool allowEditorViewportHotkeys =
            m_config.editorViewport &&
            // ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput;

        if (allowEditorViewportHotkeys && !m_editorMovementEnabled)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W))
            {
                m_gizmoOperation = ImGuizmo::TRANSLATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E))
            {
                m_gizmoOperation = ImGuizmo::ROTATE;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R))
            {
                m_gizmoOperation = ImGuizmo::SCALE;
            }
        }
        if (allowEditorViewportHotkeys && ImGui::IsKeyPressed(ImGuiKey_F))
        {
            FrameSelectedEntity(EditorShell::GetInstance());
        }

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        int debugView = static_cast<int>(renderer.GetPostProcessDebugView());

        const ImVec2 overlayPos(viewportMin.x + viewportSize.x - 10.0f, viewportMin.y + 10.0f);
        ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.92f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 4.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoNav;

        const std::string overlayName = "##ViewportSettingsOverlay" + m_config.name;
        ImGui::Begin(overlayName.c_str(), nullptr, flags);
        bool overlayPopupOpen = false;
        if (m_config.editorViewport)
        {
            if (m_gizmoOperation == ImGuizmo::TRANSLATE)
            {
                ImGui::TextUnformatted("Move");
            }
            else if (m_gizmoOperation == ImGuizmo::ROTATE)
            {
                ImGui::TextUnformatted("Rotate");
            }
            else
            {
                ImGui::TextUnformatted("Scale");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_gizmoMode == ImGuizmo::LOCAL ? "Local" : "World");
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();

            if (ImGui::SmallButton("Transform"))
            {
                ImGui::OpenPopup("TransformPopup");
            }
            if (ImGui::BeginPopup("TransformPopup"))
            {
                overlayPopupOpen = true;
                if (ImGui::MenuItem("Move", "W", m_gizmoOperation == ImGuizmo::TRANSLATE))
                {
                    m_gizmoOperation = ImGuizmo::TRANSLATE;
                }
                if (ImGui::MenuItem("Rotate", "E", m_gizmoOperation == ImGuizmo::ROTATE))
                {
                    m_gizmoOperation = ImGuizmo::ROTATE;
                }
                if (ImGui::MenuItem("Scale", "R", m_gizmoOperation == ImGuizmo::SCALE))
                {
                    m_gizmoOperation = ImGuizmo::SCALE;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Local Space", nullptr, m_gizmoMode == ImGuizmo::LOCAL))
                {
                    m_gizmoMode = ImGuizmo::LOCAL;
                }
                if (ImGui::MenuItem("World Space", nullptr, m_gizmoMode == ImGuizmo::WORLD))
                {
                    m_gizmoMode = ImGuizmo::WORLD;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Frame Selected", "F"))
                {
                    FrameSelectedEntity(EditorShell::GetInstance());
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
        }

        if (ImGui::SmallButton("View"))
        {
            ImGui::OpenPopup("ViewPopup");
        }
        if (ImGui::BeginPopup("ViewPopup"))
        {
            overlayPopupOpen = true;
            if (m_config.editorViewport)
            {
                ImGui::MenuItem("RHI Scene Geometry Preview", nullptr, &m_useRhiPreview, m_basicRenderer != nullptr);
                ImGui::TextDisabled("Active: %s RHI%s", m_activeRhiVulkan ? "Vulkan" : "OpenGL",
                                    m_activeRhiVulkan ? " (readback bridge)" : "");
                ImGui::TextDisabled("Visible commands: %zu, draws: %zu", m_rhiSceneCommandCount, m_rhiDrawCount);
                ImGui::TextDisabled("%s", m_vulkanStatus.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Vulkan device creation is working; viewport presentation is still being implemented.");
                ImGui::Separator();
                ImGui::MenuItem("Grid", nullptr, &m_showGrid);
                ImGui::MenuItem("Debug Shapes", nullptr, &m_showDebugShapes);
                ImGui::MenuItem("Navigation Mesh", nullptr, &m_showNavigation);
                ImGui::MenuItem("Selected Agent Path", nullptr, &m_showAgentPaths);
                ImGui::Separator();
            }
            ImGui::TextUnformatted("Debug View");
            ImGui::Separator();
            for (int viewIndex = 0; viewIndex < IM_ARRAYSIZE(kDebugViewLabels); ++viewIndex)
            {
                if (ImGui::MenuItem(kDebugViewLabels[viewIndex], nullptr, debugView == viewIndex))
                {
                    renderer.SetPostProcessDebugView(static_cast<render::PostProcessDebugView>(viewIndex));
                    debugView = viewIndex;
                }
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("Debug: %s", GetDebugViewLabel(static_cast<render::PostProcessDebugView>(debugView)));
        ImGui::SameLine();
        if (ImGui::SmallButton("Quality"))
        {
            ImGui::OpenPopup("QualityPopup");
        }
        if (ImGui::BeginPopup("QualityPopup"))
        {
            overlayPopupOpen = true;
            if (ImGui::SmallButton("Performance"))
            {
                m_renderScale = 0.75f;
                m_resizeStableFrames = kResizeDebounceFrames;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Balanced"))
            {
                m_renderScale = 0.85f;
                m_resizeStableFrames = kResizeDebounceFrames;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Native"))
            {
                m_renderScale = 1.0f;
                m_resizeStableFrames = kResizeDebounceFrames;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Supersample"))
            {
                m_renderScale = 1.5f;
                m_resizeStableFrames = kResizeDebounceFrames;
            }
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Render Scale", &m_renderScale, kMinRenderScale, kMaxRenderScale, "%.2fx"))
            {
                m_renderScale = glm::clamp(m_renderScale, kMinRenderScale, kMaxRenderScale);
                m_resizeStableFrames = kResizeDebounceFrames;
            }
            const int targetWidth = std::max(1, static_cast<int>(std::lround(viewportSize.x * m_renderScale)));
            const int targetHeight = std::max(1, static_cast<int>(std::lround(viewportSize.y * m_renderScale)));
            ImGui::TextDisabled("Target: %d x %d", targetWidth, targetHeight);
            const bool upscalingActive = m_renderScale < 0.999f;
            ImGui::TextDisabled("Upscaler: Spatial (%s)", upscalingActive ? "Active" : "Bypassed");
            ImGui::BeginDisabled(!upscalingActive);
            ImGui::SetNextItemWidth(180.0f);
            ImGui::SliderFloat("Sharpness", &m_upscaleSharpness, 0.0f, 1.0f, "%.2f");
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }

        if (m_config.editorViewport)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Snap"))
            {
                ImGui::OpenPopup("SnapPopup");
            }
            if (ImGui::BeginPopup("SnapPopup"))
            {
                overlayPopupOpen = true;
                ImGui::MenuItem("Enabled", nullptr, &m_enableSnap);
                ImGui::BeginDisabled(!m_enableSnap);
                ImGui::SetNextItemWidth(210.0f);
                ImGui::DragFloat3("Move", &m_translateSnap.x, 0.05f, 0.01f, 100.0f, "%.2f");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("Rotate", &m_rotateSnapDegrees, 0.5f, 0.1f, 180.0f, "%.1f deg");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("Scale", &m_scaleSnap, 0.01f, 0.01f, 10.0f, "%.2f");
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }

            if (auto *selectedEntity = EditorShell::GetInstance().GetSelectedEntity())
            {
                if (auto *terrainComponent = selectedEntity->GetComponent<scene::TerrainComponent>())
                {
                    if (terrainComponent->IsEnabled() && terrainComponent->IsPaintEnabled())
                    {
                        RenderTerrainPaintToolbar(*terrainComponent);
                    }
                }
                if (auto *foliageComponent = selectedEntity->GetComponent<scene::FoliageComponent>())
                {
                    if (foliageComponent->IsEnabled())
                    {
                        RenderFoliagePaintToolbar(*foliageComponent);
                    }
                }
                if (auto *meshComponent = selectedEntity->GetComponent<scene::MeshComponent>())
                {
                    auto *material = meshComponent->GetMaterial();
                    RenderTexturePaintToolbar(material ? material->GetConfig().albedoTexture : nullptr);
                }
                if (auto *terrainComponent = selectedEntity->GetComponent<scene::TerrainComponent>())
                {
                    auto *material = terrainComponent->GetMaterial();
                    RenderTexturePaintToolbar(material ? material->GetConfig().albedoTexture : nullptr);
                }
            }
        }

        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows) ||
                             overlayPopupOpen;
        m_settingsOverlayBottom = ImGui::GetWindowPos().y + ImGui::GetWindowSize().y;
        ImGui::End();
        ImGui::PopStyleVar(3);

        return hovered;
    }

    bool ViewportPanel::RenderViewSelectionGizmo(const ImVec2 &viewportMin, const ImVec2 &viewportSize)
    {
        auto &camera = EditorShell::GetInstance().GetEditorCamera();
        constexpr float gizmoRadius = 39.0f;
        constexpr float gizmoToolbarGap = 10.0f;
        const float defaultCenterY = viewportMin.y + 58.0f;
        const float toolbarCenterY = m_settingsOverlayBottom + gizmoToolbarGap + gizmoRadius;
        const float maxCenterY = viewportMin.y + viewportSize.y - gizmoRadius;
        const ImVec2 center(viewportMin.x + viewportSize.x - 58.0f,
                            std::min(std::max(defaultCenterY, toolbarCenterY), maxCenterY));
        constexpr float axisLength = 30.0f;
        constexpr float endpointRadius = 10.0f;

        glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(camera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        rotation = glm::rotate(rotation, glm::radians(camera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::mat3 viewRotation = glm::transpose(glm::mat3(rotation));

        struct AxisEndpoint
        {
            glm::vec3 direction;
            ImU32 color;
            const char *label;
        };
        const AxisEndpoint axes[] = {
            {{1.0f, 0.0f, 0.0f}, IM_COL32(235, 75, 75, 255), "X"},
            {{0.0f, 1.0f, 0.0f}, IM_COL32(90, 205, 105, 255), "Y"},
            {{0.0f, 0.0f, 1.0f}, IM_COL32(75, 135, 235, 255), "Z"},
            {{-1.0f, 0.0f, 0.0f}, IM_COL32(145, 55, 55, 255), "-X"},
            {{0.0f, -1.0f, 0.0f}, IM_COL32(55, 125, 65, 255), "-Y"},
            {{0.0f, 0.0f, -1.0f}, IM_COL32(45, 80, 145, 255), "-Z"},
        };

        auto *drawList = ImGui::GetWindowDrawList();
        drawList->AddCircleFilled(center, 39.0f, IM_COL32(25, 27, 34, 205));
        drawList->AddCircle(center, 39.0f, IM_COL32(110, 115, 130, 180), 0, 1.0f);

        struct ProjectedAxis { const AxisEndpoint *axis; ImVec2 endpoint; float depth; };
        ProjectedAxis projected[IM_ARRAYSIZE(axes)];
        for (int index = 0; index < IM_ARRAYSIZE(axes); ++index)
        {
            const glm::vec3 viewDirection = viewRotation * axes[index].direction;
            projected[index] = {&axes[index],
                                ImVec2(center.x + viewDirection.x * axisLength,
                                       center.y - viewDirection.y * axisLength),
                                viewDirection.z};
        }
        std::sort(std::begin(projected), std::end(projected),
                  [](const ProjectedAxis &left, const ProjectedAxis &right) { return left.depth < right.depth; });

        bool hovered = false;
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        for (const auto &item : projected)
        {
            drawList->AddLine(center, item.endpoint, item.axis->color, 2.5f);
            const float dx = mouse.x - item.endpoint.x;
            const float dy = mouse.y - item.endpoint.y;
            const bool endpointHovered = dx * dx + dy * dy <= endpointRadius * endpointRadius;
            hovered = hovered || endpointHovered;
            drawList->AddCircleFilled(item.endpoint, endpointRadius,
                                      endpointHovered ? IM_COL32(245, 245, 245, 255) : item.axis->color);
            const ImVec2 textSize = ImGui::CalcTextSize(item.axis->label);
            drawList->AddText(ImVec2(item.endpoint.x - textSize.x * 0.5f, item.endpoint.y - textSize.y * 0.5f),
                              endpointHovered ? IM_COL32(30, 30, 35, 255) : IM_COL32(255, 255, 255, 255),
                              item.axis->label);
            if (endpointHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const glm::vec3 direction = item.axis->direction;
                if (direction.x > 0.5f) { camera.yawDegrees = 90.0f; camera.pitchDegrees = 0.0f; }
                else if (direction.x < -0.5f) { camera.yawDegrees = -90.0f; camera.pitchDegrees = 0.0f; }
                else if (direction.y > 0.5f) { camera.yawDegrees = 0.0f; camera.pitchDegrees = -90.0f; }
                else if (direction.y < -0.5f) { camera.yawDegrees = 0.0f; camera.pitchDegrees = 90.0f; }
                else if (direction.z > 0.5f) { camera.yawDegrees = 0.0f; camera.pitchDegrees = 0.0f; }
                else { camera.yawDegrees = 180.0f; camera.pitchDegrees = 0.0f; }
                FrameSceneOrthographic(EditorShell::GetInstance(), viewportSize);
            }
        }
        return hovered;
    }

    void ViewportPanel::RenderEditorOverlays(const ImVec2 &viewportMin, const ImVec2 &viewportSize, bool viewportClicked, bool controlsHovered)
    {
        m_isTransformGizmoUsing = false;
        if (!m_renderTarget || !m_renderTarget->IsInitialized() || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return;
        }

        auto &editorShell = EditorShell::GetInstance();
        auto &editorCamera = editorShell.GetEditorCamera();
        const glm::mat4 cameraTransform = glm::translate(glm::mat4(1.0f), editorCamera.position) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(editorCamera.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(editorCamera.pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
        const render::CameraData freshCameraData = editorCamera.camera.GetCameraDataForTransform(cameraTransform,
                                                                                                 m_renderTarget->GetWidth(),
                                                                                                 m_renderTarget->GetHeight());
        const auto isFiniteMatrix = [](const glm::mat4 &matrix)
        {
            for (int column = 0; column < 4; ++column)
                if (glm::any(glm::isnan(matrix[column])) || glm::any(glm::isinf(matrix[column])))
                    return false;
            return true;
        };
        const bool cachedCameraValid = m_hasEditorCameraData &&
                                       isFiniteMatrix(m_editorCameraData.view) &&
                                       isFiniteMatrix(m_editorCameraData.projection);
        const render::CameraData cameraData = cachedCameraValid ? m_editorCameraData : freshCameraData;
        const float viewportAspect = viewportSize.x / viewportSize.y;
        const glm::mat4 gizmoProjection = editorCamera.orthographic
                                               ? glm::ortho(-editorCamera.orthographicSize * viewportAspect,
                                                            editorCamera.orthographicSize * viewportAspect,
                                                            -editorCamera.orthographicSize,
                                                            editorCamera.orthographicSize,
                                                            editorCamera.camera.GetNearPlane(),
                                                            editorCamera.camera.GetFarPlane())
                                               : glm::perspective(glm::radians(editorCamera.camera.GetFOV()),
                                                                  viewportAspect,
                                                                  editorCamera.camera.GetNearPlane(),
                                                                  editorCamera.camera.GetFarPlane());

        ImGuizmo::SetOrthographic(editorCamera.orthographic);
        ImGuizmo::Enable(true);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);
        bool gizmoBlocksSelection = false;
        bool splineHandleClicked = false;
        bool entityGizmoSubmitted = false;
        bool shapeHandleBlocksGizmo = false;

        if (m_showDebugShapes)
        {
            DrawEditorDebugShapes(editorShell.GetEngine().GetScene(), editorShell.GetSelectedEntity(), cameraData, viewportMin, viewportSize);
        }

        auto *activeScene = editorShell.GetEngine().GetScene();
        if (activeScene)
        {
            auto *selected = editorShell.GetSelectedEntity();
            if (selected && selected->GetComponent<scene::RectTransformComponent>())
            {
                activeScene->GetUISystem().RebuildLayout(*activeScene, glm::vec2(viewportSize.x, viewportSize.y));
                if (const auto *uiElement = activeScene->GetUISystem().FindElement(selected->GetID()))
                {
                    auto *drawList = ImGui::GetWindowDrawList();
                    const ImVec2 rectMin(viewportMin.x + uiElement->rect.min.x,
                                         viewportMin.y + viewportSize.y - uiElement->rect.max.y);
                    const ImVec2 rectMax(viewportMin.x + uiElement->rect.max.x,
                                         viewportMin.y + viewportSize.y - uiElement->rect.min.y);
                    drawList->PushClipRect(viewportMin,
                                           ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
                                           true);
                    drawList->AddRectFilled(rectMin, rectMax, IM_COL32(50, 145, 255, 22));
                    drawList->AddRect(rectMin, rectMax, IM_COL32(70, 175, 255, 245), 0.0f, 0, 1.5f);
                    constexpr float handleRadius = 4.0f;
                    drawList->AddCircleFilled(rectMin, handleRadius, IM_COL32(230, 245, 255, 255));
                    drawList->AddCircleFilled(rectMax, handleRadius, IM_COL32(230, 245, 255, 255));
                    drawList->AddCircleFilled(ImVec2(rectMax.x, rectMin.y), handleRadius, IM_COL32(230, 245, 255, 255));
                    drawList->AddCircleFilled(ImVec2(rectMin.x, rectMax.y), handleRadius, IM_COL32(230, 245, 255, 255));

                    const auto *rectTransform = selected->GetComponent<scene::RectTransformComponent>();
                    const ImVec2 pivot(rectMin.x + (rectMax.x - rectMin.x) * rectTransform->GetPivot().x,
                                       rectMax.y - (rectMax.y - rectMin.y) * rectTransform->GetPivot().y);
                    drawList->AddCircle(pivot, 6.0f, IM_COL32(255, 210, 70, 255), 0, 1.5f);
                    drawList->AddLine(ImVec2(pivot.x - 8.0f, pivot.y), ImVec2(pivot.x + 8.0f, pivot.y),
                                      IM_COL32(255, 210, 70, 255), 1.0f);
                    drawList->AddLine(ImVec2(pivot.x, pivot.y - 8.0f), ImVec2(pivot.x, pivot.y + 8.0f),
                                      IM_COL32(255, 210, 70, 255), 1.0f);
                    drawList->PopClipRect();
                }
            }
        }
        if (activeScene && (m_showNavigation || m_showAgentPaths))
        {
            auto *drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(viewportMin,
                                   ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y),
                                   true);

            if (m_showNavigation)
            {
                const auto drawNavigation = [&](const scene::NavigationSystem &navigation)
                {
                    const auto &points = navigation.GetDebugWalkablePoints();
                    const float halfCell = navigation.GetSettings().cellSize * 0.5f;
                    const std::size_t stride = std::max<std::size_t>(1, (points.size() + 9999) / 10000);
                    for (std::size_t index = 0; index < points.size(); index += stride)
                    {
                        const glm::vec3 center = points[index] + glm::vec3(0.0f, 0.035f, 0.0f);
                        const auto c0=ProjectWorldPoint(center+glm::vec3(-halfCell,0,-halfCell),cameraData,viewportMin,viewportSize);
                        const auto c1=ProjectWorldPoint(center+glm::vec3( halfCell,0,-halfCell),cameraData,viewportMin,viewportSize);
                        const auto c2=ProjectWorldPoint(center+glm::vec3( halfCell,0, halfCell),cameraData,viewportMin,viewportSize);
                        const auto c3=ProjectWorldPoint(center+glm::vec3(-halfCell,0, halfCell),cameraData,viewportMin,viewportSize);
                        if(!c0.visible||!c1.visible||!c2.visible||!c3.visible) continue;
                        drawList->AddQuadFilled(c0.screen,c1.screen,c2.screen,c3.screen,IM_COL32(35,205,125,72));
                        drawList->AddQuad(c0.screen,c1.screen,c2.screen,c3.screen,IM_COL32(70,245,165,135),0.75f);
                    }
                };
                const auto visit = [&](scene::Entity *entity, const auto &self) -> void
                {
                    if (auto *mesh = entity->GetComponent<scene::NavigationMeshComponent>()) drawNavigation(mesh->GetNavigation());
                    for (auto *child : entity->GetChildren()) self(child, self);
                };
                for (auto *root : activeScene->GetRootEntities()) visit(root, visit);
            }

            if (m_showAgentPaths)
            {
                if (auto *entity = editorShell.GetSelectedEntity())
                {
                    if (const auto *agent = entity->GetComponent<scene::NavAgentComponent>())
                    {
                        const auto &path = agent->GetPath();
                        const std::size_t nextPoint = agent->GetNextPathPointIndex();
                        if (agent->HasPath() && nextPoint < path.size())
                        {
                            DrawWorldLine(drawList, entity->GetWorldPosition() + glm::vec3(0.0f, 0.08f, 0.0f),
                                          path[nextPoint] + glm::vec3(0.0f, 0.08f, 0.0f), cameraData,
                                          viewportMin, viewportSize, IM_COL32(255, 195, 40, 245), 2.5f);
                            for (std::size_t pointIndex = nextPoint; pointIndex + 1 < path.size(); ++pointIndex)
                            {
                                DrawWorldLine(drawList, path[pointIndex] + glm::vec3(0.0f, 0.08f, 0.0f),
                                              path[pointIndex + 1] + glm::vec3(0.0f, 0.08f, 0.0f), cameraData,
                                              viewportMin, viewportSize, IM_COL32(255, 195, 40, 245), 2.5f);
                            }
                            for (std::size_t pointIndex = nextPoint; pointIndex < path.size(); ++pointIndex)
                            {
                                const auto &point = path[pointIndex];
                                const auto projected = ProjectWorldPoint(point + glm::vec3(0.0f, 0.08f, 0.0f),
                                                                         cameraData, viewportMin, viewportSize);
                                if (projected.visible)
                                    drawList->AddCircleFilled(projected.screen, 3.5f, IM_COL32(255, 225, 105, 255), 10);
                            }
                        }
                    }
                }
            }
            drawList->PopClipRect();
        }

        if (auto *selectedEntity = editorShell.GetSelectedEntity())
        {
            if (m_resizeHandleAxis >= 0 &&
                (m_resizeHandleEntityId != selectedEntity->GetID() ||
                 !ImGui::IsMouseDown(ImGuiMouseButton_Left)))
            {
                editorShell.EndSceneEdit();
                m_resizeHandleAxis = -1;
                m_resizeHandleEntityId = 0;
                m_resizeHandleTarget = 0;
            }
            if (m_splinePointEntity != selectedEntity)
            {
                if (m_isSplinePointGizmoUsing)
                {
                    editorShell.EndSceneEdit();
                    m_isSplinePointGizmoUsing = false;
                }
                m_splinePointEntity = selectedEntity;
                m_selectedSplinePoint = -1;
            }
            if (m_oceanPointEntity != selectedEntity)
            {
                if (m_isOceanPointGizmoUsing)
                {
                    editorShell.EndSceneEdit();
                    m_isOceanPointGizmoUsing = false;
                }
                m_oceanPointEntity = selectedEntity;
            }

            auto *splineComponent = selectedEntity->GetComponent<scene::SplineComponent>();
            auto *oceanComponent = selectedEntity->GetComponent<scene::OceanComponent>();
            if (!splineComponent || m_selectedSplineEntityId != selectedEntity->GetID())
            {
                if (m_splinePointEditActive)
                {
                    editorShell.EndSceneEdit();
                    m_splinePointEditActive = false;
                }
                m_selectedSplineEntityId = splineComponent ? selectedEntity->GetID() : 0;
                m_selectedSplinePointIndex = -1;
            }
            if (!oceanComponent || m_selectedOceanEntityId != selectedEntity->GetID())
            {
                if (m_oceanPointEditActive)
                {
                    editorShell.EndSceneEdit();
                    m_oceanPointEditActive = false;
                }
                m_selectedOceanEntityId = oceanComponent ? selectedEntity->GetID() : 0;
                m_selectedOceanAreaIndex = -1;
                m_selectedOceanPointIndex = -1;
            }

            // Collider and volume dimensions are component properties, not entity scale.
            // Six face handles make those authored bounds directly editable in the viewport.
            if (m_showDebugShapes && !splineComponent && !oceanComponent)
            {
                auto *collider = selectedEntity->GetComponent<scene::ColliderComponent>();
                auto *iblCapture = selectedEntity->GetComponent<scene::IblCaptureComponent>();
                auto *cloud = selectedEntity->GetComponent<scene::VolumetricCloudComponent>();
                auto *audioVolume = selectedEntity->GetComponent<scene::AudioEnvironmentVolumeComponent>();
                int resizeTarget = 0;
                glm::vec3 localCenter(0.0f);
                glm::vec3 authoredSize(1.0f);
                if (collider && collider->IsEnabled() &&
                    collider->GetShape() != scene::ColliderShape::Terrain &&
                    collider->GetShape() != scene::ColliderShape::Mesh)
                {
                    resizeTarget = 1;
                    localCenter = collider->GetCenter();
                    if (collider->GetShape() == scene::ColliderShape::Box)
                        authoredSize = collider->GetSize();
                    else if (collider->GetShape() == scene::ColliderShape::Sphere)
                        authoredSize = glm::vec3(collider->GetRadius() * 2.0f);
                    else
                        authoredSize = glm::vec3(collider->GetRadius() * 2.0f,
                                                 collider->GetHeight(),
                                                 collider->GetRadius() * 2.0f);
                }
                else if (iblCapture && iblCapture->IsEnabled())
                {
                    resizeTarget = 2;
                    authoredSize = iblCapture->GetSize();
                }
                else if (cloud && cloud->IsEnabled())
                {
                    resizeTarget = 3;
                    authoredSize = cloud->GetSize();
                }
                else if (audioVolume && audioVolume->IsEnabled())
                {
                    resizeTarget = 4;
                    authoredSize = audioVolume->GetShape() == scene::AudioEnvironmentShape::Box
                                       ? audioVolume->GetSize()
                                       : glm::vec3(audioVolume->GetRadius() * 2.0f);
                }

                if (resizeTarget != 0)
                {
                    const glm::mat4 boundsTransform = selectedEntity->GetWorldTransform() *
                        glm::translate(glm::mat4(1.0f), localCenter) *
                        glm::scale(glm::mat4(1.0f), authoredSize);
                    const glm::vec3 worldCenter(boundsTransform[3]);
                    const auto projectedCenter = ProjectWorldPoint(worldCenter, cameraData, viewportMin, viewportSize);
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    int hoveredAxis = -1;
                    int hoveredSign = 1;
                    float nearestDistanceSquared = 100.0f;
                    std::array<ProjectedPoint, 6> projectedHandles{};

                    for (int axis = 0; axis < 3; ++axis)
                    {
                        for (int signIndex = 0; signIndex < 2; ++signIndex)
                        {
                            const int sign = signIndex == 0 ? -1 : 1;
                            glm::vec3 localFace(0.0f);
                            localFace[axis] = static_cast<float>(sign) * 0.5f;
                            const int handleIndex = axis * 2 + signIndex;
                            projectedHandles[handleIndex] = ProjectWorldPoint(
                                glm::vec3(boundsTransform * glm::vec4(localFace, 1.0f)),
                                cameraData, viewportMin, viewportSize);
                            if (!projectedHandles[handleIndex].visible)
                                continue;
                            const float dx = mouse.x - projectedHandles[handleIndex].screen.x;
                            const float dy = mouse.y - projectedHandles[handleIndex].screen.y;
                            const float distanceSquared = dx * dx + dy * dy;
                            if (distanceSquared < nearestDistanceSquared)
                            {
                                nearestDistanceSquared = distanceSquared;
                                hoveredAxis = axis;
                                hoveredSign = sign;
                            }
                        }
                    }

                    const bool activeForSelection = m_resizeHandleAxis >= 0 &&
                                                    m_resizeHandleEntityId == selectedEntity->GetID() &&
                                                    m_resizeHandleTarget == resizeTarget;
                    shapeHandleBlocksGizmo = hoveredAxis >= 0 || activeForSelection;
                    gizmoBlocksSelection = gizmoBlocksSelection || shapeHandleBlocksGizmo;

                    auto *drawList = ImGui::GetWindowDrawList();
                    drawList->PushClipRect(viewportMin,
                                           ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);
                    for (int handleIndex = 0; handleIndex < 6; ++handleIndex)
                    {
                        if (!projectedHandles[handleIndex].visible)
                            continue;
                        const int axis = handleIndex / 2;
                        const int sign = (handleIndex & 1) == 0 ? -1 : 1;
                        const bool active = activeForSelection && axis == m_resizeHandleAxis && sign == m_resizeHandleSign;
                        const bool hovered = axis == hoveredAxis && sign == hoveredSign;
                        const ImVec2 point = projectedHandles[handleIndex].screen;
                        const float radius = active || hovered ? 6.0f : 4.5f;
                        drawList->AddCircleFilled(point, radius,
                                                  active ? IM_COL32(255, 215, 70, 255)
                                                         : hovered ? IM_COL32(245, 250, 255, 255)
                                                                   : IM_COL32(80, 205, 245, 245), 12);
                        drawList->AddCircle(point, radius, IM_COL32(25, 55, 70, 255), 12, 1.5f);
                    }
                    drawList->PopClipRect();

                    if (!activeForSelection && hoveredAxis >= 0 && viewportClicked &&
                        m_isViewportHovered && !controlsHovered)
                    {
                        const ProjectedPoint &handle = projectedHandles[hoveredAxis * 2 + (hoveredSign > 0 ? 1 : 0)];
                        const glm::vec2 screenAxis(handle.screen.x - projectedCenter.screen.x,
                                                   handle.screen.y - projectedCenter.screen.y);
                        const float halfPixels = glm::length(screenAxis);
                        if (projectedCenter.visible && halfPixels > 0.001f)
                        {
                            m_resizeHandleEntityId = selectedEntity->GetID();
                            m_resizeHandleTarget = resizeTarget;
                            m_resizeHandleAxis = hoveredAxis;
                            m_resizeHandleSign = hoveredSign;
                            m_resizeHandleStartMouse = glm::vec2(mouse.x, mouse.y);
                            m_resizeHandleScreenDirection = screenAxis / halfPixels;
                            m_resizeHandleStartSize = authoredSize;
                            m_resizeHandleStartHalfPixels = halfPixels;
                            editorShell.BeginSceneEdit(resizeTarget == 1 ? "Resize Collider" : "Resize Volume");
                            shapeHandleBlocksGizmo = true;
                            gizmoBlocksSelection = true;
                        }
                    }

                    if (activeForSelection)
                    {
                        const glm::vec2 mouseDelta = glm::vec2(mouse.x, mouse.y) - m_resizeHandleStartMouse;
                        const float signedPixelDelta = glm::dot(mouseDelta, m_resizeHandleScreenDirection);
                        glm::vec3 newSize = m_resizeHandleStartSize;
                        newSize[m_resizeHandleAxis] = std::max(
                            m_resizeHandleStartSize[m_resizeHandleAxis] *
                                (1.0f + signedPixelDelta / m_resizeHandleStartHalfPixels),
                            0.0001f);

                        if (resizeTarget == 1)
                        {
                            if (collider->GetShape() == scene::ColliderShape::Box)
                                collider->SetSize(newSize);
                            else if (collider->GetShape() == scene::ColliderShape::Sphere)
                                collider->SetRadius(newSize[m_resizeHandleAxis] * 0.5f);
                            else if (m_resizeHandleAxis == 1)
                                collider->SetHeight(newSize.y);
                            else
                                collider->SetRadius(newSize[m_resizeHandleAxis] * 0.5f);
                            selectedEntity->AddPrefabOverride(m_resizeHandleAxis == 1 && collider->GetShape() == scene::ColliderShape::Capsule
                                                                  ? "Component:ColliderComponent:Height"
                                                                  : collider->GetShape() == scene::ColliderShape::Box
                                                                        ? "Component:ColliderComponent:Size"
                                                                        : "Component:ColliderComponent:Radius");
                        }
                        else if (resizeTarget == 2)
                        {
                            iblCapture->SetSize(newSize);
                            iblCapture->MarkDirty();
                            selectedEntity->AddPrefabOverride("Component:IblCaptureComponent:Size");
                        }
                        else if (resizeTarget == 3)
                        {
                            cloud->SetSize(newSize);
                            selectedEntity->AddPrefabOverride("Component:VolumetricCloudComponent:Size");
                        }
                        else
                        {
                            if (audioVolume->GetShape() == scene::AudioEnvironmentShape::Box)
                            {
                                audioVolume->SetSize(newSize);
                                selectedEntity->AddPrefabOverride("Component:AudioEnvironmentVolumeComponent:Size");
                            }
                            else
                            {
                                audioVolume->SetRadius(newSize[m_resizeHandleAxis] * 0.5f);
                                selectedEntity->AddPrefabOverride("Component:AudioEnvironmentVolumeComponent:Radius");
                            }
                        }
                        editorShell.MarkSceneDirty();
                        m_isTransformGizmoUsing = true;
                    }
                }
            }

            bool splinePointClickConsumed = false;
            bool oceanPointClickConsumed = false;
            if (splineComponent)
            {
                const auto &points = splineComponent->GetPoints();
                if (m_selectedSplinePointIndex >= static_cast<int>(points.size()))
                {
                    m_selectedSplinePointIndex = -1;
                }
                const bool canPickPoint = viewportClicked && m_isViewportHovered && !controlsHovered && !ImGuizmo::IsUsing();
                const int pickedPoint = DrawAndPickSplineControlPoints(*selectedEntity,
                                                                       *splineComponent,
                                                                       m_selectedSplinePointIndex,
                                                                       cameraData,
                                                                       viewportMin,
                                                                       viewportSize,
                                                                       canPickPoint);
                if (pickedPoint >= 0)
                {
                    m_selectedSplinePointIndex = pickedPoint;
                    splinePointClickConsumed = true;
                }
            }

            bool terrainPaintActive = false;
            bool foliagePaintActive = false;
            bool texturePaintActive = false;
            static bool s_terrainStrokeActive = false;
            static bool s_foliageStrokeActive = false;
            static scene::EntityID s_foliageStrokeEntityId = 0;
            static scene::FoliageInstanceSnapshot s_foliageStrokeBefore;
            if (g_texturePaint.enabled)
            {
                const bool brushActive = m_isViewportHovered && !controlsHovered &&
                                         !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
                                         !ImGui::GetIO().KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
                // Exact UV raycasts are deliberately skipped while idle; on large
                // meshes this removes an otherwise needless triangle walk per frame.
                if (brushActive)
                {
                    if (const auto ray = BuildPickRay(cameraData, viewportMin, viewportSize))
                    {
                        std::optional<MeshUvHit> hit;
                        if (auto *meshComponent = selectedEntity->GetComponent<scene::MeshComponent>())
                            hit = RaycastMeshUv(*selectedEntity, *meshComponent, *ray);
                        if (!hit)
                        {
                            if (auto *terrain = selectedEntity->GetComponent<scene::TerrainComponent>())
                            {
                                glm::vec3 worldHit{0.0f};
                                auto *material = terrain->GetMaterial();
                                auto *texture = material ? material->GetConfig().albedoTexture : nullptr;
                                if (terrain->Raycast(ray->origin, ray->direction, worldHit))
                                {
                                    const glm::vec3 localHit(glm::inverse(selectedEntity->GetWorldTransform()) * glm::vec4(worldHit, 1.0f));
                                    const float width = std::max(1.0f, static_cast<float>(terrain->GetWidth() - 1) * terrain->GetCellSize());
                                    const float depth = std::max(1.0f, static_cast<float>(terrain->GetDepth() - 1) * terrain->GetCellSize());
                                    hit = MeshUvHit{{localHit.x / width, localHit.z / depth}, worldHit, texture,
                                                    glm::length(worldHit - ray->origin)};
                                }
                            }
                        }
                        if (hit)
                        {
                            texturePaintActive = true;
                            auto *meshComponent = selectedEntity->GetComponent<scene::MeshComponent>();
                            auto *terrainComponent = selectedEntity->GetComponent<scene::TerrainComponent>();
                            const bool alreadyPrivate = hit->texture &&
                                                        hit->texture->GetFilePath().find("PaintedTextures") != std::string::npos;
                            if (!alreadyPrivate)
                            {
                                auto *privateTexture = CreatePrivatePaintTexture(*selectedEntity,
                                                                                 meshComponent,
                                                                                 meshComponent ? nullptr : terrainComponent,
                                                                                 hit->submeshIndex,
                                                                                 hit->texture);
                                if (!privateTexture)
                                    return;
                                hit->texture = privateTexture;
                            }
                            if (g_texturePaint.texture != hit->texture || !g_texturePaint.painter)
                            {
                                g_texturePaint.texture = hit->texture;
                                g_texturePaint.painter = std::make_unique<render::TexturePainter>(*hit->texture);
                            }
                            g_texturePaint.painter->SetBrushTexture(g_texturePaint.brushTexture);
                            g_texturePaint.painter->SetBrushTextureScale(g_texturePaint.brushTextureScale);
                            if (!g_texturePaint.strokeActive)
                            {
                                editorShell.BeginSceneEdit("Paint Texture");
                                g_texturePaint.strokeActive = true;
                            }
                            if (g_texturePaint.painter->Paint(hit->uv, g_texturePaint.radiusPixels,
                                                              g_texturePaint.color, g_texturePaint.opacity))
                                editorShell.MarkSceneDirty();
                        }
                    }
                }
            }
            if (auto *terrainComponent = selectedEntity->GetComponent<scene::TerrainComponent>())
            {
                auto *foliageComponent = selectedEntity->GetComponent<scene::FoliageComponent>();
                const bool canTerrainPaint = terrainComponent->IsEnabled() && terrainComponent->IsPaintEnabled() && !g_texturePaint.enabled;
                const bool canFoliagePaint = terrainComponent->IsEnabled() &&
                                             foliageComponent &&
                                             foliageComponent->IsEnabled() &&
                                             foliageComponent->IsPaintEnabled();
                if (canTerrainPaint || canFoliagePaint)
                {
                    if (const auto ray = BuildPickRay(cameraData, viewportMin, viewportSize))
                    {
                        glm::vec3 hitPoint{0.0f};
                        if (terrainComponent->Raycast(ray->origin, ray->direction, hitPoint))
                        {
                            const glm::mat4 worldTransform = selectedEntity->GetWorldTransform();
                            // Brush radii are authored and applied in terrain-local units. Preserve the
                            // owner's scale here so the preview covers the same local-space footprint.
                            const glm::vec3 right = glm::vec3(worldTransform[0]);
                            const glm::vec3 forward = glm::vec3(worldTransform[2]);
                            auto *drawList = ImGui::GetWindowDrawList();
                            drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);
                            DrawWorldCircle(drawList,
                                            hitPoint,
                                            right * (canFoliagePaint ? foliageComponent->GetBrushRadius() : terrainComponent->GetBrushRadius()),
                                            forward * (canFoliagePaint ? foliageComponent->GetBrushRadius() : terrainComponent->GetBrushRadius()),
                                            cameraData,
                                            viewportMin,
                                            viewportSize,
                                            canFoliagePaint ? IM_COL32(100, 180, 255, 230) : IM_COL32(120, 220, 140, 230));
                            drawList->PopClipRect();

                            const bool brushActive = m_isViewportHovered &&
                                                     !ImGuizmo::IsOver() &&
                                                     !ImGuizmo::IsUsing() &&
                                                     !ImGui::GetIO().KeyAlt && ImGui::IsMouseDown(ImGuiMouseButton_Left);
                            terrainPaintActive = canTerrainPaint && brushActive && !canFoliagePaint;
                            foliagePaintActive = canFoliagePaint && brushActive;
                            if (terrainPaintActive && !s_terrainStrokeActive)
                            {
                                editorShell.BeginSceneEdit("Paint Terrain");
                                s_terrainStrokeActive = true;
                            }
                            if (foliagePaintActive && !s_foliageStrokeActive)
                            {
                                s_foliageStrokeEntityId = selectedEntity->GetID();
                                s_foliageStrokeBefore = foliageComponent->CaptureInstanceSnapshot();
                                s_foliageStrokeActive = true;
                            }
                            if (terrainPaintActive && terrainComponent->PaintAtWorldPosition(hitPoint, ImGui::GetIO().DeltaTime))
                            {
                                editorShell.MarkSceneDirty();
                            }
                            if (foliagePaintActive &&
                                foliageComponent->ApplyBrushAtWorldPosition(
                                    hitPoint,
                                    glm::vec3(worldTransform[1]),
                                    [terrainComponent](float localX, float localZ)
                                    {
                                        return terrainComponent->GetHeightAtLocalPosition(localX, localZ);
                                    }))
                            {
                                selectedEntity->AddPrefabOverride("Component:FoliageComponent:Instances");
                                editorShell.MarkSceneDirty();
                            }
                        }
                    }
                }
            }

            if (s_terrainStrokeActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                editorShell.EndSceneEdit();
                s_terrainStrokeActive = false;
            }
            if (s_foliageStrokeActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                scene::FoliageInstanceSnapshot after;
                if (auto *scene = editorShell.GetScene())
                    if (auto *entity = scene->FindEntityByID(s_foliageStrokeEntityId))
                        if (auto *foliage = entity->GetComponent<scene::FoliageComponent>())
                            after = foliage->CaptureInstanceSnapshot();

                if (!after.empty() && s_foliageStrokeBefore != after)
                {
                    const auto entityId = s_foliageStrokeEntityId;
                    const std::size_t retainedBytes = (s_foliageStrokeBefore.size() + after.size()) * sizeof(std::vector<scene::FoliageInstance>) +
                                                      [&]()
                                                      {
                                                          std::size_t bytes = 0;
                                                          for (const auto &instances : s_foliageStrokeBefore) bytes += instances.size() * sizeof(scene::FoliageInstance);
                                                          for (const auto &instances : after) bytes += instances.size() * sizeof(scene::FoliageInstance);
                                                          return bytes;
                                                      }();
                    editorShell.PushSceneEditCommand(
                        "Paint Foliage",
                        [entityId, snapshot = std::move(s_foliageStrokeBefore)]()
                        {
                            auto *scene = EditorShell::GetInstance().GetScene();
                            auto *entity = scene ? scene->FindEntityByID(entityId) : nullptr;
                            auto *foliage = entity ? entity->GetComponent<scene::FoliageComponent>() : nullptr;
                            if (!foliage) return false;
                            foliage->RestoreInstanceSnapshot(snapshot);
                            EditorShell::GetInstance().MarkSceneDirty();
                            return true;
                        },
                        [entityId, snapshot = std::move(after)]()
                        {
                            auto *scene = EditorShell::GetInstance().GetScene();
                            auto *entity = scene ? scene->FindEntityByID(entityId) : nullptr;
                            auto *foliage = entity ? entity->GetComponent<scene::FoliageComponent>() : nullptr;
                            if (!foliage) return false;
                            foliage->RestoreInstanceSnapshot(snapshot);
                            EditorShell::GetInstance().MarkSceneDirty();
                            return true;
                        },
                        retainedBytes);
                }
                s_foliageStrokeBefore.clear();
                s_foliageStrokeEntityId = 0;
                s_foliageStrokeActive = false;
            }
            if (g_texturePaint.strokeActive && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (g_texturePaint.painter)
                {
                    g_texturePaint.painter->EndStroke();
                    g_texturePaint.painter->Save();
                }
                editorShell.EndSceneEdit();
                g_texturePaint.strokeActive = false;
            }

            if (terrainPaintActive || foliagePaintActive || texturePaintActive)
            {
                if (viewportClicked)
                {
                    editorShell.Log(EditorShell::ConsoleSeverity::Info, "[Pick] blocked=paint-active");
                }
                return;
            }

            const glm::mat4 entityWorldTransform = selectedEntity->GetWorldTransform();
            glm::mat4 entityTransform = entityWorldTransform;
            glm::mat4 entityGizmoDelta(1.0f);
            bool entityGizmoUsesBoundsCenter = false;
            const auto constrainOrthographicTranslation = [&](glm::mat4 &transform, const glm::vec3 &originalPosition)
            {
                if (!editorCamera.orthographic || m_gizmoOperation != ImGuizmo::TRANSLATE)
                {
                    return;
                }

                const glm::vec3 cameraForward = -glm::normalize(glm::vec3(glm::inverse(cameraData.view)[2]));
                const glm::vec3 absoluteForward = glm::abs(cameraForward);
                if (absoluteForward.x >= absoluteForward.y && absoluteForward.x >= absoluteForward.z)
                    transform[3].x = originalPosition.x;
                else if (absoluteForward.y >= absoluteForward.z)
                    transform[3].y = originalPosition.y;
                else
                    transform[3].z = originalPosition.z;
            };

            const auto submitEntityGizmo = [&](float *gizmoSnap)
            {
                const glm::mat4 gizmoInputTransform = entityTransform;
                auto *drawList = ImGui::GetWindowDrawList();
                const int vertexCountBefore = drawList->VtxBuffer.Size;
                ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                                     glm::value_ptr(gizmoProjection),
                                     m_gizmoOperation,
                                     m_gizmoMode,
                                     glm::value_ptr(entityTransform),
                                     glm::value_ptr(entityGizmoDelta),
                                     gizmoSnap);
                constrainOrthographicTranslation(entityTransform, glm::vec3(gizmoInputTransform[3]));
                entityGizmoDelta = entityTransform * glm::inverse(gizmoInputTransform);
                if (drawList->VtxBuffer.Size > vertexCountBefore)
                    return;

                auto *meshComponent = selectedEntity->GetComponent<scene::MeshComponent>();
                const auto *mesh = meshComponent ? meshComponent->GetMesh() : nullptr;
                if (!mesh)
                    return;

                glm::vec3 localCenter = mesh->GetBounds().center;
                const int submeshIndex = meshComponent->GetSubmeshIndex();
                if (submeshIndex >= 0 && static_cast<std::size_t>(submeshIndex) < mesh->GetSubmeshCount())
                    localCenter = mesh->GetSubmesh(static_cast<std::size_t>(submeshIndex)).bounds.center;

                entityTransform = entityWorldTransform;
                entityTransform[3] = entityWorldTransform * glm::vec4(localCenter, 1.0f);
                const glm::mat4 boundsInputTransform = entityTransform;
                entityGizmoDelta = glm::mat4(1.0f);
                entityGizmoUsesBoundsCenter = true;
                ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                                     glm::value_ptr(gizmoProjection),
                                     m_gizmoOperation,
                                     m_gizmoMode,
                                     glm::value_ptr(entityTransform),
                                     glm::value_ptr(entityGizmoDelta),
                                     gizmoSnap);
                constrainOrthographicTranslation(entityTransform, glm::vec3(boundsInputTransform[3]));
                entityGizmoDelta = entityTransform * glm::inverse(boundsInputTransform);
            };

            if (oceanComponent && oceanComponent->IsEnabled())
            {
                const auto &areas = oceanComponent->GetAreas();
                if (m_selectedOceanAreaIndex >= static_cast<int>(areas.size()))
                {
                    m_selectedOceanAreaIndex = -1;
                    m_selectedOceanPointIndex = -1;
                }
                else if (m_selectedOceanAreaIndex >= 0)
                {
                    const auto &selectedArea = areas[static_cast<std::size_t>(m_selectedOceanAreaIndex)].points;
                    if (m_selectedOceanPointIndex >= static_cast<int>(selectedArea.size()))
                    {
                        m_selectedOceanPointIndex = -1;
                    }
                }
            }

            const bool editingSplinePoint = splineComponent &&
                                            m_selectedSplinePointIndex >= 0 &&
                                            m_selectedSplinePointIndex < static_cast<int>(splineComponent->GetPoints().size());
            const auto hasValidSelectedOceanPoint = [&]()
            {
                if (!oceanComponent || !oceanComponent->IsEnabled() ||
                    m_selectedOceanAreaIndex < 0 ||
                    m_selectedOceanPointIndex < 0)
                {
                    return false;
                }

                const auto &areas = oceanComponent->GetAreas();
                if (m_selectedOceanAreaIndex >= static_cast<int>(areas.size()))
                {
                    return false;
                }

                const auto &selectedArea = areas[static_cast<std::size_t>(m_selectedOceanAreaIndex)].points;
                return m_selectedOceanPointIndex < static_cast<int>(selectedArea.size());
            };
            if (editingSplinePoint)
            {
                if (m_gizmoOperation == ImGuizmo::SCALE || m_gizmoOperation == ImGuizmo::BOUNDS)
                {
                    m_gizmoOperation = ImGuizmo::TRANSLATE;
                }
                const auto &point = splineComponent->GetPoints()[static_cast<std::size_t>(m_selectedSplinePointIndex)];
                const glm::vec3 rotationRadians = glm::radians(point.rotation);
                const glm::mat4 pointTransform = glm::translate(glm::mat4(1.0f), point.position) *
                                                 BuildSplinePointFrame(*splineComponent, static_cast<std::size_t>(m_selectedSplinePointIndex)) *
                                                 glm::eulerAngleXYZ(rotationRadians.x, rotationRadians.y, rotationRadians.z);
                entityTransform *= pointTransform;
            }
            else if (hasValidSelectedOceanPoint())
            {
                if (m_gizmoOperation == ImGuizmo::SCALE || m_gizmoOperation == ImGuizmo::BOUNDS || m_gizmoOperation == ImGuizmo::ROTATE)
                {
                    m_gizmoOperation = ImGuizmo::TRANSLATE;
                }
            }
            float *snapValues = nullptr;
            if (m_enableSnap)
            {
                switch (m_gizmoOperation)
                {
                case ImGuizmo::TRANSLATE:
                    snapValues = &m_translateSnap.x;
                    break;
                case ImGuizmo::ROTATE:
                    snapValues = &m_rotateSnapDegrees;
                    break;
                case ImGuizmo::SCALE:
                    snapValues = &m_scaleSnap;
                    break;
                case ImGuizmo::BOUNDS:
                default:
                    break;
                }
            }
            if (oceanComponent && oceanComponent->IsEnabled())
            {
                const auto &areas = oceanComponent->GetAreas();

                auto *drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

                constexpr float kOceanPointRadius = 7.0f;
                constexpr float kOceanPointHitRadius = 11.0f;
                constexpr float kOceanInsertRadius = 6.0f;
                constexpr float kOceanInsertHitRadius = 10.0f;
                const ImVec2 mousePosition = ImGui::GetIO().MousePos;
                int hoveredAreaIndex = -1;
                int hoveredPointIndex = -1;
                float nearestPointDistanceSquared = kOceanPointHitRadius * kOceanPointHitRadius;
                int hoveredInsertAreaIndex = -1;
                int hoveredInsertSegmentIndex = -1;
                float nearestInsertDistanceSquared = kOceanInsertHitRadius * kOceanInsertHitRadius;

                for (std::size_t areaIndex = 0; areaIndex < areas.size(); ++areaIndex)
                {
                    const auto &points = areas[areaIndex].points;
                    if (points.size() < 2)
                    {
                        continue;
                    }

                    std::vector<glm::vec3> worldPoints;
                    worldPoints.reserve(points.size());
                    for (const auto &point : points)
                    {
                        worldPoints.push_back(glm::vec3(entityWorldTransform * glm::vec4(point.x, 0.0f, point.y, 1.0f)));
                    }

                    for (std::size_t pointIndex = 0; pointIndex < worldPoints.size(); ++pointIndex)
                    {
                        const std::size_t nextPointIndex = (pointIndex + 1) % worldPoints.size();
                        DrawWorldLine(drawList,
                                      worldPoints[pointIndex],
                                      worldPoints[nextPointIndex],
                                      cameraData,
                                      viewportMin,
                                      viewportSize,
                                      oceanComponent->GetInvertAreaMask() ? IM_COL32(70, 200, 255, 220) : IM_COL32(40, 140, 220, 220),
                                      2.0f);
                    }

                    for (std::size_t pointIndex = 0; pointIndex < worldPoints.size(); ++pointIndex)
                    {
                        const ProjectedPoint projected = ProjectWorldPoint(worldPoints[pointIndex], cameraData, viewportMin, viewportSize);
                        if (!projected.visible)
                        {
                            continue;
                        }

                        const float dx = projected.screen.x - mousePosition.x;
                        const float dy = projected.screen.y - mousePosition.y;
                        const float distanceSquared = dx * dx + dy * dy;
                        if (distanceSquared <= nearestPointDistanceSquared)
                        {
                            nearestPointDistanceSquared = distanceSquared;
                            hoveredAreaIndex = static_cast<int>(areaIndex);
                            hoveredPointIndex = static_cast<int>(pointIndex);
                        }

                        const bool selected = static_cast<int>(areaIndex) == m_selectedOceanAreaIndex &&
                                              static_cast<int>(pointIndex) == m_selectedOceanPointIndex;
                        const bool hovered = static_cast<int>(areaIndex) == hoveredAreaIndex &&
                                             static_cast<int>(pointIndex) == hoveredPointIndex;
                        const ImU32 fillColor = selected  ? IM_COL32(95, 215, 255, 255)
                                                : hovered ? IM_COL32(170, 235, 255, 255)
                                                          : IM_COL32(45, 155, 230, 245);
                        drawList->AddCircleFilled(projected.screen, selected ? 8.5f : kOceanPointRadius, fillColor, 16);
                        drawList->AddCircle(projected.screen, selected ? 8.5f : kOceanPointRadius, IM_COL32(10, 25, 35, 255), 16, 2.0f);

                        const std::string label = std::to_string(areaIndex) + ":" + std::to_string(pointIndex);
                        drawList->AddText(ImVec2(projected.screen.x + 10.0f, projected.screen.y - 9.0f), IM_COL32(220, 245, 255, 255), label.c_str());
                    }

                    for (std::size_t segmentIndex = 0; segmentIndex < worldPoints.size(); ++segmentIndex)
                    {
                        const std::size_t nextPointIndex = (segmentIndex + 1) % worldPoints.size();
                        const glm::vec3 midpoint = (worldPoints[segmentIndex] + worldPoints[nextPointIndex]) * 0.5f;
                        const ProjectedPoint projected = ProjectWorldPoint(midpoint, cameraData, viewportMin, viewportSize);
                        if (!projected.visible || hoveredPointIndex >= 0)
                        {
                            continue;
                        }

                        const float dx = projected.screen.x - mousePosition.x;
                        const float dy = projected.screen.y - mousePosition.y;
                        const float distanceSquared = dx * dx + dy * dy;
                        if (distanceSquared <= nearestInsertDistanceSquared)
                        {
                            nearestInsertDistanceSquared = distanceSquared;
                            hoveredInsertAreaIndex = static_cast<int>(areaIndex);
                            hoveredInsertSegmentIndex = static_cast<int>(segmentIndex);
                        }

                        const bool hoveredInsert = static_cast<int>(areaIndex) == hoveredInsertAreaIndex &&
                                                   static_cast<int>(segmentIndex) == hoveredInsertSegmentIndex;
                        const float radius = hoveredInsert ? 7.5f : kOceanInsertRadius;
                        drawList->AddCircleFilled(projected.screen, radius,
                                                  hoveredInsert ? IM_COL32(100, 220, 255, 255) : IM_COL32(35, 70, 90, 225), 16);
                        drawList->AddCircle(projected.screen, radius, IM_COL32(180, 240, 255, 245), 16, 1.5f);
                        drawList->AddLine(ImVec2(projected.screen.x - 3.0f, projected.screen.y), ImVec2(projected.screen.x + 3.0f, projected.screen.y), IM_COL32(235, 255, 255, 255), 1.5f);
                        drawList->AddLine(ImVec2(projected.screen.x, projected.screen.y - 3.0f), ImVec2(projected.screen.x, projected.screen.y + 3.0f), IM_COL32(235, 255, 255, 255), 1.5f);
                    }
                }

                drawList->PopClipRect();

                const bool insertPointClicked = viewportClicked && m_isViewportHovered && !controlsHovered && hoveredInsertAreaIndex >= 0 && hoveredInsertSegmentIndex >= 0;
                if (!insertPointClicked && viewportClicked && m_isViewportHovered && !controlsHovered && hoveredAreaIndex >= 0 && hoveredPointIndex >= 0)
                {
                    m_selectedOceanAreaIndex = hoveredAreaIndex;
                    m_selectedOceanPointIndex = hoveredPointIndex;
                    splineHandleClicked = true;
                    oceanPointClickConsumed = true;
                }

                if (insertPointClicked)
                {
                    const auto &points = areas[static_cast<std::size_t>(hoveredInsertAreaIndex)].points;
                    const std::size_t startPointIndex = static_cast<std::size_t>(hoveredInsertSegmentIndex);
                    const std::size_t nextPointIndex = (startPointIndex + 1) % points.size();
                    const std::size_t insertionIndex = startPointIndex + 1;
                    const glm::vec2 newPoint = (points[startPointIndex] + points[nextPointIndex]) * 0.5f;
                    editorShell.ExecuteSceneEdit("Insert Ocean Area Point", [oceanComponent, selectedEntity, hoveredInsertAreaIndex, insertionIndex, newPoint]()
                                                 {
                                                     oceanComponent->InsertPoint(static_cast<std::size_t>(hoveredInsertAreaIndex), insertionIndex, newPoint);
                                                     selectedEntity->AddPrefabOverride("Component:OceanComponent:Areas." + std::to_string(hoveredInsertAreaIndex) + ".PointCount"); });
                    m_selectedOceanAreaIndex = hoveredInsertAreaIndex;
                    m_selectedOceanPointIndex = static_cast<int>(insertionIndex);
                    splineHandleClicked = true;
                    return;
                }

                if (oceanPointClickConsumed)
                {
                    return;
                }
            }
            if (splineComponent && splineComponent->IsEnabled())
            {
                const auto &points = splineComponent->GetPoints();
                if (m_selectedSplinePoint >= static_cast<int>(points.size()))
                {
                    m_selectedSplinePoint = -1;
                }

                auto *drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(viewportMin, ImVec2(viewportMin.x + viewportSize.x, viewportMin.y + viewportSize.y), true);

                std::vector<glm::vec3> worldPoints;
                worldPoints.reserve(points.size());
                for (const auto &point : points)
                {
                    worldPoints.push_back(glm::vec3(entityWorldTransform * glm::vec4(point.position, 1.0f)));
                }

                for (std::size_t pointIndex = 1; pointIndex < worldPoints.size(); ++pointIndex)
                {
                    DrawWorldLine(drawList, worldPoints[pointIndex - 1], worldPoints[pointIndex], cameraData, viewportMin, viewportSize,
                                  IM_COL32(255, 190, 70, 210), 2.0f);
                }
                if (splineComponent->IsClosed() && worldPoints.size() > 2)
                {
                    DrawWorldLine(drawList, worldPoints.back(), worldPoints.front(), cameraData, viewportMin, viewportSize,
                                  IM_COL32(255, 190, 70, 210), 2.0f);
                }

                constexpr float kSplinePointRadius = 7.0f;
                constexpr float kSplinePointHitRadius = 11.0f;
                constexpr float kSplineInsertRadius = 6.0f;
                constexpr float kSplineInsertHitRadius = 10.0f;
                int hoveredPoint = -1;
                float nearestPointDistanceSquared = kSplinePointHitRadius * kSplinePointHitRadius;
                const ImVec2 mousePosition = ImGui::GetIO().MousePos;
                std::vector<ProjectedPoint> projectedPoints;
                projectedPoints.reserve(worldPoints.size());
                for (std::size_t pointIndex = 0; pointIndex < worldPoints.size(); ++pointIndex)
                {
                    const ProjectedPoint projected = ProjectWorldPoint(worldPoints[pointIndex], cameraData, viewportMin, viewportSize);
                    projectedPoints.push_back(projected);
                    if (!projected.visible)
                    {
                        continue;
                    }

                    const float dx = projected.screen.x - mousePosition.x;
                    const float dy = projected.screen.y - mousePosition.y;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared <= nearestPointDistanceSquared)
                    {
                        nearestPointDistanceSquared = distanceSquared;
                        hoveredPoint = static_cast<int>(pointIndex);
                    }
                }

                int hoveredInsertSegment = -1;
                std::vector<ProjectedPoint> projectedInsertPoints;
                const std::size_t segmentCount = splineComponent->IsClosed() && worldPoints.size() > 2
                                                     ? worldPoints.size()
                                                 : worldPoints.size() > 1 ? worldPoints.size() - 1
                                                                          : 0;
                projectedInsertPoints.reserve(segmentCount);
                float nearestInsertDistanceSquared = kSplineInsertHitRadius * kSplineInsertHitRadius;
                for (std::size_t segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex)
                {
                    const std::size_t nextPointIndex = (segmentIndex + 1) % worldPoints.size();
                    const glm::vec3 midpoint = (worldPoints[segmentIndex] + worldPoints[nextPointIndex]) * 0.5f;
                    const ProjectedPoint projected = ProjectWorldPoint(midpoint, cameraData, viewportMin, viewportSize);
                    projectedInsertPoints.push_back(projected);
                    if (!projected.visible || hoveredPoint >= 0)
                    {
                        continue;
                    }

                    const float dx = projected.screen.x - mousePosition.x;
                    const float dy = projected.screen.y - mousePosition.y;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared <= nearestInsertDistanceSquared)
                    {
                        nearestInsertDistanceSquared = distanceSquared;
                        hoveredInsertSegment = static_cast<int>(segmentIndex);
                    }
                }

                const bool insertPointClicked = viewportClicked && m_isViewportHovered && !controlsHovered && hoveredInsertSegment >= 0;
                if (!insertPointClicked && viewportClicked && m_isViewportHovered && !controlsHovered && hoveredPoint >= 0)
                {
                    m_selectedSplinePoint = hoveredPoint;
                    splineHandleClicked = true;
                }

                for (std::size_t segmentIndex = 0; segmentIndex < projectedInsertPoints.size(); ++segmentIndex)
                {
                    if (!projectedInsertPoints[segmentIndex].visible)
                    {
                        continue;
                    }
                    const bool hoveredInsert = static_cast<int>(segmentIndex) == hoveredInsertSegment;
                    const ImVec2 center = projectedInsertPoints[segmentIndex].screen;
                    const float radius = hoveredInsert ? 7.5f : kSplineInsertRadius;
                    drawList->AddCircleFilled(center, radius,
                                              hoveredInsert ? IM_COL32(100, 220, 135, 255) : IM_COL32(40, 75, 55, 225), 16);
                    drawList->AddCircle(center, radius, IM_COL32(180, 255, 195, 245), 16, 1.5f);
                    drawList->AddLine(ImVec2(center.x - 3.0f, center.y), ImVec2(center.x + 3.0f, center.y),
                                      IM_COL32(235, 255, 240, 255), 1.5f);
                    drawList->AddLine(ImVec2(center.x, center.y - 3.0f), ImVec2(center.x, center.y + 3.0f),
                                      IM_COL32(235, 255, 240, 255), 1.5f);
                }

                for (std::size_t pointIndex = 0; pointIndex < projectedPoints.size(); ++pointIndex)
                {
                    if (!projectedPoints[pointIndex].visible)
                    {
                        continue;
                    }
                    const bool selected = static_cast<int>(pointIndex) == m_selectedSplinePoint;
                    const bool hoveredPointHandle = static_cast<int>(pointIndex) == hoveredPoint;
                    const ImU32 fillColor = selected             ? IM_COL32(255, 215, 70, 255)
                                            : hoveredPointHandle ? IM_COL32(255, 235, 150, 255)
                                                                 : IM_COL32(245, 145, 45, 245);
                    drawList->AddCircleFilled(projectedPoints[pointIndex].screen, selected ? 8.5f : kSplinePointRadius, fillColor, 16);
                    drawList->AddCircle(projectedPoints[pointIndex].screen, selected ? 8.5f : kSplinePointRadius,
                                        IM_COL32(35, 25, 15, 255), 16, 2.0f);

                    const std::string pointLabel = std::to_string(pointIndex);
                    const ImVec2 labelPosition(projectedPoints[pointIndex].screen.x + 10.0f,
                                               projectedPoints[pointIndex].screen.y - 9.0f);
                    drawList->AddText(ImVec2(labelPosition.x + 1.0f, labelPosition.y + 1.0f),
                                      IM_COL32(15, 10, 5, 240), pointLabel.c_str());
                    drawList->AddText(labelPosition, IM_COL32(255, 245, 220, 255), pointLabel.c_str());
                }
                drawList->PopClipRect();

                if (insertPointClicked)
                {
                    const std::size_t startPointIndex = static_cast<std::size_t>(hoveredInsertSegment);
                    const std::size_t nextPointIndex = (startPointIndex + 1) % points.size();
                    const std::size_t insertionIndex = startPointIndex + 1;
                    const glm::vec3 newPointPosition = (points[startPointIndex].position + points[nextPointIndex].position) * 0.5f;
                    editorShell.ExecuteSceneEdit("Insert Spline Point", [splineComponent, selectedEntity, insertionIndex, newPointPosition]()
                                                 {
                                                     splineComponent->InsertPoint(insertionIndex, newPointPosition);
                                                     selectedEntity->AddPrefabOverride("Component:SplineComponent:PointCount"); });
                    m_selectedSplinePoint = static_cast<int>(insertionIndex);
                    splineHandleClicked = true;
                    return;
                }

                if (m_selectedSplinePoint >= 0)
                {
                    glm::mat4 pointTransform = entityWorldTransform;
                    pointTransform[3] = glm::vec4(worldPoints[static_cast<std::size_t>(m_selectedSplinePoint)], 1.0f);
                    const glm::vec3 originalPointPosition(pointTransform[3]);
                    ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                                         glm::value_ptr(gizmoProjection),
                                         ImGuizmo::TRANSLATE,
                                         m_gizmoMode,
                                         glm::value_ptr(pointTransform),
                                         nullptr,
                                         m_enableSnap ? &m_translateSnap.x : nullptr);
                    constrainOrthographicTranslation(pointTransform, originalPointPosition);

                    const bool pointGizmoUsing = ImGuizmo::IsUsing();
                    const bool pointGizmoHovered = ImGuizmo::IsOver();
                    gizmoBlocksSelection = pointGizmoUsing || pointGizmoHovered || splineHandleClicked;
                    if (pointGizmoUsing && !m_isSplinePointGizmoUsing)
                    {
                        editorShell.BeginSceneEdit("Move Spline Point");
                    }
                    if (pointGizmoUsing)
                    {
                        m_isTransformGizmoUsing = true;
                        const glm::vec3 worldPosition(pointTransform[3]);
                        const glm::vec3 localPosition(glm::inverse(entityWorldTransform) * glm::vec4(worldPosition, 1.0f));
                        splineComponent->SetPointPosition(static_cast<std::size_t>(m_selectedSplinePoint), localPosition);
                        selectedEntity->AddPrefabOverride("Component:SplineComponent:Points." + std::to_string(m_selectedSplinePoint));
                        editorShell.MarkSceneDirty();
                    }
                    if (!pointGizmoUsing && m_isSplinePointGizmoUsing)
                    {
                        editorShell.EndSceneEdit();
                    }
                    m_isSplinePointGizmoUsing = pointGizmoUsing;

                    if (viewportClicked && !splineHandleClicked && !pointGizmoUsing && !pointGizmoHovered && !controlsHovered)
                    {
                        m_selectedSplinePoint = -1;
                    }
                }
                else
                {
                    if (hasValidSelectedOceanPoint())
                    {
                        const glm::vec2 point = oceanComponent->GetAreas()[static_cast<std::size_t>(m_selectedOceanAreaIndex)].points[static_cast<std::size_t>(m_selectedOceanPointIndex)];
                        glm::mat4 pointTransform = entityWorldTransform;
                        pointTransform[3] = glm::vec4(glm::vec3(entityWorldTransform * glm::vec4(point.x, 0.0f, point.y, 1.0f)), 1.0f);
                        const glm::vec3 originalPointPosition(pointTransform[3]);
                        ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                                             glm::value_ptr(gizmoProjection),
                                             ImGuizmo::TRANSLATE,
                                             m_gizmoMode,
                                             glm::value_ptr(pointTransform),
                                             nullptr,
                                             m_enableSnap ? &m_translateSnap.x : nullptr);
                        constrainOrthographicTranslation(pointTransform, originalPointPosition);

                        const bool pointGizmoUsing = ImGuizmo::IsUsing();
                        const bool pointGizmoHovered = ImGuizmo::IsOver();
                        gizmoBlocksSelection = pointGizmoUsing || pointGizmoHovered || splineHandleClicked;
                        if (pointGizmoUsing && !m_isOceanPointGizmoUsing)
                        {
                            editorShell.BeginSceneEdit("Move Ocean Area Point");
                        }
                        if (pointGizmoUsing)
                        {
                            m_isTransformGizmoUsing = true;
                            const glm::vec3 worldPosition(pointTransform[3]);
                            const glm::vec3 localPosition(glm::inverse(entityWorldTransform) * glm::vec4(worldPosition, 1.0f));
                            oceanComponent->SetAreaPoint(static_cast<std::size_t>(m_selectedOceanAreaIndex),
                                                         static_cast<std::size_t>(m_selectedOceanPointIndex),
                                                         glm::vec2(localPosition.x, localPosition.z));
                            selectedEntity->AddPrefabOverride("Component:OceanComponent:Areas." + std::to_string(m_selectedOceanAreaIndex) + ".Points." + std::to_string(m_selectedOceanPointIndex));
                            editorShell.MarkSceneDirty();
                        }
                        if (!pointGizmoUsing && m_isOceanPointGizmoUsing)
                        {
                            editorShell.EndSceneEdit();
                        }
                        m_isOceanPointGizmoUsing = pointGizmoUsing;

                        if (viewportClicked && !splineHandleClicked && !pointGizmoUsing && !pointGizmoHovered && !controlsHovered)
                        {
                            m_selectedOceanAreaIndex = -1;
                            m_selectedOceanPointIndex = -1;
                        }
                    }
                    else
                    {
                        if (!shapeHandleBlocksGizmo)
                        {
                            submitEntityGizmo(snapValues);
                            entityGizmoSubmitted = true;
                        }
                    }
                }
            }
            else
            {
                if (m_isSplinePointGizmoUsing)
                {
                    editorShell.EndSceneEdit();
                    m_isSplinePointGizmoUsing = false;
                }
                m_selectedSplinePoint = -1;
                if (hasValidSelectedOceanPoint())
                {
                    const glm::vec2 point = oceanComponent->GetAreas()[static_cast<std::size_t>(m_selectedOceanAreaIndex)].points[static_cast<std::size_t>(m_selectedOceanPointIndex)];
                    glm::mat4 pointTransform = entityWorldTransform;
                    pointTransform[3] = glm::vec4(glm::vec3(entityWorldTransform * glm::vec4(point.x, 0.0f, point.y, 1.0f)), 1.0f);
                    const glm::vec3 originalPointPosition(pointTransform[3]);
                    ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                                         glm::value_ptr(gizmoProjection),
                                         ImGuizmo::TRANSLATE,
                                         m_gizmoMode,
                                         glm::value_ptr(pointTransform),
                                         nullptr,
                                         m_enableSnap ? &m_translateSnap.x : nullptr);
                    constrainOrthographicTranslation(pointTransform, originalPointPosition);

                    const bool pointGizmoUsing = ImGuizmo::IsUsing();
                    const bool pointGizmoHovered = ImGuizmo::IsOver();
                    gizmoBlocksSelection = pointGizmoUsing || pointGizmoHovered || splineHandleClicked;
                    if (pointGizmoUsing && !m_isOceanPointGizmoUsing)
                    {
                        editorShell.BeginSceneEdit("Move Ocean Area Point");
                    }
                    if (pointGizmoUsing)
                    {
                        m_isTransformGizmoUsing = true;
                        const glm::vec3 worldPosition(pointTransform[3]);
                        const glm::vec3 localPosition(glm::inverse(entityWorldTransform) * glm::vec4(worldPosition, 1.0f));
                        oceanComponent->SetAreaPoint(static_cast<std::size_t>(m_selectedOceanAreaIndex),
                                                     static_cast<std::size_t>(m_selectedOceanPointIndex),
                                                     glm::vec2(localPosition.x, localPosition.z));
                        selectedEntity->AddPrefabOverride("Component:OceanComponent:Areas." + std::to_string(m_selectedOceanAreaIndex) + ".Points." + std::to_string(m_selectedOceanPointIndex));
                        editorShell.MarkSceneDirty();
                    }
                    if (!pointGizmoUsing && m_isOceanPointGizmoUsing)
                    {
                        editorShell.EndSceneEdit();
                    }
                    m_isOceanPointGizmoUsing = pointGizmoUsing;

                    if (viewportClicked && !splineHandleClicked && !pointGizmoUsing && !pointGizmoHovered && !controlsHovered)
                    {
                        m_selectedOceanAreaIndex = -1;
                        m_selectedOceanPointIndex = -1;
                    }
                }
                else
                {
                    if (!shapeHandleBlocksGizmo)
                    {
                        submitEntityGizmo(snapValues);
                        entityGizmoSubmitted = true;
                    }
                }
            }
            // IsOver is global state inside ImGuizmo. Only trust it in a frame
            // where this viewport actually submitted a gizmo.
            // IsOver() covers the gizmo's projected hit regions and can be true
            // well away from a visible handle (especially at shallow camera
            // angles).  Only an interaction that the gizmo actually captured
            // should consume a viewport selection click.
            if (entityGizmoSubmitted)
            {
                const bool entityGizmoUsing = ImGuizmo::IsUsing();
                if (ImGuizmo::IsUsing())
                {
                    gizmoBlocksSelection = true;
                    m_isTransformGizmoUsing = true;
                    if (editingSplinePoint)
                    {
                        if (!m_splinePointEditActive)
                        {
                            editorShell.BeginSceneEdit(m_gizmoOperation == ImGuizmo::ROTATE ? "Rotate Spline Point" : "Move Spline Point");
                            m_splinePointEditActive = true;
                        }
                        const glm::mat4 localPointTransform = glm::inverse(selectedEntity->GetWorldTransform()) * entityTransform;
                        const std::size_t pointIndex = static_cast<std::size_t>(m_selectedSplinePointIndex);
                        if (m_gizmoOperation == ImGuizmo::ROTATE)
                        {
                            const glm::mat4 relativePointTransform = glm::inverse(BuildSplinePointFrame(*splineComponent, pointIndex)) * localPointTransform;
                            splineComponent->SetPointRotation(pointIndex, ExtractRotationDegrees(relativePointTransform));
                            selectedEntity->AddPrefabOverride("Component:SplineComponent:PointRotations." + std::to_string(m_selectedSplinePointIndex));
                        }
                        else
                        {
                            splineComponent->SetPointPosition(pointIndex, glm::vec3(localPointTransform[3]));
                            selectedEntity->AddPrefabOverride("Component:SplineComponent:Points." + std::to_string(m_selectedSplinePointIndex));
                        }
                    }
                    else
                    {
                        const glm::vec3 previousPosition = selectedEntity->GetPosition();
                        const glm::vec3 previousRotation = selectedEntity->GetRotation();
                        const glm::vec3 previousScale = selectedEntity->GetScale();
                        if (entityGizmoUsesBoundsCenter)
                            entityTransform = entityGizmoDelta * entityWorldTransform;
                        ApplyWorldTransformToEntity(*selectedEntity, entityTransform);
                        if (selectedEntity->GetPosition() != previousPosition)
                            selectedEntity->AddPrefabOverride("Transform.Position");
                        if (selectedEntity->GetRotation() != previousRotation)
                            selectedEntity->AddPrefabOverride("Transform.Rotation");
                        if (selectedEntity->GetScale() != previousScale)
                            selectedEntity->AddPrefabOverride("Transform.Scale");
                    }
                    editorShell.MarkSceneDirty();
                }
                else if (!entityGizmoUsing)
                {
                    m_isOceanPointGizmoUsing = false;
                }
            }
            else if (m_splinePointEditActive)
            {
                editorShell.EndSceneEdit();
                m_splinePointEditActive = false;
            }
            else if (m_oceanPointEditActive)
            {
                editorShell.EndSceneEdit();
                m_oceanPointEditActive = false;
            }

            if (splinePointClickConsumed)
            {
                return;
            }
        }
        else
        {
            if (m_resizeHandleAxis >= 0)
            {
                editorShell.EndSceneEdit();
                m_resizeHandleAxis = -1;
                m_resizeHandleEntityId = 0;
                m_resizeHandleTarget = 0;
            }
            if (m_isSplinePointGizmoUsing)
            {
                editorShell.EndSceneEdit();
                m_isSplinePointGizmoUsing = false;
            }
            if (m_isOceanPointGizmoUsing)
            {
                editorShell.EndSceneEdit();
                m_isOceanPointGizmoUsing = false;
            }
            m_splinePointEntity = nullptr;
            m_oceanPointEntity = nullptr;
            m_selectedSplinePoint = -1;
            m_selectedOceanAreaIndex = -1;
            m_selectedOceanPointIndex = -1;
        }

        if (viewportClicked)
        {
            if (controlsHovered)
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "[Pick] blocked=viewport-controls-hovered");
                return;
            }
            if (!m_isViewportHovered)
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "[Pick] blocked=viewport-not-hovered");
                return;
            }
            if (gizmoBlocksSelection)
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Info,
                                splineHandleClicked ? "[Pick] blocked=spline-point" : "[Pick] blocked=gizmo-using");
                return;
            }

            m_selectedSplinePointIndex = -1;
            PickDebugInfo pickDebugInfo;
            auto *pickedEntity = PickEntity(editorShell.GetEngine().GetScene(), cameraData, viewportMin, viewportSize, &pickDebugInfo);
            if (!pickDebugInfo.rayBuilt && m_hasEditorCameraData)
            {
                PickDebugInfo fallbackPickDebugInfo;
                pickedEntity = PickEntity(editorShell.GetEngine().GetScene(), freshCameraData, viewportMin, viewportSize, &fallbackPickDebugInfo);
                if (fallbackPickDebugInfo.rayBuilt)
                {
                    fallbackPickDebugInfo.rayFailureReason = "cached-camera-failed: " + pickDebugInfo.rayFailureReason + "; fresh-camera-used";
                    pickDebugInfo = std::move(fallbackPickDebugInfo);
                }
            }
            editorShell.SetSelectedEntity(pickedEntity);
            editorShell.Log(EditorShell::ConsoleSeverity::Info,
                            FormatPickDebugMessage(pickDebugInfo, ImGui::GetIO().MousePos, viewportMin, viewportSize));
        }
    }

    void ViewportPanel::RenderFrame(scene::CameraComponent &cameraComponent)
    {
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &editorShell = EditorShell::GetInstance();
        auto *activeScene = editorShell.GetEngine().GetScene();
        auto &renderer = editorShell.GetEngine().GetRenderer();
        auto *sceneRenderTarget = GetSceneRenderTarget();
        renderer.BeginFrame(sceneRenderTarget);
        std::vector<render::IPostProcessEffect *> postProcessEffects;
        postProcessEffects.reserve(cameraComponent.GetPostProcessEffects().size());
        for (const auto &effect : cameraComponent.GetPostProcessEffects())
        {
            postProcessEffects.push_back(effect.get());
        }

        renderer.RenderFrame(cameraComponent.GetCameraData(sceneRenderTarget->GetWidth(), sceneRenderTarget->GetHeight()),
                             sceneRenderTarget,
                             activeScene ? activeScene->GetLights() : std::vector<scene::Light *>{},
                             &postProcessEffects,
                             activeScene);
        renderer.EndFrame(sceneRenderTarget);
        PresentSceneRenderTarget();
    }

    void ViewportPanel::RenderRhiFrame(const render::CameraData &cameraData, std::span<const render::RenderCommand> commands)
    {
        if (!m_useRhiPreview || !m_basicRenderer || !m_rhiDevice || !m_renderTarget)
            return;

        const auto *target = GetSceneRenderTarget();
        if (!target || target->GetWidth() <= 0 || target->GetHeight() <= 0 ||
            !m_basicRenderer->Resize(static_cast<std::uint32_t>(target->GetWidth()), static_cast<std::uint32_t>(target->GetHeight())))
            return;

        std::vector<render::BasicDraw> draws;
        m_rhiSceneCommandCount = commands.size();
        draws.reserve(commands.size());
        std::unordered_set<const render::Mesh *> activeMeshes;
        std::unordered_set<const render::Texture *> activeSrgbTextures;
        std::unordered_set<const render::Texture *> activeLinearTextures;
        activeMeshes.reserve(commands.size());

        const auto uploadTexture = [&](const render::Texture *source,
                                       render::rhi::Format format,
                                       std::unordered_map<const render::Texture *, render::rhi::Texture> &cache,
                                       std::unordered_set<const render::Texture *> &activeTextures,
                                       const char *debugName) -> render::rhi::TextureHandle
        {
            if (!source || source->GetType() != GL_TEXTURE_2D || source->GetTextureID() == 0 ||
                source->GetWidth() <= 0 || source->GetHeight() <= 0)
                return {};

            activeTextures.insert(source);
            if (const auto cached = cache.find(source); cached != cache.end())
                return cached->second.Get();

            const std::size_t pixelCount = static_cast<std::size_t>(source->GetWidth()) *
                                           static_cast<std::size_t>(source->GetHeight());
            std::vector<std::byte> rgbaPixels(pixelCount * 4);
            glBindTexture(GL_TEXTURE_2D, source->GetTextureID());
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaPixels.data());

            auto uploaded = render::rhi::Texture(
                *m_rhiDevice,
                m_rhiDevice->CreateTexture(
                    {static_cast<std::uint32_t>(source->GetWidth()),
                     static_cast<std::uint32_t>(source->GetHeight()),
                     format,
                     render::rhi::TextureUsage::Sampled,
                     debugName},
                    rgbaPixels));
            if (!uploaded)
                return {};

            return cache.emplace(source, std::move(uploaded)).first->second.Get();
        };

        for (const auto &command : commands)
        {
            if (!command.mesh)
                continue;
            activeMeshes.insert(command.mesh);

            auto meshIt = m_rhiMeshes.find(command.mesh);
            if (meshIt == m_rhiMeshes.end())
            {
                const auto &source = command.mesh->GetMeshData();
                if (source.vertices.empty() || source.indices.empty())
                    continue;
                std::vector<render::BasicVertex> vertices;
                vertices.reserve(source.vertices.size());
                for (const auto &vertex : source.vertices)
                    vertices.push_back({vertex.position, vertex.normal, vertex.uv, vertex.tangent});
                auto uploaded = m_basicRenderer->CreateMesh({vertices, source.indices});
                meshIt = m_rhiMeshes.emplace(command.mesh, std::move(uploaded)).first;
            }

            std::uint32_t firstIndex = 0;
            std::uint32_t indexCount = 0;
            if (command.submeshIndex < command.mesh->GetSubmeshCount())
            {
                const auto range = command.mesh->GetSubmeshLodRange(command.submeshIndex, command.lodIndex);
                firstIndex = range.indexOffset;
                indexCount = range.indexCount;
            }

            glm::vec4 baseColor(1.0f);
            glm::vec2 uvScale(1.0f);
            render::rhi::TextureHandle baseColorTexture;
            render::rhi::TextureHandle normalTexture;
            render::rhi::TextureHandle metallicTexture;
            render::rhi::TextureHandle roughnessTexture;
            float metallic = 0.0f;
            float roughness = 1.0f;
            glm::vec3 emission(0.0f);
            float alphaCutoff = 0.5f;
            std::uint32_t alphaMode = 0;
            std::uint32_t metallicChannel = 0;
            std::uint32_t roughnessChannel = 0;
            bool flipNormalY = false;
            if (command.material)
            {
                const auto &material = command.material->GetConfig();
                baseColor = material.color;
                uvScale = material.uvScale;
                metallic = material.metallic;
                roughness = material.roughness;
                emission = material.emission;
                alphaCutoff = material.alphaCutoff;
                alphaMode = static_cast<std::uint32_t>(material.alphaMode);
                metallicChannel = static_cast<std::uint32_t>(material.metallicTextureChannel);
                roughnessChannel = static_cast<std::uint32_t>(material.roughnessTextureChannel);
                flipNormalY = material.flipNormalY;
                baseColorTexture = uploadTexture(material.albedoTexture, render::rhi::Format::R8G8B8A8Srgb,
                                                 m_rhiSrgbTextures, activeSrgbTextures, "RHI material albedo");
                normalTexture = uploadTexture(material.normalTexture, render::rhi::Format::R8G8B8A8Unorm,
                                              m_rhiLinearTextures, activeLinearTextures, "RHI material normal");
                metallicTexture = uploadTexture(material.metallicTexture, render::rhi::Format::R8G8B8A8Unorm,
                                                m_rhiLinearTextures, activeLinearTextures, "RHI material metallic");
                roughnessTexture = uploadTexture(material.roughnessTexture, render::rhi::Format::R8G8B8A8Unorm,
                                                 m_rhiLinearTextures, activeLinearTextures, "RHI material roughness");
            }

            const auto appendDraw = [&](const glm::mat4 &model)
            {
                draws.push_back(render::BasicDraw{
                    .mesh = &meshIt->second,
                    .model = model,
                    .baseColor = baseColor,
                    .uvScale = uvScale,
                    .baseColorTexture = baseColorTexture,
                    .normalTexture = normalTexture,
                    .metallicTexture = metallicTexture,
                    .roughnessTexture = roughnessTexture,
                    .metallic = metallic,
                    .roughness = roughness,
                    .emission = emission,
                    .alphaCutoff = alphaCutoff,
                    .alphaMode = alphaMode,
                    .metallicChannel = metallicChannel,
                    .roughnessChannel = roughnessChannel,
                    .flipNormalY = flipNormalY,
                    .firstIndex = firstIndex,
                    .indexCount = indexCount,
                });
            };

            if (command.instanceModels && !command.instanceModels->empty())
            {
                for (const auto &instanceModel : *command.instanceModels)
                    appendDraw(instanceModel);
            }
            else
            {
                appendDraw(command.model);
            }
        }

        const auto pruneTextureCache = [](auto &cache, const auto &activeTextures)
        {
            std::erase_if(cache, [&](const auto &entry) { return !activeTextures.contains(entry.first); });
        };
        pruneTextureCache(m_rhiSrgbTextures, activeSrgbTextures);
        pruneTextureCache(m_rhiLinearTextures, activeLinearTextures);

        for (auto meshIt = m_rhiMeshes.begin(); meshIt != m_rhiMeshes.end();)
        {
            if (!activeMeshes.contains(meshIt->first))
                meshIt = m_rhiMeshes.erase(meshIt);
            else
                ++meshIt;
        }

        try
        {
            m_rhiDrawCount = draws.size();
            glm::mat4 projection = cameraData.projection;
            if (m_activeRhiVulkan)
            {
                // Existing editor cameras still emit OpenGL -1..1 NDC depth.
                // Convert it to Vulkan's 0..1 range at the migration boundary.
                glm::mat4 depthRangeConversion(1.0f);
                depthRangeConversion[2][2] = 0.5f;
                depthRangeConversion[3][2] = 0.5f;
                projection = depthRangeConversion * projection;
            }
            render::BasicLighting lighting;
            lighting.cameraPosition = glm::vec3(glm::inverse(cameraData.view)[3]);
            if (auto *activeScene = EditorShell::GetInstance().GetEngine().GetScene())
            {
                for (const auto *light : activeScene->GetLights())
                {
                    if (!light || light->type != scene::LightType::Directional)
                        continue;
                    lighting.directionalDirection = light->direction;
                    lighting.directionalColor = light->color;
                    lighting.directionalIntensity = light->intensity;
                    break;
                }
            }
            m_basicRenderer->Render(projection * cameraData.view, lighting, draws);
            if (m_activeRhiVulkan)
            {
                auto &vulkanDevice = static_cast<render::rhi::vulkan::VulkanDevice &>(*m_rhiDevice);
                const auto pixels = vulkanDevice.ReadTextureRgba8(m_basicRenderer->GetColorTexture());
                m_rhiChangedPixelCount = 0;
                for (std::size_t pixel = 0; pixel + 3 < pixels.size(); pixel += 4)
                {
                    const int red = std::to_integer<unsigned char>(pixels[pixel]);
                    const int green = std::to_integer<unsigned char>(pixels[pixel + 1]);
                    const int blue = std::to_integer<unsigned char>(pixels[pixel + 2]);
                    // BasicRenderer clears to approximately (10, 15, 23).
                    if (std::abs(red - 10) > 3 || std::abs(green - 15) > 3 || std::abs(blue - 23) > 3)
                        ++m_rhiChangedPixelCount;
                }
                if (m_vulkanBridgeTexture == 0)
                {
                    GLuint texture = 0;
                    glGenTextures(1, &texture);
                    m_vulkanBridgeTexture = texture;
                }
                glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(m_vulkanBridgeTexture));
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8,
                             static_cast<GLsizei>(m_basicRenderer->GetWidth()),
                             static_cast<GLsizei>(m_basicRenderer->GetHeight()), 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                m_rhiViewportTexture = m_vulkanBridgeTexture;
            }
            else
            {
                auto &openGlDevice = static_cast<render::rhi::opengl::OpenGLDevice &>(*m_rhiDevice);
                m_rhiViewportTexture = openGlDevice.GetTextureNativeHandle(m_basicRenderer->GetColorTexture());
            }
        }
        catch (const std::exception &error)
        {
            std::cerr << "Viewport RHI render failed: " << error.what() << std::endl;
            m_useRhiPreview = false;
            m_rhiViewportTexture = 0;
        }

        // The legacy renderer and ImGui still share this context during the
        // migration, so invalidate their cached assumptions after RHI work.
        render::Graphics::ResetStateCache();
        render::Graphics::BindFramebuffer(0);
    }

    render::RenderTarget *ViewportPanel::GetSceneRenderTarget() const
    {
        return std::abs(m_renderScale - 1.0f) < 0.001f || !m_scaledRenderTarget
                   ? m_renderTarget
                   : m_scaledRenderTarget;
    }

    void ViewportPanel::PresentSceneRenderTarget()
    {
        auto *source = GetSceneRenderTarget();
        if (!source || source == m_renderTarget || !m_renderTarget || !m_upscaler)
            return;

        const float sharpness = m_renderScale < 1.0f ? m_upscaleSharpness : 0.0f;
        m_upscaler->Upscale(*source, *m_renderTarget, render::UpscalerConfig{.sharpness = sharpness});
    }

    bool ViewportPanel::ShouldRenderFrame() const
    {
        return IsOpen() && WasVisibleLastFrame() && m_renderTarget && m_renderTarget->IsInitialized() && m_renderTarget->GetWidth() > 0 && m_renderTarget->GetHeight() > 0;
    }

    void ViewportPanel::ClearFrame()
    {
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(m_renderTarget);
        renderer.EndFrame(m_renderTarget);
    }

    void ViewportPanel::Shutdown()
    {
        m_rhiMeshes.clear();
        m_rhiSrgbTextures.clear();
        m_rhiLinearTextures.clear();
        if (m_basicRenderer)
        {
            m_basicRenderer->Shutdown();
            m_basicRenderer.reset();
        }
        m_rhiDevice.reset();
        m_rhiViewportTexture = 0;
        if (m_vulkanBridgeTexture != 0)
        {
            const GLuint texture = static_cast<GLuint>(m_vulkanBridgeTexture);
            glDeleteTextures(1, &texture);
            m_vulkanBridgeTexture = 0;
        }
        if (m_renderTarget)
        {
            m_renderTarget->Cleanup();
            delete m_renderTarget;
            m_renderTarget = nullptr;
        }
        if (m_scaledRenderTarget)
        {
            m_scaledRenderTarget->Cleanup();
            delete m_scaledRenderTarget;
            m_scaledRenderTarget = nullptr;
        }
        if (m_upscaler)
        {
            m_upscaler->Shutdown();
            m_upscaler.reset();
        }
    }
    ViewportPanel::~ViewportPanel() = default;
}
