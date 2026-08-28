#include "PlutoGE/render/SpatialUpscaler.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Shader.h"

#include <algorithm>

namespace PlutoGE::render
{
    SpatialUpscaler::SpatialUpscaler() = default;

    SpatialUpscaler::~SpatialUpscaler()
    {
        Shutdown();
    }

    bool SpatialUpscaler::Initialize()
    {
        if (m_shader)
            return true;

        ShaderSource source;
        source.vertexSource = R"(
            #version 330 core
            out vec2 UV;
            void main()
            {
                vec2 vertices[3] = vec2[3](
                    vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = gl_Position.xy * 0.5 + 0.5;
            }
        )";
        source.fragmentSource = R"(
            #version 330 core
            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSource;
            uniform vec2 uSourceSize;
            uniform float uSharpness;

            float Luma(vec3 color)
            {
                return dot(color, vec3(0.2126, 0.7152, 0.0722));
            }

            vec3 SampleSource(vec2 pixel)
            {
                return texture(uSource, (pixel + 0.5) / uSourceSize).rgb;
            }

            void main()
            {
                vec2 sourcePosition = UV * uSourceSize - 0.5;
                vec2 base = floor(sourcePosition);
                vec2 fraction = sourcePosition - base;

                vec3 north = SampleSource(base + vec2(0.0, -1.0));
                vec3 west = SampleSource(base + vec2(-1.0, 0.0));
                vec3 center = SampleSource(base);
                vec3 east = SampleSource(base + vec2(1.0, 0.0));
                vec3 south = SampleSource(base + vec2(0.0, 1.0));
                vec3 southEast = SampleSource(base + vec2(1.0, 1.0));
                vec3 northEast = SampleSource(base + vec2(1.0, -1.0));
                vec3 southWest = SampleSource(base + vec2(-1.0, 1.0));

                float horizontalEdge = abs(Luma(west) - Luma(east)) +
                                       abs(Luma(north) - Luma(southEast));
                float verticalEdge = abs(Luma(north) - Luma(south)) +
                                     abs(Luma(west) - Luma(southEast));

                vec3 horizontal = mix(mix(west, center, fraction.x),
                                      mix(southWest, south, fraction.x), fraction.y);
                vec3 vertical = mix(mix(north, northEast, fraction.x),
                                    mix(center, east, fraction.x), fraction.y);
                float edgeBlend = verticalEdge / max(horizontalEdge + verticalEdge, 1e-5);
                vec3 reconstructed = mix(horizontal, vertical, edgeBlend);

                vec2 texel = 1.0 / uSourceSize;
                vec3 localNorth = texture(uSource, UV - vec2(0.0, texel.y)).rgb;
                vec3 localSouth = texture(uSource, UV + vec2(0.0, texel.y)).rgb;
                vec3 localWest = texture(uSource, UV - vec2(texel.x, 0.0)).rgb;
                vec3 localEast = texture(uSource, UV + vec2(texel.x, 0.0)).rgb;
                vec3 localMin = min(reconstructed, min(min(localNorth, localSouth), min(localWest, localEast)));
                vec3 localMax = max(reconstructed, max(max(localNorth, localSouth), max(localWest, localEast)));
                vec3 sharpened = reconstructed + uSharpness *
                    (reconstructed * 4.0 - localNorth - localSouth - localWest - localEast);

                FragColor = vec4(clamp(sharpened, localMin, localMax), 1.0);
            }
        )";

        m_shader.reset(Shader::Create(source));
        return m_shader != nullptr;
    }

    bool SpatialUpscaler::Upscale(const RenderTarget &source, RenderTarget &destination,
                                  const UpscalerConfig &config)
    {
        if (!destination.IsInitialized())
            return false;

        return Render(source, &destination, destination.GetWidth(), destination.GetHeight(), config);
    }

    bool SpatialUpscaler::UpscaleToFramebuffer(const RenderTarget &source, int outputWidth, int outputHeight,
                                               const UpscalerConfig &config)
    {
        return Render(source, nullptr, outputWidth, outputHeight, config);
    }

    bool SpatialUpscaler::Render(const RenderTarget &source, RenderTarget *destination,
                                 int outputWidth, int outputHeight, const UpscalerConfig &config)
    {
        if ((!m_shader && !Initialize()) || !source.IsInitialized() || outputWidth <= 0 || outputHeight <= 0)
            return false;

        Graphics::Disable(GL_DEPTH_TEST);
        Graphics::Disable(GL_CULL_FACE);
        if (destination)
            Graphics::BindRenderTarget(destination);
        else
            Graphics::UnbindRenderTarget();
        Graphics::SetViewport(0, 0, outputWidth, outputHeight);
        m_shader->Bind();
        Graphics::ActiveTexture(GL_TEXTURE0);
        Graphics::BindTexture(GL_TEXTURE_2D, source.GetColorTextureID());
        m_shader->SetUniform("uSource", 0);
        m_shader->SetUniform("uSourceSize", glm::vec2(source.GetWidth(), source.GetHeight()));
        m_shader->SetUniform("uSharpness", std::clamp(config.sharpness, 0.0f, 1.0f));
        Graphics::DrawFullscreenTriangle();
        Graphics::UnbindRenderTarget();
        return true;
    }

    void SpatialUpscaler::Shutdown()
    {
        if (m_shader)
        {
            m_shader.reset();
        }
    }
}
