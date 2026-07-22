#include "PlutoGE/editor_native.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ClothComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/NavAgentComponent.h"
#include "PlutoGE/scene/components/NavigationMeshComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/ScriptComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include "PlutoGE/scene/components/SoundEmitterComponent.h"
#include "PlutoGE/scene/components/SoundListenerComponent.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr uint32_t kApiVersion = 3;
    constexpr PlutoEditorHandle kEngineHandle = 0x504c55544f454e47ull;
    thread_local std::string g_lastError;

    void SetError(std::string message)
    {
        g_lastError = std::move(message);
    }

    struct ViewportState
    {
        PlutoEditorHandle handle = 0;
        std::unique_ptr<PlutoGE::render::RenderTarget> renderTarget;
        ImGuiContext *imguiContext = nullptr;
        uint32_t selectedEntityId = 0;
        ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
        uint64_t frameCount = 0;
        double accumulatedFrameMs = 0.0;
        double maximumFrameMs = 0.0;
        double lastResizeMs = 0.0;
        int width = 0;
        int height = 0;
        float targetRefreshHz = 0.0f;
        bool previousMouseButtons[3]{};
        bool previousFocus = false;
        bool gizmoActive = false;
        PlutoGE::render::CameraData lastCameraData{};
        bool hasCameraData = false;
    };

    struct EngineState
    {
        PlutoGE::core::Engine *engine = nullptr;
        std::unique_ptr<PlutoGE::assets::Project> project;
        std::unique_ptr<PlutoGE::scene::Scene> scene;
        std::unordered_map<PlutoEditorHandle, std::unique_ptr<ViewportState>> viewports;
        PlutoEditorHandle nextViewportHandle = 1;
        std::mutex mutex;
    };

    std::unique_ptr<EngineState> g_state;
    std::mutex g_stateMutex;

    EngineState *ResolveEngine(PlutoEditorHandle handle)
    {
        return handle == kEngineHandle ? g_state.get() : nullptr;
    }

    ViewportState *ResolveViewport(EngineState &state, PlutoEditorHandle handle)
    {
        const auto iterator = state.viewports.find(handle);
        return iterator == state.viewports.end() ? nullptr : iterator->second.get();
    }

    void CollectEntities(PlutoGE::scene::Entity *entity, std::vector<PlutoGE::scene::Entity *> &entities)
    {
        if (!entity)
        {
            return;
        }
        entities.push_back(entity);
        for (auto *child : entity->GetChildren())
        {
            CollectEntities(child, entities);
        }
    }

    std::vector<PlutoGE::scene::Entity *> FlattenScene(const PlutoGE::scene::Scene &scene)
    {
        std::vector<PlutoGE::scene::Entity *> entities;
        for (auto *root : scene.GetRootEntities())
        {
            CollectEntities(root, entities);
        }
        return entities;
    }

    void PopulateInitialScene(PlutoGE::core::Engine &engine, PlutoGE::scene::Scene &scene)
    {
        auto world = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "World"});
        auto *worldEntity = scene.AddEntity(std::move(world));

        auto previewCube = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Preview Cube"});
        previewCube->SetPosition(glm::vec3(0.0f, 0.5f, 0.0f));
        auto *previewCubeEntity = scene.AddEntity(std::move(previewCube), worldEntity);
        if (previewCubeEntity)
        {
            auto *mesh = previewCubeEntity->CreateComponent<PlutoGE::scene::MeshComponent>(PlutoGE::scene::MeshComponentConfig{
                .mesh = engine.GetAssetManager().LoadMeshAsset(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference)),
                .material = engine.GetAssetManager().LoadMaterialAsset(std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference)),
            });
            if (mesh)
            {
                mesh->SetSourceMeshPath(std::string(PlutoGE::assets::Project::kBuiltinCubeMeshReference));
                mesh->SetMaterialAssetForMaterialSlot(0, std::string(PlutoGE::assets::Project::kBuiltinDefaultShadedMaterialReference));
            }
        }

        auto directionalLight = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Directional Light"});
        auto *directionalLightEntity = scene.AddEntity(std::move(directionalLight), worldEntity);
        if (directionalLightEntity)
        {
            auto *light = directionalLightEntity->CreateComponent<PlutoGE::scene::LightComponent>();
            if (light)
            {
                light->SetLightType(PlutoGE::scene::LightType::Directional);
                light->SetIntensity(4.0f);
                light->SetDirection(glm::normalize(glm::vec3(-0.45f, -0.85f, -0.25f)));
            }
        }
        scene.AddEntity(std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Camera"}));
    }

    bool InitializeViewport(ViewportState &viewport, int width, int height)
    {
        viewport.imguiContext = ImGui::CreateContext();
        if (!viewport.imguiContext)
        {
            SetError("Failed to create the viewport ImGui context.");
            return false;
        }

        ImGui::SetCurrentContext(viewport.imguiContext);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        ImGui::StyleColorsDark();
        if (!ImGui_ImplOpenGL3_Init("#version 430 core"))
        {
            SetError("Failed to initialize the ImGui OpenGL backend.");
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
            return false;
        }

        PlutoGE::render::RenderTargetConfig targetConfig;
        targetConfig.width = std::max(width, 1);
        targetConfig.height = std::max(height, 1);
        targetConfig.clearColor = glm::vec4(0.055f, 0.065f, 0.08f, 1.0f);
        viewport.renderTarget = std::make_unique<PlutoGE::render::RenderTarget>(targetConfig);
        if (!viewport.renderTarget->IsInitialized())
        {
            SetError("Failed to create the PlutoGE viewport render target.");
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
            viewport.renderTarget.reset();
            return false;
        }

        viewport.width = targetConfig.width;
        viewport.height = targetConfig.height;
        return true;
    }

    void DestroyViewport(EngineState &state, ViewportState &viewport)
    {
        if (viewport.imguiContext)
        {
            ImGui::SetCurrentContext(viewport.imguiContext);
            ImGui_ImplOpenGL3_Shutdown();
        }
        if (viewport.renderTarget)
        {
            state.engine->GetRenderer().ReleaseRenderTarget(viewport.renderTarget.get());
            viewport.renderTarget.reset();
        }
        if (viewport.imguiContext)
        {
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
        }
    }

    PlutoGE::render::CameraData BuildCameraData(const PlutoEditorViewportFrame &frame)
    {
        const glm::vec3 position(frame.camera_x, frame.camera_y, frame.camera_z);
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = glm::rotate(transform, glm::radians(frame.camera_yaw_degrees), glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, glm::radians(frame.camera_pitch_degrees), glm::vec3(1.0f, 0.0f, 0.0f));

        PlutoGE::render::Camera camera(PlutoGE::render::CameraConfig{
            .fovY = std::clamp(frame.camera_fov_degrees, 10.0f, 150.0f),
            .nearPlane = 0.1f,
            .farPlane = 1000.0f,
        });
        return camera.GetCameraDataForTransform(transform, std::max(frame.width, 1), std::max(frame.height, 1));
    }

    bool BuildPickRay(const ViewportState &viewport,
                      float mouseX,
                      float mouseY,
                      glm::vec3 &origin,
                      glm::vec3 &direction)
    {
        if (!viewport.hasCameraData || viewport.width <= 0 || viewport.height <= 0 ||
            mouseX < 0.0f || mouseY < 0.0f ||
            mouseX > static_cast<float>(viewport.width) || mouseY > static_cast<float>(viewport.height))
        {
            return false;
        }

        const float clipX = mouseX / static_cast<float>(viewport.width) * 2.0f - 1.0f;
        const float clipY = 1.0f - mouseY / static_cast<float>(viewport.height) * 2.0f;
        glm::vec4 eyeDirection = glm::inverse(viewport.lastCameraData.projection) * glm::vec4(clipX, clipY, -1.0f, 1.0f);
        eyeDirection = glm::vec4(eyeDirection.x, eyeDirection.y, -1.0f, 0.0f);

        const glm::mat4 inverseView = glm::inverse(viewport.lastCameraData.view);
        glm::vec4 worldOrigin = inverseView * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (std::abs(worldOrigin.w) <= 0.000001f)
        {
            return false;
        }

        origin = glm::vec3(worldOrigin / worldOrigin.w);
        direction = glm::normalize(glm::vec3(inverseView * eyeDirection));
        return std::isfinite(direction.x) && std::isfinite(direction.y) && std::isfinite(direction.z);
    }

    bool IntersectSphere(const glm::vec3 &origin,
                         const glm::vec3 &direction,
                         const glm::vec3 &center,
                         float radius,
                         float &distance)
    {
        const glm::vec3 offset = origin - center;
        const float b = glm::dot(offset, direction);
        const float c = glm::dot(offset, offset) - radius * radius;
        if (c <= 0.0f)
        {
            distance = 0.0f;
            return true;
        }
        if (b > 0.0f)
        {
            return false;
        }

        const float discriminant = b * b - c;
        if (discriminant < 0.0f)
        {
            return false;
        }

        distance = std::max(0.0f, -b - std::sqrt(discriminant));
        return true;
    }

    uint32_t PickEntity(EngineState &state, ViewportState &viewport, float mouseX, float mouseY)
    {
        glm::vec3 rayOrigin{0.0f};
        glm::vec3 rayDirection{0.0f};
        if (!BuildPickRay(viewport, mouseX, mouseY, rayOrigin, rayDirection))
        {
            return 0;
        }

        PlutoGE::scene::Entity *pickedEntity = nullptr;
        float closestDistance = std::numeric_limits<float>::max();
        for (auto *entity : FlattenScene(*state.scene))
        {
            if (!entity || !entity->IsActive())
            {
                continue;
            }

            if (auto *terrain = entity->GetComponent<PlutoGE::scene::TerrainComponent>();
                terrain && terrain->IsEnabled())
            {
                glm::vec3 hitPoint{0.0f};
                if (terrain->Raycast(rayOrigin, rayDirection, hitPoint))
                {
                    const float distance = glm::length(hitPoint - rayOrigin);
                    if (distance < closestDistance)
                    {
                        closestDistance = distance;
                        pickedEntity = entity;
                    }
                }
            }

            auto *meshComponent = entity->GetComponent<PlutoGE::scene::MeshComponent>();
            if (!meshComponent || !meshComponent->IsEnabled() || !meshComponent->IsVisible() || !meshComponent->GetMesh())
            {
                continue;
            }

            const auto &bounds = meshComponent->GetMesh()->GetBounds();
            const glm::mat4 worldTransform = entity->GetWorldTransform() * meshComponent->GetMeshOffsetTransform();
            const glm::vec3 center = glm::vec3(worldTransform * glm::vec4(bounds.center, 1.0f));
            const float maximumScale = std::max({
                glm::length(glm::vec3(worldTransform[0])),
                glm::length(glm::vec3(worldTransform[1])),
                glm::length(glm::vec3(worldTransform[2]))});
            float distance = 0.0f;
            if (IntersectSphere(rayOrigin, rayDirection, center,
                                std::max(bounds.radius * maximumScale, 0.001f), distance) &&
                distance < closestDistance)
            {
                closestDistance = distance;
                pickedEntity = entity;
            }
        }

        return pickedEntity ? pickedEntity->GetID() : 0;
    }

    void ApplyGizmo(EngineState &state,
                    ViewportState &viewport,
                    const PlutoEditorViewportFrame &frame,
                    const PlutoGE::render::CameraData &cameraData)
    {
        viewport.gizmoActive = false;
        auto *entity = state.scene->FindEntityByID(viewport.selectedEntityId);
        if (!entity)
        {
            return;
        }

        glm::mat4 worldTransform = entity->GetWorldTransform();
        glm::mat4 gizmoProjection = glm::perspective(glm::radians(std::clamp(frame.camera_fov_degrees, 10.0f, 150.0f)),
                                                     static_cast<float>(std::max(frame.width, 1)) / static_cast<float>(std::max(frame.height, 1)),
                                                     0.1f,
                                                     1000.0f);
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(true);
        ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
        ImGuizmo::SetRect(0.0f, 0.0f, static_cast<float>(frame.width), static_cast<float>(frame.height));
        ImGuizmo::Manipulate(glm::value_ptr(cameraData.view),
                             glm::value_ptr(gizmoProjection),
                             viewport.gizmoOperation,
                             ImGuizmo::LOCAL,
                             glm::value_ptr(worldTransform));

        if (!ImGuizmo::IsUsing())
        {
            return;
        }

        viewport.gizmoActive = true;

        glm::mat4 localTransform = worldTransform;
        if (auto *parent = entity->GetParent())
        {
            localTransform = glm::inverse(parent->GetWorldTransform()) * worldTransform;
        }

        float translation[3]{};
        float rotation[3]{};
        float scale[3]{};
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(localTransform), translation, rotation, scale);
        entity->SetPosition(glm::make_vec3(translation));
        entity->SetRotation(glm::make_vec3(rotation));
        entity->SetScale(glm::make_vec3(scale));
    }

    void DrawOverlay(EngineState &state,
                     ViewportState &viewport,
                     const PlutoEditorViewportFrame &frame,
                     const PlutoGE::render::CameraData &cameraData)
    {
        ImGui::SetCurrentContext(viewport.imguiContext);
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2(static_cast<float>(frame.width), static_cast<float>(frame.height));
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
        io.DeltaTime = std::clamp(frame.delta_seconds, 1.0f / 1000.0f, 0.25f);
        const bool focused = frame.focused != 0;
        if (focused != viewport.previousFocus)
        {
            io.AddFocusEvent(focused);
            viewport.previousFocus = focused;
        }
        io.AddMousePosEvent(frame.mouse_x, frame.mouse_y);
        io.AddMouseWheelEvent(0.0f, frame.mouse_wheel);

        const bool buttons[3]{frame.mouse_left != 0, frame.mouse_right != 0, frame.mouse_middle != 0};
        for (int index = 0; index < 3; ++index)
        {
            if (buttons[index] != viewport.previousMouseButtons[index])
            {
                io.AddMouseButtonEvent(index, buttons[index]);
                viewport.previousMouseButtons[index] = buttons[index];
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.82f);
        constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                           ImGuiWindowFlags_AlwaysAutoResize |
                                           ImGuiWindowFlags_NoSavedSettings |
                                           ImGuiWindowFlags_NoFocusOnAppearing |
                                           ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("Viewport overlay", nullptr, flags))
        {
            if (ImGui::Button("Move")) viewport.gizmoOperation = ImGuizmo::TRANSLATE;
            ImGui::SameLine();
            if (ImGui::Button("Rotate")) viewport.gizmoOperation = ImGuizmo::ROTATE;
            ImGui::SameLine();
            if (ImGui::Button("Scale")) viewport.gizmoOperation = ImGuizmo::SCALE;
            ImGui::SameLine();
            const double average = viewport.frameCount > 0 ? viewport.accumulatedFrameMs / static_cast<double>(viewport.frameCount) : 0.0;
            ImGui::Text("%.1f Hz  %.2f ms", frame.target_refresh_hz, average);
        }
        ImGui::End();

        ApplyGizmo(state, viewport, frame, cameraData);
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    int32_t ValidateFrame(const PlutoEditorViewportFrame *frame)
    {
        if (!frame || frame->struct_size < sizeof(PlutoEditorViewportFrame) || frame->width <= 0 || frame->height <= 0 || !frame->get_proc_address)
        {
            SetError("Invalid viewport frame data.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    template <std::size_t Size>
    void CopyString(char (&destination)[Size], std::string_view source)
    {
        std::memset(destination, 0, Size);
        std::memcpy(destination, source.data(), std::min(source.size(), Size - 1));
    }

    std::vector<PlutoGE::scene::Component *> FlattenComponents(PlutoGE::scene::Entity &entity)
    {
        std::vector<PlutoGE::scene::Component *> components;
        for (const auto &bucket : entity.GetComponentBuckets())
            components.insert(components.end(), bucket.begin(), bucket.end());
        return components;
    }

    const char *GetComponentDisplayName(const PlutoGE::scene::Component &component)
    {
        using namespace PlutoGE::scene;
        if (dynamic_cast<const MeshComponent *>(&component)) return "Mesh Component";
        if (dynamic_cast<const TerrainComponent *>(&component)) return "Terrain Component";
        if (dynamic_cast<const FoliageComponent *>(&component)) return "Foliage Component";
        if (dynamic_cast<const ClothComponent *>(&component)) return "Cloth Component";
        if (dynamic_cast<const ParticleSystemComponent *>(&component)) return "Particle System Component";
        if (dynamic_cast<const SplineComponent *>(&component)) return "Spline Track Component";
        if (dynamic_cast<const OceanComponent *>(&component)) return "Ocean Component";
        if (dynamic_cast<const AnimationComponent *>(&component)) return "Animation Component";
        if (dynamic_cast<const SkeletonAttachmentComponent *>(&component)) return "Skeleton Attachment Component";
        if (dynamic_cast<const CameraComponent *>(&component)) return "Camera Component";
        if (dynamic_cast<const LightComponent *>(&component)) return "Light Component";
        if (dynamic_cast<const RigidbodyComponent *>(&component)) return "Rigidbody Component";
        if (dynamic_cast<const NavAgentComponent *>(&component)) return "Navigation Agent Component";
        if (dynamic_cast<const NavigationMeshComponent *>(&component)) return "Navigation Mesh Component";
        if (dynamic_cast<const ColliderComponent *>(&component)) return "Collider Component";
        if (dynamic_cast<const IblCaptureComponent *>(&component)) return "IBL Capture Component";
        if (dynamic_cast<const VolumetricCloudComponent *>(&component)) return "Volumetric Cloud Component";
        if (dynamic_cast<const PhysicalSkyComponent *>(&component)) return "Physical Sky Component";
        if (dynamic_cast<const ScriptComponent *>(&component)) return "Script Component";
        if (dynamic_cast<const SoundEmitterComponent *>(&component)) return "Sound Emitter Component";
        if (dynamic_cast<const SoundListenerComponent *>(&component)) return "Sound Listener Component";
        if (dynamic_cast<const CanvasComponent *>(&component)) return "Canvas Component";
        if (dynamic_cast<const RectTransformComponent *>(&component)) return "Rect Transform Component";
        if (dynamic_cast<const UIImageComponent *>(&component)) return "UI Image Component";
        if (dynamic_cast<const UITextComponent *>(&component)) return "UI Text Component";
        if (dynamic_cast<const UIButtonComponent *>(&component)) return "UI Button Component";
        return "Component";
    }

    void ReplaceScene(EngineState &state, std::unique_ptr<PlutoGE::scene::Scene> scene)
    {
        state.engine->SetScene(nullptr);
        state.scene = std::move(scene);
        state.engine->SetScene(state.scene.get());
        for (auto &[handle, viewport] : state.viewports)
            viewport->selectedEntityId = 0;
    }
}

extern "C"
{
    uint32_t pluto_editor_api_version(void)
    {
        return kApiVersion;
    }

    int32_t pluto_editor_engine_create(const PlutoEditorEngineConfig *config, PlutoEditorHandle *engineHandle)
    {
        if (!config || !engineHandle || config->struct_size < sizeof(PlutoEditorEngineConfig) || config->api_version != kApiVersion || !config->get_proc_address)
        {
            SetError("Invalid engine configuration or API version.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }

        std::scoped_lock stateLock(g_stateMutex);
        if (g_state)
        {
            SetError("Only one PlutoGE engine instance may be hosted in a process.");
            return PLUTO_EDITOR_ALREADY_INITIALIZED;
        }

        auto state = std::make_unique<EngineState>();
        state->engine = &PlutoGE::core::Engine::GetInstance();
        PlutoGE::core::EngineConfig engineConfig;
        engineConfig.windowConfig.title = "PlutoGE Avalonia Host";
        engineConfig.windowConfig.width = std::max(config->initial_width, 1);
        engineConfig.windowConfig.height = std::max(config->initial_height, 1);
        engineConfig.windowConfig.visible = false;
        engineConfig.windowConfig.externalOpenGLProcAddress = config->get_proc_address;
        engineConfig.windowConfig.externalOpenGLUserData = config->user_data;
        if (!state->engine->Initialize(engineConfig))
        {
            SetError("PlutoGE requires a current desktop OpenGL 4.3 context.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

        state->scene = std::make_unique<PlutoGE::scene::Scene>();
        PopulateInitialScene(*state->engine, *state->scene);
        state->engine->SetScene(state->scene.get());
        g_state = std::move(state);
        *engineHandle = kEngineHandle;
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_engine_destroy(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            SetError("Invalid engine handle.");
            return PLUTO_EDITOR_INVALID_HANDLE;
        }

        {
            std::scoped_lock engineLock(state->mutex);
            for (auto &[handle, viewport] : state->viewports)
            {
                DestroyViewport(*state, *viewport);
            }
            state->viewports.clear();
            state->engine->SetScene(nullptr);
            state->scene.reset();
            state->engine->Shutdown();
        }
        g_state.reset();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_create(PlutoEditorHandle engineHandle, PlutoEditorHandle *viewportHandle)
    {
        if (!viewportHandle)
        {
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto viewport = std::make_unique<ViewportState>();
        viewport->handle = ++state->nextViewportHandle;
        *viewportHandle = viewport->handle;
        state->viewports.emplace(viewport->handle, std::move(viewport));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_destroy(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        const auto iterator = state->viewports.find(viewportHandle);
        if (iterator == state->viewports.end())
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        DestroyViewport(*state, *iterator->second);
        state->viewports.erase(iterator);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_render(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, const PlutoEditorViewportFrame *frame)
    {
        const int32_t frameValidation = ValidateFrame(frame);
        if (frameValidation != PLUTO_EDITOR_OK)
        {
            return frameValidation;
        }

        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }

        auto &window = state->engine->GetWindow();
        window.SetExternalOpenGLContext(frame->get_proc_address, frame->user_data);
        window.SetExternalExtents(frame->width, frame->height);
        if (!window.EnsureOpenGLContextCurrent(viewport->renderTarget == nullptr))
        {
            SetError("The current viewport context is not desktop OpenGL 4.3 or is not shared with the engine context.");
            return viewport->renderTarget ? PLUTO_EDITOR_CONTEXT_NOT_SHARED : PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

        if (!viewport->renderTarget && !InitializeViewport(*viewport, frame->width, frame->height))
        {
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        if (glIsTexture(viewport->renderTarget->GetColorTextureID()) == GL_FALSE)
        {
            SetError("Avalonia created a viewport context that does not share PlutoGE OpenGL objects.");
            return PLUTO_EDITOR_CONTEXT_NOT_SHARED;
        }

        if (viewport->width != frame->width || viewport->height != frame->height)
        {
            const auto resizeStart = std::chrono::steady_clock::now();
            if (!viewport->renderTarget->Resize(frame->width, frame->height))
            {
                SetError("Failed to resize the viewport render target.");
                return PLUTO_EDITOR_INTERNAL_ERROR;
            }
            viewport->lastResizeMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resizeStart).count();
            viewport->width = frame->width;
            viewport->height = frame->height;
        }

        auto &renderer = state->engine->GetRenderer();
        renderer.BeginProfilingFrame();
        renderer.ClearRenderCommands();
        state->scene->SubmitRenderCommands();
        const auto cameraData = BuildCameraData(*frame);
        viewport->lastCameraData = cameraData;
        viewport->hasCameraData = true;
        renderer.BeginFrame(viewport->renderTarget.get());
        renderer.RenderFrame(cameraData,
                             viewport->renderTarget.get(),
                             state->scene->GetLights(),
                             nullptr,
                             state->scene.get(),
                             true,
                             true);
        renderer.EndFrame(viewport->renderTarget.get());

        glBindFramebuffer(GL_READ_FRAMEBUFFER, viewport->renderTarget->GetFramebufferID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(frame->framebuffer));
        glBlitFramebuffer(0, 0, frame->width, frame->height,
                          0, 0, frame->width, frame->height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(frame->framebuffer));
        glViewport(0, 0, frame->width, frame->height);

        DrawOverlay(*state, *viewport, *frame, cameraData);

        const double frameMs = std::max(0.0, static_cast<double>(frame->delta_seconds) * 1000.0);
        ++viewport->frameCount;
        viewport->accumulatedFrameMs += frameMs;
        viewport->maximumFrameMs = std::max(viewport->maximumFrameMs, frameMs);
        viewport->targetRefreshHz = frame->target_refresh_hz;
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_set_selected_entity(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, uint32_t entityId)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        viewport->selectedEntityId = entityId;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_pick_entity(PlutoEditorHandle engineHandle,
                                              PlutoEditorHandle viewportHandle,
                                              float mouseX,
                                              float mouseY,
                                              uint32_t *entityId)
    {
        if (!entityId || !std::isfinite(mouseX) || !std::isfinite(mouseY))
        {
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }

        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }

        ImGui::SetCurrentContext(viewport->imguiContext);
        if (viewport->imguiContext && (ImGui::GetIO().WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
        {
            *entityId = viewport->selectedEntityId;
            return PLUTO_EDITOR_OK;
        }

        viewport->selectedEntityId = PickEntity(*state, *viewport, mouseX, mouseY);
        *entityId = viewport->selectedEntityId;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_set_gizmo_operation(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, int32_t operation)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport)
        {
            return PLUTO_EDITOR_INVALID_HANDLE;
        }
        switch (operation)
        {
        case PLUTO_EDITOR_GIZMO_TRANSLATE: viewport->gizmoOperation = ImGuizmo::TRANSLATE; break;
        case PLUTO_EDITOR_GIZMO_ROTATE: viewport->gizmoOperation = ImGuizmo::ROTATE; break;
        case PLUTO_EDITOR_GIZMO_SCALE: viewport->gizmoOperation = ImGuizmo::SCALE; break;
        default: return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_get_gizmo_active(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, uint8_t *active)
    {
        if (!active) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport) return PLUTO_EDITOR_INVALID_HANDLE;
        *active = viewport->gizmoActive ? 1 : 0;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_viewport_get_stats(PlutoEditorHandle engineHandle, PlutoEditorHandle viewportHandle, PlutoEditorFrameStats *stats)
    {
        if (!stats) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *viewport = ResolveViewport(*state, viewportHandle);
        if (!viewport) return PLUTO_EDITOR_INVALID_HANDLE;
        stats->frame_count = viewport->frameCount;
        stats->average_frame_ms = viewport->frameCount ? viewport->accumulatedFrameMs / static_cast<double>(viewport->frameCount) : 0.0;
        stats->maximum_frame_ms = viewport->maximumFrameMs;
        stats->resize_ms = viewport->lastResizeMs;
        stats->width = viewport->width;
        stats->height = viewport->height;
        stats->target_refresh_hz = viewport->targetRefreshHz;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_get_entity_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        *count = static_cast<uint32_t>(FlattenScene(*state->scene).size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_get_entity(PlutoEditorHandle engineHandle, uint32_t index, PlutoEditorEntityInfo *entityInfo)
    {
        if (!entityInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        const auto entities = FlattenScene(*state->scene);
        if (index >= entities.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto *entity = entities[index];
        entityInfo->id = entity->GetID();
        entityInfo->parent_id = entity->GetParent() ? entity->GetParent()->GetID() : 0;
        entityInfo->active = entity->IsSelfActive() ? 1 : 0;
        std::memset(entityInfo->name, 0, sizeof(entityInfo->name));
        const std::string name = entity->GetName();
        std::memcpy(entityInfo->name, name.data(), std::min(name.size(), sizeof(entityInfo->name) - 1));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_transform(PlutoEditorHandle engineHandle, uint32_t entityId, PlutoEditorTransform *transform)
    {
        if (!transform) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        const auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const glm::vec3 position = entity->GetPosition();
        const glm::vec3 rotation = entity->GetRotation();
        const glm::vec3 scale = entity->GetScale();
        std::memcpy(transform->position, glm::value_ptr(position), sizeof(transform->position));
        std::memcpy(transform->rotation, glm::value_ptr(rotation), sizeof(transform->rotation));
        std::memcpy(transform->scale, glm::value_ptr(scale), sizeof(transform->scale));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_set_transform(PlutoEditorHandle engineHandle, uint32_t entityId, const PlutoEditorTransform *transform)
    {
        if (!transform) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        entity->SetPosition(glm::make_vec3(transform->position));
        entity->SetRotation(glm::make_vec3(transform->rotation));
        entity->SetScale(glm::make_vec3(transform->scale));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_load(PlutoEditorHandle engineHandle, const char *manifestPath)
    {
        if (!manifestPath || manifestPath[0] == '\0') return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);

        std::string errorMessage;
        auto project = PlutoGE::assets::Project::Load(manifestPath, &errorMessage);
        if (!project)
        {
            SetError(errorMessage.empty() ? "Failed to load project." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        project->RefreshAssetRegistry();
        state->engine->GetAssetManager().SetProjectContext(
            project->GetRootDirectory().string(), project->GetManifest().assetDirectory);

        std::unique_ptr<PlutoGE::scene::Scene> scene;
        const auto &startupScene = project->GetManifest().startupScene;
        if (!startupScene.empty())
        {
            const auto scenePath = project->ResolveAssetReference(startupScene);
            if (!scenePath.empty())
                scene = PlutoGE::scene::SceneSerializer::Load(scenePath.string(), &errorMessage);
        }
        if (!scene)
            scene = std::make_unique<PlutoGE::scene::Scene>();

        state->project = std::move(project);
        ReplaceScene(*state, std::move(scene));
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_info(PlutoEditorHandle engineHandle, PlutoEditorProjectInfo *projectInfo)
    {
        if (!projectInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project)
        {
            SetError("No project is loaded.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        const auto &manifest = state->project->GetManifest();
        CopyString(projectInfo->name, manifest.name);
        CopyString(projectInfo->manifest_path, state->project->GetManifestPath().string());
        CopyString(projectInfo->asset_directory, state->project->GetAssetDirectoryPath().string());
        CopyString(projectInfo->startup_scene, manifest.startupScene);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_asset_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(state->project->GetManifest().assetEntries.size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_asset(PlutoEditorHandle engineHandle, uint32_t index, PlutoEditorAssetInfo *assetInfo)
    {
        if (!assetInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &assets = state->project->GetManifest().assetEntries;
        if (index >= assets.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &asset = assets[index];
        assetInfo->type = static_cast<int32_t>(asset.type);
        assetInfo->size = static_cast<uint64_t>(asset.size);
        CopyString(assetInfo->reference, asset.reference);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_load(PlutoEditorHandle engineHandle, const char *pathOrReference)
    {
        if (!pathOrReference || pathOrReference[0] == '\0') return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);

        std::filesystem::path scenePath(pathOrReference);
        if (state->project && PlutoGE::assets::Project::IsProjectAssetReference(pathOrReference))
            scenePath = state->project->ResolveAssetReference(pathOrReference);

        std::string errorMessage;
        auto scene = PlutoGE::scene::SceneSerializer::Load(scenePath.string(), &errorMessage);
        if (!scene)
        {
            SetError(errorMessage.empty() ? "Failed to load scene." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }
        ReplaceScene(*state, std::move(scene));
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_get_info(PlutoEditorHandle engineHandle, PlutoEditorSceneInfo *sceneInfo)
    {
        if (!sceneInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        CopyString(sceneInfo->path, state->scene ? state->scene->GetFilePath() : std::string{});
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_component_count(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(FlattenComponents(*entity).size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_component(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, PlutoEditorComponentInfo *componentInfo)
    {
        if (!componentInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto *component = components[componentIndex];
        componentInfo->index = componentIndex;
        componentInfo->enabled = component->IsEnabled() ? 1 : 0;
        CopyString(componentInfo->name, GetComponentDisplayName(*component));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_get_property_count(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(components[componentIndex]->Serialize().size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_get_property(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint32_t propertyIndex, PlutoEditorComponentProperty *propertyInfo)
    {
        if (!propertyInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto properties = components[componentIndex]->Serialize();
        if (propertyIndex >= properties.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &property = properties[propertyIndex];
        propertyInfo->type = static_cast<int32_t>(property.type);
        CopyString(propertyInfo->name, property.name);
        CopyString(propertyInfo->value, property.value);
        return PLUTO_EDITOR_OK;
    }

    const char *pluto_editor_get_last_error(void)
    {
        return g_lastError.c_str();
    }
}
