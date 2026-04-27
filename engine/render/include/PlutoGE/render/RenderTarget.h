#pragma once

#include <glad/glad.h>
#include <iostream>

namespace PlutoGE::render
{
    struct RenderTargetConfig
    {
        int width = 800;  // Default width
        int height = 600; // Default height
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
            }
        }
        ~RenderTarget() = default;

        void Cleanup();

    private:
        RenderTargetConfig m_config;
        GLuint m_framebufferID = 0;        // OpenGL framebuffer ID
        GLuint m_colorTextureID = 0;       // OpenGL texture ID for color attachment
        GLuint m_depthStencilBufferID = 0; // OpenGL renderbuffer ID for depth and stencil attachment
        int m_width = 0;
        int m_height = 0;

    protected:
        friend class Graphics;
        bool Initialize(int width, int height);
        void Bind() const;
        void Unbind() const;

        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        GLuint GetFramebufferID() const { return m_framebufferID; }
        GLuint GetColorTextureID() const { return m_colorTextureID; }
        GLuint GetDepthStencilBufferID() const { return m_depthStencilBufferID; }
    };
}