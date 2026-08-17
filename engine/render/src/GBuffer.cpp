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
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        // Position
        glGenTextures(1, &m_positionTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_positionTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_positionTexture, 0);

        // Normal
        glGenTextures(1, &m_normalTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_normalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8_SNORM, width, height, 0, GL_RGBA, GL_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_normalTexture, 0);

        // Albedo
        glGenTextures(1, &m_albedoTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_albedoTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_albedoTexture, 0);

        // Motion vectors
        glGenTextures(1, &m_motionTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_motionTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, width, height, 0, GL_RG, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT3, GL_TEXTURE_2D, m_motionTexture, 0);

        // Baked lighting RGB plus static-mask alpha
        glGenTextures(1, &m_bakedLightingTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_bakedLightingTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT4, GL_TEXTURE_2D, m_bakedLightingTexture, 0);

        // Debug data
        glGenTextures(1, &m_debugTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_debugTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT5, GL_TEXTURE_2D, m_debugTexture, 0);

        // HDR material emission
        glGenTextures(1, &m_emissionTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_emissionTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT6, GL_TEXTURE_2D, m_emissionTexture, 0);

        GLuint attachments[7] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4, GL_COLOR_ATTACHMENT5, GL_COLOR_ATTACHMENT6};
        glDrawBuffers(7, attachments);

        // Depth
        glGenTextures(1, &m_depthTexture);
        Graphics::BindTexture(GL_TEXTURE_2D, m_depthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && "GBuffer framebuffer is not complete!");
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);

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
            Graphics::DeleteTextures(1, &m_positionTexture);
            m_positionTexture = 0;
        }

        if (m_normalTexture)
        {
            Graphics::DeleteTextures(1, &m_normalTexture);
            m_normalTexture = 0;
        }

        if (m_albedoTexture)
        {
            Graphics::DeleteTextures(1, &m_albedoTexture);
            m_albedoTexture = 0;
        }

        if (m_motionTexture)
        {
            Graphics::DeleteTextures(1, &m_motionTexture);
            m_motionTexture = 0;
        }

        if (m_bakedLightingTexture)
        {
            Graphics::DeleteTextures(1, &m_bakedLightingTexture);
            m_bakedLightingTexture = 0;
        }

        if (m_debugTexture)
        {
            Graphics::DeleteTextures(1, &m_debugTexture);
            m_debugTexture = 0;
        }

        if (m_emissionTexture)
        {
            Graphics::DeleteTextures(1, &m_emissionTexture);
            m_emissionTexture = 0;
        }

        if (m_depthTexture)
        {
            Graphics::DeleteTextures(1, &m_depthTexture);
            m_depthTexture = 0;
        }

        if (m_fbo)
        {
            Graphics::DeleteFramebuffers(1, &m_fbo);
            m_fbo = 0;
        }

        m_isInitialized = false;
    }
}
