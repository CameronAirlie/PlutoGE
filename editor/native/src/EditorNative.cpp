#include "PlutoGE/editor_native.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
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
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <limits>
#include <map>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#endif

namespace
{
    constexpr uint32_t kApiVersion = 10;
    constexpr PlutoEditorHandle kEngineHandle = 0x504c55544f454e47ull;
    constexpr std::size_t kSharedRenderTargetCount = 3;
    thread_local std::string g_lastError;

    void SetError(std::string message)
    {
        g_lastError = std::move(message);
    }

    struct SharedRenderSlot
    {
        std::unique_ptr<PlutoGE::render::RenderTarget> renderTarget;
        GLsync producerFence = nullptr;
        GLsync consumerFence = nullptr;
        uint64_t serial = 0;
    };

    struct ViewportState
    {
        PlutoEditorHandle handle = 0;
        std::unique_ptr<PlutoGE::render::RenderTarget> renderTarget;
        std::array<SharedRenderSlot, kSharedRenderTargetCount> sharedRenderSlots;
        std::size_t nextSharedRenderSlot = 0;
        uint64_t nextSharedRenderSerial = 1;
        PlutoEditorViewportFrame latestFrame{};
        bool hasLatestFrame = false;
        std::chrono::steady_clock::time_point lastFrameSubmission{};
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
        float gpuFrameMs = -1.0f;
        bool previousMouseButtons[3]{};
        bool previousFocus = false;
        bool gizmoActive = false;
        bool overlayWantsMouse = false;
        std::string workerError;
        PlutoGE::render::CameraData lastCameraData{};
        bool hasCameraData = false;
    };

#if defined(_WIN32)
    class SharedWglContext
    {
    public:
        bool Create()
        {
            m_sourceContext = wglGetCurrentContext();
            HDC sourceDc = wglGetCurrentDC();
            if (!m_sourceContext || !sourceDc)
            {
                m_error = "Avalonia did not expose a current WGL context.";
                return false;
            }

            const int pixelFormat = GetPixelFormat(sourceDc);
            PIXELFORMATDESCRIPTOR descriptor{};
            descriptor.nSize = sizeof(descriptor);
            descriptor.nVersion = 1;
            if (pixelFormat <= 0 ||
                DescribePixelFormat(sourceDc, pixelFormat, sizeof(descriptor), &descriptor) == 0)
            {
                m_error = "Unable to inspect Avalonia's WGL pixel format.";
                return false;
            }

            constexpr wchar_t windowClassName[] = L"PlutoGESharedRenderContextWindow";
            const HINSTANCE module = GetModuleHandleW(nullptr);
            WNDCLASSEXW windowClass{};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.style = CS_OWNDC;
            windowClass.lpfnWndProc = DefWindowProcW;
            windowClass.hInstance = module;
            windowClass.lpszClassName = windowClassName;
            if (!RegisterClassExW(&windowClass) &&
                GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            {
                m_error = "Unable to register the hidden WGL render-window class.";
                return false;
            }

            m_window = CreateWindowExW(
                0, windowClassName, L"PlutoGE shared render context", WS_POPUP,
                0, 0, 1, 1, nullptr, nullptr, module, nullptr);
            if (!m_window)
            {
                m_error = "Unable to create the hidden WGL render window.";
                return false;
            }

            m_dc = GetDC(m_window);
            if (!m_dc || !SetPixelFormat(m_dc, pixelFormat, &descriptor))
            {
                m_error = "Unable to apply Avalonia's pixel format to the shared WGL context.";
                Destroy();
                return false;
            }

            using CreateContextAttribs = HGLRC(WINAPI *)(HDC, HGLRC, const int *);
            auto createContextAttribs = reinterpret_cast<CreateContextAttribs>(
                wglGetProcAddress("wglCreateContextAttribsARB"));
            if (!createContextAttribs)
            {
                m_error = "WGL_ARB_create_context is unavailable.";
                Destroy();
                return false;
            }

            constexpr int WglContextMajorVersionArb = 0x2091;
            constexpr int WglContextMinorVersionArb = 0x2092;
            constexpr int WglContextProfileMaskArb = 0x9126;
            constexpr int WglContextCoreProfileBitArb = 0x00000001;
            const int attributes[]{
                WglContextMajorVersionArb, 4,
                WglContextMinorVersionArb, 3,
                WglContextProfileMaskArb, WglContextCoreProfileBitArb,
                0,
            };
            m_context = createContextAttribs(m_dc, m_sourceContext, attributes);
            if (!m_context)
            {
                m_error = "Unable to create an OpenGL 4.3 context in Avalonia's share group.";
                Destroy();
                return false;
            }
            return true;
        }

        bool MakeCurrent()
        {
            if (!m_dc || !m_context || !wglMakeCurrent(m_dc, m_context))
            {
                m_error = "Unable to make the shared WGL render context current.";
                return false;
            }
            return true;
        }

        void ClearCurrent()
        {
            if (wglGetCurrentContext() == m_context)
                wglMakeCurrent(nullptr, nullptr);
        }

        void Destroy()
        {
            ClearCurrent();
            if (m_context)
            {
                wglDeleteContext(m_context);
                m_context = nullptr;
            }
            if (m_dc && m_window)
            {
                ReleaseDC(m_window, m_dc);
                m_dc = nullptr;
            }
            if (m_window)
            {
                DestroyWindow(m_window);
                m_window = nullptr;
            }
            m_sourceContext = nullptr;
        }

        [[nodiscard]] const std::string &GetError() const { return m_error; }

        ~SharedWglContext() { Destroy(); }

    private:
        HWND m_window = nullptr;
        HDC m_dc = nullptr;
        HGLRC m_context = nullptr;
        HGLRC m_sourceContext = nullptr;
        std::string m_error;
    };
#endif

    struct PendingComponentEdit
    {
        uint32_t entityId = 0;
        uint32_t componentIndex = 0;
        uint32_t propertyIndex = 0;
        std::string value;
    };

    struct EngineState
    {
        PlutoGE::core::Engine *engine = nullptr;
        std::unique_ptr<PlutoGE::assets::Project> project;
        std::unique_ptr<PlutoGE::scene::Scene> scene;
        std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> editorCameraPostProcessEffects;
        std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> retiredPostProcessEffects;
        std::unordered_map<PlutoEditorHandle, std::unique_ptr<ViewportState>> viewports;
        std::vector<std::unique_ptr<ViewportState>> retiredViewports;
        std::vector<PendingComponentEdit> pendingComponentEdits;
        std::string runtimeSceneSnapshot;
        std::string runtimeScenePath;
        PlutoEditorHandle nextViewportHandle = 1;
        std::mutex mutex;
        std::condition_variable renderCondition;
        std::thread renderThread;
        bool workerEnabled = false;
        bool workerStopRequested = false;
        bool workerPauseRequested = false;
        bool workerPaused = false;
        bool workerOperational = false;
        bool workerInitializationComplete = false;
        bool workerInitializationSucceeded = false;
        std::string workerError;
#if defined(_WIN32)
        std::unique_ptr<SharedWglContext> sharedContext;
        HDC pausedCallerDc = nullptr;
        HGLRC pausedCallerContext = nullptr;
        DWORD pausedCallerThreadId = 0;
#endif
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

    std::unique_ptr<PlutoGE::render::RenderTarget> CreateViewportRenderTarget(int width, int height)
    {
        PlutoGE::render::RenderTargetConfig targetConfig;
        targetConfig.width = std::max(width, 1);
        targetConfig.height = std::max(height, 1);
        targetConfig.clearColor = glm::vec4(0.055f, 0.065f, 0.08f, 1.0f);
        auto renderTarget = std::make_unique<PlutoGE::render::RenderTarget>(targetConfig);
        return renderTarget->IsInitialized() ? std::move(renderTarget) : nullptr;
    }

    bool InitializeViewport(ViewportState &viewport, int width, int height, bool sharedWorker)
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

        if (sharedWorker)
        {
            for (auto &slot : viewport.sharedRenderSlots)
            {
                slot.renderTarget = CreateViewportRenderTarget(width, height);
                if (!slot.renderTarget)
                    break;
            }
        }
        else
        {
            viewport.renderTarget = CreateViewportRenderTarget(width, height);
        }

        const bool targetsInitialized = sharedWorker
                                            ? std::all_of(
                                                  viewport.sharedRenderSlots.begin(),
                                                  viewport.sharedRenderSlots.end(),
                                                  [](const SharedRenderSlot &slot)
                                                  { return slot.renderTarget != nullptr; })
                                            : viewport.renderTarget != nullptr;
        if (!targetsInitialized)
        {
            SetError("Failed to create the PlutoGE viewport render target.");
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(viewport.imguiContext);
            viewport.imguiContext = nullptr;
            viewport.renderTarget.reset();
            for (auto &slot : viewport.sharedRenderSlots)
            {
                if (slot.renderTarget)
                {
                    slot.renderTarget->Cleanup();
                    slot.renderTarget.reset();
                }
            }
            return false;
        }

        viewport.width = std::max(width, 1);
        viewport.height = std::max(height, 1);
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
        for (auto &slot : viewport.sharedRenderSlots)
        {
            if (slot.consumerFence)
            {
                glWaitSync(slot.consumerFence, 0, GL_TIMEOUT_IGNORED);
                glDeleteSync(slot.consumerFence);
                slot.consumerFence = nullptr;
            }
            if (slot.producerFence)
            {
                glDeleteSync(slot.producerFence);
                slot.producerFence = nullptr;
            }
            if (slot.renderTarget)
            {
                state.engine->GetRenderer().ReleaseRenderTarget(slot.renderTarget.get());
                slot.renderTarget.reset();
            }
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
            .nearPlane = std::clamp(frame.camera_near_plane, 0.001f, 1000.0f),
            .farPlane = std::max(frame.camera_far_plane, frame.camera_near_plane + 0.001f),
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
        viewport.overlayWantsMouse = io.WantCaptureMouse || ImGuizmo::IsOver() || ImGuizmo::IsUsing();
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

    void CopyString(char *destination, uint32_t destinationSize, std::string_view source)
    {
        if (!destination || destinationSize == 0)
            return;
        std::memset(destination, 0, destinationSize);
        std::memcpy(destination, source.data(), std::min(source.size(), static_cast<size_t>(destinationSize - 1)));
    }

    std::string JoinOptions(const std::vector<std::string> &options)
    {
        std::string joined;
        for (const auto &option : options)
        {
            if (!joined.empty()) joined.push_back('\n');
            joined += option;
        }
        return joined;
    }

    bool IsEditableProperty(const PlutoGE::scene::Property &property)
    {
        if (property.name == "PostProcessEffectCount") return false;
        return property.name.size() < 5 || property.name.substr(property.name.size() - 5) != ".Type";
    }

    std::vector<PlutoGE::scene::Component *> FlattenComponents(PlutoGE::scene::Entity &entity);

    bool ApplyPendingComponentEdits(EngineState &state)
    {
        if (state.pendingComponentEdits.empty()) return true;

        std::map<std::pair<uint32_t, uint32_t>, std::vector<PendingComponentEdit>> groupedEdits;
        for (auto &edit : state.pendingComponentEdits)
            groupedEdits[{edit.entityId, edit.componentIndex}].push_back(std::move(edit));
        state.pendingComponentEdits.clear();

        for (const auto &[componentKey, edits] : groupedEdits)
        {
            auto *entity = state.scene->FindEntityByID(componentKey.first);
            if (!entity)
            {
                SetError("The component's entity no longer exists.");
                return false;
            }
            const auto components = FlattenComponents(*entity);
            if (componentKey.second >= components.size())
            {
                SetError("The component no longer exists.");
                return false;
            }

            auto properties = components[componentKey.second]->Serialize();
            for (const auto &edit : edits)
            {
                if (edit.propertyIndex >= properties.size() || !IsEditableProperty(properties[edit.propertyIndex]))
                {
                    SetError("The component property is no longer available.");
                    return false;
                }
                properties[edit.propertyIndex].value = edit.value;
            }

            try
            {
                components[componentKey.second]->Deserialize(properties);
            }
            catch (const std::exception &exception)
            {
                SetError(exception.what());
                return false;
            }
        }
        return true;
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

    enum class AddableComponentType
    {
        Mesh, Terrain, Spline, Ocean, Foliage, Cloth, ParticleSystem, Camera, Light,
        Animation, Rigidbody, Collider, NavAgent, NavigationMesh, IblCapture,
        PhysicalSky, VolumetricCloud, Script, SoundEmitter, SoundListener,
        Canvas, RectTransform, UIImage, UIText, UIButton,
    };

    struct AddableComponentDefinition
    {
        const char *typeName;
        const char *displayName;
        const char *category;
        AddableComponentType type;
    };

    constexpr AddableComponentDefinition kAddableComponentDefinitions[] = {
        {"MeshComponent", "Mesh", "Rendering", AddableComponentType::Mesh},
        {"TerrainComponent", "Terrain", "Rendering", AddableComponentType::Terrain},
        {"SplineComponent", "Spline Track", "Rendering", AddableComponentType::Spline},
        {"OceanComponent", "Ocean", "Rendering", AddableComponentType::Ocean},
        {"FoliageComponent", "Foliage", "Rendering", AddableComponentType::Foliage},
        {"ClothComponent", "Cloth", "Rendering", AddableComponentType::Cloth},
        {"ParticleSystemComponent", "Particle System", "Rendering", AddableComponentType::ParticleSystem},
        {"CameraComponent", "Camera", "Rendering", AddableComponentType::Camera},
        {"LightComponent", "Light", "Rendering", AddableComponentType::Light},
        {"AnimationComponent", "Animation", "Animation", AddableComponentType::Animation},
        {"RigidbodyComponent", "Rigidbody", "Physics", AddableComponentType::Rigidbody},
        {"ColliderComponent", "Collider", "Physics", AddableComponentType::Collider},
        {"NavAgentComponent", "Navigation Agent", "AI", AddableComponentType::NavAgent},
        {"NavigationMeshComponent", "Navigation Mesh", "AI", AddableComponentType::NavigationMesh},
        {"IblCaptureComponent", "IBL Capture", "Environment", AddableComponentType::IblCapture},
        {"PhysicalSkyComponent", "Physical Sky", "Environment", AddableComponentType::PhysicalSky},
        {"VolumetricCloudComponent", "Volumetric Cloud", "Environment", AddableComponentType::VolumetricCloud},
        {"ScriptComponent", "Script", "Scripting", AddableComponentType::Script},
        {"SoundEmitterComponent", "Sound Emitter", "Audio", AddableComponentType::SoundEmitter},
        {"SoundListenerComponent", "Sound Listener", "Audio", AddableComponentType::SoundListener},
        {"CanvasComponent", "Canvas", "UI", AddableComponentType::Canvas},
        {"RectTransformComponent", "Rect Transform", "UI", AddableComponentType::RectTransform},
        {"UIImageComponent", "Image", "UI", AddableComponentType::UIImage},
        {"UITextComponent", "Text", "UI", AddableComponentType::UIText},
        {"UIButtonComponent", "Button", "UI", AddableComponentType::UIButton},
    };

    bool CanAddComponent(const PlutoGE::scene::Entity &entity, AddableComponentType type)
    {
        using namespace PlutoGE::scene;
        switch (type)
        {
        case AddableComponentType::Mesh: return !entity.HasComponent<MeshComponent>();
        case AddableComponentType::Terrain: return !entity.HasComponent<TerrainComponent>();
        case AddableComponentType::Spline: return !entity.HasComponent<SplineComponent>();
        case AddableComponentType::Ocean: return !entity.HasComponent<OceanComponent>();
        case AddableComponentType::Foliage: return !entity.HasComponent<FoliageComponent>();
        case AddableComponentType::Cloth: return !entity.HasComponent<ClothComponent>();
        case AddableComponentType::ParticleSystem: return !entity.HasComponent<ParticleSystemComponent>();
        case AddableComponentType::Camera: return !entity.HasComponent<CameraComponent>();
        case AddableComponentType::Light: return !entity.HasComponent<LightComponent>();
        case AddableComponentType::Animation: return !entity.HasComponent<AnimationComponent>();
        case AddableComponentType::Rigidbody: return !entity.HasComponent<RigidbodyComponent>();
        case AddableComponentType::Collider: return !entity.HasComponent<ColliderComponent>();
        case AddableComponentType::NavAgent: return !entity.HasComponent<NavAgentComponent>();
        case AddableComponentType::NavigationMesh: return !entity.HasComponent<NavigationMeshComponent>();
        case AddableComponentType::IblCapture: return !entity.HasComponent<IblCaptureComponent>();
        case AddableComponentType::PhysicalSky: return !entity.HasComponent<PhysicalSkyComponent>();
        case AddableComponentType::VolumetricCloud: return !entity.HasComponent<VolumetricCloudComponent>();
        case AddableComponentType::Script: return !entity.HasComponent<ScriptComponent>();
        case AddableComponentType::SoundEmitter: return !entity.HasComponent<SoundEmitterComponent>();
        case AddableComponentType::SoundListener: return !entity.HasComponent<SoundListenerComponent>();
        case AddableComponentType::Canvas: return !entity.HasComponent<CanvasComponent>();
        case AddableComponentType::RectTransform: return !entity.HasComponent<RectTransformComponent>();
        case AddableComponentType::UIImage: return !entity.HasComponent<UIImageComponent>();
        case AddableComponentType::UIText: return !entity.HasComponent<UITextComponent>();
        case AddableComponentType::UIButton: return !entity.HasComponent<UIButtonComponent>();
        }
        return false;
    }

    bool AddComponent(EngineState &state, PlutoGE::scene::Entity &entity, AddableComponentType type)
    {
        using namespace PlutoGE;
        if (!CanAddComponent(entity, type)) return false;
        auto &assetManager = state.engine->GetAssetManager();
        switch (type)
        {
        case AddableComponentType::Mesh:
        {
            auto *component = entity.CreateComponent<scene::MeshComponent>(scene::MeshComponentConfig{
                .mesh = nullptr,
                .material = assetManager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
            });
            if (component) component->SetMaterialAssetForMaterialSlot(0, std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            return component != nullptr;
        }
        case AddableComponentType::Terrain:
        {
            auto *component = entity.CreateComponent<scene::TerrainComponent>(scene::TerrainComponentConfig{
                .material = assetManager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
            });
            if (component) component->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            return component != nullptr;
        }
        case AddableComponentType::Spline:
        {
            auto *component = entity.CreateComponent<scene::SplineComponent>(scene::SplineComponentConfig{
                .material = assetManager.LoadMaterialAsset(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference)),
                .materialAssetReference = std::string(assets::Project::kBuiltinDefaultShadedMaterialReference),
            });
            if (component) component->Rebuild();
            return component != nullptr;
        }
        case AddableComponentType::Ocean: return entity.CreateComponent<scene::OceanComponent>() != nullptr;
        case AddableComponentType::Foliage:
        {
            auto *component = entity.CreateComponent<scene::FoliageComponent>();
            if (component) component->SetMaterialAssetReference(std::string(assets::Project::kBuiltinDefaultShadedMaterialReference));
            return component != nullptr;
        }
        case AddableComponentType::Cloth: return entity.CreateComponent<scene::ClothComponent>() != nullptr;
        case AddableComponentType::ParticleSystem: return entity.CreateComponent<scene::ParticleSystemComponent>() != nullptr;
        case AddableComponentType::Camera:
        {
            bool hasCamera = false;
            if (entity.GetScene())
                for (auto *candidate : FlattenScene(*entity.GetScene()))
                    hasCamera |= candidate && candidate->HasComponent<scene::CameraComponent>();
            auto *component = entity.CreateComponent<scene::CameraComponent>(new render::Camera(render::CameraConfig{
                .fovY = 60.0f, .nearPlane = 0.1f, .farPlane = 100.0f,
            }));
            if (component) component->SetMainCamera(!hasCamera);
            return component != nullptr;
        }
        case AddableComponentType::Light: return entity.CreateComponent<scene::LightComponent>() != nullptr;
        case AddableComponentType::Animation: return entity.CreateComponent<scene::AnimationComponent>() != nullptr;
        case AddableComponentType::Rigidbody: return entity.CreateComponent<scene::RigidbodyComponent>() != nullptr;
        case AddableComponentType::Collider:
            return entity.CreateComponent<scene::ColliderComponent>(scene::ColliderComponentConfig{
                .shape = entity.HasComponent<scene::TerrainComponent>() ? scene::ColliderShape::Terrain
                       : entity.HasComponent<scene::SplineComponent>() ? scene::ColliderShape::Mesh
                                                                      : scene::ColliderShape::Box,
            }) != nullptr;
        case AddableComponentType::NavAgent: return entity.CreateComponent<scene::NavAgentComponent>() != nullptr;
        case AddableComponentType::NavigationMesh: return entity.CreateComponent<scene::NavigationMeshComponent>() != nullptr;
        case AddableComponentType::IblCapture: return entity.CreateComponent<scene::IblCaptureComponent>() != nullptr;
        case AddableComponentType::PhysicalSky: return entity.CreateComponent<scene::PhysicalSkyComponent>() != nullptr;
        case AddableComponentType::VolumetricCloud: return entity.CreateComponent<scene::VolumetricCloudComponent>() != nullptr;
        case AddableComponentType::Script: return entity.CreateComponent<scene::ScriptComponent>(scene::ScriptComponentConfig{}) != nullptr;
        case AddableComponentType::SoundEmitter: return entity.CreateComponent<scene::SoundEmitterComponent>() != nullptr;
        case AddableComponentType::SoundListener: return entity.CreateComponent<scene::SoundListenerComponent>() != nullptr;
        case AddableComponentType::Canvas: return entity.CreateComponent<scene::CanvasComponent>() != nullptr;
        case AddableComponentType::RectTransform: return entity.CreateComponent<scene::RectTransformComponent>() != nullptr;
        case AddableComponentType::UIImage: return entity.CreateComponent<scene::UIImageComponent>() != nullptr;
        case AddableComponentType::UIText: return entity.CreateComponent<scene::UITextComponent>() != nullptr;
        case AddableComponentType::UIButton: return entity.CreateComponent<scene::UIButtonComponent>() != nullptr;
        }
        return false;
    }

    void ReplaceScene(EngineState &state, std::unique_ptr<PlutoGE::scene::Scene> scene)
    {
        state.engine->StopRuntime();
        state.engine->SetScene(nullptr);
        state.pendingComponentEdits.clear();
        state.runtimeSceneSnapshot.clear();
        state.runtimeScenePath.clear();
        state.scene = std::move(scene);
        state.engine->SetScene(state.scene.get());
        for (auto &[handle, viewport] : state.viewports)
            viewport->selectedEntityId = 0;
    }

    void ApplyProjectEditorPostProcess(EngineState &state, const PlutoGE::assets::ProjectManifest &manifest)
    {
        std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> effects;
        bool projectStackApplied = false;

        if (!manifest.editorCameraPostProcessPreset.empty())
        {
            bool loaded = false;
            const auto preset = state.engine->GetAssetManager().LoadPostProcessPresetAsset(
                manifest.editorCameraPostProcessPreset, &loaded);
            if (loaded)
            {
                effects = PlutoGE::assets::InstantiatePostProcessPreset(preset);
                projectStackApplied = true;
            }
        }

        if (!projectStackApplied && !manifest.editorCameraPostProcessEffects.empty())
        {
            projectStackApplied = true;
            effects.reserve(manifest.editorCameraPostProcessEffects.size());
            for (const auto &serializedEffect : manifest.editorCameraPostProcessEffects)
            {
                auto effect = PlutoGE::render::CreatePostProcessEffect(serializedEffect.typeName);
                if (!effect)
                    continue;

                effect->SetEnabled(serializedEffect.enabled);
                auto parameters = effect->GetParameters();
                for (const auto &serializedParameter : serializedEffect.parameters)
                {
                    const auto parameter = std::find_if(parameters.begin(), parameters.end(),
                                                        [&serializedParameter](const PlutoGE::render::PostProcessParameter &candidate)
                                                        {
                                                            return candidate.name == serializedParameter.name;
                                                        });
                    if (parameter == parameters.end())
                        continue;
                    parameter->type = static_cast<PlutoGE::render::PostProcessParameterType>(serializedParameter.type);
                    parameter->value = serializedParameter.value;
                }
                effect->SetParameters(parameters);
                effects.push_back(std::move(effect));
            }
        }

        if (!projectStackApplied)
            effects = PlutoGE::assets::InstantiatePostProcessPreset(PlutoGE::assets::CreateDefaultPostProcessPresetAsset());

        for (auto &effect : state.editorCameraPostProcessEffects)
            state.retiredPostProcessEffects.push_back(std::move(effect));
        state.editorCameraPostProcessEffects = std::move(effects);
    }

    void StoreEditorPostProcessSettings(EngineState &state)
    {
        if (!state.project)
            return;
        auto &manifest = state.project->GetManifest();
        manifest.editorCameraPostProcessPreset.clear();
        manifest.editorCameraPostProcessEffects.clear();
        manifest.editorCameraPostProcessEffects.reserve(state.editorCameraPostProcessEffects.size());
        for (const auto &effect : state.editorCameraPostProcessEffects)
        {
            PlutoGE::assets::ProjectPostProcessEffect serialized;
            serialized.typeName = effect->GetTypeName();
            serialized.enabled = effect->IsEnabled();
            for (const auto &parameter : effect->GetParameters())
            {
                serialized.parameters.push_back(PlutoGE::assets::ProjectPostProcessParameter{
                    .name = parameter.name,
                    .type = static_cast<int>(parameter.type),
                    .value = parameter.value,
                });
            }
            manifest.editorCameraPostProcessEffects.push_back(std::move(serialized));
        }
    }

    bool InitializeEngineState(EngineState &state, const PlutoGE::core::EngineConfig &engineConfig)
    {
        if (!state.engine->Initialize(engineConfig))
            return false;

        state.editorCameraPostProcessEffects = PlutoGE::assets::InstantiatePostProcessPreset(
            PlutoGE::assets::CreateDefaultPostProcessPresetAsset());
        state.scene = std::make_unique<PlutoGE::scene::Scene>();
        PopulateInitialScene(*state.engine, *state.scene);
        state.engine->SetScene(state.scene.get());
        return true;
    }

    void DestroyEngineStateResources(EngineState &state)
    {
        for (auto &[handle, viewport] : state.viewports)
            DestroyViewport(state, *viewport);
        state.viewports.clear();
        for (auto &viewport : state.retiredViewports)
            DestroyViewport(state, *viewport);
        state.retiredViewports.clear();
        state.engine->SetScene(nullptr);
        state.scene.reset();
        state.retiredPostProcessEffects.clear();
        state.editorCameraPostProcessEffects.clear();
        state.engine->Shutdown();
    }

    void WaitForConsumer(SharedRenderSlot &slot)
    {
        if (!slot.consumerFence)
            return;
        glWaitSync(slot.consumerFence, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(slot.consumerFence);
        slot.consumerFence = nullptr;
    }

    void WaitForProducer(SharedRenderSlot &slot)
    {
        if (!slot.producerFence)
            return;
        glClientWaitSync(slot.producerFence, GL_SYNC_FLUSH_COMMANDS_BIT, GL_TIMEOUT_IGNORED);
        glDeleteSync(slot.producerFence);
        slot.producerFence = nullptr;
    }

    bool ResizeViewportTargets(
        EngineState &state,
        ViewportState &viewport,
        int width,
        int height,
        bool sharedWorker)
    {
        if (viewport.width == width && viewport.height == height)
            return true;

        const auto resizeStart = std::chrono::steady_clock::now();
        if (sharedWorker)
        {
            for (auto &slot : viewport.sharedRenderSlots)
            {
                WaitForConsumer(slot);
                WaitForProducer(slot);
                slot.serial = 0;
                if (!slot.renderTarget || !slot.renderTarget->Resize(width, height))
                {
                    viewport.workerError = "Failed to resize a shared PlutoGE viewport render target.";
                    return false;
                }
            }
        }
        else if (!viewport.renderTarget || !viewport.renderTarget->Resize(width, height))
        {
            SetError("Failed to resize the PlutoGE viewport render target.");
            return false;
        }

        viewport.lastResizeMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - resizeStart).count();
        viewport.width = width;
        viewport.height = height;
        return true;
    }

    void RenderViewportContents(
        EngineState &state,
        ViewportState &viewport,
        PlutoEditorViewportFrame frame,
        PlutoGE::render::RenderTarget &renderTarget,
        float deltaSeconds)
    {
        frame.delta_seconds = std::clamp(deltaSeconds, 0.0f, 0.25f);
        const auto cameraData = BuildCameraData(frame);
        viewport.lastCameraData = cameraData;
        viewport.hasCameraData = true;

        auto &renderer = state.engine->GetRenderer();
        renderer.BeginFrame(&renderTarget);
        std::vector<PlutoGE::render::IPostProcessEffect *> postProcessEffects;
        postProcessEffects.reserve(state.editorCameraPostProcessEffects.size());
        for (const auto &effect : state.editorCameraPostProcessEffects)
            postProcessEffects.push_back(effect.get());
        renderer.RenderFrame(cameraData,
                             &renderTarget,
                             state.scene->GetLights(),
                             &postProcessEffects,
                             state.scene.get(),
                             true,
                             true);
        renderer.EndFrame(&renderTarget);
        viewport.gpuFrameMs = renderer.GetTotalGpuPassTimeMs();

        glBindFramebuffer(GL_FRAMEBUFFER, renderTarget.GetFramebufferID());
        glViewport(0, 0, frame.width, frame.height);
        DrawOverlay(state, viewport, frame, cameraData);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        const double frameMs = std::max(0.0, static_cast<double>(frame.delta_seconds) * 1000.0);
        ++viewport.frameCount;
        viewport.accumulatedFrameMs += frameMs;
        viewport.maximumFrameMs = std::max(viewport.maximumFrameMs, frameMs);
        viewport.targetRefreshHz = frame.target_refresh_hz;
    }

    bool RenderSharedViewport(
        EngineState &state,
        ViewportState &viewport,
        PlutoEditorViewportFrame frame,
        float deltaSeconds)
    {
        if (!viewport.imguiContext &&
            !InitializeViewport(viewport, frame.width, frame.height, true))
        {
            viewport.workerError = g_lastError.empty()
                                       ? "Failed to initialize the shared viewport render targets."
                                       : g_lastError;
            return false;
        }
        if (!ResizeViewportTargets(state, viewport, frame.width, frame.height, true))
            return false;

        auto &slot = viewport.sharedRenderSlots[viewport.nextSharedRenderSlot];
        viewport.nextSharedRenderSlot =
            (viewport.nextSharedRenderSlot + 1) % viewport.sharedRenderSlots.size();
        WaitForConsumer(slot);
        WaitForProducer(slot);

        RenderViewportContents(state, viewport, frame, *slot.renderTarget, deltaSeconds);
        slot.serial = viewport.nextSharedRenderSerial++;
        slot.producerFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        return true;
    }

    int32_t PresentSharedViewport(
        ViewportState &viewport,
        const PlutoEditorViewportFrame &frame)
    {
        if (!viewport.workerError.empty())
        {
            SetError(viewport.workerError);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        viewport.latestFrame = frame;
        viewport.hasLatestFrame = true;
        viewport.lastFrameSubmission = std::chrono::steady_clock::now();

        SharedRenderSlot *latestSlot = nullptr;
        for (auto &slot : viewport.sharedRenderSlots)
        {
            if (slot.renderTarget && slot.serial != 0 &&
                (!latestSlot || slot.serial > latestSlot->serial))
            {
                latestSlot = &slot;
            }
        }
        if (!latestSlot)
            return PLUTO_EDITOR_OK;

        const GLuint colorTexture = latestSlot->renderTarget->GetColorTextureID();
        if (glIsTexture(colorTexture) == GL_FALSE)
        {
            SetError("Avalonia's viewport context is not in the worker render context share group.");
            return PLUTO_EDITOR_CONTEXT_NOT_SHARED;
        }

        if (latestSlot->producerFence)
        {
            glWaitSync(latestSlot->producerFence, 0, GL_TIMEOUT_IGNORED);
            glDeleteSync(latestSlot->producerFence);
            latestSlot->producerFence = nullptr;
        }

        GLuint readFramebuffer = 0;
        glGenFramebuffers(1, &readFramebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glFramebufferTexture2D(
            GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0);
        if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(frame.framebuffer));
            glDeleteFramebuffers(1, &readFramebuffer);
            SetError("Failed to attach the worker's shared color texture for presentation.");
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(frame.framebuffer));
        glBlitFramebuffer(
            0, 0, latestSlot->renderTarget->GetWidth(), latestSlot->renderTarget->GetHeight(),
            0, 0, frame.width, frame.height,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(frame.framebuffer));
        glViewport(0, 0, frame.width, frame.height);
        glDeleteFramebuffers(1, &readFramebuffer);

        if (latestSlot->consumerFence)
            glDeleteSync(latestSlot->consumerFence);
        latestSlot->consumerFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        glFlush();
        return PLUTO_EDITOR_OK;
    }

#if defined(_WIN32)
    void SharedRenderWorker(
        EngineState &state,
        PlutoGE::core::EngineConfig engineConfig)
    {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        bool initialized = state.sharedContext && state.sharedContext->MakeCurrent();
        if (initialized)
            initialized = InitializeEngineState(state, engineConfig);

        {
            std::scoped_lock lock(state.mutex);
            state.workerInitializationSucceeded = initialized;
            state.workerInitializationComplete = true;
            state.workerOperational = initialized;
            if (!initialized)
            {
                state.workerError = state.sharedContext
                                        ? state.sharedContext->GetError()
                                        : "The shared WGL render context was unavailable.";
                if (state.workerError.empty())
                    state.workerError = "PlutoGE failed to initialize on the shared WGL render context.";
            }
        }
        state.renderCondition.notify_all();

        if (!initialized)
        {
            if (state.sharedContext)
                state.sharedContext->ClearCurrent();
            return;
        }

        auto previousFrameTime = std::chrono::steady_clock::now();
        std::unique_lock lock(state.mutex);
        while (!state.workerStopRequested)
        {
            if (state.workerPauseRequested)
            {
                state.sharedContext->ClearCurrent();
                state.workerPaused = true;
                state.renderCondition.notify_all();
                state.renderCondition.wait(lock, [&state]
                {
                    return state.workerStopRequested || !state.workerPauseRequested;
                });
                if (state.workerStopRequested)
                    break;
                if (!state.sharedContext->MakeCurrent())
                {
                    state.workerError = state.sharedContext->GetError();
                    state.workerPaused = false;
                    state.renderCondition.notify_all();
                    break;
                }
                state.workerPaused = false;
                previousFrameTime = std::chrono::steady_clock::now();
                state.renderCondition.notify_all();
                continue;
            }

            state.renderCondition.wait(lock, [&state]
            {
                if (state.workerStopRequested || state.workerPauseRequested)
                    return true;
                return std::any_of(
                    state.viewports.begin(),
                    state.viewports.end(),
                    [](const auto &entry)
                    { return entry.second->hasLatestFrame; });
            });
            if (state.workerStopRequested)
                break;
            if (state.workerPauseRequested)
                continue;

            for (auto &viewport : state.retiredViewports)
                DestroyViewport(state, *viewport);
            state.retiredViewports.clear();
            state.retiredPostProcessEffects.clear();

            if (!ApplyPendingComponentEdits(state))
            {
                for (auto &[handle, viewport] : state.viewports)
                    viewport->workerError = "A pending component edit could not be applied.";
                continue;
            }

            const auto frameTime = std::chrono::steady_clock::now();
            for (auto &[handle, viewport] : state.viewports)
            {
                if (viewport->hasLatestFrame &&
                    frameTime - viewport->lastFrameSubmission > std::chrono::milliseconds(250))
                {
                    viewport->hasLatestFrame = false;
                }
            }
            const bool hasActiveViewport = std::any_of(
                state.viewports.begin(),
                state.viewports.end(),
                [](const auto &entry)
                { return entry.second->hasLatestFrame; });
            if (!hasActiveViewport)
                continue;

            const float rawDeltaSeconds =
                std::chrono::duration<float>(frameTime - previousFrameTime).count();
            const float deltaSeconds = rawDeltaSeconds > 0.1f
                                           ? 1.0f / 60.0f
                                           : std::clamp(rawDeltaSeconds, 0.0f, 0.1f);
            previousFrameTime = frameTime;

            auto &renderer = state.engine->GetRenderer();
            renderer.BeginProfilingFrame();
            renderer.ClearRenderCommands();
            std::vector<PlutoGE::render::CameraData> submissionCameras;
            submissionCameras.reserve(state.viewports.size());
            float targetRefreshHz = 0.0f;
            for (const auto &[handle, viewport] : state.viewports)
            {
                if (!viewport->hasLatestFrame)
                    continue;
                submissionCameras.push_back(BuildCameraData(viewport->latestFrame));
                targetRefreshHz =
                    std::max(targetRefreshHz, viewport->latestFrame.target_refresh_hz);
            }
            renderer.SetSubmissionCullingCameras(submissionCameras);

            state.engine->UpdateAsyncMeshImports();
            state.scene->Update(deltaSeconds);
            for (auto &[handle, viewport] : state.viewports)
            {
                if (!viewport->hasLatestFrame)
                    continue;

                auto frame = viewport->latestFrame;
                viewport->latestFrame.mouse_wheel = 0.0f;
                auto &window = state.engine->GetWindow();
                window.SetExternalExtents(frame.width, frame.height);
                if (!RenderSharedViewport(state, *viewport, frame, deltaSeconds))
                    viewport->hasLatestFrame = false;
            }

            const bool vSyncEnabled = state.project && state.project->GetManifest().vSyncEnabled;
            const float refreshHz = targetRefreshHz > 1.0f ? targetRefreshHz : 60.0f;
            float producerHz = refreshHz;
            if (!vSyncEnabled)
            {
                // The embedded producer remains independent of presentation, but
                // deliberately leaves CPU/GPU time for Avalonia's compositor.
                producerHz = std::clamp(refreshHz * 1.5f, 90.0f, 240.0f);
                float maximumGpuFrameMs = 0.0f;
                for (const auto &[handle, viewport] : state.viewports)
                {
                    if (viewport->hasLatestFrame && viewport->gpuFrameMs > 0.0f)
                        maximumGpuFrameMs =
                            std::max(maximumGpuFrameMs, viewport->gpuFrameMs);
                }
                if (maximumGpuFrameMs > 0.1f)
                {
                    constexpr float kMaximumProducerGpuUtilization = 0.9f;
                    producerHz = std::min(
                        producerHz,
                        1000.0f * kMaximumProducerGpuUtilization / maximumGpuFrameMs);
                }
                producerHz = std::max(producerHz, std::min(refreshHz, 90.0f));
            }

            const auto nextFrameTime =
                frameTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<float>(1.0f / producerHz));
            if (vSyncEnabled)
            {
                state.renderCondition.wait_until(lock, nextFrameTime, [&state]
                {
                    return state.workerStopRequested || state.workerPauseRequested;
                });
            }
            else
            {
                state.renderCondition.wait_until(lock, nextFrameTime, [&state]
                {
                    return state.workerStopRequested || state.workerPauseRequested;
                });
            }
        }

        DestroyEngineStateResources(state);
        state.workerOperational = false;
        state.workerPaused = false;
        state.renderCondition.notify_all();
        lock.unlock();
        state.sharedContext->ClearCurrent();
    }
#endif
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

#if defined(_WIN32)
        state->sharedContext = std::make_unique<SharedWglContext>();
        if (!state->sharedContext->Create())
        {
            SetError("Unable to create PlutoGE's independent shared render context: " +
                     state->sharedContext->GetError());
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }
        state->workerEnabled = true;
        state->renderThread = std::thread(SharedRenderWorker, std::ref(*state), engineConfig);
        {
            std::unique_lock initializationLock(state->mutex);
            state->renderCondition.wait(initializationLock, [&state]
            {
                return state->workerInitializationComplete;
            });
            if (!state->workerInitializationSucceeded)
            {
                const std::string workerError = state->workerError;
                initializationLock.unlock();
                if (state->renderThread.joinable())
                    state->renderThread.join();
                state->sharedContext->Destroy();
                SetError(workerError);
                return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
            }
        }
#else
        if (!InitializeEngineState(*state, engineConfig))
        {
            SetError("PlutoGE requires a current desktop OpenGL 4.3 context.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }
#endif

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

        if (state->workerEnabled)
        {
            {
                std::scoped_lock engineLock(state->mutex);
                state->workerStopRequested = true;
            }
            state->renderCondition.notify_all();
            if (state->renderThread.joinable())
                state->renderThread.join();
#if defined(_WIN32)
            if (state->sharedContext)
                state->sharedContext->Destroy();
#endif
        }
        else
        {
            std::scoped_lock engineLock(state->mutex);
            DestroyEngineStateResources(*state);
        }
        g_state.reset();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_engine_acquire_render_context(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
            return PLUTO_EDITOR_INVALID_HANDLE;
        if (!state->workerEnabled)
            return PLUTO_EDITOR_OK;

#if defined(_WIN32)
        std::unique_lock engineLock(state->mutex);
        if (state->workerPauseRequested || state->workerPaused)
        {
            SetError("The shared render context is already acquired.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }

        state->pausedCallerDc = wglGetCurrentDC();
        state->pausedCallerContext = wglGetCurrentContext();
        state->pausedCallerThreadId = GetCurrentThreadId();
        state->workerPauseRequested = true;
        state->renderCondition.notify_all();
        state->renderCondition.wait(engineLock, [state]
        {
            return state->workerPaused || !state->workerOperational;
        });
        if (!state->workerPaused || !state->sharedContext->MakeCurrent())
        {
            state->workerPauseRequested = false;
            state->renderCondition.notify_all();
            SetError(state->sharedContext
                         ? state->sharedContext->GetError()
                         : "The shared render context is unavailable.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }
#endif

        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_engine_release_render_context(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state)
            return PLUTO_EDITOR_INVALID_HANDLE;
        if (!state->workerEnabled)
            return PLUTO_EDITOR_OK;

#if defined(_WIN32)
        std::unique_lock engineLock(state->mutex);
        if (!state->workerPauseRequested || !state->workerPaused ||
            state->pausedCallerThreadId != GetCurrentThreadId())
        {
            SetError("The shared render context must be released by the thread that acquired it.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }

        state->sharedContext->ClearCurrent();
        const bool restored = state->pausedCallerContext
                                  ? wglMakeCurrent(state->pausedCallerDc, state->pausedCallerContext) == TRUE
                                  : wglMakeCurrent(nullptr, nullptr) == TRUE;
        state->pausedCallerDc = nullptr;
        state->pausedCallerContext = nullptr;
        state->pausedCallerThreadId = 0;
        state->workerPauseRequested = false;
        state->renderCondition.notify_all();
        state->renderCondition.wait(engineLock, [state]
        {
            return !state->workerPaused || !state->workerOperational;
        });
        if (!restored)
        {
            SetError("Failed to restore Avalonia's WGL context after native editor work.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }
#endif

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
        if (state->workerEnabled)
            state->retiredViewports.push_back(std::move(iterator->second));
        else
            DestroyViewport(*state, *iterator->second);
        state->viewports.erase(iterator);
        state->renderCondition.notify_all();
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

        if (state->workerEnabled)
        {
            const int32_t result = PresentSharedViewport(*viewport, *frame);
            state->renderCondition.notify_all();
            if (result == PLUTO_EDITOR_OK)
                g_lastError.clear();
            return result;
        }

        if (!window.EnsureOpenGLContextCurrent(viewport->renderTarget == nullptr))
        {
            SetError("The current viewport context is not desktop OpenGL 4.3 or is not shared with the engine context.");
            return viewport->renderTarget ? PLUTO_EDITOR_CONTEXT_NOT_SHARED : PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

        if (!viewport->renderTarget && !InitializeViewport(*viewport, frame->width, frame->height, false))
        {
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }

        if (glIsTexture(viewport->renderTarget->GetColorTextureID()) == GL_FALSE)
        {
            SetError("Avalonia created a viewport context that does not share PlutoGE OpenGL objects.");
            return PLUTO_EDITOR_CONTEXT_NOT_SHARED;
        }

        // Effect destructors can release GPU resources, so defer removals until
        // Avalonia has made the viewport's shared OpenGL context current.
        state->retiredPostProcessEffects.clear();

        if (!ApplyPendingComponentEdits(*state))
            return PLUTO_EDITOR_INVALID_ARGUMENT;

        if (!ResizeViewportTargets(*state, *viewport, frame->width, frame->height, false))
            return PLUTO_EDITOR_INTERNAL_ERROR;

        const auto cameraData = BuildCameraData(*frame);
        auto &renderer = state->engine->GetRenderer();
        renderer.BeginProfilingFrame();
        renderer.ClearRenderCommands();
        renderer.SetSubmissionCullingCameras({cameraData});
        state->engine->UpdateAsyncMeshImports();
        state->scene->Update(std::clamp(frame->delta_seconds, 0.0f, 0.25f));
        RenderViewportContents(
            *state, *viewport, *frame, *viewport->renderTarget, frame->delta_seconds);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, viewport->renderTarget->GetFramebufferID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(frame->framebuffer));
        glBlitFramebuffer(0, 0, frame->width, frame->height,
                          0, 0, frame->width, frame->height,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(frame->framebuffer));
        glViewport(0, 0, frame->width, frame->height);
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

        if (viewport->overlayWantsMouse)
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
        stats->gpu_frame_ms = viewport->gpuFrameMs;
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

    int32_t pluto_editor_entity_get_active(PlutoEditorHandle engineHandle, uint32_t entityId, uint8_t *active)
    {
        if (!active) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        const auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *active = entity->IsSelfActive() ? 1 : 0;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_set_active(PlutoEditorHandle engineHandle, uint32_t entityId, uint8_t active)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        entity->SetActive(active != 0);
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
        if (!state->engine->GetWindow().EnsureOpenGLContextCurrent(true))
        {
            SetError("Failed to make the PlutoGE resource context current while loading the project.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }
        ApplyProjectEditorPostProcess(*state, project->GetManifest());

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

    int32_t pluto_editor_project_refresh(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project)
        {
            SetError("No project is loaded.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        state->project->RefreshAssetRegistry();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_save(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project)
        {
            SetError("No project is loaded.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        if (state->engine->IsRuntimeRunning())
        {
            SetError("Stop Play mode before saving the project.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        if (state->scene)
        {
            std::filesystem::path scenePath(state->scene->GetFilePath());
            if (scenePath.empty())
                scenePath = state->project->GetAssetDirectoryPath() / "Scenes" / "Main.plutoscene";
            std::error_code errorCode;
            std::filesystem::create_directories(scenePath.parent_path(), errorCode);
            std::string sceneError;
            if (errorCode || !PlutoGE::scene::SceneSerializer::Save(*state->scene, scenePath.string(), &sceneError))
            {
                SetError(errorCode ? "Failed to create the project scene directory."
                                   : (sceneError.empty() ? "Failed to save the project scene." : sceneError));
                return PLUTO_EDITOR_INTERNAL_ERROR;
            }
            state->scene->SetFilePath(std::filesystem::absolute(scenePath).lexically_normal().string());
            if (state->project->GetManifest().startupScene.empty())
                state->project->GetManifest().startupScene = state->project->MakeAssetReference(scenePath);
        }
        StoreEditorPostProcessSettings(*state);
        state->project->RefreshAssetRegistry();
        std::string errorMessage;
        if (!state->project->Save(&errorMessage))
        {
            SetError(errorMessage.empty() ? "Failed to save project." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_get_settings(PlutoEditorHandle engineHandle, PlutoEditorProjectSettings *settings)
    {
        if (!settings) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &manifest = state->project->GetManifest();
        CopyString(settings->name, manifest.name);
        CopyString(settings->window_title, manifest.windowTitle);
        CopyString(settings->startup_scene, manifest.startupScene);
        CopyString(settings->script_assembly, manifest.scriptAssembly);
        settings->window_width = manifest.windowWidth;
        settings->window_height = manifest.windowHeight;
        settings->vsync_enabled = manifest.vSyncEnabled ? 1 : 0;
        settings->editor_font_size = manifest.editorFontSize;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_project_set_settings(PlutoEditorHandle engineHandle, const PlutoEditorProjectSettings *settings)
    {
        if (!settings) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->project) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto &manifest = state->project->GetManifest();
        const bool vSyncChanged = manifest.vSyncEnabled != (settings->vsync_enabled != 0);
        manifest.name = settings->name;
        manifest.windowTitle = settings->window_title;
        manifest.startupScene = settings->startup_scene;
        manifest.scriptAssembly = settings->script_assembly;
        manifest.windowWidth = std::max(settings->window_width, 64);
        manifest.windowHeight = std::max(settings->window_height, 64);
        manifest.vSyncEnabled = settings->vsync_enabled != 0;
        manifest.editorFontSize = std::clamp(settings->editor_font_size, 10.0f, 24.0f);
        if (vSyncChanged)
        {
            for (auto &[handle, viewport] : state->viewports)
            {
                viewport->frameCount = 0;
                viewport->accumulatedFrameMs = 0.0;
                viewport->maximumFrameMs = 0.0;
            }
        }
        state->renderCondition.notify_all();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_scene_new(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto scene = std::make_unique<PlutoGE::scene::Scene>();
        PopulateInitialScene(*state->engine, *scene);
        ReplaceScene(*state, std::move(scene));
        g_lastError.clear();
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

        if (!state->engine->GetWindow().EnsureOpenGLContextCurrent(true))
        {
            SetError("Failed to make the PlutoGE resource context current while loading the scene.");
            return PLUTO_EDITOR_OPENGL_UNAVAILABLE;
        }

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

    int32_t pluto_editor_scene_save(PlutoEditorHandle engineHandle, const char *path)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->scene)
        {
            SetError("No scene is loaded.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        if (state->engine->IsRuntimeRunning())
        {
            SetError("Stop Play mode before saving the scene.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        std::filesystem::path scenePath = path && path[0] != '\0'
                                              ? std::filesystem::path(path)
                                              : std::filesystem::path(state->scene->GetFilePath());
        if (scenePath.empty())
        {
            SetError("Choose a file name before saving this scene.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        scenePath = std::filesystem::absolute(scenePath).lexically_normal();
        std::error_code errorCode;
        std::filesystem::create_directories(scenePath.parent_path(), errorCode);
        if (errorCode)
        {
            SetError("Failed to create the scene directory: " + scenePath.parent_path().string());
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }
        std::string errorMessage;
        if (!PlutoGE::scene::SceneSerializer::Save(*state->scene, scenePath.string(), &errorMessage))
        {
            SetError(errorMessage.empty() ? "Failed to save scene." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }
        state->scene->SetFilePath(scenePath.string());
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_runtime_start(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->scene) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (state->engine->IsRuntimeRunning()) return PLUTO_EDITOR_OK;
        std::string errorMessage;
        if (!PlutoGE::scene::SceneSerializer::SaveToString(*state->scene, state->runtimeSceneSnapshot, &errorMessage))
        {
            SetError(errorMessage.empty() ? "Failed to snapshot the scene before Play." : errorMessage);
            return PLUTO_EDITOR_INTERNAL_ERROR;
        }
        state->runtimeScenePath = state->scene->GetFilePath();
        state->engine->StartRuntime();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_runtime_stop(PlutoEditorHandle engineHandle)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->engine->IsRuntimeRunning()) return PLUTO_EDITOR_OK;
        state->engine->StopRuntime();
        if (!state->runtimeSceneSnapshot.empty())
        {
            std::string errorMessage;
            auto restoredScene = PlutoGE::scene::SceneSerializer::LoadFromString(state->runtimeSceneSnapshot, &errorMessage);
            if (!restoredScene)
            {
                SetError(errorMessage.empty() ? "Runtime stopped, but the pre-Play scene could not be restored." : errorMessage);
                return PLUTO_EDITOR_INTERNAL_ERROR;
            }
            restoredScene->SetFilePath(state->runtimeScenePath);
            ReplaceScene(*state, std::move(restoredScene));
        }
        state->runtimeSceneSnapshot.clear();
        state->runtimeScenePath.clear();
        g_lastError.clear();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_runtime_is_running(PlutoEditorHandle engineHandle, uint8_t *running)
    {
        if (!running) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        *running = state->engine->IsRuntimeRunning() ? 1 : 0;
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_create(PlutoEditorHandle engineHandle, uint32_t parentId, const char *name, uint32_t *entityId)
    {
        if (!entityId) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *parent = parentId == 0 ? nullptr : state->scene->FindEntityByID(parentId);
        if (parentId != 0 && !parent) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{
            .name = name && name[0] != '\0' ? name : "GameObject",
        });
        auto *created = state->scene->AddEntity(std::move(entity), parent);
        if (!created) return PLUTO_EDITOR_INTERNAL_ERROR;
        *entityId = created->GetID();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_duplicate(PlutoEditorHandle engineHandle, uint32_t sourceId, uint32_t *entityId)
    {
        if (!entityId) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *source = state->scene->FindEntityByID(sourceId);
        if (!source) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto *duplicate = PlutoGE::scene::Prefab::DuplicateEntity(*state->scene, *source, source->GetParent(), true);
        if (!duplicate) return PLUTO_EDITOR_INTERNAL_ERROR;
        duplicate->SetName(source->GetName() + " Copy");
        duplicate->SetPosition(source->GetPosition() + glm::vec3(0.25f, 0.0f, 0.25f));
        *entityId = duplicate->GetID();
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_delete(PlutoEditorHandle engineHandle, uint32_t entityId)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->scene->FindEntityByID(entityId)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        return state->scene->DestroyEntity(entityId) ? PLUTO_EDITOR_OK : PLUTO_EDITOR_INTERNAL_ERROR;
    }

    int32_t pluto_editor_entity_set_name(PlutoEditorHandle engineHandle, uint32_t entityId, const char *name)
    {
        if (!name || name[0] == '\0') return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        entity->SetName(name);
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

    int32_t pluto_editor_component_set_enabled(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint8_t enabled)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        components[componentIndex]->SetEnabled(enabled != 0);
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
        propertyInfo->editable = IsEditableProperty(property) ? 1 : 0;
        CopyString(propertyInfo->name, property.name);
        CopyString(propertyInfo->value, property.value);
        CopyString(propertyInfo->enum_options, JoinOptions(property.enumOptions));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_component_set_property(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex, uint32_t propertyIndex, const char *value)
    {
        if (!value) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene->FindEntityByID(entityId);
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto properties = components[componentIndex]->Serialize();
        if (propertyIndex >= properties.size() || !IsEditableProperty(properties[propertyIndex]))
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto existing = std::find_if(state->pendingComponentEdits.begin(), state->pendingComponentEdits.end(),
                                     [entityId, componentIndex, propertyIndex](const PendingComponentEdit &edit)
                                     {
                                         return edit.entityId == entityId && edit.componentIndex == componentIndex &&
                                                edit.propertyIndex == propertyIndex;
                                     });
        if (existing != state->pendingComponentEdits.end())
            existing->value = value;
        else
            state->pendingComponentEdits.push_back(PendingComponentEdit{entityId, componentIndex, propertyIndex, value});
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_addable_component_type_count(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (!state->scene || !state->scene->FindEntityByID(entityId)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(std::size(kAddableComponentDefinitions));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_get_addable_component_type(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t typeIndex, PlutoEditorAddableComponentType *componentType)
    {
        if (!componentType) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene ? state->scene->FindEntityByID(entityId) : nullptr;
        if (!entity || typeIndex >= std::size(kAddableComponentDefinitions)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &definition = kAddableComponentDefinitions[typeIndex];
        componentType->can_add = CanAddComponent(*entity, definition.type) ? 1 : 0;
        CopyString(componentType->type_name, definition.typeName);
        CopyString(componentType->display_name, definition.displayName);
        CopyString(componentType->category, definition.category);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_add_component(PlutoEditorHandle engineHandle, uint32_t entityId, const char *typeName)
    {
        if (!typeName) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene ? state->scene->FindEntityByID(entityId) : nullptr;
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (!ApplyPendingComponentEdits(*state)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto definition = std::find_if(std::begin(kAddableComponentDefinitions), std::end(kAddableComponentDefinitions),
                                             [typeName](const AddableComponentDefinition &candidate)
                                             {
                                                 return std::string_view(candidate.typeName) == typeName;
                                             });
        if (definition == std::end(kAddableComponentDefinitions) || !AddComponent(*state, *entity, definition->type))
        {
            SetError("The selected component cannot be added to this GameObject.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_entity_remove_component(PlutoEditorHandle engineHandle, uint32_t entityId, uint32_t componentIndex)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto *entity = state->scene ? state->scene->FindEntityByID(entityId) : nullptr;
        if (!entity) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (!ApplyPendingComponentEdits(*state)) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto components = FlattenComponents(*entity);
        if (componentIndex >= components.size() || !entity->RemoveComponent(components[componentIndex]))
        {
            SetError("The selected component could not be removed.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_post_process_get_registered_type_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        if (!ResolveEngine(engineHandle)) return PLUTO_EDITOR_INVALID_HANDLE;
        *count = static_cast<uint32_t>(PlutoGE::render::GetRegisteredPostProcessEffectTypes().size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_post_process_get_registered_type(PlutoEditorHandle engineHandle, uint32_t typeIndex, char *typeName, uint32_t typeNameSize)
    {
        if (!typeName || typeNameSize == 0) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        if (!ResolveEngine(engineHandle)) return PLUTO_EDITOR_INVALID_HANDLE;
        const auto &types = PlutoGE::render::GetRegisteredPostProcessEffectTypes();
        if (typeIndex >= types.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        CopyString(typeName, typeNameSize, types[typeIndex]);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_effect_count(PlutoEditorHandle engineHandle, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        *count = static_cast<uint32_t>(state->editorCameraPostProcessEffects.size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_effect(PlutoEditorHandle engineHandle, uint32_t effectIndex, PlutoEditorPostProcessEffectInfo *effectInfo)
    {
        if (!effectInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &effect = state->editorCameraPostProcessEffects[effectIndex];
        effectInfo->index = effectIndex;
        effectInfo->enabled = effect->IsEnabled() ? 1 : 0;
        CopyString(effectInfo->type_name, effect->GetTypeName());
        CopyString(effectInfo->display_name, effect->GetDisplayName());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_add_post_process_effect(PlutoEditorHandle engineHandle, const char *typeName)
    {
        if (!typeName) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto effect = PlutoGE::render::CreatePostProcessEffect(typeName);
        if (!effect)
        {
            SetError("Unknown post-process effect type.");
            return PLUTO_EDITOR_INVALID_ARGUMENT;
        }
        state->editorCameraPostProcessEffects.push_back(std::move(effect));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_remove_post_process_effect(PlutoEditorHandle engineHandle, uint32_t effectIndex)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        state->retiredPostProcessEffects.push_back(std::move(state->editorCameraPostProcessEffects[effectIndex]));
        state->editorCameraPostProcessEffects.erase(state->editorCameraPostProcessEffects.begin() + effectIndex);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_move_post_process_effect(PlutoEditorHandle engineHandle, uint32_t fromIndex, uint32_t toIndex)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        auto &effects = state->editorCameraPostProcessEffects;
        if (fromIndex >= effects.size() || toIndex >= effects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        if (fromIndex == toIndex) return PLUTO_EDITOR_OK;
        auto effect = std::move(effects[fromIndex]);
        effects.erase(effects.begin() + fromIndex);
        effects.insert(effects.begin() + toIndex, std::move(effect));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_set_post_process_effect_enabled(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint8_t enabled)
    {
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        state->editorCameraPostProcessEffects[effectIndex]->SetEnabled(enabled != 0);
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_parameter_count(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint32_t *count)
    {
        if (!count) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        *count = static_cast<uint32_t>(state->editorCameraPostProcessEffects[effectIndex]->GetParameters().size());
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_get_post_process_parameter(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint32_t parameterIndex, PlutoEditorPostProcessParameter *parameterInfo)
    {
        if (!parameterInfo) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto parameters = state->editorCameraPostProcessEffects[effectIndex]->GetParameters();
        if (parameterIndex >= parameters.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        const auto &parameter = parameters[parameterIndex];
        parameterInfo->type = static_cast<int32_t>(parameter.type);
        CopyString(parameterInfo->name, parameter.name);
        CopyString(parameterInfo->value, parameter.value);
        CopyString(parameterInfo->enum_options, JoinOptions(parameter.enumOptions));
        return PLUTO_EDITOR_OK;
    }

    int32_t pluto_editor_camera_set_post_process_parameter(PlutoEditorHandle engineHandle, uint32_t effectIndex, uint32_t parameterIndex, const char *value)
    {
        if (!value) return PLUTO_EDITOR_INVALID_ARGUMENT;
        std::scoped_lock stateLock(g_stateMutex);
        auto *state = ResolveEngine(engineHandle);
        if (!state) return PLUTO_EDITOR_INVALID_HANDLE;
        std::scoped_lock engineLock(state->mutex);
        if (effectIndex >= state->editorCameraPostProcessEffects.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        auto parameters = state->editorCameraPostProcessEffects[effectIndex]->GetParameters();
        if (parameterIndex >= parameters.size()) return PLUTO_EDITOR_INVALID_ARGUMENT;
        parameters[parameterIndex].value = value;
        state->editorCameraPostProcessEffects[effectIndex]->SetParameters(parameters);
        return PLUTO_EDITOR_OK;
    }

    const char *pluto_editor_get_last_error(void)
    {
        return g_lastError.c_str();
    }
}
