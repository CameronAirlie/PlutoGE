#include "PlutoGE/ui/panels/ViewportPanel.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/render/Renderer.h"
#include <ImGuizmo.h>
#include <iostream>

#include <algorithm>
#include <optional>

#include <imgui.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
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
        };

        struct PickRay
        {
            glm::vec3 origin{0.0f};
            glm::vec3 direction{0.0f, 0.0f, -1.0f};
        };

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

            const glm::vec3 origin = glm::vec3(nearPoint);
            const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));
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
                if (!entity || !entity->IsActive())
                {
                    continue;
                }

                auto *meshComponent = entity->GetComponent<scene::MeshComponent>();
                if (!meshComponent || !meshComponent->GetMesh())
                {
                    continue;
                }

                const glm::mat4 worldTransform = entity->GetWorldTransform();
                const glm::mat4 inverseWorldTransform = glm::inverse(worldTransform);
                glm::vec3 localOrigin = glm::vec3(inverseWorldTransform * glm::vec4(ray->origin, 1.0f));
                glm::vec3 localDirection = glm::vec3(inverseWorldTransform * glm::vec4(ray->direction, 0.0f));
                const float directionLengthSquared = glm::dot(localDirection, localDirection);
                if (directionLengthSquared <= kRayEpsilon)
                {
                    continue;
                }
                localDirection = glm::normalize(localDirection);

                render::Mesh *mesh = meshComponent->GetMesh();
                if (!IntersectBounds(mesh->GetBounds(), localOrigin, localDirection))
                {
                    continue;
                }

                const auto &meshData = mesh->GetMeshData();
                if (meshData.vertices.empty() || meshData.indices.size() < 3)
                {
                    continue;
                }

                for (std::size_t index = 0; index + 2 < meshData.indices.size(); index += 3)
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
                    const glm::vec3 worldHitPoint = glm::vec3(worldTransform * glm::vec4(localHitPoint, 1.0f));
                    const float worldDistance = glm::length(worldHitPoint - ray->origin);
                    if (worldDistance < selectedDistance)
                    {
                        selectedDistance = worldDistance;
                        selectedEntity = entity;
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

        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        int debugView = static_cast<int>(renderer.GetPostProcessDebugView());
        if (m_panelControlsEnabled)
        {
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::Combo("Debug View", &debugView, kDebugViewLabels, IM_ARRAYSIZE(kDebugViewLabels)))
            {
                renderer.SetPostProcessDebugView(static_cast<render::PostProcessDebugView>(debugView));
            }
            if (m_config.editorViewport)
            {
                ImGui::Separator();
                RenderEditorToolbar();
            }
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::SliderFloat("Render Scale", &m_renderScale, kMinRenderScale, kMaxRenderScale, "%.2fx"))
            {
                m_renderScale = glm::clamp(m_renderScale, kMinRenderScale, kMaxRenderScale);
                m_resizeStableFrames = kResizeDebounceFrames;
            }
            ImGui::Separator();
        }

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
        const bool viewportClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        m_isViewportHovered = ImGui::IsItemHovered();
        m_isViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        if (m_config.editorViewport)
        {
            RenderEditorOverlays(viewportMin, imageSize, viewportClicked);
        }
    }

    void ViewportPanel::RenderEditorToolbar()
    {
        const bool allowEditorViewportHotkeys =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            !EditorShell::GetInstance().GetEngine().IsRuntimeRunning();

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
        }

        ImGui::TextUnformatted("Transform");
        ImGui::SameLine();
        if (ImGui::RadioButton("Position (W)", m_gizmoOperation == ImGuizmo::TRANSLATE))
        {
            m_gizmoOperation = ImGuizmo::TRANSLATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotation (E)", m_gizmoOperation == ImGuizmo::ROTATE))
        {
            m_gizmoOperation = ImGuizmo::ROTATE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale (R)", m_gizmoOperation == ImGuizmo::SCALE))
        {
            m_gizmoOperation = ImGuizmo::SCALE;
        }

        ImGui::TextUnformatted("Space");
        ImGui::SameLine();
        if (ImGui::RadioButton("Local", m_gizmoMode == ImGuizmo::LOCAL))
        {
            m_gizmoMode = ImGuizmo::LOCAL;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("World", m_gizmoMode == ImGuizmo::WORLD))
        {
            m_gizmoMode = ImGuizmo::WORLD;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Grid", &m_showGrid);

        ImGui::Checkbox("Snap", &m_enableSnap);
        if (m_enableSnap)
        {
            switch (m_gizmoOperation)
            {
            case ImGuizmo::TRANSLATE:
                ImGui::SetNextItemWidth(240.0f);
                ImGui::DragFloat3("Move Snap", &m_translateSnap.x, 0.05f, 0.01f, 100.0f, "%.2f");
                break;
            case ImGuizmo::ROTATE:
                ImGui::SetNextItemWidth(180.0f);
                ImGui::DragFloat("Rotate Snap", &m_rotateSnapDegrees, 0.5f, 0.1f, 180.0f, "%.1f deg");
                break;
            case ImGuizmo::SCALE:
                ImGui::SetNextItemWidth(180.0f);
                ImGui::DragFloat("Scale Snap", &m_scaleSnap, 0.01f, 0.01f, 10.0f, "%.2f");
                break;
            case ImGuizmo::BOUNDS:
            default:
                break;
            }
        }

        ImGui::Separator();
    }

    void ViewportPanel::RenderEditorOverlays(const ImVec2 &viewportMin, const ImVec2 &viewportSize, bool viewportClicked)
    {
        m_isTransformGizmoUsing = false;
        if (!m_renderTarget || !m_renderTarget->IsInitialized() || viewportSize.x <= 0.0f || viewportSize.y <= 0.0f)
        {
            return;
        }

        auto &editorShell = EditorShell::GetInstance();
        if (editorShell.GetEngine().IsRuntimeRunning())
        {
            return;
        }

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

        if (auto *selectedEntity = editorShell.GetSelectedEntity())
        {
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
            if (ImGuizmo::IsUsing())
            {
                m_isTransformGizmoUsing = true;
                ApplyWorldTransformToEntity(*selectedEntity, entityTransform);
            }
        }

        if (viewportClicked && m_isViewportHovered && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing())
        {
            editorShell.SetSelectedEntity(PickEntity(editorShell.GetEngine().GetScene(), cameraData, viewportMin, viewportSize));
        }
    }

    void ViewportPanel::RenderFrame(scene::CameraComponent &cameraComponent)
    {
        if (!m_renderTarget || !m_renderTarget->IsInitialized())
            return;

        auto &renderer = EditorShell::GetInstance().GetEngine().GetRenderer();
        renderer.BeginFrame(m_renderTarget);
        auto lights = EditorShell::GetInstance().GetEngine().GetScene()->GetLights();
        renderer.RenderFrame(cameraComponent, m_renderTarget, lights);
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
