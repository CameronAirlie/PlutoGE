#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#endif

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/ComponentFactory.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "EditorSession.h"
#include "EditorViewportInteraction.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
#ifdef _WIN32
    const char *g_hostPhase = "startup";

    LONG WINAPI HostUnhandledExceptionFilter(EXCEPTION_POINTERS *exceptionPointers)
    {
        const auto code = exceptionPointers && exceptionPointers->ExceptionRecord
                            ? exceptionPointers->ExceptionRecord->ExceptionCode
                            : 0ul;
        std::cerr << "Unhandled native exception 0x" << std::hex << code << std::dec
                  << " during " << g_hostPhase << '\n';
        HANDLE process = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        if (SymInitialize(process, nullptr, TRUE))
        {
            void *frames[32]{};
            const USHORT count = CaptureStackBackTrace(0, 32, frames, nullptr);
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            for (USHORT index = 0; index < count; ++index)
            {
                const DWORD64 address = reinterpret_cast<DWORD64>(frames[index]);
                DWORD64 displacement = 0;
                if (SymFromAddr(process, address, &displacement, symbol))
                    std::cerr << "  " << symbol->Name << "+0x" << std::hex << displacement << std::dec << '\n';
                else
                    std::cerr << "  0x" << std::hex << address << std::dec << '\n';
            }
            SymCleanup(process);
        }
        std::cerr.flush();
        return EXCEPTION_EXECUTE_HANDLER;
    }
#endif

    struct HostOptions
    {
        void *parentWindow = nullptr;
        int width = 960;
        int height = 640;
        bool gameView = false;
    };

    HostOptions ParseOptions(int argc, char **argv)
    {
        HostOptions options;
        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);
            if (argument == "--parent-hwnd" && index + 1 < argc)
            {
                const auto value = std::stoull(argv[++index], nullptr, 0);
                options.parentWindow = reinterpret_cast<void *>(static_cast<std::uintptr_t>(value));
            }
            else if (argument == "--width" && index + 1 < argc)
            {
                options.width = std::max(1, std::atoi(argv[++index]));
            }
            else if (argument == "--height" && index + 1 < argc)
            {
                options.height = std::max(1, std::atoi(argv[++index]));
            }
            else if (argument == "--view-mode" && index + 1 < argc)
            {
                options.gameView = std::string_view(argv[++index]) == "game";
            }
        }
        return options;
    }

#ifdef _WIN32
    class CommandStream
    {
    public:
        CommandStream()
            : m_input(GetStdHandle(STD_INPUT_HANDLE))
        {
        }

        bool IsConnected() const
        {
            return m_input && m_input != INVALID_HANDLE_VALUE && GetFileType(m_input) == FILE_TYPE_PIPE;
        }

        bool Poll(std::vector<std::string> &commands)
        {
            if (!IsConnected())
            {
                return true;
            }

            DWORD available = 0;
            if (!PeekNamedPipe(m_input, nullptr, 0, nullptr, &available, nullptr))
            {
                return GetLastError() != ERROR_BROKEN_PIPE;
            }

            while (available > 0)
            {
                char buffer[1024];
                DWORD bytesRead = 0;
                const DWORD requested = std::min<DWORD>(available, sizeof(buffer));
                if (!ReadFile(m_input, buffer, requested, &bytesRead, nullptr) || bytesRead == 0)
                {
                    return false;
                }

                m_pending.append(buffer, bytesRead);
                available -= bytesRead;
            }

            std::size_t lineEnd = 0;
            while ((lineEnd = m_pending.find('\n')) != std::string::npos)
            {
                auto line = m_pending.substr(0, lineEnd);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                m_pending.erase(0, lineEnd + 1);
                if (!line.empty())
                {
                    commands.push_back(std::move(line));
                }
            }
            return true;
        }

    private:
        HANDLE m_input = INVALID_HANDLE_VALUE;
        std::string m_pending;
    };
#endif

    void WriteEvent(std::string_view json)
    {
        std::cout << json << '\n' << std::flush;
    }

    constexpr float kCameraBoostMultiplier = 2.5f;
    constexpr float kCameraScrollStepFactor = 1.2f;
    constexpr float kCameraMouseSensitivity = 0.12f;

    glm::mat4 BuildEditorCameraTransform(const glm::vec3 &position, float yawDegrees, float pitchDegrees)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = glm::rotate(transform, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::rotate(transform, glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }

    PlutoGE::render::CameraData BuildEditorCamera(int width, int height, const EditorSession::EditorCameraState &cameraState)
    {
        const float safeAspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));
        const glm::mat4 transform = BuildEditorCameraTransform(cameraState.position, cameraState.yawDegrees, cameraState.pitchDegrees);
        const glm::vec3 forward = -glm::normalize(glm::vec3(transform[2]));
        const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
        PlutoGE::render::CameraData camera;
        camera.view = glm::lookAt(cameraState.position, cameraState.position + forward, up);
        camera.projection = glm::perspective(glm::radians(cameraState.fovY), safeAspect, cameraState.farPlane, cameraState.nearPlane);
        camera.nearPlane = cameraState.nearPlane;
        camera.farPlane = cameraState.farPlane;
        return camera;
    }

    void CollectSceneCameras(PlutoGE::scene::Entity *entity,
                             PlutoGE::scene::CameraComponent *&mainCamera,
                             PlutoGE::scene::CameraComponent *&fallbackCamera)
    {
        if (!entity || !entity->IsActive()) return;
        if (auto *camera = entity->GetComponent<PlutoGE::scene::CameraComponent>(); camera && camera->IsEnabled() && camera->GetCamera())
        {
            if (!fallbackCamera) fallbackCamera = camera;
            if (camera->IsMainCamera()) mainCamera = camera;
        }
        for (auto *child : entity->GetChildren()) CollectSceneCameras(child, mainCamera, fallbackCamera);
    }

    PlutoGE::scene::CameraComponent *FindRuntimeCamera(PlutoGE::scene::Scene *scene)
    {
        PlutoGE::scene::CameraComponent *mainCamera = nullptr;
        PlutoGE::scene::CameraComponent *fallbackCamera = nullptr;
        if (scene)
        {
            for (auto *root : scene->GetRootEntities()) CollectSceneCameras(root, mainCamera, fallbackCamera);
        }
        return mainCamera ? mainCamera : fallbackCamera;
    }
}

int main(int argc, char **argv)
{
#ifndef _WIN32
    std::cerr << "PlutoGEEditorHost embedded mode is currently supported on Windows only.\n";
    return 2;
#else
    if (argc == 2 && std::string_view(argv[1]) == "--component-registry-smoke")
    {
        PlutoGE::scene::Scene scene;
        auto entity = std::make_unique<PlutoGE::scene::Entity>(PlutoGE::scene::EntityConfig{.name = "Registry Smoke"});
        auto *created = scene.AddEntity(std::move(entity));
        auto *component = PlutoGE::scene::AddComponentByTypeName(*created, "MeshComponent");
        const bool registered = component && scene.GetMeshComponents().size() == 1;
        const bool typedLookup = created->GetComponent<PlutoGE::scene::MeshComponent>() == component;
        std::cout << "{\"registered\":" << (registered ? "true" : "false")
                  << ",\"typedLookup\":" << (typedLookup ? "true" : "false") << "}" << std::endl;
        return registered && typedLookup ? 0 : 2;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetUnhandledExceptionFilter(HostUnhandledExceptionFilter);

    const auto options = ParseOptions(argc, argv);
    const bool embedded = options.parentWindow != nullptr;

    auto &engine = PlutoGE::core::Engine::GetInstance();
    PlutoGE::core::EngineConfig config{
        PlutoGE::platform::WindowConfig{
            .title = options.gameView ? "PlutoGE Game View" : "PlutoGE Scene View",
            .width = options.width,
            .height = options.height,
            .resizable = !embedded,
            .visible = !embedded,
            .fullscreen = false,
            .resizeCallback = nullptr,
            .nativeParent = options.parentWindow,
            .embedded = embedded,
        }};

    if (!engine.Initialize(config))
    {
        WriteEvent(R"({"type":"error","message":"Engine initialization failed"})");
        return 1;
    }

    auto &window = engine.GetWindow();
    auto &renderer = engine.GetRenderer();
    renderer.SetVSyncEnabled(true);

    EditorSession editorSession(engine);
    if (!editorSession.Initialize())
    {
        WriteEvent(R"({"type":"error","message":"Editor scene initialization failed"})");
        engine.Shutdown();
        return 1;
    }

    CommandStream commandStream;
    bool running = true;
    bool visible = !embedded;
    bool editorCameraLookActive = false;
    bool editorCameraInputChanged = false;
    EditorViewportInteraction viewportInteraction;
    auto previousFrameTime = std::chrono::steady_clock::now();

    WriteEvent(R"({"type":"ready","protocol":1})");
    WriteEvent(editorSession.BuildSnapshotEvent());

    while (running && !window.ShouldClose())
    {
        std::vector<std::string> commands;
        if (!commandStream.Poll(commands))
        {
            break;
        }

        for (const auto &commandLine : commands)
        {
            g_hostPhase = "command handling";
            std::istringstream command(commandLine);
            std::string name;
            command >> name;
            if (name == "bounds")
            {
                int x = 0;
                int y = 0;
                int width = 0;
                int height = 0;
                if (command >> x >> y >> width >> height)
                {
                    window.SetEmbeddedBounds(x, y, width, height);
                }
            }
            else if (name == "visible")
            {
                int nextVisible = 1;
                if (command >> nextVisible)
                {
                    visible = nextVisible != 0;
                    window.SetEmbeddedVisible(visible);
                }
            }
            else if (name == "ping")
            {
                std::string token;
                command >> token;
                WriteEvent("{\"type\":\"pong\",\"token\":\"" + token + "\"}");
            }
            else if (name == "shutdown")
            {
                running = false;
            }
            else
            {
                std::string errorMessage;
                if (editorSession.HandleCommand(commandLine, errorMessage))
                {
                    WriteEvent(editorSession.BuildSnapshotEvent());
                }
                else
                {
                    WriteEvent(R"({"type":"editor-error","message":"Editor command failed; see native diagnostics"})");
                    if (!errorMessage.empty()) std::cerr << errorMessage << std::endl;
                }
            }
        }

        g_hostPhase = "window event polling";
        if (embedded) window.PollEmbeddedEvents();
        else window.PollEvents();
        const auto currentFrameTime = std::chrono::steady_clock::now();
        const float deltaTime = std::min(std::chrono::duration<float>(currentFrameTime - previousFrameTime).count(), 0.1f);
        previousFrameTime = currentFrameTime;
        const auto &input = window.GetInputState();
        auto &editorCamera = editorSession.GetEditorCamera();
        if (!options.gameView && !engine.IsRuntimeRunning() && input.IsMouseButtonDown(GLFW_MOUSE_BUTTON_RIGHT))
        {
            if (!editorCameraLookActive)
            {
                editorCameraLookActive = true;
                window.SetEditorCursorLocked(true);
            }

            editorCamera.yawDegrees -= static_cast<float>(input.mouseState.deltaX) * kCameraMouseSensitivity;
            editorCamera.pitchDegrees = std::clamp(editorCamera.pitchDegrees - static_cast<float>(input.mouseState.deltaY) * kCameraMouseSensitivity, -89.0f, 89.0f);
            editorCameraInputChanged = editorCameraInputChanged || input.mouseState.deltaX != 0.0 || input.mouseState.deltaY != 0.0;
            if (input.mouseState.scrollDeltaY != 0.0)
            {
                editorCamera.speedAdjustment = std::clamp(editorCamera.speedAdjustment * std::pow(kCameraScrollStepFactor, static_cast<float>(input.mouseState.scrollDeltaY)), 0.1f, 10.0f);
                editorCameraInputChanged = true;
            }

            const glm::mat4 transform = BuildEditorCameraTransform(editorCamera.position, editorCamera.yawDegrees, editorCamera.pitchDegrees);
            const glm::vec3 forward = -glm::normalize(glm::vec3(transform[2]));
            const glm::vec3 right = glm::normalize(glm::vec3(transform[0]));
            glm::vec3 movement(0.0f);
            if (input.keys[GLFW_KEY_W]) movement += forward;
            if (input.keys[GLFW_KEY_S]) movement -= forward;
            if (input.keys[GLFW_KEY_D]) movement += right;
            if (input.keys[GLFW_KEY_A]) movement -= right;
            if (input.keys[GLFW_KEY_E]) movement.y += 1.0f;
            if (input.keys[GLFW_KEY_Q]) movement.y -= 1.0f;
            if (glm::dot(movement, movement) > 0.0f)
            {
                float speed = editorCamera.moveSpeed * editorCamera.speedAdjustment;
                if (input.keys[GLFW_KEY_LEFT_SHIFT] || input.keys[GLFW_KEY_RIGHT_SHIFT]) speed *= kCameraBoostMultiplier;
                editorCamera.position += glm::normalize(movement) * speed * deltaTime;
                editorCameraInputChanged = true;
            }
        }
        else if (editorCameraLookActive)
        {
            window.SetEditorCursorLocked(false);
            editorCameraLookActive = false;
            if (editorCameraInputChanged)
            {
                WriteEvent(editorSession.BuildSnapshotEvent());
                editorCameraInputChanged = false;
            }
        }

        const auto extents = window.GetExtents();
        const auto editorCameraData = BuildEditorCamera(extents.width, extents.height, editorCamera);
        if (!options.gameView && viewportInteraction.Update(editorSession, editorCameraData, input, extents.width, extents.height, engine.IsRuntimeRunning()))
        {
            WriteEvent(editorSession.BuildSnapshotEvent());
        }
        if (options.gameView) renderer.ClearSubmissionCullingCameras();
        else renderer.SetSubmissionCullingCameras({editorCameraData});
        renderer.BeginProfilingFrame();
        g_hostPhase = "scene update";
        editorSession.Update(deltaTime);

        if (visible)
        {
            auto *scene = editorSession.GetScene();
            std::vector<PlutoGE::render::IPostProcessEffect *> editorPostProcessEffects;
            editorPostProcessEffects.reserve(editorSession.GetEditorPostProcessEffects().size());
            for (const auto &effect : editorSession.GetEditorPostProcessEffects())
            {
                if (effect) editorPostProcessEffects.push_back(effect.get());
            }
            g_hostPhase = "renderer begin frame";
            renderer.BeginFrame();
            if (options.gameView)
            {
                g_hostPhase = "game view render";
                if (auto *runtimeCamera = FindRuntimeCamera(scene)) renderer.RenderFrame(*runtimeCamera, nullptr, scene ? scene->GetLights() : std::vector<PlutoGE::scene::Light *>{});
            }
            else
            {
                g_hostPhase = "scene view render";
                renderer.RenderFrame(editorCameraData, nullptr, scene ? scene->GetLights() : std::vector<PlutoGE::scene::Light *>{}, &editorPostProcessEffects, scene, editorCamera.gridVisible, true);
                if (!engine.IsRuntimeRunning()) viewportInteraction.Render(editorSession, editorCameraData, extents.width, extents.height);
            }
            const auto &frameStats = renderer.GetCpuFrameStats();
            if (editorSession.SetViewportStats(frameStats.submittedRenderCommandCount, frameStats.visibleRenderCommandCount))
            {
                WriteEvent(editorSession.BuildSnapshotEvent());
            }
            renderer.ClearRenderCommands();
            g_hostPhase = "renderer end frame";
            renderer.EndFrame();
        }
        else
        {
            renderer.ClearRenderCommands();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    WriteEvent(R"({"type":"stopping"})");
    viewportInteraction.Shutdown();
    editorSession.Shutdown();
    engine.Shutdown();
    return 0;
#endif
}
