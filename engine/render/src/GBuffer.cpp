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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
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

        // Motion vectors
        glGenTextures(1, &m_motionTexture);
        glBindTexture(GL_TEXTURE_2D, m_motionTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, m_motionTexture, 0);

        // Baked lighting RGB plus static-mask alpha
        glGenTextures(1, &m_bakedLightingTexture);
        glBindTexture(GL_TEXTURE_2D, m_bakedLightingTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, m_bakedLightingTexture, 0);

        GLuint attachments[5] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
        glDrawBuffers(5, attachments);

        // Depth
        glGenTextures(1, &m_depthTexture);
        glBindTexture(GL_TEXTURE_2D, m_depthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

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

        if (m_motionTexture)
        {
            glDeleteTextures(1, &m_motionTexture);
            m_motionTexture = 0;
        }

        if (m_bakedLightingTexture)
        {
            glDeleteTextures(1, &m_bakedLightingTexture);
            m_bakedLightingTexture = 0;
        }

        if (m_depthTexture)
        {
            glDeleteTextures(1, &m_depthTexture);
            m_depthTexture = 0;
        }

        if (m_fbo)
        {
            glDeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }

        m_isInitialized = false;
    }
}