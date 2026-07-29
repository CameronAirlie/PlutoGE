#include "PlutoGE/render/passes/DecalPass.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        struct ScreenBounds
        {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
        };

        bool CalculateDecalScreenBounds(const glm::mat4 &model,
                                        const CameraData &camera,
                                        int viewportWidth,
                                        int viewportHeight,
                                        ScreenBounds &bounds)
        {
            const glm::mat4 viewProjection = camera.projection * camera.view;
            glm::vec2 minimum(1.0f);
            glm::vec2 maximum(-1.0f);
            bool hasPointInFront = false;
            bool hasPointBehindCamera = false;

            for (int corner = 0; corner < 8; ++corner)
            {
                const glm::vec3 localPosition(
                    (corner & 1) != 0 ? 0.5f : -0.5f,
                    (corner & 2) != 0 ? 0.5f : -0.5f,
                    (corner & 4) != 0 ? 0.5f : -0.5f);
                const glm::vec4 clipPosition = viewProjection * model * glm::vec4(localPosition, 1.0f);
                if (clipPosition.w <= 0.0001f)
                {
                    hasPointBehindCamera = true;
                    continue;
                }

                hasPointInFront = true;
                const glm::vec2 ndc = glm::vec2(clipPosition) / clipPosition.w;
                minimum = glm::min(minimum, ndc);
                maximum = glm::max(maximum, ndc);
            }

            if (!hasPointInFront)
                return false;

            // A projector crossing the camera plane cannot be bounded reliably
            // from its visible corners. Keep the conservative full-screen path.
            if (hasPointBehindCamera)
            {
                bounds = {0, 0, viewportWidth, viewportHeight};
                return true;
            }

            minimum = glm::clamp(minimum, glm::vec2(-1.0f), glm::vec2(1.0f));
            maximum = glm::clamp(maximum, glm::vec2(-1.0f), glm::vec2(1.0f));
            if (minimum.x >= maximum.x || minimum.y >= maximum.y)
                return false;

            const int left = std::clamp(
                static_cast<int>(std::floor((minimum.x * 0.5f + 0.5f) * viewportWidth)), 0, viewportWidth);
            const int bottom = std::clamp(
                static_cast<int>(std::floor((minimum.y * 0.5f + 0.5f) * viewportHeight)), 0, viewportHeight);
            const int right = std::clamp(
                static_cast<int>(std::ceil((maximum.x * 0.5f + 0.5f) * viewportWidth)), 0, viewportWidth);
            const int top = std::clamp(
                static_cast<int>(std::ceil((maximum.y * 0.5f + 0.5f) * viewportHeight)), 0, viewportHeight);
            bounds = {left, bottom, right - left, top - bottom};
            return bounds.width > 0 && bounds.height > 0;
        }

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
        glEnable(GL_SCISSOR_TEST);

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

            ScreenBounds screenBounds;
            if (!CalculateDecalScreenBounds(
                    decal.model, ctx.cameraData, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight(), screenBounds))
                continue;
            glScissor(screenBounds.x, screenBounds.y, screenBounds.width, screenBounds.height);

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
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        ctx.gBuffer->Unbind();
    }
}
