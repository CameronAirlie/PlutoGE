#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"

#include <iostream>

namespace PlutoGE::render
{
    void Material::Bind(Shader *shader)
    {
        if (!shader)
        {
            std::cerr << "Material has no shader assigned!" << std::endl;
            return;
        }

        shader->Bind();

        shader->SetUniform("uColor", m_config.color);
        shader->SetUniform("uMetallicFactor", m_config.metallic);
        shader->SetUniform("uRoughnessFactor", m_config.roughness);

        // Set common uniforms (camera and model data)
        // shader->SetUniform("uModel", modelMatrix);
        // shader->SetUniform("uView", cameraData.view);
        // shader->SetUniform("uProjection", cameraData.projection);

        // Set material-specific uniforms
        if (m_config.albedoTexture)
        {
            shader->SetUniform("uAlbedoTexture", m_config.albedoTexture, 0);
            shader->SetUniform("uHasAlbedoTexture", 1.0f);
        }
        else
        {
            shader->SetUniform("uHasAlbedoTexture", 0.0f);
        }

        if (m_config.normalTexture)
        {
            shader->SetUniform("uNormalTexture", m_config.normalTexture, 1);
            shader->SetUniform("uHasNormalTexture", 1.0f);
        }
        else
        {
            shader->SetUniform("uHasNormalTexture", 0.0f);
        }

        if (m_config.metallicTexture)
        {
            shader->SetUniform("uMetallicTexture", m_config.metallicTexture, 2);
            shader->SetUniform("uHasMetallicTexture", 1.0f);
        }
        else
        {
            shader->SetUniform("uHasMetallicTexture", 0.0f);
        }

        if (m_config.roughnessTexture)
        {
            shader->SetUniform("uRoughnessTexture", m_config.roughnessTexture, 3);
            shader->SetUniform("uHasRoughnessTexture", 1.0f);
        }
        else
        {
            shader->SetUniform("uHasRoughnessTexture", 0.0f);
        }
    }
}