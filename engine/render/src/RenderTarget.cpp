#include "PlutoGE/render/RenderTarget.h"

namespace PlutoGE::render
{
    bool RenderTarget::Initialize(int width, int height)
    {
        m_width = width;
        m_height = height;

        // Generate framebuffer
        glGenFramebuffers(1, &m_framebufferID);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferID);

        // Create color texture
        glGenTextures(1, &m_colorTextureID);
        glBindTexture(GL_TEXTURE_2D, m_colorTextureID);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTextureID, 0);

        // Create depth and stencil renderbuffer
        glGenRenderbuffers(1, &m_depthStencilBufferID);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthStencilBufferID);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthStencilBufferID);

        // Check if framebuffer is complete
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            Cleanup();
            return false; // Failed to create framebuffer
        }

        // Unbind framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void RenderTarget::Cleanup()
    {
        if (m_colorTextureID)
            glDeleteTextures(1, &m_colorTextureID);
        if (m_depthStencilBufferID)
            glDeleteRenderbuffers(1, &m_depthStencilBufferID);
        if (m_framebufferID)
            glDeleteFramebuffers(1, &m_framebufferID);

        m_colorTextureID = 0;
        m_depthStencilBufferID = 0;
        m_framebufferID = 0;
    }

    void RenderTarget::Bind() const
    {
        glViewport(0, 0, m_width, m_height);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebufferID);
    }

    void RenderTarget::Unbind() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

}