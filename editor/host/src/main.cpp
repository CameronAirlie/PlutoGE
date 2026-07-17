#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"

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
    struct HostOptions
    {
        void *parentWindow = nullptr;
        int width = 960;
        int height = 640;
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

    PlutoGE::render::CameraData BuildCamera(int width, int height, float yaw, float pitch, float distance)
    {
        const float safeAspect = static_cast<float>(std::max(1, width)) / static_cast<float>(std::max(1, height));
        const glm::vec3 target(0.0f);
        const glm::vec3 position{
            target.x + distance * std::cos(pitch) * std::sin(yaw),
            target.y + distance * std::sin(pitch),
            target.z + distance * std::cos(pitch) * std::cos(yaw),
        };

        PlutoGE::render::CameraData camera;
        camera.view = glm::lookAt(position, target, glm::vec3(0.0f, 1.0f, 0.0f));
        camera.projection = glm::perspective(glm::radians(50.0f), safeAspect, camera.farPlane, camera.nearPlane);
        return camera;
    }
}

int main(int argc, char **argv)
{
#ifndef _WIN32
    std::cerr << "PlutoGEEditorHost embedded mode is currently supported on Windows only.\n";
    return 2;
#else
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    const auto options = ParseOptions(argc, argv);
    const bool embedded = options.parentWindow != nullptr;

    auto &engine = PlutoGE::core::Engine::GetInstance();
    PlutoGE::core::EngineConfig config{
        PlutoGE::platform::WindowConfig{
            .title = "PlutoGE Engine Viewport",
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

    CommandStream commandStream;
    bool running = true;
    bool visible = !embedded;
    float yaw = glm::radians(35.0f);
    float pitch = glm::radians(28.0f);
    float distance = 8.0f;

    WriteEvent(R"({"type":"ready","protocol":1})");

    while (running && !window.ShouldClose())
    {
        std::vector<std::string> commands;
        if (!commandStream.Poll(commands))
        {
            break;
        }

        for (const auto &commandLine : commands)
        {
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
        }

        window.PollEvents();
        const auto &input = window.GetInputState();
        if (input.IsMouseButtonDown(1))
        {
            yaw -= static_cast<float>(input.mouseState.deltaX) * 0.005f;
            pitch = std::clamp(pitch - static_cast<float>(input.mouseState.deltaY) * 0.005f,
                               glm::radians(-85.0f),
                               glm::radians(85.0f));
        }
        distance = std::clamp(distance - static_cast<float>(input.mouseState.scrollDeltaY) * 0.7f, 2.0f, 40.0f);

        if (visible)
        {
            const auto extents = window.GetExtents();
            const auto camera = BuildCamera(extents.width, extents.height, yaw, pitch, distance);
            renderer.BeginFrame();
            renderer.RenderFrame(camera, nullptr, {}, nullptr, nullptr, true, true);
            renderer.ClearRenderCommands();
            renderer.EndFrame();
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    WriteEvent(R"({"type":"stopping"})");
    engine.Shutdown();
    return 0;
#endif
}
