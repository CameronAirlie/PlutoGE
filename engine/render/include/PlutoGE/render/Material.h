#pragma once

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Texture;
    class Shader;
    struct MaterialConfig
    {
        glm::vec3 color{1.0f, 1.0f, 1.0f}; // Base color (default to white)
        Texture *albedoTexture = nullptr;  // Pointer to an albedo texture (if any)

        float metallic = 0.0f;              // Metallic factor (0.0 = non-metal, 1.0 = metal)
        Texture *metallicTexture = nullptr; // Pointer to a metallic texture (if any)

        float roughness = 1.0f;              // Roughness factor (0.0 = smooth, 1.0 = rough)
        Texture *roughnessTexture = nullptr; // Pointer to a roughness texture (if any)
    };

    class Material
    {
    public:
        Material() = default;
        Material(const MaterialConfig &config) : m_config(config) {}
        ~Material() = default;

        void SetShader(Shader *shader) { m_shader = shader; }
        Shader *GetShader() const { return m_shader; }

    protected:
        friend class Graphics;
        MaterialConfig &GetConfig() { return m_config; }

    private:
        MaterialConfig m_config;    // Material configuration data
        Shader *m_shader = nullptr; // Pointer to the shader used for this material (can be set during rendering)
    };
}