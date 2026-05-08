#pragma once

#include <glad/glad.h>

namespace PlutoGE::render
{
    class GBuffer
    {
    public:
        GBuffer() = default;
        ~GBuffer() = default;

        GLuint GetPositionTextureID() const { return m_positionTexture; }
        GLuint GetNormalTextureID() const { return m_normalTexture; }
        GLuint GetAlbedoTextureID() const { return m_albedoTexture; }
        GLuint GetDepthRboID() const { return m_depthRbo; }

        bool Initialize(int width, int height);
        void Bind();
        void Unbind();
        void Cleanup();

        bool IsInitialized() const { return m_isInitialized; }

        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

        GLuint GetFBO() const { return m_fbo; }

    private:
        bool m_isInitialized = false;
        int m_width = 0;
        int m_height = 0;

        GLuint m_fbo = 0;
        GLuint m_positionTexture = 0;
        GLuint m_normalTexture = 0;
        GLuint m_albedoTexture = 0;
        GLuint m_depthRbo = 0;
    };
}