#pragma once

#include <glad/glad.h>
#include <string>
#include <iostream>
#include <unordered_map>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct ShaderSource
    {
        std::string vertexSource;
        std::string fragmentSource;
    };

    struct ShaderConfig
    {
        // Future configuration options can be added here
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
    };

    class Texture;
    class Shader
    {
    public:
        Shader() = default;
        ~Shader() = default;

        static Shader *Create(const ShaderConfig &config);
        static Shader *Create(const ShaderSource &source);

        //         static Shader *CreateDefault()
        //         {
        //             ShaderSource defaultSource;

        //             defaultSource.vertexSource = R"(
        //                 #version 330 core
        //                 layout(location = 0) in vec3 aPos;
        //                 layout(location = 1) in vec3 aNormal;
        //                 layout(location = 2) in vec2 aUV;
        //                 layout(location = 3) in vec4 aTangent;

        //                 uniform mat4 uModel;
        //                 uniform mat4 uView;
        //                 uniform mat4 uProjection;

        //                 out vec3 FragPos;
        //                 out vec3 Normal;
        //                 out vec2 UV;
        //                 out mat3 TBN;

        //                 void main()
        //                 {
        //                     FragPos = vec3(uModel * vec4(aPos, 1.0));
        //                     Normal = mat3(transpose(inverse(uModel))) * aNormal; // Transform normal to world space
        //                     UV = aUV;
        //                     gl_Position = uProjection * uView * vec4(FragPos, 1.0);
        //                     TBN = mat3(
        //                         normalize(mat3(uModel) * aTangent.xyz), // Tangent
        //                         normalize(cross(Normal, normalize(mat3(uModel) * aTangent.xyz))), // Bitangent
        //                         Normal // Normal
        //                     );
        //                 }
        //             )";

        //             defaultSource.fragmentSource = R"(
        // #version 330 core
        // out vec4 FragColor;

        // in vec3 FragPos;
        // in vec3 Normal;
        // in vec2 UV;
        // in mat3 TBN;

        // uniform vec3 uColor;
        // uniform sampler2D uAlbedoTexture;
        // uniform float uHasAlbedoTexture;

        // uniform sampler2D uNormalTexture;
        // uniform float uHasNormalTexture;

        // uniform float uMetallic;
        // uniform sampler2D uMetallicTexture;
        // uniform float uHasMetallicTexture;

        // uniform sampler2D uRoughnessTexture;
        // uniform float uRoughness;
        // uniform float uHasRoughnessTexture;

        // void main()
        // {
        //     vec3 ambient = 0.1 * uColor;

        //     vec3 lightDir = normalize(vec3(0.5, 1.0, 0.6));
        //     vec3 normal = normalize(Normal);

        //     if (uHasNormalTexture > 0.5)
        //     {
        //         normal = texture(uNormalTexture, UV).rgb;
        //         normal = normalize(normal * 2.0 - 1.0);
        //         normal = normalize(TBN * normal); // Transform to world space
        //     }

        //     float lightIntensity = 1.0;
        //     float lightDirection = max(dot(normal, lightDir), 0.0);

        //     vec3 albedo = uColor;
        //     if (uHasAlbedoTexture > 0.5)
        //     {
        //         vec4 texAlbedo = texture(uAlbedoTexture, UV);
        //         if (texAlbedo.a < 0.1)
        //             discard;
        //         albedo = texAlbedo.rgb;
        //     }

        //     vec3 diffuse = albedo * lightDirection * lightIntensity;

        //     vec4 color = vec4(ambient + diffuse, 1.0);
        //     FragColor = color;
        // }
        //             )";

        //             return CreateShaderFromSource(defaultSource);
        //         }

        static Shader *FullScreenQuad()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aUV;

            out vec2 UV;

            void main()
            {
                vec2 vertices[3]=vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5); // Map from [-1, 1] to [0, 1]
            }
        )";

            source.fragmentSource = R"(
            #version 330 core
            out vec4 FragColor;

            in vec2 UV;

            uniform sampler2D uColorTexture;
            uniform sampler2D uDepthTexture;

            void main()
            {
                FragColor = texture(uColorTexture, UV);
                
            }
        )";

            return CreateShaderFromSource(source);
        }

        static Shader *CreateGeometryPassShader()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aUV;

            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;

            out vec3 FragPos;
            out vec3 Normal;
            out vec2 UV;

            void main()
            {
                FragPos = vec3(uModel * vec4(aPos, 1.0));
                Normal = mat3(transpose(inverse(uModel))) * aNormal;
                UV = aUV;
                gl_Position = uProjection * uView * vec4(FragPos, 1.0);
            }
        )";

            source.fragmentSource = R"(
            #version 330 core
            
            layout (location = 0) out vec3 gPosition;
            layout (location = 1) out vec4 gNormalRoughness;
            layout (location = 2) out vec4 gAlbedoMetallic;
            
            in vec3 FragPos;
            in vec3 Normal;
            in vec2 UV;

            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture = 0.0;
            uniform vec4 uColor = vec4(1.0, 1.0, 1.0, 1.0); // Placeholder color
            
            uniform sampler2D uNormalTexture;
            uniform float uHasNormalTexture = 0.0;

            uniform sampler2D uMetallicTexture;
            uniform float uHasMetallicTexture = 0.0;
            uniform float uMetallicFactor = 0.0;
            
            uniform sampler2D uRoughnessTexture;
            uniform float uHasRoughnessTexture = 0.0;
            uniform float uRoughnessFactor = 1.0;
            
            void main()
            {
                gPosition = FragPos;
                vec3 albedo = uColor.rgb;
                float opacity = uColor.a;
                float metallic = clamp(uMetallicFactor, 0.0, 1.0);
                float roughness = clamp(uRoughnessFactor, 0.04, 1.0);

                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 texAlbedo = texture(uAlbedoTexture, UV);
                    opacity *= texAlbedo.a;
                    if (opacity < 0.1)
                        discard;
                    albedo *= texAlbedo.rgb;
                }

                if (uHasMetallicTexture > 0.5)
                {
                    metallic *= texture(uMetallicTexture, UV).r;
                }

                if (uHasRoughnessTexture > 0.5)
                {
                    roughness *= texture(uRoughnessTexture, UV).r;
                }

                gNormalRoughness = vec4(normalize(Normal), clamp(roughness, 0.04, 1.0));
                gAlbedoMetallic = vec4(albedo, clamp(metallic, 0.0, 1.0));
            }
        )";

            return CreateShaderFromSource(source);
        }

        static Shader *CreateLightingPassShader()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aUV;

            out vec2 UV;

            void main()
            {
                vec2 vertices[3]=vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5); // Map from [-1, 1] to [0, 1]
            }
        )";

            source.fragmentSource = R"(
#version 330 core

out vec4 FragColor;
in vec2 UV;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;

const int MAX_LIGHTS = 16;
const float PI = 3.14159265359;

struct Light {
    vec3 Position;
    vec3 Color;
    float Intensity;
    float Range;
    vec3 Direction;
    int Type;
};

uniform int uLightCount;
uniform Light uLights[MAX_LIGHTS];
uniform vec3 uViewPos;

float DistributionGGX(vec3 normal, vec3 halfwayDir, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSq = alpha * alpha;
    float ndoth = max(dot(normal, halfwayDir), 0.0);
    float ndothSq = ndoth * ndoth;
    float denominator = ndothSq * (alphaSq - 1.0) + 1.0;
    return alphaSq / max(PI * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness)
{
    float ndotv = max(dot(normal, viewDir), 0.0);
    float ndotl = max(dot(normal, lightDir), 0.0);
    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float ComputePointAttenuation(vec3 fragPos, Light light)
{
    float distanceToLight = length(light.Position - fragPos);
    float normalizedDistance = light.Range > 0.0001 ? distanceToLight / light.Range : 1.0;
    float attenuation = clamp(1.0 - normalizedDistance, 0.0, 1.0);
    return attenuation * attenuation;
}

float ComputeSpotAttenuation(vec3 fragPos, vec3 lightDir, Light light)
{
    float distanceAttenuation = ComputePointAttenuation(fragPos, light);
    float spotEffect = dot(-lightDir, normalize(light.Direction));
    return distanceAttenuation * smoothstep(0.9, 0.975, spotEffect);
}

vec3 EvaluatePbrLighting(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, vec3 lightDir, vec3 radiance)
{
    vec3 halfwayDir = normalize(viewDir + lightDir);
    float ndotv = max(dot(normal, viewDir), 0.0);
    float ndotl = max(dot(normal, lightDir), 0.0);

    if (ndotl <= 0.0 || ndotv <= 0.0)
    {
        return vec3(0.0);
    }

    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    vec3 fresnel = FresnelSchlick(max(dot(halfwayDir, viewDir), 0.0), f0);
    float distribution = DistributionGGX(normal, halfwayDir, roughness);
    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);

    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * ndotv * ndotl, 0.0001);
    vec3 kS = fresnel;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * ndotl;
}

vec3 ComputeLightContribution(vec3 fragPos, vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, Light light)
{
    vec3 lightDir;
    float attenuation = 1.0;

    if (light.Type == 0)
    {
        lightDir = normalize(light.Position - fragPos);
        attenuation = ComputePointAttenuation(fragPos, light);
    }
    else if (light.Type == 1)
    {
        lightDir = normalize(-light.Direction);
    }
    else
    {
        lightDir = normalize(light.Position - fragPos);
        attenuation = ComputeSpotAttenuation(fragPos, lightDir, light);
    }

    vec3 radiance = light.Color * light.Intensity * attenuation;
    return EvaluatePbrLighting(normal, viewDir, albedo, metallic, roughness, lightDir, radiance);
}

void main()
{
    vec3 fragPos = texture(gPosition, UV).rgb;
    vec4 normalRoughness = texture(gNormal, UV);
    vec4 albedoMetallic = texture(gAlbedoSpec, UV);

    vec3 normal = normalize(normalRoughness.rgb);
    vec3 albedo = albedoMetallic.rgb;
    float roughness = clamp(normalRoughness.a, 0.04, 1.0);
    float metallic = clamp(albedoMetallic.a, 0.0, 1.0);
    vec3 viewDir = normalize(uViewPos - fragPos);

    vec3 lighting = vec3(0.03) * albedo * (1.0 - metallic);
    for (int i = 0; i < min(uLightCount, MAX_LIGHTS); ++i)
    {
        lighting += ComputeLightContribution(fragPos, normal, viewDir, albedo, metallic, roughness, uLights[i]);
    }

    lighting = lighting / (lighting + vec3(1.0));
    lighting = pow(lighting, vec3(1.0 / 2.2));
    FragColor = vec4(lighting, 1.0);
}
        )";

            return CreateShaderFromSource(source);
        }

        void Bind() const
        {
            if (m_programID == 0)
            {
                std::cerr << "Error: Attempting to bind an uninitialized shader!" << std::endl;
                return;
            }
            glUseProgram(m_programID);
        }

        void Unbind() const
        {
            glUseProgram(0);
        }

        void SetUniform(const std::string &name, const glm::mat4 &value) const;
        void SetUniform(const std::string &name, const glm::vec4 &value) const;
        void SetUniform(const std::string &name, const glm::vec3 &value) const;
        void SetUniform(const std::string &name, float value) const;
        void SetUniform(const std::string &name, int value) const;
        void SetUniform(const std::string &name, const Texture *texture, int slot) const;

    protected:
        friend class Graphics;
        GLuint GetProgramID() const { return m_programID; }

    private:
        static Shader *CreateShaderFromSource(const ShaderSource &source);

        ShaderConfig m_config;
        GLuint m_programID = 0; // OpenGL shader program ID
        mutable std::unordered_map<std::string, GLint> m_uniformLocationCache;
        GLuint GetUniformLocation(const std::string &name) const;
    };
}