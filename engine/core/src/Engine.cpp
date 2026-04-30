#include "PlutoGE/core/Engine.h"

#include "PlutoGE/scene/Scene.h"

#include <chrono>
#include <iostream>

#include "PlutoGE/platform/Window.h"

namespace PlutoGE::core
{
    bool Engine::Initialize(const EngineConfig &config)
    {
        m_config = config;

        if (!m_window.Create(m_config.windowConfig))
        {
            std::cerr << "Failed to create window." << std::endl;
            return false;
        }

        render::RendererConfig rendererConfig;
        rendererConfig.window = &m_window;
        if (!m_renderer.Initialize(rendererConfig))
        {
            std::cerr << "Failed to initialize renderer." << std::endl;
            return false;
        }

        m_scriptEngine.Initialize();

        m_isInitialized = true;
        return true;
    }

    void Engine::Run()
    {
        auto previousFrame = std::chrono::steady_clock::now();

        while (m_isInitialized && (!m_window.IsOpen() || !m_window.ShouldClose()))
        {
            const auto currentFrame = std::chrono::steady_clock::now();
            const float deltaTime = std::chrono::duration<float>(currentFrame - previousFrame).count();
            previousFrame = currentFrame;

            if (m_window.IsOpen())
            {
                m_window.PollEvents();
            }

            if (m_scene)
            {
                m_scene->Update(deltaTime);
            }
        }
    }

    void Engine::Shutdown()
    {
        m_scriptEngine.Shutdown();
        m_renderer.Shutdown();
        m_window.Close();
        m_isInitialized = false;
    }
}