#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>

namespace PlutoGE::render
{
    struct RenderTargetConfig
    {
        int width = 800;                                          // Default width
        int height = 600;                                         // Default height
        glm::vec4 clearColor = glm::vec4(0.1f, 0.1f, 0.1f, 1.0f); // Default clear color
    };

    class Graphics;
    class RenderTarget
    {
    public:
        RenderTarget(RenderTargetConfig config) : m_config(config)
        {
            if (!Initialize(config.width, config.height))
            {
                std::cerr << "Failed to initialize RenderTarget" << std::endl;
                m_isInitialized = false;
            }
            m_isInitialized = true;
        }
        ~RenderTarget() = default;

        glm::vec4 GetClearColor() const { return m_config.clearColor; }
        void SetClearColor(const glm::vec4 &color) { m_config.clearColor = color; }

        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        GLuint GetFramebufferID() const { return m_framebufferID; }
        GLuint GetColorTextureID() const { return m_colorTextureID; }
        GLuint GetDepthTextureID() const { return m_depthTextureID; }
        GLuint GetDepthStencilBufferID() const { return m_depthStencilBufferID; }

        bool IsInitialized() const { return m_isInitialized; }

        void Cleanup();

    private:
        RenderTargetConfig m_config;
        bool m_isInitialized = false;
        GLuint m_framebufferID = 0;        // OpenGL framebuffer ID
        GLuint m_colorTextureID = 0;       // OpenGL texture ID for color attachment
        GLuint m_depthTextureID = 0;       // OpenGL texture ID for depth attachment (sampleable)
        GLuint m_depthStencilBufferID = 0; // OpenGL renderbuffer ID for depth and stencil attachment (legacy, optional)
        int m_width = 0;
        int m_height = 0;

    protected:
        friend class Graphics;
        bool Initialize(int width, int height);
        void Bind() const;
        void Unbind() const;
    };
}