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
#include "SharedTexturePublisher.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
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
        bool streamed = false;
        bool cpuStream = false;
        bool sharedTextureSmoke = false;
        std::uint32_t electronProcessId = 0;
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
            else if (argument == "--streamed")
            {
                options.streamed = true;
            }
            else if (argument == "--cpu-stream")
            {
                options.cpuStream = true;
            }
            else if (argument == "--electron-pid" && index + 1 < argc)
            {
                options.electronProcessId = static_cast<std::uint32_t>(std::stoul(argv[++index]));
            }
            else if (argument == "--shared-texture-smoke")
            {
                options.sharedTextureSmoke = true;
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

        bool Poll(std::vector<std::string> &commands, std::size_t maxCommands = 256)
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
            while (commands.size() < maxCommands &&
                   (lineEnd = m_pending.find('\n')) != std::string::npos)
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

    std::string Base64Encode(const std::vector<std::uint8_t> &bytes)
    {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        encoded.reserve(((bytes.size() + 2) / 3) * 4);
        for (std::size_t index = 0; index < bytes.size(); index += 3)
        {
            const std::uint32_t first = bytes[index];
            const std::uint32_t second = index + 1 < bytes.size() ? bytes[index + 1] : 0;
            const std::uint32_t third = index + 2 < bytes.size() ? bytes[index + 2] : 0;
            const std::uint32_t value = (first << 16u) | (second << 8u) | third;
            encoded.push_back(alphabet[(value >> 18u) & 0x3fu]);
            encoded.push_back(alphabet[(value >> 12u) & 0x3fu]);
            encoded.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6u) & 0x3fu] : '=');
            encoded.push_back(index + 2 < bytes.size() ? alphabet[value & 0x3fu] : '=');
        }
        return encoded;
    }

#ifdef _WIN32
    void *LoadHostOpenGlProcedure(const char *name)
    {
        static HMODULE openGlModule = LoadLibraryA("opengl32.dll");
        PROC procedure = wglGetProcAddress(name);
        if (procedure == nullptr || procedure == reinterpret_cast<PROC>(1) ||
            procedure == reinterpret_cast<PROC>(2) || procedure == reinterpret_cast<PROC>(3) ||
            procedure == reinterpret_cast<PROC>(-1))
        {
            procedure = openGlModule ? GetProcAddress(openGlModule, name) : nullptr;
        }
        return reinterpret_cast<void *>(procedure);
    }
#endif

    class CpuFramePublisher
    {
    public:
        bool PublishIfDue(int sourceWidth, int sourceHeight)
        {
            if (sourceWidth <= 0 || sourceHeight <= 0) return false;
            const auto now = std::chrono::steady_clock::now();
            if (m_lastPublish.time_since_epoch().count() != 0 &&
                now - m_lastPublish < std::chrono::milliseconds(125))
            {
                return false;
            }

#ifdef _WIN32
            if (!glad_glReadPixels && !gladLoadGLLoader(&LoadHostOpenGlProcedure)) return false;
#endif
            if (!glad_glReadPixels) return false;

            constexpr int maxCaptureDimension = 800;
            const float captureScale = std::min(
                1.0f,
                static_cast<float>(maxCaptureDimension) /
                    static_cast<float>(std::max(sourceWidth, sourceHeight)));
            const int width = std::max(1, static_cast<int>(std::round(sourceWidth * captureScale)));
            const int height = std::max(1, static_cast<int>(std::round(sourceHeight * captureScale)));

            m_sourcePixels.resize(static_cast<std::size_t>(sourceWidth) * sourceHeight * 4u);
            GLint previousReadFramebuffer = 0;
            GLint previousReadBuffer = GL_BACK;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
            glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, sourceWidth, sourceHeight, GL_RGBA, GL_UNSIGNED_BYTE, m_sourcePixels.data());
            glReadBuffer(static_cast<GLenum>(previousReadBuffer));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));

            m_capturePixels.resize(static_cast<std::size_t>(width) * height * 4u);
            for (int outputY = 0; outputY < height; ++outputY)
            {
                const int sourceTopY = std::min(sourceHeight - 1, outputY * sourceHeight / height);
                const int sourceY = sourceHeight - 1 - sourceTopY;
                for (int outputX = 0; outputX < width; ++outputX)
                {
                    const int sourceX = std::min(sourceWidth - 1, outputX * sourceWidth / width);
                    const auto sourceOffset = (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4u;
                    const auto destinationOffset = (static_cast<std::size_t>(outputY) * width + outputX) * 4u;
                    std::copy_n(m_sourcePixels.data() + sourceOffset, 4, m_capturePixels.data() + destinationOffset);
                }
            }

            if (sourceWidth != m_previousSourceWidth || sourceHeight != m_previousSourceHeight)
            {
                ++m_generation;
                m_previousSourceWidth = sourceWidth;
                m_previousSourceHeight = sourceHeight;
            }

            std::ostringstream event;
            event << "{\"type\":\"viewport-frame\",\"transport\":\"cpu\",\"sequence\":" << ++m_sequence
                  << ",\"generation\":" << m_generation
                  << ",\"width\":" << width << ",\"height\":" << height
                  << ",\"sourceWidth\":" << sourceWidth << ",\"sourceHeight\":" << sourceHeight
                  << ",\"data\":\"" << Base64Encode(m_capturePixels) << "\"}";
            WriteEvent(event.str());
            m_lastPublish = now;
            return true;
        }

    private:
        std::chrono::steady_clock::time_point m_lastPublish{};
        std::vector<std::uint8_t> m_sourcePixels;
        std::vector<std::uint8_t> m_capturePixels;
        std::uint64_t m_sequence = 0;
        std::uint64_t m_generation = 0;
        int m_previousSourceWidth = 0;
        int m_previousSourceHeight = 0;
    };

    struct RemoteInputState
    {
        std::array<bool, 512> keys{};
        std::array<bool, 8> buttons{};
        double x = 0.0;
        double y = 0.0;
        double deltaX = 0.0;
        double deltaY = 0.0;
        double scrollX = 0.0;
        double scrollY = 0.0;

        void Reset()
        {
            keys.fill(false);
            buttons.fill(false);
            deltaX = 0.0;
            deltaY = 0.0;
            scrollX = 0.0;
            scrollY = 0.0;
        }

        void Apply(PlutoGE::platform::InputState &input)
        {
            input.keys = keys;
            std::copy(buttons.begin(), buttons.end(), input.mouseState.buttons);
            input.mouseState.x = x;
            input.mouseState.y = y;
            input.mouseState.deltaX += deltaX;
            input.mouseState.deltaY += deltaY;
            input.mouseState.scrollDeltaX += scrollX;
            input.mouseState.scrollDeltaY += scrollY;
            deltaX = 0.0;
            deltaY = 0.0;
            scrollX = 0.0;
            scrollY = 0.0;
        }
    };

    std::string JsonEscape(std::string_view value)
    {
        std::ostringstream output;
        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20)
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character) << std::dec;
                else
                    output << static_cast<char>(character);
            }
        }
        return output.str();
    }

    void AppendCpuPassTimings(std::ostringstream &event, const std::vector<PlutoGE::render::CpuPassTiming> &timings)
    {
        event << ",\"cpuPasses\":[";
        for (std::size_t index = 0; index < timings.size(); ++index)
        {
            if (index > 0) event << ',';
            event << "{\"name\":\"" << JsonEscape(timings[index].name)
                  << "\",\"timeMs\":" << timings[index].cpuTimeMs << '}';
        }
        event << ']';
    }

    void AppendGpuPassTimings(std::ostringstream &event,
                              std::string_view field,
                              const std::vector<PlutoGE::render::GpuPassTiming> &timings,
                              bool rendered)
    {
        event << ",\"" << field << "\":[";
        for (std::size_t index = 0; index < timings.size(); ++index)
        {
            if (index > 0) event << ',';
            event << "{\"name\":\"" << JsonEscape(timings[index].name)
                  << "\",\"timeMs\":" << (rendered ? timings[index].gpuTimeMs : 0.0f)
                  << ",\"available\":" << (rendered && timings[index].hasResult ? "true" : "false") << '}';
        }
        event << ']';
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
    // Embedded viewports start uncapped. Loading a project or changing its
    // settings applies the project's VSync preference through the renderer,
    // just as it does for a standalone engine window.
    renderer.SetVSyncEnabled(!embedded);
    if (options.streamed) window.SetCursorLockOverride(true);

    if (options.sharedTextureSmoke)
    {
        SharedTexturePublisher publisher;
        std::string error;
        bool succeeded = options.electronProcessId != 0 && publisher.Initialize(options.electronProcessId, error);
        if (succeeded)
        {
            renderer.BeginFrame();
            const auto frame = publisher.Publish(options.width, options.height, error);
            renderer.EndFrame();
            succeeded = frame.has_value();
            if (frame) publisher.Release(frame->generation, frame->slot);
        }
        publisher.Shutdown();
        std::cout << "{\"sharedTexture\":" << (succeeded ? "true" : "false")
                  << ",\"message\":\"" << JsonEscape(error) << "\"}" << std::endl;
        engine.Shutdown();
        return succeeded ? 0 : 3;
    }

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
    bool surfaceShown = !embedded;
    DWORD peerForegroundProcessId = 0;
    bool editorCameraLookActive = false;
    bool editorCameraInputChanged = false;
    EditorViewportInteraction viewportInteraction;
    CpuFramePublisher framePublisher;
    SharedTexturePublisher sharedTexturePublisher;
    bool sharedTextureTransportFailed = false;
    std::chrono::steady_clock::time_point sharedTextureStarvedSince{};
    RemoteInputState remoteInput;
    auto previousFrameTime = std::chrono::steady_clock::now();
    auto performanceWindowStart = previousFrameTime;
    auto previousLoopEnd = previousFrameTime;
    int performanceFrameCount = 0;
    float performanceFrameTimeTotalMs = 0.0f;
    float performanceMaxFrameTimeMs = 0.0f;
    float performanceEventPollingTotalMs = 0.0f;
    float performanceUpdateTotalMs = 0.0f;
    float performanceRenderTotalMs = 0.0f;
    float performancePresentTotalMs = 0.0f;
    float performanceCommandTotalMs = 0.0f;
    float performanceInteractionTotalMs = 0.0f;
    float performanceWaitTotalMs = 0.0f;
    float performanceCpuPassTotalMs = 0.0f;
    float performanceGpuPassTotalMs = 0.0f;

    if (options.streamed && !options.cpuStream && options.electronProcessId != 0)
    {
        std::string sharedTextureError;
        if (sharedTexturePublisher.Initialize(options.electronProcessId, sharedTextureError))
        {
            WriteEvent(R"({"type":"editor-log","message":"GPU shared-texture viewport transport enabled"})");
        }
        else
        {
            WriteEvent("{\"type\":\"editor-log\",\"success\":false,\"message\":\"GPU viewport unavailable; using CPU fallback: " +
                       JsonEscape(sharedTextureError) + "\"}");
        }
    }

    WriteEvent(R"({"type":"ready","protocol":1})");
    WriteEvent(editorSession.BuildSnapshotEvent());

    while (running && !window.ShouldClose())
    {
        const auto loopStart = std::chrono::steady_clock::now();
        float eventPollingMs = 0.0f;
        float updateMs = 0.0f;
        float renderMs = 0.0f;
        float presentMs = 0.0f;
        float commandMs = 0.0f;
        float interactionMs = 0.0f;
        float waitMs = 0.0f;
        float cpuPassMs = 0.0f;
        float gpuPassMs = 0.0f;

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
                    // Apply hides immediately, before any following synchronous
                    // load command in this batch. Waiting until the end of the
                    // frame leaves the native HWND above Chromium while its
                    // thread is not pumping messages.
                    if (embedded && !visible && surfaceShown)
                    {
                        if (editorCameraLookActive)
                        {
                            window.SetEditorCursorLocked(false);
                            editorCameraLookActive = false;
                            editorCameraInputChanged = false;
                        }
                        window.SetCursorLockOverride(true);
                        window.GetInputState().ClearKeyStates();
                        window.SetEmbeddedInteractionEnabled(false);
                        window.SetEmbeddedVisible(false);
                        // Dispatch any queued input/focus messages before
                        // entering a synchronous project/scene load.
                        window.PollEmbeddedEvents();
                        surfaceShown = false;
                    }
                }
            }
            else if (name == "owner")
            {
                std::string value;
                if (command >> value)
                {
                    const auto handle = std::stoull(value, nullptr, 0);
                    window.SetEmbeddedOwner(reinterpret_cast<void *>(static_cast<std::uintptr_t>(handle)));
                }
            }
            else if (name == "peer_process")
            {
                unsigned long processId = 0;
                if (command >> processId)
                {
                    peerForegroundProcessId = static_cast<DWORD>(processId);
                }
            }
            else if (name == "ping")
            {
                std::string token;
                command >> token;
                WriteEvent("{\"type\":\"pong\",\"token\":\"" + token + "\"}");
            }
            else if (name == "focus")
            {
                if (!options.streamed) window.Focus();
            }
            else if (name == "input_pointer")
            {
                double x = 0.0;
                double y = 0.0;
                double deltaX = 0.0;
                double deltaY = 0.0;
                if (command >> x >> y >> deltaX >> deltaY)
                {
                    remoteInput.x = x;
                    remoteInput.y = y;
                    remoteInput.deltaX += deltaX;
                    remoteInput.deltaY += deltaY;
                }
            }
            else if (name == "input_button")
            {
                int button = -1;
                int down = 0;
                if (command >> button >> down && button >= 0 && button < static_cast<int>(remoteInput.buttons.size()))
                {
                    remoteInput.buttons[static_cast<std::size_t>(button)] = down != 0;
                }
            }
            else if (name == "input_wheel")
            {
                double x = 0.0;
                double y = 0.0;
                if (command >> x >> y)
                {
                    remoteInput.scrollX += x;
                    remoteInput.scrollY += y;
                }
            }
            else if (name == "input_key")
            {
                int key = -1;
                int down = 0;
                if (command >> key >> down && key >= 0 && key < static_cast<int>(remoteInput.keys.size()))
                {
                    remoteInput.keys[static_cast<std::size_t>(key)] = down != 0;
                }
            }
            else if (name == "input_reset")
            {
                remoteInput.Reset();
            }
            else if (name == "frame_release")
            {
                std::uint64_t generation = 0;
                std::uint32_t slot = 0;
                if (command >> generation >> slot) sharedTexturePublisher.Release(generation, slot);
            }
            else if (name == "shared_texture_failed")
            {
                if (!sharedTextureTransportFailed)
                {
                    sharedTextureTransportFailed = true;
                    WriteEvent(R"({"type":"editor-log","success":false,"message":"Electron rejected the GPU viewport texture; CPU fallback enabled"})");
                }
            }
            else if (name == "shutdown")
            {
                running = false;
            }
            else
            {
                std::string requestToken;
                std::string argument;
                while (command >> argument)
                {
                    if (argument.rfind("request=", 0) == 0)
                    {
                        requestToken = argument.substr(8);
                    }
                }

                const bool loadingOperation = !requestToken.empty() &&
                                              (name == "load_project" || name == "create_project" ||
                                               name == "load_scene" || name == "new_scene");
                if (embedded && loadingOperation)
                {
                    if (editorCameraLookActive)
                    {
                        window.SetEditorCursorLocked(false);
                        editorCameraLookActive = false;
                        editorCameraInputChanged = false;
                    }
                    window.SetCursorLockOverride(true);
                    window.GetInputState().ClearKeyStates();
                    window.SetEmbeddedInteractionEnabled(false);
                    window.SetEmbeddedVisible(false);
                    window.PollEmbeddedEvents();
                    surfaceShown = false;
                }

                std::string errorMessage;
                if (!requestToken.empty())
                {
                    editorSession.SetOperationProgressCallback(
                        [requestToken](int percent, const std::string &detail)
                        {
                            std::ostringstream progress;
                            progress << "{\"type\":\"operation-progress\",\"token\":\""
                                     << JsonEscape(requestToken) << "\",\"progress\":" << percent
                                     << ",\"detail\":\"" << JsonEscape(detail) << "\"}";
                            WriteEvent(progress.str());
                        });
                }
                else
                {
                    editorSession.SetOperationProgressCallback({});
                }
                const bool succeeded = editorSession.HandleCommand(commandLine, errorMessage);
                editorSession.SetOperationProgressCallback({});
                if (succeeded)
                {
                    WriteEvent(editorSession.BuildSnapshotEvent());
                }
                else
                {
                    WriteEvent(R"({"type":"editor-error","message":"Editor command failed; see native diagnostics"})");
                    if (!errorMessage.empty()) std::cerr << errorMessage << std::endl;
                }
                if (!requestToken.empty())
                {
                    std::ostringstream operation;
                    operation << "{\"type\":\"operation-complete\",\"token\":\""
                              << JsonEscape(requestToken) << "\",\"success\":"
                              << (succeeded ? "true" : "false") << ",\"message\":\""
                              << JsonEscape(errorMessage) << "\"}";
                    WriteEvent(operation.str());
                }
            }
        }
        commandMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - loopStart).count();

        window.SetScriptInputEnabled(engine.IsRuntimeRunning());

        g_hostPhase = "window event polling";
        const auto eventPollingStart = std::chrono::steady_clock::now();
        if (embedded) window.PollEmbeddedEvents();
        else window.PollEvents();
        if (options.streamed) remoteInput.Apply(window.GetInputState());
        const auto eventPollingEnd = std::chrono::steady_clock::now();
        eventPollingMs = std::chrono::duration<float, std::milli>(eventPollingEnd - eventPollingStart).count();
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
        interactionMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - eventPollingEnd).count();
        g_hostPhase = "scene update";
        const auto updateStart = std::chrono::steady_clock::now();
        editorSession.Update(deltaTime);
        const auto updateEnd = std::chrono::steady_clock::now();
        updateMs = std::chrono::duration<float, std::milli>(updateEnd - updateStart).count();

        // Keep the native HWND and renderer state together. Merely skipping a
        // present leaves an invisible native window in front of Chromium, where
        // it still consumes mouse input. Also hide while a native dialog or a
        // different application has focus so the overlay cannot cover it.
        auto *nativeOwner = static_cast<HWND>(window.GetConfig().nativeParent);
        auto *nativeSurface = static_cast<HWND>(window.GetNativeHandle());
        auto *foregroundWindow = GetForegroundWindow();
        DWORD foregroundProcessId = 0;
        if (foregroundWindow)
        {
            GetWindowThreadProcessId(foregroundWindow, &foregroundProcessId);
        }
        const bool ownerCanPresent = !embedded ||
                                     (IsWindow(nativeOwner) && IsWindowVisible(nativeOwner) && !IsIconic(nativeOwner) &&
                                      (foregroundWindow == nativeOwner || foregroundWindow == nativeSurface ||
                                       (peerForegroundProcessId != 0 && foregroundProcessId == peerForegroundProcessId)));
        const bool shouldShowSurface = visible && ownerCanPresent && !options.streamed;
        const bool shouldRender = options.streamed ? visible : shouldShowSurface;
        if (embedded && !options.streamed && surfaceShown != shouldShowSurface)
        {
            if (shouldShowSurface)
            {
                window.SetEmbeddedInteractionEnabled(true);
                window.SetEmbeddedVisible(true);
                window.SetCursorLockOverride(false);
            }
            else
            {
                // A hidden GLFW window may miss the mouse-button release that
                // ended camera look. Release capture and clear held input so
                // it cannot keep controlling the cursor over another window.
                if (editorCameraLookActive)
                {
                    window.SetEditorCursorLocked(false);
                    editorCameraLookActive = false;
                    if (editorCameraInputChanged)
                    {
                        WriteEvent(editorSession.BuildSnapshotEvent());
                        editorCameraInputChanged = false;
                    }
                }
                window.SetCursorLockOverride(true);
                window.GetInputState().ClearKeyStates();
                window.SetEmbeddedInteractionEnabled(false);
                window.SetEmbeddedVisible(false);
            }
            surfaceShown = shouldShowSurface;
        }

        if (shouldRender)
        {
            const auto renderStart = std::chrono::steady_clock::now();
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
                renderer.RenderFrame(editorCameraData, nullptr, scene ? scene->GetLights() : std::vector<PlutoGE::scene::Light *>{}, &editorPostProcessEffects, scene, editorCamera.gridVisible, editorSession.GetDebugShapesVisible());
                if (!engine.IsRuntimeRunning()) viewportInteraction.Render(editorSession, editorCameraData, extents.width, extents.height);
            }
            const auto &frameStats = renderer.GetCpuFrameStats();
            if (editorSession.SetViewportStats(frameStats.submittedRenderCommandCount, frameStats.visibleRenderCommandCount))
            {
                WriteEvent(editorSession.BuildSnapshotEvent());
            }
            renderer.ClearRenderCommands();
            const auto presentStart = std::chrono::steady_clock::now();
            renderMs = std::chrono::duration<float, std::milli>(presentStart - renderStart).count();
            if (options.streamed)
            {
                bool publishedSharedTexture = false;
                if (sharedTexturePublisher.IsAvailable() && !sharedTextureTransportFailed)
                {
                    std::string sharedTextureError;
                    if (const auto frame = sharedTexturePublisher.Publish(extents.width, extents.height, sharedTextureError))
                    {
                        std::ostringstream event;
                        event << "{\"type\":\"viewport-shared-frame\",\"transport\":\"shared-texture\",\"sequence\":"
                              << frame->sequence << ",\"generation\":" << frame->generation
                              << ",\"slot\":" << frame->slot << ",\"width\":" << frame->width
                              << ",\"height\":" << frame->height << ",\"handle\":\""
                              << frame->remoteHandle << "\"}";
                        WriteEvent(event.str());
                        publishedSharedTexture = true;
                        sharedTextureStarvedSince = {};
                    }
                    else if (!sharedTextureError.empty())
                    {
                        WriteEvent("{\"type\":\"editor-log\",\"success\":false,\"message\":\"GPU viewport failed; using CPU fallback: " +
                                   JsonEscape(sharedTextureError) + "\"}");
                        sharedTextureTransportFailed = true;
                    }
                    else
                    {
                        const auto now = std::chrono::steady_clock::now();
                        if (sharedTextureStarvedSince.time_since_epoch().count() == 0)
                            sharedTextureStarvedSince = now;
                        else if (now - sharedTextureStarvedSince > std::chrono::seconds(2))
                        {
                            sharedTextureTransportFailed = true;
                            WriteEvent(R"({"type":"editor-log","success":false,"message":"GPU viewport release timed out; CPU fallback enabled"})");
                        }
                    }
                }
                if (!publishedSharedTexture &&
                    (!sharedTexturePublisher.IsAvailable() || sharedTextureTransportFailed))
                    framePublisher.PublishIfDue(extents.width, extents.height);
            }
            g_hostPhase = "renderer end frame";
            renderer.EndFrame(nullptr, !options.streamed);
            const auto presentEnd = std::chrono::steady_clock::now();
            presentMs = std::chrono::duration<float, std::milli>(presentEnd - presentStart).count();
            cpuPassMs = renderer.GetTotalCpuPassTimeMs();
            gpuPassMs = renderer.GetTotalGpuPassTimeMs();
        }
        else
        {
            renderer.ClearRenderCommands();
            const auto waitStart = std::chrono::steady_clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            waitMs += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - waitStart).count();
        }

        const auto loopEnd = std::chrono::steady_clock::now();
        // Include time spent publishing the preceding performance and snapshot
        // events. FPS already included that wall time, while loopStart-to-loopEnd
        // frame timing did not.
        const float frameTimeMs = std::chrono::duration<float, std::milli>(loopEnd - previousLoopEnd).count();
        previousLoopEnd = loopEnd;
        ++performanceFrameCount;
        performanceFrameTimeTotalMs += frameTimeMs;
        performanceMaxFrameTimeMs = std::max(performanceMaxFrameTimeMs, frameTimeMs);
        performanceEventPollingTotalMs += eventPollingMs;
        performanceUpdateTotalMs += updateMs;
        performanceRenderTotalMs += renderMs;
        performancePresentTotalMs += presentMs;
        performanceCommandTotalMs += commandMs;
        performanceInteractionTotalMs += interactionMs;
        performanceWaitTotalMs += waitMs;
        performanceCpuPassTotalMs += cpuPassMs;
        performanceGpuPassTotalMs += gpuPassMs;

        const float performanceWindowMs = std::chrono::duration<float, std::milli>(loopEnd - performanceWindowStart).count();
        if (performanceWindowMs >= 250.0f && performanceFrameCount > 0)
        {
            const float inverseFrameCount = 1.0f / static_cast<float>(performanceFrameCount);
            const float averageFrameTimeMs = performanceFrameTimeTotalMs * inverseFrameCount;
            const float averageCommandMs = performanceCommandTotalMs * inverseFrameCount;
            const float averageEventPollingMs = performanceEventPollingTotalMs * inverseFrameCount;
            const float averageInteractionMs = performanceInteractionTotalMs * inverseFrameCount;
            const float averageUpdateMs = performanceUpdateTotalMs * inverseFrameCount;
            const float averageRenderMs = performanceRenderTotalMs * inverseFrameCount;
            const float averagePresentMs = performancePresentTotalMs * inverseFrameCount;
            const float averageWaitMs = performanceWaitTotalMs * inverseFrameCount;
            const float accountedFrameTimeMs = averageCommandMs + averageEventPollingMs + averageInteractionMs +
                                               averageUpdateMs + averageRenderMs + averagePresentMs + averageWaitMs;
            const float averageOverheadMs = std::max(0.0f, averageFrameTimeMs - accountedFrameTimeMs);
            const auto &cpuFrameStats = renderer.GetCpuFrameStats();
            const auto &lightingTiming = renderer.GetLightingGpuTiming();
            const bool renderedThisFrame = renderer.GetProfiledRenderCount() > 0;
            const auto extents = window.GetExtents();
            std::ostringstream event;
            event << std::fixed << std::setprecision(3)
                  << "{\"type\":\"performance\",\"fps\":" << (static_cast<float>(performanceFrameCount) * 1000.0f / performanceWindowMs)
                  << ",\"frameTimeMs\":" << averageFrameTimeMs
                  << ",\"maxFrameTimeMs\":" << performanceMaxFrameTimeMs
                  << ",\"commandMs\":" << averageCommandMs
                  << ",\"eventPollingMs\":" << averageEventPollingMs
                  << ",\"interactionMs\":" << averageInteractionMs
                  << ",\"updateMs\":" << averageUpdateMs
                  << ",\"renderMs\":" << averageRenderMs
                  << ",\"presentMs\":" << averagePresentMs
                  << ",\"waitMs\":" << averageWaitMs
                  << ",\"overheadMs\":" << averageOverheadMs
                  << ",\"cpuPassMs\":" << (performanceCpuPassTotalMs * inverseFrameCount)
                  << ",\"gpuPassMs\":" << (performanceGpuPassTotalMs * inverseFrameCount)
                  << ",\"visible\":" << (shouldRender ? "true" : "false")
                  << ",\"vSync\":" << (renderer.IsVSyncEnabled() ? "true" : "false")
                  << ",\"profiledRenderCount\":" << renderer.GetProfiledRenderCount()
                  << ",\"viewportWidth\":" << extents.width
                  << ",\"viewportHeight\":" << extents.height;
            AppendCpuPassTimings(event, renderer.GetCpuPassTimings());
            AppendGpuPassTimings(event, "gpuPasses", renderer.GetGpuPassTimings(), renderedThisFrame);
            AppendGpuPassTimings(event, "postProcessGpuPasses", renderer.GetPostProcessGpuTimings(), renderedThisFrame);
            event << ",\"lighting\":{\"setupMs\":" << (renderedThisFrame ? lightingTiming.setupMs : 0.0f)
                  << ",\"setupAvailable\":" << (renderedThisFrame && lightingTiming.hasSetupResult ? "true" : "false")
                  << ",\"ambientMs\":" << (renderedThisFrame ? lightingTiming.ambientMs : 0.0f)
                  << ",\"ambientAvailable\":" << (renderedThisFrame && lightingTiming.hasAmbientResult ? "true" : "false")
                  << ",\"lightAccumulationMs\":" << (renderedThisFrame ? lightingTiming.lightAccumulationMs : 0.0f)
                  << ",\"lightAccumulationAvailable\":" << (renderedThisFrame && lightingTiming.hasLightAccumulationResult ? "true" : "false")
                  << ",\"lightCount\":" << (renderedThisFrame ? lightingTiming.lightCount : 0)
                  << ",\"shadowedLightCount\":" << (renderedThisFrame ? lightingTiming.shadowedLightCount : 0) << '}';
            event << ",\"workload\":{\"submittedRenderCommands\":" << cpuFrameStats.submittedRenderCommandCount
                  << ",\"submissionCulledRenderCommands\":" << cpuFrameStats.submissionCulledRenderCommandCount
                  << ",\"visibleRenderCommands\":" << cpuFrameStats.visibleRenderCommandCount
                  << ",\"frustumCulledRenderCommands\":" << cpuFrameStats.frustumCulledRenderCommandCount
                  << ",\"visibleSingleLodCommands\":" << cpuFrameStats.visibleSingleLodCommandCount
                  << ",\"visibleMultiLodCommands\":" << cpuFrameStats.visibleMultiLodCommandCount
                  << ",\"renderCommandSorts\":" << cpuFrameStats.renderCommandSortCount
                  << ",\"geometryLogicalBatches\":" << cpuFrameStats.geometrySubmittedBatchCount
                  << ",\"geometryMaterialGroups\":" << cpuFrameStats.geometryMaterialGroupCount
                  << ",\"geometryApiDrawCalls\":" << cpuFrameStats.geometryApiDrawCallCount
                  << ",\"geometryInstances\":" << cpuFrameStats.geometrySubmittedInstanceCount
                  << ",\"geometryTriangles\":" << cpuFrameStats.geometrySubmittedTriangleCount
                  << ",\"geometryTrianglesByLod\":[" << cpuFrameStats.geometrySubmittedTrianglesByLod[0] << ','
                  << cpuFrameStats.geometrySubmittedTrianglesByLod[1] << ','
                  << cpuFrameStats.geometrySubmittedTrianglesByLod[2] << ','
                  << cpuFrameStats.geometrySubmittedTrianglesByLod[3] << ']'
                  << ",\"shadowUpdatedSurfaces\":" << cpuFrameStats.shadowUpdatedSurfaceCount
                  << ",\"shadowUpdatedDirectionalCascades\":" << cpuFrameStats.shadowUpdatedDirectionalCascadeCount
                  << ",\"shadowUpdatedPixels\":" << cpuFrameStats.shadowUpdatedPixelCount
                  << ",\"shadowInstances\":" << cpuFrameStats.shadowSubmittedInstanceCount
                  << ",\"shadowLogicalBatches\":" << cpuFrameStats.shadowSubmittedBatchCount
                  << ",\"shadowMaterialGroups\":" << cpuFrameStats.shadowMaterialGroupCount
                  << ",\"shadowApiDrawCalls\":" << cpuFrameStats.shadowApiDrawCallCount
                  << ",\"shadowTriangles\":" << cpuFrameStats.shadowSubmittedTriangleCount
                  << ",\"intermediateTargetResizeMs\":" << cpuFrameStats.intermediateTargetResizeMs
                  << ",\"intermediateTargetResizes\":" << cpuFrameStats.intermediateTargetResizeCount
                  << ",\"gBufferResizeMs\":" << cpuFrameStats.gBufferResizeMs
                  << ",\"gBufferResizes\":" << cpuFrameStats.gBufferResizeCount << "}}";
            WriteEvent(event.str());
            if (!options.gameView) WriteEvent(editorSession.BuildSnapshotEvent());

            performanceWindowStart = loopEnd;
            performanceFrameCount = 0;
            performanceFrameTimeTotalMs = 0.0f;
            performanceMaxFrameTimeMs = 0.0f;
            performanceEventPollingTotalMs = 0.0f;
            performanceUpdateTotalMs = 0.0f;
            performanceRenderTotalMs = 0.0f;
            performancePresentTotalMs = 0.0f;
            performanceCommandTotalMs = 0.0f;
            performanceInteractionTotalMs = 0.0f;
            performanceWaitTotalMs = 0.0f;
            performanceCpuPassTotalMs = 0.0f;
            performanceGpuPassTotalMs = 0.0f;
        }
    }

    WriteEvent(R"({"type":"stopping"})");
    sharedTexturePublisher.Shutdown();
    viewportInteraction.Shutdown();
    editorSession.Shutdown();
    engine.Shutdown();
    return 0;
#endif
}
