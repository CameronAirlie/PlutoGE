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
            layout (location = 1) out vec3 gNormal;
            layout (location = 2) out vec4 gAlbedoSpec;
            
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
            
            uniform float uSpecular = 1.0; // Placeholder specular value
            
            float mix(float a, float b, float factor)
            {
                return a * (1.0 - factor) + b * factor;
            }
            
            vec3 mix(vec3 a, vec3 b, float factor)
            {
                return a * (1.0 - factor) + b * factor;
            }
            
            void main()
            {
                gPosition = FragPos;
                gNormal = normalize(Normal);
                vec3 albedo = uColor.rgb;
                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 texAlbedo = texture(uAlbedoTexture, UV);
                    if (texAlbedo.a < 0.1)
                        discard;
                    // albedo = texAlbedo.rgb;
                    albedo = mix(albedo, texAlbedo.rgb, uColor.a);
                }
                 
                gAlbedoSpec = vec4(albedo, uSpecular);
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

struct Light {
    vec3 Position;
    vec3 Color;
    float Intensity;
    float Range;
};

uniform int uLightCount;
uniform Light uLights[MAX_LIGHTS];
uniform vec3 uViewPos;

void main()
{
    vec3 FragPos = texture(gPosition, UV).rgb;
    vec3 Normal = normalize(texture(gNormal, UV).rgb);
    vec3 Albedo = texture(gAlbedoSpec, UV).rgb;
    float Specular = texture(gAlbedoSpec, UV).a;

    vec3 viewDir = normalize(uViewPos - FragPos);
    vec3 lighting = 0.1 * Albedo;

    for (int i = 0; i < min(uLightCount, MAX_LIGHTS); ++i)
    {
        vec3 lightOffset = uLights[i].Position - FragPos;
        float distanceToLight = length(lightOffset);
        vec3 lightDir = distanceToLight > 0.0001 ? lightOffset / distanceToLight : vec3(0.0, 1.0, 0.0);
        float diff = max(dot(Normal, lightDir), 0.0);

        vec3 halfwayDir = normalize(lightDir + viewDir);
        float spec = pow(max(dot(Normal, halfwayDir), 0.0), 16.0);

        float normalizedDistance = uLights[i].Range > 0.0001 ? distanceToLight / uLights[i].Range : 1.0;
        float attenuation = clamp(1.0 - normalizedDistance, 0.0, 1.0);
        attenuation *= attenuation;

        vec3 contribution = (diff * Albedo + spec * Specular) * uLights[i].Color;
        lighting += contribution * uLights[i].Intensity * attenuation;
    }

    // FragColor = vec4(Albedo, 1.0);
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