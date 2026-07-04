#include "PlutoGE/ui/panels/ViewportPanel.h"

// Editor selection access is validated by EditorShell before panel use.
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/ui/panels/ContentBrowserPanel.h"
#include <ImGuizmo.h>
#include <iostream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <imgui.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr int kDefaultViewportWidth = 1280;
        constexpr int kDefaultViewportHeight = 720;
        constexpr int kResizeDebounceFrames = 2;
        constexpr float kMinRenderScale = 0.5f;
        constexpr float kMaxRenderScale = 1.0f;
        constexpr float kRayEpsilon = 0.0001f;
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

        bool IntersectBounds(const render::MeshBounds &bounds, const glm::vec3 &origin, const glm::vec3 &direction)
        {
            const glm::vec3 offset = origin - bounds.center;
            const float b = glm::dot(offset, direction);
            const float c = glm::dot(offset, offset) - bounds.radius * bounds.radius;
            if (c <= 0.0f)
            {
                return true;
            }

            if (b > 0.0f)
            {
                return false;
            }

            const float discriminant = b * b - c;
            return discriminant >= 0.0f;
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
            DrawWorldCircle(drawList, center, right * radius, up * (radius + cylinderHalfHeight), cameraData, viewportMin, viewportSize, color);
            DrawWorldCircle(drawList, center, forward * radius, up * (radius + cylinderHalfHeight), cameraData, viewportMin, viewportSize, color);

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
            if (std::abs(determinant) <= kRayEpsilon)
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
            if (hitDistance <= kRayEpsilon)
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
                                              const render::Submesh &submesh,
                                              scene::AnimationComponent *animationComponent)
        {
            glm::mat4 transform = entity.GetWorldTransform() * meshComponent.GetMeshOffsetTransform();
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

        std::optional<PickRay> BuildPickRay(const render::CameraData &cameraData,
                                            const ImVec2 &viewportMin,
                                            const ImVec2 &viewportSize)
        {
            if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
            {
                return std::nullopt;
            }

            const ImVec2 mousePosition = ImGui::GetIO().MousePos;
            const float normalizedX = (mousePosition.x - viewportMin.x) / viewportSize.x;
            const float normalizedY = (mousePosition.y - viewportMin.y) / viewportSize.y;
            if (normalizedX < 0.0f || normalizedX > 1.0f || normalizedY < 0.0f || normalizedY > 1.0f)
            {
                return std::nullopt;
            }

            const float clipX = normalizedX * 2.0f - 1.0f;
            const float clipY = 1.0f - normalizedY * 2.0f;
            const glm::mat4 inverseViewProjection = glm::inverse(cameraData.projection * cameraData.view);

            glm::vec4 nearPoint = inverseViewProjection * glm::vec4(clipX, clipY, -1.0f, 1.0f);
            glm::vec4 farPoint = inverseViewProjection * glm::vec4(clipX, clipY, 1.0f, 1.0f);
            if (std::abs(nearPoint.w) <= kRayEpsilon || std::abs(farPoint.w) <= kRayEpsilon)
            {
                return std::nullopt;
            }

            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;

            const glm::mat4 inverseView = glm::inverse(cameraData.view);
            glm::vec4 cameraWorldPosition = inverseView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            if (std::abs(cameraWorldPosition.w) <= kRayEpsilon)
            {
                return std::nullopt;
            }

            cameraWorldPosition /= cameraWorldPosition.w;

            const glm::vec3 origin = glm::vec3(cameraWorldPosition);
            const glm::vec3 direction = glm::normalize(glm::vec3(farPoint) - origin);
            if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
            {
                return std::nullopt;
            }

            return PickRay{.origin = origin, .direction = direction};
        }

        scene::Entity *PickEntity(scene::Scene *scene,
                                  const render::CameraData &cameraData,
                                  const ImVec2 &viewportMin,
                                  const ImVec2 &viewportSize)
        {
            constexpr std::size_t kMaxExactPickTrianglesPerSubmesh = 250000;

            if (!scene)
            {
                return nullptr;
            }

            const auto ray = BuildPickRay(cameraData, viewportMin, viewportSize);
            if (!ray.has_value())
            {
                return nullptr;
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
                auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                auto *terrainComponent = entity->GetComponent<scene::TerrainComponent>();
                if ((!meshComponent || !meshComponent->IsVisible() || !meshComponent->GetMesh()) && !terrainComponent)
                {
                    continue;
                }

                if (terrainComponent && terrainComponent->IsEnabled())
                {
                    glm::vec3 hitPoint{0.0f};
                    if (terrainComponent->Raycast(ray->origin, ray->direction, hitPoint))
                    {
                        const float worldDistance = glm::length(hitPoint - ray->origin);
                        if (worldDistance < selectedDistance)
                        {
                            selectedDistance = worldDistance;
                            selectedEntity = entity;
                        }
                    }
                }

                if (!meshComponent || !meshComponent->IsVisible() || !meshComponent->GetMesh())
                {
                    continue;
                }

                render::Mesh *mesh = meshComponent->GetMesh();
                const auto &meshData = mesh->GetMeshData();
                if (meshData.vertices.empty() || meshData.indices.size() < 3)
                {
                    continue;
                }

                scene::AnimationComponent *animationComponent = FindAnimationComponent(entity);
                const bool useApproximateSubmeshPick = mesh->HasSkeleton() && animationComponent && animationComponent->GetClipCount() > 0;

                const size_t meshSubmeshCount = std::max<size_t>(mesh->GetSubmeshCount(), 1);
                const size_t submeshBegin = meshComponent->GetSubmeshIndex() >= 0 ? static_cast<size_t>(meshComponent->GetSubmeshIndex()) : 0;
                const size_t submeshEnd = meshComponent->GetSubmeshIndex() >= 0 ? std::min(submeshBegin + static_cast<size_t>(std::max(1, meshComponent->GetSubmeshRangeCount())), meshSubmeshCount) : meshSubmeshCount;

                for (size_t submeshIndex = submeshBegin; submeshIndex < submeshEnd; ++submeshIndex)
                {
                    const auto &submesh = submeshIndex < mesh->GetSubmeshCount() ? mesh->GetSubmesh(submeshIndex) : render::Submesh{};
                    if (submesh.indexCount < 3 ||
                        submesh.indexOffset + submesh.indexCount > meshData.indices.size())
                    {
                        continue;
                    }

                    const glm::mat4 submeshWorldTransform = ComputePickSubmeshTransform(*entity, *meshComponent, submesh, animationComponent);
                    const glm::mat4 inverseSubmeshWorldTransform = glm::inverse(submeshWorldTransform);
                    glm::vec3 localOrigin = glm::vec3(inverseSubmeshWorldTransform * glm::vec4(ray->origin, 1.0f));
                    glm::vec3 localDirection = glm::vec3(inverseSubmeshWorldTransform * glm::vec4(ray->direction, 0.0f));
                    const float directionLengthSquared = glm::dot(localDirection, localDirection);
                    if (directionLengthSquared <= kRayEpsilon)
                    {
                        continue;
                    }
                    localDirection = glm::normalize(localDirection);

                    if (!IntersectBounds(submesh.bounds, localOrigin, localDirection))
                    {
                        continue;
                    }

                    const std::size_t triangleCount = submesh.indexCount / 3;
                    if (useApproximateSubmeshPick || triangleCount > kMaxExactPickTrianglesPerSubmesh)
                    {
                        const glm::vec3 worldCenter = glm::vec3(submeshWorldTransform * glm::vec4(submesh.bounds.center, 1.0f));
                        const float approximateDistance = glm::length(worldCenter - ray->origin);
                        if (approximateDistance < selectedDistance)
                        {
                            selectedDistance = approximateDistance;
                            selectedEntity = entity;
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
                        if (!IntersectTriangle(localOrigin, localDirection, v0, v1, v2, localDistance))
                        {
                            continue;
                        }

                        const glm::vec3 localHitPoint = localOrigin + localDirection * localDistance;
                        const glm::vec3 worldHitPoint = glm::vec3(submeshWorldTransform * glm::vec4(localHitPoint, 1.0f));
                        const float worldDistance = glm::length(worldHitPoint - ray->origin);
                        if (worldDistance < selectedDistance)
                        {
                            selectedDistance = worldDistance;
                            selectedEntity = entity;
                        }
                    }
                }
            }

            return selectedEntity;
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

    }

    const char *ViewportPanel::GetDebugViewLabel(render::PostProcessDebugView debugView)
    {
        return kDebugViewLabels[static_cast<int>(debugView)];
    }

    void ViewportPanel::Initialize()
    {
        m_renderScale = glm::clamp(m_config.initialRenderScale, kMinRenderScale, kMaxRenderScale);

        auto renderConfig = render::RenderTargetConfig{
            .width = std::max(1, static_cast<int>(std::lround(static_cast<float>(kDefaultViewportWidth) * m_renderScale))),
            .height = std::max(1, static_cast<int>(std::lround(static_cast<float>(kDefaultViewportHeight) * m_renderScale))),
            .clearColor = m_config.clearColor,
        };
        m_renderTarget = new render::RenderTarget(renderConfig);
        if (!m_renderTarget->IsInitialized())
        {
            std::cerr << "Failed to initialize RenderTarget in ViewportPanel" << std::endl;
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
        const int scaledWidth = std::max(1, static_cast<int>(std::lround(static_cast<float>(newWidth) * m_renderScale)));
        const int scaledHeight = std::max(1, static_cast<int>(std::lround(static_cast<float>(newHeight) * m_renderScale)));
        if ((scaledWidth != m_renderTarget->GetWidth() || scaledHeight != m_renderTarget->GetHeight()) && ++m_resizeStableFrames >= kResizeDebounceFrames)
        {
            if (!m_renderTarget->Resize(scaledWidth, scaledHeight))
            {
                std::cerr << "Failed to resize RenderTarget in ViewportPanel" << std::endl;
                return;
            }
        }

        ImTextureID texId = (ImTextureID)(uintptr_t)m_renderTarget->GetColorTextureID();
        ImVec2 imageSize = ImVec2(panelSize.x, panelSize.y);
        ImGui::Image(texId, imageSize, ImVec2(0, 1), ImVec2(1, 0));
        const ImVec2 viewportMin = ImGui::GetItemRectMin();
        m_viewportMin = glm::vec2(viewportMin.x, viewportMin.y);
        m_viewportSize = glm::vec2(imageSize.x, imageSize.y);
        const bool viewportClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        m_isViewportHovered = ImGui::IsItemHovered();
        m_isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (m_config.editorViewport && ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserAssetDragDropPayload))
            {
                const std::string reference(static_cast<const char *>(payload->Data), payload->DataSize > 0 ? payload->DataSize - 1 : 0);
                if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Prefab)
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
                else if (assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::Mesh)
                {
                    InstantiateMeshAssetIntoScene(reference, nullptr);
                }
            }
            if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kContentBrowserMeshSubassetDragDropPayload))
            {
                const auto *meshPayload = static_cast<const ContentBrowserMeshSubassetPayload *>(payload->Data);
                if (meshPayload)
                {
                    InstantiateMeshAssetIntoScene(meshPayload->sourceReference, nullptr, meshPayload->submeshIndex, meshPayload->submeshCount, meshPayload->materialSlot);
                }
            }
            ImGui::EndDragDropTarget();
        }

        bool controlsHovered = false;
        if (m_panelControlsEnabled)
        {
            controlsHovered = RenderViewportSettingsOverlay(viewportMin, imageSize);
            m_isViewportHovered = m_isViewportHovered && !controlsHovered;
        }

        if (m_config.editorViewport)
        {
            RenderEditorOverlays(viewportMin, imageSize, viewportClicked && !controlsHovered);
        }
    }

    bool ViewportPanel::RenderViewportSettingsOverlay(const ImVec2 &viewportMin, const ImVec2 &viewportSize)
    {
        const bool allowEditorViewportHotkeys =
            m_config.editorViewport &&
            // ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput;

        if (allowEditorViewportHotkeys)
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
            if (ImGui::IsKeyPressed(ImGuiKey_F))
            {
                FrameSelectedEntity(EditorShell::GetInstance());
            }
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
                ImGui::MenuItem("Grid", nullptr, &m_showGrid);
                ImGui::MenuItem("Debug Shapes", nullptr, &m_showDebugShapes);
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
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Render Scale", &m_renderScale, kMinRenderScale, kMaxRenderScale, "%.2fx"))
            {
                m_renderScale = glm::clamp(m_renderScale, kMinRenderScale, kMaxRenderScale);
                m_resizeStableFrames = kResizeDebounceFrames;
            }
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
            }
        }

        const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup | ImGuiHoveredFlags_ChildWindows) ||
                             overlayPopupOpen;
        ImGui::End();
        ImGui::PopStyleVar(3);

        return hovered;
    }

    void ViewportPanel::RenderEditorOverlays(const ImVec2 &viewportMin, const ImVec2 &viewportSize, bool viewportClicked)
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
        const render::CameraData cameraData = editorCamera.camera.GetCameraDataForTransform(cameraTransform,
                                                                                            m_renderTarget->GetWidth(),
                                                                                            m_renderTarget->GetHeight());

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(true);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(viewportMin.x, viewportMin.y, viewportSize.x, viewportSize.y);
        bool gizmoBlocksSelection = false;

        if (m_showDebugShapes)
        {
            DrawEditorDebugShapes(editorShell.GetEngine().GetScene(), editorShell.GetSelectedEntity(), cameraData, viewportMin, viewportSize);
        }

        if (auto *selectedEntity = editorShell.GetSelectedEntity())
        {
            bool terrainPaintActive = false;
            bool foliagePaintActive = false;
            static bool s_terrainStrokeActive = false;
            static bool s_foliageStrokeActive = false;
            if (auto *terrainComponent = selectedEntity->GetComponent<scene::TerrainComponent>())
            {
                auto *foliageComponent = selectedEntity->GetComponent<scene::FoliageComponent>();
                const bool canTerrainPaint = terrainComponent->IsEnabled() && terrainComponent->IsPaintEnabled();
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
                            const glm::vec3 right = SafeNormalizedAxis(glm::vec3(worldTransform[0]), glm::vec3(1.0f, 0.0f, 0.0f));
                            const glm::vec3 forward = SafeNormalizedAxis(glm::vec3(worldTransform[2]), glm::vec3(0.0f, 0.0f, 1.0f));
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
                                                     ImGui::IsMouseDown(ImGuiMouseButton_Left);
                            terrainPaintActive = canTerrainPaint && brushActive && !canFoliagePaint;
                            foliagePaintActive = canFoliagePaint && brushActive;
                            if (terrainPaintActive && !s_terrainStrokeActive)
                            {
                                editorShell.BeginSceneEdit("Paint Terrain");
                                s_terrainStrokeActive = true;
                            }
                            if (foliagePaintActive && !s_foliageStrokeActive)
                            {
                                editorShell.BeginSceneEdit("Paint Foliage");
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
                editorShell.EndSceneEdit();
                s_foliageStrokeActive = false;
            }

            if (terrainPaintActive || foliagePaintActive)
            {
                return;
            }

            glm::mat4 entityTransform = selectedEntity->GetWorldTransform();
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
            ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                                 glm::value_ptr(cameraData.projection),
                                 m_gizmoOperation,
                                 m_gizmoMode,
                                 glm::value_ptr(entityTransform),
                                 nullptr,
                                 snapValues);
            // IsOver is global state inside ImGuizmo. Only trust it in a frame
            // where this viewport actually submitted a gizmo.
            // IsOver() covers the gizmo's projected hit regions and can be true
            // well away from a visible handle (especially at shallow camera
            // angles).  Only an interaction that the gizmo actually captured
            // should consume a viewport selection click.
            gizmoBlocksSelection = ImGuizmo::IsUsing();
            if (ImGuizmo::IsUsing())
            {
                m_isTransformGizmoUsing = true;
                ApplyWorldTransformToEntity(*selectedEntity, entityTransform);
                editorShell.MarkSceneDirty();
            }
        }

        if (viewportClicked && m_isViewportHovered && !gizmoBlocksSelection)
        {
            editorShell.SetSelectedEntity(PickEntity(editorShell.GetEngine().GetScene(), cameraData, viewportMin, viewportSize));
        }
    }

    void ViewportPanel::RenderFrame(scene::CameraComponent &cameraComponent)
    {
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &editorShell = EditorShell::GetInstance();
        auto *activeScene = editorShell.GetEngine().GetScene();
        auto &renderer = editorShell.GetEngine().GetRenderer();
        renderer.BeginFrame(m_renderTarget);
        std::vector<render::IPostProcessEffect *> postProcessEffects;
        postProcessEffects.reserve(cameraComponent.GetPostProcessEffects().size());
        for (const auto &effect : cameraComponent.GetPostProcessEffects())
        {
            postProcessEffects.push_back(effect.get());
        }

        renderer.RenderFrame(cameraComponent.GetCameraData(m_renderTarget->GetWidth(), m_renderTarget->GetHeight()),
                             m_renderTarget,
                             activeScene ? activeScene->GetLights() : std::vector<scene::Light *>{},
                             &postProcessEffects,
                             activeScene);
        renderer.EndFrame(m_renderTarget);
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
        if (m_renderTarget)
        {
            m_renderTarget->Cleanup();
            delete m_renderTarget;
            m_renderTarget = nullptr;
        }
    }
}
