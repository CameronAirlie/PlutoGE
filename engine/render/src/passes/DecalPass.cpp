#include "PlutoGE/render/passes/DecalPass.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

namespace PlutoGE::render
{
    namespace
    {
        Shader *CreateDecalShader()
        {
            return Shader::Create(ShaderSource{
                .vertexSource = R"(
#version 430 core
out vec2 vUV;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)",
                .fragmentSource = R"(
#version 430 core
in vec2 vUV;
layout(location = 2) out vec4 outAlbedo;

uniform sampler2D uWorldPosition;
uniform sampler2D uWorldNormal;
uniform sampler2D uDecalTexture;
uniform bool uHasAlbedoTexture;
uniform mat4 uInverseModel;
uniform vec3 uProjectorNormal;
uniform vec4 uTint;
uniform vec4 uMaterialColor;
uniform vec2 uUvScale;
uniform int uAlphaMode;
uniform float uAlphaCutoff;
uniform float uNormalCutoff;

void main()
{
    vec3 worldPosition = texture(uWorldPosition, vUV).xyz;
    vec3 localPosition = (uInverseModel * vec4(worldPosition, 1.0)).xyz;
    if (any(greaterThan(abs(localPosition), vec3(0.5))))
        discard;

    vec3 surfaceNormal = normalize(texture(uWorldNormal, vUV).xyz);
    if (dot(surfaceNormal, uProjectorNormal) < uNormalCutoff)
        discard;

    vec2 decalUV = (localPosition.xy + vec2(0.5)) * uUvScale;
    vec4 materialSample = (uHasAlbedoTexture ? texture(uDecalTexture, decalUV) : vec4(1.0)) *
                          uMaterialColor;
    vec4 decal = materialSample * uTint;
    if (uAlphaMode == 0)
        decal.a = uTint.a;
    else if (uAlphaMode == 1)
    {
        if (materialSample.a < uAlphaCutoff)
            discard;
        decal.a = uTint.a;
    }
    if (decal.a <= 0.001)
        discard;
    outAlbedo = decal;
}
)"});
        }
    }

    DecalPass::~DecalPass()
    {
        delete m_shader;
        if (m_fullscreenVao)
            glDeleteVertexArrays(1, &m_fullscreenVao);
    }

    void DecalPass::Initialize()
    {
        m_shader = CreateDecalShader();
        glGenVertexArrays(1, &m_fullscreenVao);
    }

    void DecalPass::Execute(const RenderContext &ctx)
    {
        if (!m_shader || !ctx.gBuffer || !ctx.gBuffer->IsInitialized() ||
            !ctx.decalCommands || ctx.decalCommands->empty())
            return;

        ctx.gBuffer->Bind();
        const GLenum drawBuffers[] = {GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT2};
        glDrawBuffers(3, drawBuffers);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_shader->Bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetPositionTextureID());
        m_shader->SetUniform("uWorldPosition", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetNormalTextureID());
        m_shader->SetUniform("uWorldNormal", 1);
        glBindVertexArray(m_fullscreenVao);

        for (const auto &decal : *ctx.decalCommands)
        {
            if (!decal.material || decal.tint.a <= 0.0f)
                continue;
            const auto &material = decal.material->GetConfig();
            m_shader->SetUniform("uInverseModel", glm::inverse(decal.model));
            m_shader->SetUniform("uProjectorNormal", glm::normalize(glm::vec3(decal.model[2])));
            m_shader->SetUniform("uTint", decal.tint);
            m_shader->SetUniform("uMaterialColor", material.color);
            m_shader->SetUniform("uUvScale", material.uvScale);
            m_shader->SetUniform("uAlphaMode", static_cast<int>(material.alphaMode));
            m_shader->SetUniform("uAlphaCutoff", material.alphaCutoff);
            m_shader->SetUniform("uNormalCutoff", decal.normalCutoff);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, material.albedoTexture ? material.albedoTexture->GetTextureID() : 0);
            m_shader->SetUniform("uHasAlbedoTexture", material.albedoTexture != nullptr);
            m_shader->SetUniform("uDecalTexture", 2);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glBindVertexArray(0);
        m_shader->Unbind();
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        ctx.gBuffer->Unbind();
    }
}
