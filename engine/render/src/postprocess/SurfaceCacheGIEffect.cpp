#include "PlutoGE/render/postprocess/SurfaceCacheGIEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>
#include <functional>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBool(const std::string &value) { return value == "true" || value == "1"; }
        void HashCombine(std::size_t &seed, std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }
    }

    SurfaceCacheGIEffect::~SurfaceCacheGIEffect()
    {
    }

    std::vector<PostProcessParameter> SurfaceCacheGIEffect::GetParameters() const
    {
        return {
            {"Atlas Size", PostProcessParameterType::Int, std::to_string(m_atlasSize)},
            {"Texels Per Unit", PostProcessParameterType::Int, std::to_string(m_texelsPerUnit)},
            {"Minimum Card Resolution", PostProcessParameterType::Int, std::to_string(m_minCardResolution)},
            {"Maximum Card Resolution", PostProcessParameterType::Int, std::to_string(m_maxCardResolution)},
            {"Capture Budget", PostProcessParameterType::Int, std::to_string(m_captureBudget)},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(m_debugView), {"Scene", "Albedo / Metallic", "Normal / Roughness", "Emission", "Card Depth"}},
        };
    }

    void SurfaceCacheGIEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        int previousAtlasSize = m_atlasSize;
        const int previousTexelsPerUnit = m_texelsPerUnit;
        const int previousMinimumResolution = m_minCardResolution;
        const int previousMaximumResolution = m_maxCardResolution;
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Atlas Size") m_atlasSize = std::clamp(std::stoi(parameter.value), 512, 8192);
            else if (parameter.name == "Texels Per Unit") m_texelsPerUnit = std::clamp(std::stoi(parameter.value), 4, 256);
            else if (parameter.name == "Minimum Card Resolution") m_minCardResolution = std::clamp(std::stoi(parameter.value), 8, 128);
            else if (parameter.name == "Maximum Card Resolution") m_maxCardResolution = std::clamp(std::stoi(parameter.value), 32, 1024);
            else if (parameter.name == "Capture Budget") m_captureBudget = std::clamp(std::stoi(parameter.value), 1, 256);
            else if (parameter.name == "Debug View") m_debugView = std::clamp(std::stoi(parameter.value), 0, 4);
        }
        m_maxCardResolution = std::max(m_maxCardResolution, m_minCardResolution);
        m_cacheLayoutDirty = m_cacheLayoutDirty || previousAtlasSize != m_atlasSize ||
                             previousTexelsPerUnit != m_texelsPerUnit ||
                             previousMinimumResolution != m_minCardResolution ||
                             previousMaximumResolution != m_maxCardResolution;
        if (previousAtlasSize != m_atlasSize)
        {
            m_atlas.reset();
        }
    }

    void SurfaceCacheGIEffect::Initialize()
    {
        ShaderSource capture;
        capture.vertexSource = R"(
            #version 330 core
            layout(location=0) in vec3 aPos;
            layout(location=1) in vec3 aNormal;
            layout(location=2) in vec2 aUV;
            layout(location=3) in vec4 aTangent;
            uniform mat4 uCardViewProjection;
            out vec2 UV; out vec3 Normal; out mat3 TBN;
            void main() {
                UV = aUV;
                Normal = normalize(aNormal);
                vec3 tangent = normalize(aTangent.xyz);
                TBN = mat3(tangent, normalize(cross(Normal, tangent) * aTangent.w), Normal);
                gl_Position = uCardViewProjection * vec4(aPos, 1.0);
            })";
        capture.fragmentSource = R"(
            #version 330 core
            layout(location=0) out vec4 OutAlbedoMetallic;
            layout(location=1) out vec4 OutNormalRoughness;
            layout(location=2) out vec3 OutEmission;
            layout(location=3) out float OutDepth;
            in vec2 UV; in vec3 Normal; in mat3 TBN;
            uniform vec4 uColor; uniform vec2 uUVScale;
            uniform sampler2D uAlbedoTexture; uniform float uHasAlbedoTexture;
            uniform sampler2D uNormalTexture; uniform float uHasNormalTexture; uniform float uFlipNormalY;
            uniform sampler2D uMetallicTexture; uniform float uHasMetallicTexture; uniform int uMetallicTextureChannel; uniform float uMetallicFactor;
            uniform sampler2D uRoughnessTexture; uniform float uHasRoughnessTexture; uniform int uRoughnessTextureChannel; uniform float uRoughnessFactor;
            uniform vec3 uEmission;
            float channel(vec4 value, int index) { return index==0?value.r:index==1?value.g:index==2?value.b:value.a; }
            void main() {
                vec2 uv = UV * uUVScale;
                vec4 albedo = uColor * (uHasAlbedoTexture > 0.5 ? texture(uAlbedoTexture, uv) : vec4(1.0));
                vec3 normal = normalize(Normal);
                if (uHasNormalTexture > 0.5) { vec3 n=texture(uNormalTexture,uv).xyz*2.0-1.0; n.y=uFlipNormalY>0.5?-n.y:n.y; normal=normalize(TBN*n); }
                float metallic = uMetallicFactor * (uHasMetallicTexture > 0.5 ? channel(texture(uMetallicTexture,uv),uMetallicTextureChannel) : 1.0);
                float roughness = uRoughnessFactor * (uHasRoughnessTexture > 0.5 ? channel(texture(uRoughnessTexture,uv),uRoughnessTextureChannel) : 1.0);
                OutAlbedoMetallic=vec4(albedo.rgb,clamp(metallic,0.0,1.0));
                OutNormalRoughness=vec4(normal,clamp(roughness,0.0,1.0));
                OutEmission=max(uEmission,vec3(0.0)); OutDepth=gl_FragCoord.z;
            })";
        m_captureShader = Shader::Create(capture);

        ShaderSource debug;
        debug.vertexSource = R"(#version 330 core
            out vec2 UV; void main(){ vec2 v[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(v[gl_VertexID],0,1); UV=gl_Position.xy*.5+.5; })";
        debug.fragmentSource = R"(#version 330 core
            in vec2 UV; out vec4 FragColor; uniform sampler2D uSceneTexture; uniform sampler2D uAtlasTexture; uniform int uDebugView;
            void main(){ if(uDebugView==0){FragColor=texture(uSceneTexture,UV);return;} vec4 value=texture(uAtlasTexture,UV); if(uDebugView==2)value=vec4(value.xyz*.5+.5,1); else if(uDebugView==4)value=vec4(vec3(value.r),1); else value.a=1; FragColor=value; })";
        m_debugShader = Shader::Create(debug);
    }

    std::size_t SurfaceCacheGIEffect::ComputeSceneSignature(const PostProcessContext &context) const
    {
        std::size_t signature = 0;
        if (!context.renderContext.renderCommands) return signature;
        for (const auto &command : *context.renderContext.renderCommands)
        {
            if (!command.isStatic || !command.mesh || !command.material || command.jointMatrices ||
                command.material->GetConfig().alphaMode != AlphaMode::Opaque || command.material->GetConfig().surfaceType != MaterialSurfaceType::Standard)
                continue;
            HashCombine(signature, reinterpret_cast<std::size_t>(command.mesh));
            HashCombine(signature, reinterpret_cast<std::size_t>(command.material));
            HashCombine(signature, command.submeshIndex);
        }
        HashCombine(signature, static_cast<std::size_t>(m_atlasSize));
        HashCombine(signature, static_cast<std::size_t>(m_texelsPerUnit));
        return signature;
    }

    void SurfaceCacheGIEffect::RebuildCards(const PostProcessContext &context)
    {
        m_cards.clear();
        m_nextCapture = 0;
        SurfaceCacheAtlasAllocator allocator(m_atlasSize, m_atlasSize, 2);
        SurfaceCardId nextId = 1;
        if (context.renderContext.renderCommands)
        {
            for (const auto &command : *context.renderContext.renderCommands)
            {
                if (!command.isStatic || !command.mesh || !command.material || command.jointMatrices ||
                    command.material->GetConfig().alphaMode != AlphaMode::Opaque || command.material->GetConfig().surfaceType != MaterialSurfaceType::Standard)
                    continue;
                auto cards = SurfaceCardGenerator::GenerateAxisCards(*command.mesh, command.submeshIndex, m_texelsPerUnit,
                                                                     m_minCardResolution, m_maxCardResolution);
                for (auto &card : cards)
                {
                    auto allocation = allocator.Allocate(card.allocation.width, card.allocation.height);
                    if (!allocation) continue;
                    card.id = nextId++;
                    card.allocation = *allocation;
                    m_cards.push_back({card, command.mesh, command.material});
                }
            }
        }
        m_stats.cardCount = static_cast<int>(m_cards.size());
        m_stats.residentCardCount = static_cast<int>(m_cards.size());
        m_stats.capturedCardCount = 0;
        m_stats.atlasUsedPixels = allocator.GetUsedPixels();
        m_stats.atlasTotalPixels = m_atlasSize * m_atlasSize;
        if (m_atlas) m_atlas->Clear();
    }

    void SurfaceCacheGIEffect::CapturePendingCards(const PostProcessContext &)
    {
        if (!m_atlas || !m_captureShader || m_cards.empty() || m_nextCapture >= m_cards.size()) return;
        m_atlas->BindForCapture();
        glEnable(GL_DEPTH_TEST); glEnable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        m_captureShader->Bind();
        const std::size_t end = std::min(m_cards.size(), m_nextCapture + static_cast<std::size_t>(m_captureBudget));
        for (; m_nextCapture < end; ++m_nextCapture)
        {
            const auto &resident = m_cards[m_nextCapture];
            const auto &rect = resident.card.allocation;
            glViewport(rect.x, rect.y, rect.width, rect.height);
            glScissor(rect.x, rect.y, rect.width, rect.height);
            constexpr GLfloat zero[4]{0,0,0,0}; constexpr GLfloat farDepth[4]{1,0,0,0};
            glClearBufferfv(GL_COLOR,0,zero); glClearBufferfv(GL_COLOR,1,zero); glClearBufferfv(GL_COLOR,2,zero); glClearBufferfv(GL_COLOR,3,farDepth); glClear(GL_DEPTH_BUFFER_BIT);
            m_captureShader->SetUniform("uCardViewProjection", resident.card.localViewProjection);
            resident.material->Bind(m_captureShader);
            resident.mesh->DrawSubmesh(resident.card.submeshIndex, 0);
        }
        m_stats.capturedCardCount = static_cast<int>(m_nextCapture);
        glDisable(GL_SCISSOR_TEST); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SurfaceCacheGIEffect::Apply(const PostProcessContext &context)
    {
        if (!context.sourceRenderTarget || !context.destinationRenderTarget || !m_captureShader || !m_debugShader) return;
        if (!m_atlas) m_atlas = std::make_unique<SurfaceCacheAtlas>();
        if (!m_atlas->Initialize(m_atlasSize)) return;
        const std::size_t signature = ComputeSceneSignature(context);
        if (m_cacheLayoutDirty || signature != m_sceneSignature)
        {
            m_sceneSignature = signature;
            m_cacheLayoutDirty = false;
            RebuildCards(context);
        }
        CapturePendingCards(context);
        if (context.renderContext.renderer)
            context.renderContext.renderer->RecordSurfaceCacheStats(
                m_stats.cardCount, m_stats.residentCardCount, m_stats.capturedCardCount,
                m_stats.atlasUsedPixels, m_stats.atlasTotalPixels);

        BeginApply(context);
        m_debugShader->Bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        m_debugShader->SetUniform("uSceneTexture", 0);
        SurfaceCacheAtlas::Layer layer = SurfaceCacheAtlas::Layer::AlbedoMetallic;
        if (m_debugView == 2) layer = SurfaceCacheAtlas::Layer::NormalRoughness;
        else if (m_debugView == 3) layer = SurfaceCacheAtlas::Layer::Emission;
        else if (m_debugView == 4) layer = SurfaceCacheAtlas::Layer::Depth;
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_atlas->GetTexture(layer));
        m_debugShader->SetUniform("uAtlasTexture", 1); m_debugShader->SetUniform("uDebugView", m_debugView);
        DrawFullscreenTriangle(); EndApply();
    }
}
