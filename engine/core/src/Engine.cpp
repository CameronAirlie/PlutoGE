#include "PlutoGE/core/Engine.h"
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

        m_isInitialized = true;
        return true;
    }

    void Engine::Run()
    {
        while (m_isInitialized && (!m_window.IsOpen() || !m_window.ShouldClose()))
        {
            if (m_window.IsOpen())
            {
                m_window.PollEvents();
            }
        }
    }

    void Engine::Shutdown()
    {
        m_renderer.Shutdown();
        m_window.Close();
        m_isInitialized = false;
    }
}