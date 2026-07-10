#include "PlutoGE/render/postprocess/SSREffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>

namespace PlutoGE::render
{
    std::vector<PostProcessParameter> SSREffect::GetParameters() const
    {
        return {
            {.name = "Intensity", .type = PostProcessParameterType::Float, .value = std::to_string(m_intensity)},
            {.name = "Max Ray Distance", .type = PostProcessParameterType::Float, .value = std::to_string(m_maxRayDistance)},
            {.name = "Thickness", .type = PostProcessParameterType::Float, .value = std::to_string(m_thickness)},
            {.name = "Start Offset", .type = PostProcessParameterType::Float, .value = std::to_string(m_startOffset)},
            {.name = "Steps", .type = PostProcessParameterType::Int, .value = std::to_string(m_stepCount)},
            {.name = "Binary Search Steps", .type = PostProcessParameterType::Int, .value = std::to_string(m_binarySearchSteps)},
            {.name = "Edge Fade", .type = PostProcessParameterType::Float, .value = std::to_string(m_edgeFade)},
            {.name = "Fresnel Power", .type = PostProcessParameterType::Float, .value = std::to_string(m_fresnelPower)},
            {.name = "Metallic Boost", .type = PostProcessParameterType::Float, .value = std::to_string(m_metallicBoost)},
        };
    }

    void SSREffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity") m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 4.0f);
            else if (parameter.name == "Max Ray Distance") m_maxRayDistance = std::clamp(std::stof(parameter.value), 0.5f, 200.0f);
            else if (parameter.name == "Thickness") m_thickness = std::clamp(std::stof(parameter.value), 0.01f, 3.0f);
            else if (parameter.name == "Start Offset") m_startOffset = std::clamp(std::stof(parameter.value), 0.001f, 1.0f);
            else if (parameter.name == "Steps") m_stepCount = std::clamp(std::stoi(parameter.value), 8, 128);
            else if (parameter.name == "Binary Search Steps") m_binarySearchSteps = std::clamp(std::stoi(parameter.value), 0, 8);
            else if (parameter.name == "Edge Fade") m_edgeFade = std::clamp(std::stof(parameter.value), 0.001f, 0.5f);
            else if (parameter.name == "Fresnel Power") m_fresnelPower = std::clamp(std::stof(parameter.value), 0.25f, 10.0f);
            else if (parameter.name == "Metallic Boost") m_metallicBoost = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
        }
    }

    void SSREffect::Initialize()
    {
        ShaderSource source;
        source.vertexSource = R"(
            #version 330 core
            out vec2 UV;
            void main()
            {
                vec2 vertices[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = gl_Position.xy * 0.5 + 0.5;
            }
        )";
        source.fragmentSource = R"(
            #version 330 core
            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneAlbedoTexture;
            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform float uIntensity;
            uniform float uMaxRayDistance;
            uniform float uThickness;
            uniform float uStartOffset;
            uniform float uEdgeFade;
            uniform float uFresnelPower;
            uniform float uMetallicBoost;
            uniform int uStepCount;
            uniform int uBinarySearchSteps;

            const int MAX_STEPS = 128;
            const int MAX_BINARY_STEPS = 8;

            bool ProjectToUv(vec3 viewPosition, out vec2 uv)
            {
                vec4 clip = uProjection * vec4(viewPosition, 1.0);
                if (clip.w <= 0.0001) return false;
                uv = clip.xy / clip.w * 0.5 + 0.5;
                return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
            }

            float SceneViewDepth(vec2 uv)
            {
                vec3 worldPosition = texture(uScenePositionTexture, uv).xyz;
                vec3 worldNormal = texture(uSceneNormalTexture, uv).xyz;
                if (dot(worldNormal, worldNormal) < 0.01) return 1e20;
                return -(uView * vec4(worldPosition, 1.0)).z;
            }

            void main()
            {
                vec3 sceneColor = texture(uSceneTexture, UV).rgb;
                vec3 worldPosition = texture(uScenePositionTexture, UV).xyz;
                vec3 worldNormal = texture(uSceneNormalTexture, UV).xyz;
                if (dot(worldNormal, worldNormal) < 0.01)
                {
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                vec3 viewPosition = (uView * vec4(worldPosition, 1.0)).xyz;
                vec3 viewNormal = normalize(mat3(uView) * normalize(worldNormal));
                vec3 viewDirection = normalize(viewPosition);
                vec3 rayDirection = normalize(reflect(viewDirection, viewNormal));

                vec3 rayOrigin = viewPosition + viewNormal * uStartOffset;
                float previousTravel = uStartOffset;
                float previousDepthDelta = -1.0;
                vec2 hitUv = vec2(0.0);
                float hitTravel = -1.0;

                for (int stepIndex = 0; stepIndex < MAX_STEPS; ++stepIndex)
                {
                    if (stepIndex >= uStepCount) break;
                    float fraction = (float(stepIndex) + 1.0) / float(uStepCount);
                    float travel = uStartOffset + uMaxRayDistance * fraction * fraction;
                    vec3 rayPosition = rayOrigin + rayDirection * travel;
                    vec2 rayUv;
                    if (!ProjectToUv(rayPosition, rayUv)) break;

                    float depthDelta = -rayPosition.z - SceneViewDepth(rayUv);
                    float adaptiveThickness = uThickness * (1.0 + travel / uMaxRayDistance);
                    bool crossedSurface = previousDepthDelta < 0.0 && depthDelta >= 0.0;
                    if (crossedSurface || (depthDelta >= 0.0 && depthDelta <= adaptiveThickness))
                    {
                        float low = previousTravel;
                        float high = travel;
                        for (int binaryIndex = 0; binaryIndex < MAX_BINARY_STEPS; ++binaryIndex)
                        {
                            if (binaryIndex >= uBinarySearchSteps) break;
                            float middle = (low + high) * 0.5;
                            vec3 middlePosition = rayOrigin + rayDirection * middle;
                            vec2 middleUv;
                            if (!ProjectToUv(middlePosition, middleUv)) { high = middle; continue; }
                            float middleDelta = -middlePosition.z - SceneViewDepth(middleUv);
                            if (middleDelta >= 0.0) high = middle; else low = middle;
                        }
                        hitTravel = high;
                        ProjectToUv(rayOrigin + rayDirection * high, hitUv);
                        break;
                    }
                    previousTravel = travel;
                    previousDepthDelta = depthDelta;
                }

                if (hitTravel < 0.0)
                {
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                float edgeDistance = min(min(hitUv.x, 1.0 - hitUv.x), min(hitUv.y, 1.0 - hitUv.y));
                float edgeConfidence = smoothstep(0.0, uEdgeFade, edgeDistance);
                float distanceConfidence = 1.0 - smoothstep(uMaxRayDistance * 0.25, uMaxRayDistance, hitTravel);
                float facingConfidence = smoothstep(0.01, 0.2, dot(rayDirection, viewNormal));
                float fresnel = pow(1.0 - clamp(dot(-viewDirection, viewNormal), 0.0, 1.0), uFresnelPower);
                float metallic = clamp(texture(uSceneAlbedoTexture, UV).a, 0.0, 1.0);
                float reflectivity = mix(0.04 + fresnel * 0.96, 1.0, metallic * uMetallicBoost);
                float confidence = edgeConfidence * distanceConfidence * facingConfidence * reflectivity * uIntensity;
                vec3 reflectedColor = texture(uSceneTexture, hitUv).rgb;
                FragColor = vec4(mix(sceneColor, reflectedColor, clamp(confidence, 0.0, 1.0)), 1.0);
            }
        )";
        m_shader = Shader::Create(source);
    }

    void SSREffect::Apply(const PostProcessContext &context)
    {
        if (!m_shader || !context.sourceRenderTarget || !context.destinationRenderTarget || !context.renderContext.gBuffer)
            return;

        BeginApply(context);
        m_shader->Bind();
        BindCommonInputs(m_shader, context);
        m_shader->SetUniform("uView", context.renderContext.cameraData.view);
        m_shader->SetUniform("uProjection", context.renderContext.cameraData.projection);
        m_shader->SetUniform("uIntensity", m_intensity);
        m_shader->SetUniform("uMaxRayDistance", m_maxRayDistance);
        m_shader->SetUniform("uThickness", m_thickness);
        m_shader->SetUniform("uStartOffset", m_startOffset);
        m_shader->SetUniform("uEdgeFade", m_edgeFade);
        m_shader->SetUniform("uFresnelPower", m_fresnelPower);
        m_shader->SetUniform("uMetallicBoost", m_metallicBoost);
        m_shader->SetUniform("uStepCount", m_stepCount);
        m_shader->SetUniform("uBinarySearchSteps", m_binarySearchSteps);
        DrawFullscreenTriangle();
        EndApply();
    }
}
