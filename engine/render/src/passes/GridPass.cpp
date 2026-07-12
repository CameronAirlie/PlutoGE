#include "PlutoGE/render/passes/GridPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <glm/glm.hpp>

#include <algorithm>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kGridStep = 1.0f;
        constexpr float kGridFadeStart = 8.0f;
        constexpr float kGridFadeEnd = 70.0f;
        constexpr int kMajorLineInterval = 10;

        Shader *CreateGridShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                out vec2 vUv;

                void main()
                {
                    vec2 positions[3] = vec2[3](
                        vec2(-1.0, -1.0),
                        vec2(3.0, -1.0),
                        vec2(-1.0, 3.0)
                    );

                    gl_Position = vec4(positions[gl_VertexID], 0.0, 1.0);
                    vUv = gl_Position.xy * 0.5 + vec2(0.5);
                }
            )";

            source.fragmentSource = R"(
                #version 330 core
                in vec2 vUv;
                out vec4 FragColor;

                uniform sampler2D uSceneDepthTexture;
                uniform mat4 uView;
                uniform mat4 uProjection;
                uniform mat4 uInverseView;
                uniform mat4 uInverseProjection;
                uniform vec3 uCameraPosition;
                uniform vec2 uViewportSize;
                uniform float uGridStep;
                uniform float uMajorGridStep;
                uniform float uFadeStart;
                uniform float uFadeEnd;

                float Saturate(float value)
                {
                    return clamp(value, 0.0, 1.0);
                }

                vec3 ComputeWorldRayDirection(vec2 uv)
                {
                    vec2 clip = uv * 2.0 - 1.0;
                    vec4 viewDirection = uInverseProjection * vec4(clip, 1.0, 1.0);
                    vec3 rayDirectionView = normalize(viewDirection.xyz / max(viewDirection.w, 0.0001));
                    return normalize((uInverseView * vec4(rayDirectionView, 0.0)).xyz);
                }

                float ComputeLineFactor(vec2 worldGrid, float stepSize)
                {
                    vec2 scaled = worldGrid / stepSize;
                    vec2 derivatives = max(fwidth(scaled), vec2(0.0001));
                    vec2 cell = abs(fract(scaled - 0.5) - 0.5) / derivatives;
                    return 1.0 - min(min(cell.x, cell.y), 1.0);
                }

                float ComputeAxisFactor(float coordinate)
                {
                    float derivative = max(fwidth(coordinate), 0.0001);
                    return 1.0 - min(abs(coordinate) / derivative, 1.0);
                }

                void main()
                {
                    vec3 rayDirection = ComputeWorldRayDirection(vUv);
                    if (abs(rayDirection.y) <= 0.0001)
                    {
                        discard;
                    }

                    float rayDistance = -uCameraPosition.y / rayDirection.y;
                    if (rayDistance <= 0.0)
                    {
                        discard;
                    }

                    vec3 worldPosition = uCameraPosition + rayDirection * rayDistance;
                    vec4 clipPosition = uProjection * uView * vec4(worldPosition, 1.0);
                    if (clipPosition.w <= 0.0)
                    {
                        discard;
                    }

                    vec3 ndc = clipPosition.xyz / clipPosition.w;
                    if (abs(ndc.x) > 1.0 || abs(ndc.y) > 1.0 || ndc.z < -1.0 || ndc.z > 1.0)
                    {
                        discard;
                    }

                    float gridDepth = ndc.z * 0.5 + 0.5;
                    float sceneDepth = texture(uSceneDepthTexture, gl_FragCoord.xy / uViewportSize).r;
                    if (sceneDepth > 0.000001 && gridDepth <= sceneDepth)
                    {
                        discard;
                    }

                    float distanceToCamera = distance(worldPosition, uCameraPosition);
                    float fade = 1.0 - Saturate((distanceToCamera - uFadeStart) / max(uFadeEnd - uFadeStart, 0.0001));
                    if (fade <= 0.001)
                    {
                        discard;
                    }

                    float minorGrid = ComputeLineFactor(worldPosition.xz, uGridStep);
                    float majorGrid = ComputeLineFactor(worldPosition.xz, uMajorGridStep);
                    float minorContribution = max(minorGrid - majorGrid, 0.0);
                    float xAxis = ComputeAxisFactor(worldPosition.z);
                    float zAxis = ComputeAxisFactor(worldPosition.x);

                    vec3 color = vec3(0.68, 0.70, 0.74) * max(minorContribution, majorGrid);
                    color = mix(color, vec3(0.85, 0.30, 0.30), xAxis);
                    color = mix(color, vec3(0.30, 0.50, 0.90), zAxis);

                    float alpha = max(max(minorContribution * 0.08, majorGrid * 0.18), max(xAxis, zAxis) * 0.42) * fade;
                    if (alpha <= 0.001)
                    {
                        discard;
                    }

                    FragColor = vec4(color, alpha);
                }
            )";

            return Shader::Create(source);
        }
    }

    void GridPass::Initialize()
    {
        m_gridShader = CreateGridShader();

        glGenVertexArrays(1, &m_vao);
    }

    void GridPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.renderEditorGrid || !ctx.temporaryRenderTarget || !ctx.hasCameraData || !m_gridShader || m_vao == 0)
        {
            return;
        }

        const glm::mat4 inverseView = glm::inverse(ctx.cameraData.view);
        const glm::mat4 inverseProjection = glm::inverse(ctx.cameraData.projection);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
        glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_gridShader->Bind();
        m_gridShader->SetUniform("uView", ctx.cameraData.view);
        m_gridShader->SetUniform("uProjection", ctx.cameraData.projection);
        m_gridShader->SetUniform("uInverseView", inverseView);
        m_gridShader->SetUniform("uInverseProjection", inverseProjection);
        m_gridShader->SetUniform("uCameraPosition", cameraPosition);
        m_gridShader->SetUniform("uViewportSize", glm::vec2(static_cast<float>(ctx.temporaryRenderTarget->GetWidth()), static_cast<float>(ctx.temporaryRenderTarget->GetHeight())));
        m_gridShader->SetUniform("uGridStep", kGridStep);
        m_gridShader->SetUniform("uMajorGridStep", kGridStep * static_cast<float>(kMajorLineInterval));
        m_gridShader->SetUniform("uFadeStart", kGridFadeStart);
        m_gridShader->SetUniform("uFadeEnd", kGridFadeEnd);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetDepthTextureID());
        m_gridShader->SetUniform("uSceneDepthTexture", 0);

        Graphics::DrawFullscreenTriangle();

        m_gridShader->Unbind();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        Graphics::UnbindRenderTarget();
    }
}
