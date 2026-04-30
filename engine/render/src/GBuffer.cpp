#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"

#include <cassert>

namespace PlutoGE::render
{
    bool GBuffer::Initialize(int width, int height)
    {
        m_width = width;
        m_height = height;

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        // Position
        glGenTextures(1, &m_positionTexture);
        glBindTexture(GL_TEXTURE_2D, m_positionTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_positionTexture, 0);

        // Normal
        glGenTextures(1, &m_normalTexture);
        glBindTexture(GL_TEXTURE_2D, m_normalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_normalTexture, 0);

        // Albedo
        glGenTextures(1, &m_albedoTexture);
        glBindTexture(GL_TEXTURE_2D, m_albedoTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_albedoTexture, 0);

        GLuint attachments[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, attachments);

        // Depth
        glGenRenderbuffers(1, &m_depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthRbo);

        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && "GBuffer framebuffer is not complete!");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        m_isInitialized = true;
        return true;
    }

    void GBuffer::Bind()
    {
        Graphics::BindFramebuffer(m_fbo);
    }

    void GBuffer::Unbind()
    {
        Graphics::UnbindFramebuffer();
    }

    void GBuffer::Cleanup()
    {
        if (m_positionTexture)
        {
            glDeleteTextures(1, &m_positionTexture);
            m_positionTexture = 0;
        }

        if (m_normalTexture)
        {
            glDeleteTextures(1, &m_normalTexture);
            m_normalTexture = 0;
        }

        if (m_albedoTexture)
        {
            glDeleteTextures(1, &m_albedoTexture);
            m_albedoTexture = 0;
        }

        if (m_depthRbo)
        {
            glDeleteRenderbuffers(1, &m_depthRbo);
            m_depthRbo = 0;
        }

        if (m_fbo)
        {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }
    }
}