#pragma once

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
    struct MaterialConfig
    {
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; // Base color (default to white)
        MaterialSurfaceType surfaceType = MaterialSurfaceType::Standard;
        Texture *albedoTexture = nullptr;        // Pointer to an albedo texture (if any)
        glm::vec2 uvScale{1.0f, 1.0f};
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool castsShadow = true;

        Texture *normalTexture = nullptr; // Pointer to a normal map texture (if any)
        bool flipNormalY = false;         // Flip green channel for DirectX-style normal maps

        float metallic = 0.0f;              // Metallic factor (0.0 = non-metal, 1.0 = metal)
        Texture *metallicTexture = nullptr; // Pointer to a metallic texture (if any)
        TextureChannel metallicTextureChannel = TextureChannel::Red;

        float roughness = 1.0f;              // Roughness factor (0.0 = smooth, 1.0 = rough)
        Texture *roughnessTexture = nullptr; // Pointer to a roughness texture (if any)
        TextureChannel roughnessTextureChannel = TextureChannel::Red;

        glm::vec3 emission{0.0f}; // HDR self-illumination color

        float transmission = 0.0f;                        // Transmitted light amount for glass-like materials
        float ior = 1.45f;                                // Index of refraction; common window glass is around 1.45-1.52
        float thickness = 0.01f;                          // Approximate material thickness in scene units
        glm::vec3 attenuationColor{1.0f, 1.0f, 1.0f};     // Color retained after passing through the material
        float attenuationDistance = 1.0f;                 // Distance at which attenuationColor is reached

        Texture *lightmapTexture = nullptr; // Optional baked lighting texture sampled with UV2
        glm::vec4 lightmapUvTransform{1.0f, 1.0f, 0.0f, 0.0f}; // scale.xy, offset.zw

        std::string shaderGraphReference;
        std::vector<ShaderGraphVariable> shaderGraphVariables;
        Shader *compiledShaderGraph = nullptr;
    };

    class Material
    {
    public:
        Material() = default;
        Material(const MaterialConfig &config) : m_config(config) {}
        ~Material() = default;

        void SetShader(Shader *shader) { m_overrideShader = shader; }
        Shader *GetShader() const { return m_overrideShader ? m_overrideShader : m_config.compiledShaderGraph; }

        void Bind(Shader *shader = nullptr);

        void SetColor(const glm::vec4 &color) { m_config.color = color; }
        void SetSurfaceType(MaterialSurfaceType surfaceType) { m_config.surfaceType = surfaceType; }
        void SetAlbedoTexture(Texture *texture) { m_config.albedoTexture = texture; }
        void SetUvScale(const glm::vec2 &uvScale) { m_config.uvScale = uvScale; }
        void SetAlphaMode(AlphaMode alphaMode) { m_config.alphaMode = alphaMode; }
        void SetAlphaCutoff(float alphaCutoff) { m_config.alphaCutoff = alphaCutoff; }
        void SetCastsShadow(bool castsShadow) { m_config.castsShadow = castsShadow; }
        void SetNormalTexture(Texture *texture) { m_config.normalTexture = texture; }
        void SetFlipNormalY(bool flipNormalY) { m_config.flipNormalY = flipNormalY; }
        void SetMetallic(float metallic) { m_config.metallic = metallic; }
        void SetMetallicTexture(Texture *texture) { m_config.metallicTexture = texture; }
        void SetMetallicTextureChannel(TextureChannel channel) { m_config.metallicTextureChannel = channel; }
        void SetRoughness(float roughness) { m_config.roughness = roughness; }
        void SetRoughnessTexture(Texture *texture) { m_config.roughnessTexture = texture; }
        void SetRoughnessTextureChannel(TextureChannel channel) { m_config.roughnessTextureChannel = channel; }
        void SetEmission(const glm::vec3 &emission) { m_config.emission = emission; }
        void SetTransmission(float transmission) { m_config.transmission = transmission; }
        void SetIor(float ior) { m_config.ior = ior; }
        void SetThickness(float thickness) { m_config.thickness = thickness; }
        void SetAttenuationColor(const glm::vec3 &color) { m_config.attenuationColor = color; }
        void SetAttenuationDistance(float distance) { m_config.attenuationDistance = distance; }
        void SetLightmapTexture(Texture *texture) { m_config.lightmapTexture = texture; }
        void SetLightmapUvTransform(const glm::vec4 &transform) { m_config.lightmapUvTransform = transform; }
        MaterialConfig &GetConfig() { return m_config; }
        const MaterialConfig &GetConfig() const { return m_config; }

    protected:
        friend class Graphics;
        friend class Renderer;

    private:
        MaterialConfig m_config;            // Material configuration data
        Shader *m_overrideShader = nullptr; // Pointer to the shader used for this material (can be set during rendering)
    };
}
