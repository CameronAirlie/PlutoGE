#pragma once

#include <glad/glad.h>
#include <string>
#include <iostream>

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

    class Shader
    {
    public:
        Shader() = default;
        ~Shader() = default;

        static Shader *Create(const ShaderConfig &config);
        static Shader *Create(const ShaderSource &source);

        static Shader *CreateDefault()
        {
            ShaderSource defaultSource;

            defaultSource.vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec3 aPos;
                layout(location = 1) in vec3 aNormal;
                layout(location = 2) in vec2 aUV;
                layout(location = 3) in vec4 aTangent;

                uniform mat4 uModel;
                uniform mat4 uView;
                uniform mat4 uProjection;

                out vec3 FragPos;
                out vec3 Normal;
                out vec2 UV;
                out mat3 TBN;

                void main()
                {
                    FragPos = vec3(uModel * vec4(aPos, 1.0));
                    Normal = mat3(transpose(inverse(uModel))) * aNormal; // Transform normal to world space
                    UV = aUV;
                    gl_Position = uProjection * uView * vec4(FragPos, 1.0);
                    TBN = mat3(
                        normalize(mat3(uModel) * aTangent.xyz), // Tangent
                        normalize(cross(Normal, normalize(mat3(uModel) * aTangent.xyz))), // Bitangent
                        Normal // Normal
                    );
                }
            )";

            defaultSource.fragmentSource = R"(
#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 UV;
in mat3 TBN;

uniform vec3 uColor;
uniform sampler2D uAlbedoTexture;
uniform float uHasAlbedoTexture;

uniform sampler2D uNormalTexture;
uniform float uHasNormalTexture;

uniform float uMetallic;
uniform sampler2D uMetallicTexture;
uniform float uHasMetallicTexture;

uniform sampler2D uRoughnessTexture;
uniform float uRoughness;
uniform float uHasRoughnessTexture;

void main()
{
    vec3 ambient = 0.1 * uColor;

    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.6));
    vec3 normal = normalize(Normal);

    if (uHasNormalTexture > 0.5)
    {
        normal = texture(uNormalTexture, UV).rgb;
        normal = normalize(normal * 2.0 - 1.0);
        normal = normalize(TBN * normal); // Transform to world space
    }

    float lightIntensity = 1.0;
    float lightDirection = max(dot(normal, lightDir), 0.0);

    vec3 albedo = uColor;
    if (uHasAlbedoTexture > 0.5)
    {
        vec4 texAlbedo = texture(uAlbedoTexture, UV);
        if (texAlbedo.a < 0.1)
            discard;
        albedo = texAlbedo.rgb;
    }

    vec3 diffuse = albedo * lightDirection * lightIntensity;

    vec4 color = vec4(ambient + diffuse, 1.0);
    FragColor = color;
}
            )";

            return CreateShaderFromSource(defaultSource);
        }

    protected:
        friend class Graphics;
        GLuint GetProgramID() const { return m_programID; }

    private:
        static Shader *CreateShaderFromSource(const ShaderSource &source);

        ShaderConfig m_config;
        GLuint m_programID = 0; // OpenGL shader program ID
    };
}