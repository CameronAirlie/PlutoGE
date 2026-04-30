#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"

#include <iostream>

namespace PlutoGE::render
{
    void Material::Bind(const CameraData &cameraData, const glm::mat4 &modelMatrix)
    {
        if (!m_shader)
        {
            std::cerr << "Material has no shader assigned!" << std::endl;
            return;
        }

        m_shader->Bind();

        // Set common uniforms (camera and model data)
        m_shader->SetUniform("uModel", modelMatrix);
        m_shader->SetUniform("uView", cameraData.view);
        m_shader->SetUniform("uProjection", cameraData.projection);

        // Set material-specific uniforms
        if (m_config.albedoTexture)
        {
            m_shader->SetUniform("uAlbedoTexture", m_config.albedoTexture, 0);
            m_shader->SetUniform("uHasAlbedoTexture", 1.0f);
        }
        else
        {
            m_shader->SetUniform("uColor", m_config.color);
            m_shader->SetUniform("uHasAlbedoTexture", 0.0f);
        }

        if (m_config.normalTexture)
        {
            m_shader->SetUniform("uNormalTexture", m_config.normalTexture, 1);
            m_shader->SetUniform("uHasNormalTexture", 1.0f);
        }
        else
        {
            m_shader->SetUniform("uHasNormalTexture", 0.0f);
        }

        if (m_config.metallicTexture)
        {
            m_shader->SetUniform("uMetallicTexture", m_config.metallicTexture, 2);
            m_shader->SetUniform("uHasMetallicTexture", 1.0f);
        }
        else
        {
            m_shader->SetUniform("uHasMetallicTexture", 0.0f);
            m_shader->SetUniform("uMetallicFactor", m_config.metallic);
        }

        if (m_config.roughnessTexture)
        {
            m_shader->SetUniform("uRoughnessTexture", m_config.roughnessTexture, 3);
            m_shader->SetUniform("uHasRoughnessTexture", 1.0f);
        }
        else
        {
            m_shader->SetUniform("uHasRoughnessTexture", 0.0f);
            m_shader->SetUniform("uRoughnessFactor", m_config.roughness);
        }
    }
}