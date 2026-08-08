#pragma once

#include <array>

#include <glad/glad.h>

namespace PlutoGE::render
{
    class SurfaceCacheAtlas
    {
    public:
        enum class Layer : unsigned int
        {
            AlbedoMetallic,
            NormalRoughness,
            Emission,
            Depth,
            DirectRadiance,
            AccumulatedRadianceA,
            AccumulatedRadianceB,
            Count
        };

        ~SurfaceCacheAtlas();
        bool Initialize(int size);
        void Cleanup();
        void BindForCapture() const;
        void BindLayerForWrite(Layer layer) const;
        void Clear() const;
        GLuint GetTexture(Layer layer) const { return m_textures[static_cast<std::size_t>(layer)]; }
        GLuint GetFramebuffer() const { return m_framebuffer; }
        int GetSize() const { return m_size; }
        bool IsInitialized() const { return m_framebuffer != 0; }

    private:
        GLuint m_framebuffer = 0;
        GLuint m_depthBuffer = 0;
        std::array<GLuint, static_cast<std::size_t>(Layer::Count)> m_textures{};
        int m_size = 0;
    };
}
