#pragma once
#include "PlutoGE/render/RenderObjectIdentity.h"

#include "PlutoGE/render/ShaderGraph.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace PlutoGE::render
{
    enum class TextureChannel
    {
        Red = 0,
        Green = 1,
        Blue = 2,
        Alpha = 3,
    };

    enum class AlphaMode
    {
        Opaque = 0,
        Mask = 1,
        Blend = 2,
    };

    enum class MaterialSurfaceType
    {
        Standard = 0,
        Glass = 1,
    };

    class Texture;
    class Shader;
    struct CameraData;
    class Material;
    struct MaterialConfig
    {
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // Base color (default to white)
        MaterialSurfaceType surfaceType = MaterialSurfaceType::Standard;
        Texture *albedoTexture = nullptr;        // Pointer to an albedo texture (if any)
        glm::vec2 uvScale{1.0f, 1.0f};
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool castsShadow = true;
        bool twoSided = false;                 // Render both front and back faces

        Texture *normalTexture = nullptr; // Pointer to a normal map texture (if any)
        bool flipNormalY = false;         // Flip green channel for DirectX-style normal maps

        float metallic = 0.0f;              // Metallic factor (0.0 = non-metal, 1.0 = metal)
        Texture *metallicTexture = nullptr; // Pointer to a metallic texture (if any)
        TextureChannel metallicTextureChannel = TextureChannel::Red;

        float roughness = 1.0f;              // Roughness factor (0.0 = smooth, 1.0 = rough)
        Texture *roughnessTexture = nullptr; // Pointer to a roughness texture (if any)
        TextureChannel roughnessTextureChannel = TextureChannel::Red;

        glm::vec3 emission{0.0f}; // HDR emission factor; legacy materials without a map use albedo.
        Texture *emissionTexture = nullptr;
        int emissionTexCoord = 0; // 0: primary UVs, 1: secondary UVs
        bool emissionChannelMask = false;
        // RGB selects output colour; W is independent channel intensity.
        std::array<glm::vec4, 3> emissionChannels{{{1,0,0,1}, {0,1,0,1}, {0,0,1,1}}};

        float subsurface = 0.0f;                         // Approximate diffuse subsurface scattering strength
        glm::vec3 subsurfaceColor{1.0f, 0.35f, 0.2f};    // Color of light scattered through the surface
        float subsurfaceRadius = 1.0f;                   // Relative scattering distance/profile width

        float transmission = 0.0f;                        // Transmitted light amount for glass-like materials
        float ior = 1.45f;                                // Index of refraction; common window glass is around 1.45-1.52
        float thickness = 0.01f;                          // Approximate material thickness in scene units
        glm::vec3 attenuationColor{1.0f, 1.0f, 1.0f};     // Color retained after passing through the material
        float attenuationDistance = 1.0f;                 // Distance at which attenuationColor is reached

        Texture *lightmapTexture = nullptr; // Optional baked lighting texture sampled with UV2
        glm::vec4 lightmapUvTransform{1.0f, 1.0f, 0.0f, 0.0f}; // scale.xy, offset.zw

        ShaderGraphOutline outline; // Resolved from the shader asset, not serialized in the material.
        std::string shaderGraphReference;
        std::shared_ptr<const ShaderGraphProgram> shaderGraphProgram;
        std::vector<ShaderGraphVariable> shaderGraphVariables;
        std::vector<ShaderGraphTextureParameter> shaderGraphTextures;
        std::array<Texture *,4> graphTextures{};
        std::array<std::uint32_t,4> graphSamplers{};
        std::vector<std::shared_ptr<Material>> additionalPasses;
        unsigned graphPassOrder = 0;
        Shader *compiledShaderGraph = nullptr;
    };

    class Material
    {
    public:
        Material() = default;
        Material(const MaterialConfig &config) : m_config(config) {}
        Material(const Material &) = default;
        Material &operator=(const Material &other)
        {
            if (this != &other)
            {
                m_config = other.m_config; m_overrideShader = other.m_overrideShader;
                Touch(); // Existing escaped references still require validation.
            }
            return *this;
        }
        ~Material() { s_changeEpoch.fetch_add(1, std::memory_order_relaxed); }

        void SetShader(Shader *shader) { if (m_overrideShader != shader) { m_overrideShader = shader; Touch(); } }
        Shader *GetShader() const { return m_overrideShader ? m_overrideShader : m_config.compiledShaderGraph; }

        void Bind(Shader *shader = nullptr);

        void SetColor(const glm::vec4 &color) { if (m_config.color != (color)) { m_config.color = color; Touch(); } }
        void SetSurfaceType(MaterialSurfaceType surfaceType) { if (m_config.surfaceType != (surfaceType)) { m_config.surfaceType = surfaceType; Touch(); } }
        void SetAlbedoTexture(Texture *texture) { if (m_config.albedoTexture != (texture)) { m_config.albedoTexture = texture; Touch(); } }
        void SetUvScale(const glm::vec2 &uvScale) { if (m_config.uvScale != (uvScale)) { m_config.uvScale = uvScale; Touch(); } }
        void SetAlphaMode(AlphaMode alphaMode) { if (m_config.alphaMode != (alphaMode)) { m_config.alphaMode = alphaMode; Touch(); } }
        void SetAlphaCutoff(float alphaCutoff) { if (m_config.alphaCutoff != (alphaCutoff)) { m_config.alphaCutoff = alphaCutoff; Touch(); } }
        void SetCastsShadow(bool castsShadow) { if (m_config.castsShadow != (castsShadow)) { m_config.castsShadow = castsShadow; Touch(); } }
        void SetTwoSided(bool twoSided) { if (m_config.twoSided != (twoSided)) { m_config.twoSided = twoSided; Touch(); } }
        void SetNormalTexture(Texture *texture) { if (m_config.normalTexture != (texture)) { m_config.normalTexture = texture; Touch(); } }
        void SetFlipNormalY(bool flipNormalY) { if (m_config.flipNormalY != (flipNormalY)) { m_config.flipNormalY = flipNormalY; Touch(); } }
        void SetMetallic(float metallic) { if (m_config.metallic != (metallic)) { m_config.metallic = metallic; Touch(); } }
        void SetMetallicTexture(Texture *texture) { if (m_config.metallicTexture != (texture)) { m_config.metallicTexture = texture; Touch(); } }
        void SetMetallicTextureChannel(TextureChannel channel) { if (m_config.metallicTextureChannel != (channel)) { m_config.metallicTextureChannel = channel; Touch(); } }
        void SetRoughness(float roughness) { if (m_config.roughness != (roughness)) { m_config.roughness = roughness; Touch(); } }
        void SetRoughnessTexture(Texture *texture) { if (m_config.roughnessTexture != (texture)) { m_config.roughnessTexture = texture; Touch(); } }
        void SetRoughnessTextureChannel(TextureChannel channel) { if (m_config.roughnessTextureChannel != (channel)) { m_config.roughnessTextureChannel = channel; Touch(); } }
        void SetEmissionTexture(Texture *texture) { if (m_config.emissionTexture != (texture)) { m_config.emissionTexture = texture; Touch(); } }
        void SetEmissionTexCoord(int texCoord) { if (m_config.emissionTexCoord != (texCoord == 1 ? 1 : 0)) { m_config.emissionTexCoord = texCoord == 1 ? 1 : 0; Touch(); } }
        void SetEmission(const glm::vec3 &emission) { if (m_config.emission != (emission)) { m_config.emission = emission; Touch(); } }
        void SetSubsurface(float subsurface) { if (m_config.subsurface != (subsurface)) { m_config.subsurface = subsurface; Touch(); } }
        void SetSubsurfaceColor(const glm::vec3 &color) { if (m_config.subsurfaceColor != (color)) { m_config.subsurfaceColor = color; Touch(); } }
        void SetSubsurfaceRadius(float radius) { if (m_config.subsurfaceRadius != (radius)) { m_config.subsurfaceRadius = radius; Touch(); } }
        void SetTransmission(float transmission) { if (m_config.transmission != (transmission)) { m_config.transmission = transmission; Touch(); } }
        void SetIor(float ior) { if (m_config.ior != (ior)) { m_config.ior = ior; Touch(); } }
        void SetThickness(float thickness) { if (m_config.thickness != (thickness)) { m_config.thickness = thickness; Touch(); } }
        void SetAttenuationColor(const glm::vec3 &color) { if (m_config.attenuationColor != (color)) { m_config.attenuationColor = color; Touch(); } }
        void SetAttenuationDistance(float distance) { if (m_config.attenuationDistance != (distance)) { m_config.attenuationDistance = distance; Touch(); } }
        void SetLightmapTexture(Texture *texture) { if (m_config.lightmapTexture != (texture)) { m_config.lightmapTexture = texture; Touch(); } }
        void SetLightmapUvTransform(const glm::vec4 &transform) { if (m_config.lightmapUvTransform != (transform)) { m_config.lightmapUvTransform = transform; Touch(); } }
        // Legacy mutable references can outlive a call. Never trust revisions after
        // one escapes; those materials retain value-based validation.
        MaterialConfig &GetConfig() { if (!m_untrackedEdits) { m_untrackedEdits = true; Touch(); } return m_config; }
        const MaterialConfig &ReadConfig() const { return m_config; }
        void SetConfig(MaterialConfig config) { m_config = std::move(config); Touch(); }
        static std::uint64_t ChangeEpoch() { return s_changeEpoch.load(std::memory_order_relaxed); }
        std::uint64_t GetRevision() const { return m_untrackedEdits ? 0 : m_revision; }
        std::uint64_t GetIdentity() const { return m_identity.Value(); }
        const MaterialConfig &GetConfig() const { return m_config; }

    protected:
        friend class Graphics;
        friend class Renderer;

    private:
        void Touch() { ++m_revision; s_changeEpoch.fetch_add(1, std::memory_order_relaxed); }
        inline static std::atomic<std::uint64_t> s_changeEpoch{1};
        RenderObjectIdentity m_identity;
        std::uint64_t m_revision = 1;
        bool m_untrackedEdits = false;
        MaterialConfig m_config;            // Material configuration data
        Shader *m_overrideShader = nullptr; // Pointer to the shader used for this material (can be set during rendering)
    };
}
