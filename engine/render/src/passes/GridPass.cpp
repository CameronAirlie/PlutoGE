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
                uniform bool uOrthographic;
                uniform vec3 uGridPlaneNormal;
                uniform vec3 uGridAxisU;
                uniform vec3 uGridAxisV;
                uniform vec3 uGridAxisUColor;
                uniform vec3 uGridAxisVColor;

                float Saturate(float value)
                {
                    return clamp(value, 0.0, 1.0);
                }

                void ComputeWorldRay(vec2 uv, out vec3 rayOrigin, out vec3 rayDirection)
                {
                    vec2 clip = uv * 2.0 - 1.0;
                    vec4 endpointA = uInverseView * uInverseProjection * vec4(clip, -1.0, 1.0);
                    vec4 endpointB = uInverseView * uInverseProjection * vec4(clip, 1.0, 1.0);
                    vec3 worldA = endpointA.xyz / endpointA.w;
                    vec3 worldB = endpointB.xyz / endpointB.w;
                    vec3 cameraForward = -normalize(uInverseView[2].xyz);
                    if (uOrthographic)
                    {
                        rayOrigin = distance(worldA, uCameraPosition) < distance(worldB, uCameraPosition) ? worldA : worldB;
                        rayDirection = cameraForward;
                    }
                    else
                    {
                        rayOrigin = uCameraPosition;
                        rayDirection = normalize(worldB - worldA);
                        if (dot(rayDirection, cameraForward) < 0.0)
                        {
                            rayDirection = -rayDirection;
                        }
                    }
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
                    vec3 rayOrigin;
                    vec3 rayDirection;
                    ComputeWorldRay(vUv, rayOrigin, rayDirection);
                    float planeDenominator = dot(rayDirection, uGridPlaneNormal);
                    if (abs(planeDenominator) <= 0.0001)
                    {
                        discard;
                    }

                    float rayDistance = -dot(rayOrigin, uGridPlaneNormal) / planeDenominator;
                    if (rayDistance <= 0.0)
                    {
                        discard;
                    }

                    vec3 worldPosition = rayOrigin + rayDirection * rayDistance;
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
                    float fade = uOrthographic ? 1.0 : 1.0 - Saturate((distanceToCamera - uFadeStart) / max(uFadeEnd - uFadeStart, 0.0001));
                    if (fade <= 0.001)
                    {
                        discard;
                    }

                    vec2 gridPosition = vec2(dot(worldPosition, uGridAxisU), dot(worldPosition, uGridAxisV));
                    float minorGrid = ComputeLineFactor(gridPosition, uGridStep);
                    float majorGrid = ComputeLineFactor(gridPosition, uMajorGridStep);
                    float minorContribution = max(minorGrid - majorGrid, 0.0);
                    float axisU = ComputeAxisFactor(gridPosition.y);
                    float axisV = ComputeAxisFactor(gridPosition.x);

                    vec3 color = vec3(0.68, 0.70, 0.74) * max(minorContribution, majorGrid);
                    color = mix(color, uGridAxisUColor, axisU);
                    color = mix(color, uGridAxisVColor, axisV);

                    float alpha = max(max(minorContribution * 0.08, majorGrid * 0.18), max(axisU, axisV) * 0.42) * fade;
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
        Graphics::SetViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        Graphics::Disable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        Graphics::Disable(GL_CULL_FACE);
        Graphics::Enable(GL_BLEND);
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
        const bool orthographic = std::abs(ctx.cameraData.projection[3][3] - 1.0f) < 0.0001f;
        glm::vec3 gridPlaneNormal(0.0f, 1.0f, 0.0f);
        glm::vec3 gridAxisU(1.0f, 0.0f, 0.0f);
        glm::vec3 gridAxisV(0.0f, 0.0f, 1.0f);
        glm::vec3 gridAxisUColor(0.85f, 0.30f, 0.30f);
        glm::vec3 gridAxisVColor(0.30f, 0.50f, 0.90f);
        if (orthographic)
        {
            const glm::vec3 cameraForward = -glm::normalize(glm::vec3(inverseView[2]));
            const glm::vec3 absoluteForward = glm::abs(cameraForward);
            if (absoluteForward.x > absoluteForward.y && absoluteForward.x > absoluteForward.z)
            {
                gridPlaneNormal = glm::vec3(1.0f, 0.0f, 0.0f);
                gridAxisU = glm::vec3(0.0f, 1.0f, 0.0f);
                gridAxisV = glm::vec3(0.0f, 0.0f, 1.0f);
                gridAxisUColor = glm::vec3(0.30f, 0.75f, 0.35f);
                gridAxisVColor = glm::vec3(0.30f, 0.50f, 0.90f);
            }
            else if (absoluteForward.z > absoluteForward.y)
            {
                gridPlaneNormal = glm::vec3(0.0f, 0.0f, 1.0f);
                gridAxisU = glm::vec3(1.0f, 0.0f, 0.0f);
                gridAxisV = glm::vec3(0.0f, 1.0f, 0.0f);
                gridAxisUColor = glm::vec3(0.85f, 0.30f, 0.30f);
                gridAxisVColor = glm::vec3(0.30f, 0.75f, 0.35f);
            }
        }
        m_gridShader->SetUniform("uOrthographic", orthographic ? 1 : 0);
        m_gridShader->SetUniform("uGridPlaneNormal", gridPlaneNormal);
        m_gridShader->SetUniform("uGridAxisU", gridAxisU);
        m_gridShader->SetUniform("uGridAxisV", gridAxisV);
        m_gridShader->SetUniform("uGridAxisUColor", gridAxisUColor);
        m_gridShader->SetUniform("uGridAxisVColor", gridAxisVColor);
        Graphics::ActiveTexture(GL_TEXTURE0);
        Graphics::BindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetDepthTextureID());
        m_gridShader->SetUniform("uSceneDepthTexture", 0);

        Graphics::DrawFullscreenTriangle();

        m_gridShader->Unbind();
        Graphics::Disable(GL_BLEND);
        glDepthMask(GL_TRUE);
        Graphics::UnbindRenderTarget();
    }
}
