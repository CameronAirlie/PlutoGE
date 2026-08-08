#include "PlutoGE/render/surfacecache/SurfaceCacheAtlas.h"

#include <array>

namespace PlutoGE::render
{
    SurfaceCacheAtlas::~SurfaceCacheAtlas() { Cleanup(); }

    bool SurfaceCacheAtlas::Initialize(int size)
    {
        if (size <= 0)
            return false;
        if (IsInitialized() && m_size == size)
            return true;
        Cleanup();
        m_size = size;
        glGenFramebuffers(1, &m_framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glGenTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        constexpr std::array<GLint, 7> internalFormats{GL_RGBA8, GL_RGBA8_SNORM, GL_RGB16F, GL_R32F, GL_RGB16F, GL_RGB16F, GL_RGB16F};
        constexpr std::array<GLenum, 7> formats{GL_RGBA, GL_RGBA, GL_RGB, GL_RED, GL_RGB, GL_RGB, GL_RGB};
        constexpr std::array<GLenum, 7> types{GL_UNSIGNED_BYTE, GL_BYTE, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT, GL_FLOAT};
        for (std::size_t index = 0; index < m_textures.size(); ++index)
        {
            glBindTexture(GL_TEXTURE_2D, m_textures[index]);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormats[index], size, size, 0, formats[index], types[index], nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index), GL_TEXTURE_2D, m_textures[index], 0);
        }
        glGenRenderbuffers(1, &m_depthBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
        constexpr std::array<GLenum, 5> attachments{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
        const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (!complete)
            Cleanup();
        else
            Clear();
        return complete;
    }

    void SurfaceCacheAtlas::Cleanup()
    {
        glDeleteRenderbuffers(1, &m_depthBuffer);
        glDeleteTextures(static_cast<GLsizei>(m_textures.size()), m_textures.data());
        glDeleteFramebuffers(1, &m_framebuffer);
        m_depthBuffer = m_framebuffer = 0;
        m_textures.fill(0);
        m_size = 0;
    }

    void SurfaceCacheAtlas::BindForCapture() const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        constexpr std::array<GLenum, 5> attachments{GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
        glDrawBuffers(static_cast<GLsizei>(attachments.size()), attachments.data());
    }

    void SurfaceCacheAtlas::BindLayerForWrite(Layer layer) const
    {
        glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
        glDrawBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(layer));
    }

    void SurfaceCacheAtlas::Clear() const
    {
        BindForCapture();
        constexpr GLfloat zero[4]{0, 0, 0, 0};
        constexpr GLfloat farDepth[4]{1, 0, 0, 0};
        glClearBufferfv(GL_COLOR, 0, zero);
        glClearBufferfv(GL_COLOR, 1, zero);
        glClearBufferfv(GL_COLOR, 2, zero);
        glClearBufferfv(GL_COLOR, 3, farDepth);
        glClearBufferfv(GL_COLOR, 4, zero);
        glClear(GL_DEPTH_BUFFER_BIT);
        BindLayerForWrite(Layer::AccumulatedRadianceA);
        glClearBufferfv(GL_COLOR, 0, zero);
        BindLayerForWrite(Layer::AccumulatedRadianceB);
        glClearBufferfv(GL_COLOR, 0, zero);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}
