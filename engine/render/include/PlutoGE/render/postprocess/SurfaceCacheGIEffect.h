#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"
#include "PlutoGE/render/surfacecache/SurfaceCache.h"
#include "PlutoGE/render/surfacecache/SurfaceCacheAtlas.h"

#include <memory>
#include <vector>

namespace PlutoGE::render
{
    class Shader;
    class Mesh;
    class Material;

    class SurfaceCacheGIEffect final : public ShaderPostProcessEffect
    {
    public:
        ~SurfaceCacheGIEffect() override;
        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "SurfaceCacheGI"; }
        std::string GetDisplayName() const override { return "Surface Cache Global Illumination"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        const SurfaceCacheStats &GetStats() const { return m_stats; }

    private:
        struct ResidentCard
        {
            SurfaceCard card;
            Mesh *mesh = nullptr;
            Material *material = nullptr;
        };
        void RebuildCards(const PostProcessContext &context);
        void CapturePendingCards(const PostProcessContext &context);
        std::size_t ComputeSceneSignature(const PostProcessContext &context) const;

        std::unique_ptr<SurfaceCacheAtlas> m_atlas;
        Shader *m_captureShader = nullptr;
        Shader *m_debugShader = nullptr;
        std::vector<ResidentCard> m_cards;
        SurfaceCacheStats m_stats;
        std::size_t m_sceneSignature = 0;
        std::size_t m_nextCapture = 0;
        int m_atlasSize = 2048;
        int m_texelsPerUnit = 32;
        int m_minCardResolution = 16;
        int m_maxCardResolution = 256;
        int m_captureBudget = 12;
        int m_debugView = 0;
        bool m_cacheLayoutDirty = true;
    };
}
