#include "PlutoGE/render/passes/ParticlePass.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        Shader *CreateParticleUpdateShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec4 aPositionAge;
                layout(location = 1) in vec4 aVelocityLifetime;
                layout(location = 2) in vec4 aColorSize;
                layout(location = 3) in vec4 aSeed;

                out vec4 vPositionAge;
                out vec4 vVelocityLifetime;
                out vec4 vColorSize;
                out vec4 vSeed;

                uniform float uDeltaTime;
                uniform int uClear;
                uniform float uStartLifetime;
                uniform float uGravityModifier;

                void main()
                {
                    int index = gl_VertexID;
                    vec4 positionAge = aPositionAge;
                    vec4 velocityLifetime = aVelocityLifetime;
                    vec4 colorSize = aColorSize;
                    vec4 seedValue = aSeed;

                    if (uClear != 0)
                    {
                        positionAge = vec4(0.0, 0.0, 0.0, 999999.0);
                        velocityLifetime = vec4(0.0, 0.0, 0.0, uStartLifetime);
                    }
                    else if (positionAge.w <= velocityLifetime.w)
                    {
                        velocityLifetime.xyz += vec3(0.0, -9.81 * uGravityModifier, 0.0) * uDeltaTime;
                        positionAge.xyz += velocityLifetime.xyz * uDeltaTime;
                        positionAge.w += uDeltaTime;
                    }

                    vPositionAge = positionAge;
                    vVelocityLifetime = velocityLifetime;
                    vColorSize = colorSize;
                    vSeed = seedValue + vec4(0.013, 0.017, 0.019, 0.0);
                }
            )";
            source.transformFeedbackVaryings = {"vPositionAge", "vVelocityLifetime", "vColorSize", "vSeed"};
            source.fragmentSource = R"(
                #version 330 core
                void main() {}
            )";
            return Shader::Create(source);
        }

        Shader *CreateParticleRenderShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec4 aPositionAge;
                layout(location = 1) in vec4 aVelocityLifetime;
                layout(location = 2) in vec4 aColorSize;

                out vec3 vPosition;
                out float vAge;
                out float vLifetime;
                out vec4 vColorSize;

                uniform mat4 uEmitterTransform;
                uniform int uSimulationSpace;

                void main()
                {
                    vec3 position = aPositionAge.xyz;
                    if (uSimulationSpace == 0)
                    {
                        position = (uEmitterTransform * vec4(position, 1.0)).xyz;
                    }
                    vPosition = position;
                    vAge = aPositionAge.w;
                    vLifetime = aVelocityLifetime.w;
                    vColorSize = aColorSize;
                }
            )";
            source.geometrySource = R"(
                #version 330 core
                layout(points) in;
                layout(triangle_strip, max_vertices = 4) out;

                in vec3 vPosition[];
                in float vAge[];
                in float vLifetime[];
                in vec4 vColorSize[];

                out vec2 gUv;
                out vec4 gColor;

                uniform mat4 uView;
                uniform mat4 uProjection;
                uniform vec3 uCameraRight;
                uniform vec3 uCameraUp;
                uniform float uStartColorAlpha;
                uniform int uColorOverLifetimeEnabled;
                uniform vec4 uEndColor;
                uniform int uSizeOverLifetimeEnabled;
                uniform float uEndSize;

                void EmitCorner(vec3 center, vec2 corner, vec2 uv, float size)
                {
                    vec3 worldPosition = center + (uCameraRight * corner.x + uCameraUp * corner.y) * size;
                    gl_Position = uProjection * uView * vec4(worldPosition, 1.0);
                    gUv = uv;
                    EmitVertex();
                }

                void main()
                {
                    if (vAge[0] >= vLifetime[0])
                    {
                        return;
                    }

                    float normalizedAge = clamp(vAge[0] / max(vLifetime[0], 0.0001), 0.0, 1.0);
                    float size = max(vColorSize[0].w, 0.0);
                    if (uSizeOverLifetimeEnabled != 0)
                    {
                        size = mix(size, max(uEndSize, 0.0), normalizedAge);
                    }

                    gColor = vec4(vColorSize[0].rgb, uStartColorAlpha);
                    if (uColorOverLifetimeEnabled != 0)
                    {
                        gColor = mix(gColor, uEndColor, normalizedAge);
                    }
                    else
                    {
                        gColor.a *= 1.0 - normalizedAge;
                    }

                    EmitCorner(vPosition[0], vec2(-0.5, -0.5), vec2(0.0, 0.0), size);
                    EmitCorner(vPosition[0], vec2( 0.5, -0.5), vec2(1.0, 0.0), size);
                    EmitCorner(vPosition[0], vec2(-0.5,  0.5), vec2(0.0, 1.0), size);
                    EmitCorner(vPosition[0], vec2( 0.5,  0.5), vec2(1.0, 1.0), size);
                    EndPrimitive();
                }
            )";
            source.fragmentSource = R"(
                #version 330 core
                in vec2 gUv;
                in vec4 gColor;
                out vec4 FragColor;

                uniform vec4 uColor;
                uniform vec3 uEmission = vec3(0.0);
                uniform sampler2D uAlbedoTexture;
                uniform float uHasAlbedoTexture;
                uniform int uParticleRenderShape;

                void main()
                {
                    float mask = 1.0;
                    if (uParticleRenderShape == 0)
                    {
                        vec2 centered = gUv * 2.0 - 1.0;
                        mask = smoothstep(1.0, 0.82, dot(centered, centered));
                    }

                    vec4 materialColor = uColor;
                    if (uHasAlbedoTexture > 0.5)
                    {
                        materialColor *= texture(uAlbedoTexture, gUv);
                    }

                    vec4 color = gColor * materialColor;
                    color.a *= mask;
                    if (color.a <= 0.01)
                    {
                        discard;
                    }

                    vec3 emissive = max(uEmission, vec3(0.0));
                    FragColor = vec4(color.rgb + emissive, color.a);
                }
            )";
            return Shader::Create(source);
        }

        Shader *CreateParticleTrailShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                layout(location = 0) in vec3 aPosition;
                layout(location = 1) in vec4 aColor;
                layout(location = 2) in vec2 aUv;

                out vec4 vColor;
                out vec2 vUv;

                uniform mat4 uView;
                uniform mat4 uProjection;

                void main()
                {
                    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
                    vColor = aColor;
                    vUv = aUv;
                }
            )";
            source.fragmentSource = R"(
                #version 330 core
                in vec4 vColor;
                in vec2 vUv;
                out vec4 FragColor;

                uniform vec4 uColor;
                uniform vec3 uEmission = vec3(0.0);
                uniform sampler2D uAlbedoTexture;
                uniform float uHasAlbedoTexture;

                void main()
                {
                    vec4 materialColor = uColor;
                    if (uHasAlbedoTexture > 0.5)
                    {
                        materialColor *= texture(uAlbedoTexture, vUv);
                    }

                    vec4 color = vColor * materialColor;
                    if (color.a <= 0.01)
                    {
                        discard;
                    }

                    vec3 emissive = max(uEmission, vec3(0.0));
                    FragColor = vec4(color.rgb + emissive, color.a);
                }
            )";
            return Shader::Create(source);
        }

        struct ParticleTrailVertex
        {
            glm::vec3 position{0.0f};
            glm::vec4 color{1.0f};
            glm::vec2 uv{0.0f};
        };

        int ToInt(scene::ParticleSimulationSpace value)
        {
            return value == scene::ParticleSimulationSpace::World ? 1 : 0;
        }

        int ToInt(scene::ParticleShape value)
        {
            return static_cast<int>(value);
        }

        int ToInt(assets::ParticleRenderShape value)
        {
            return value == assets::ParticleRenderShape::Quad ? 1 : 0;
        }

        float Hash(float value)
        {
            const float hashed = std::sin(value) * 43758.5453123f;
            return hashed - std::floor(hashed);
        }

        glm::vec3 RandomDirection(float seed)
        {
            const float z = Hash(seed + 1.0f) * 2.0f - 1.0f;
            const float angle = Hash(seed + 2.0f) * glm::two_pi<float>();
            const float radius = std::sqrt(std::max(0.0f, 1.0f - z * z));
            return glm::normalize(glm::vec3(radius * std::cos(angle), z, radius * std::sin(angle)));
        }

        glm::vec3 RandomBox(float seed, const glm::vec3 &size)
        {
            return (glm::vec3(Hash(seed + 3.0f), Hash(seed + 4.0f), Hash(seed + 5.0f)) - glm::vec3(0.5f)) * size;
        }

        glm::vec2 RandomDisc(float seed, float radius)
        {
            const float angle = Hash(seed + 9.0f) * glm::two_pi<float>();
            const float distance = std::sqrt(Hash(seed + 10.0f)) * radius;
            return glm::vec2(std::cos(angle), std::sin(angle)) * distance;
        }

        glm::vec3 EmitOffset(const scene::ParticleSystemComponent &particleSystem, float seed)
        {
            switch (particleSystem.GetShape())
            {
            case scene::ParticleShape::Sphere:
                return RandomDirection(seed) * particleSystem.GetShapeRadius() * std::cbrt(Hash(seed + 6.0f));
            case scene::ParticleShape::Box:
                return RandomBox(seed, particleSystem.GetShapeSize());
            case scene::ParticleShape::Cone:
            {
                const glm::vec2 disc = RandomDisc(seed, particleSystem.GetShapeRadius());
                return glm::vec3(disc.x, 0.0f, disc.y);
            }
            case scene::ParticleShape::Point:
            default:
                return glm::vec3(0.0f);
            }
        }

        glm::vec3 EmitDirection(const scene::ParticleSystemComponent &particleSystem, float seed, const glm::vec3 &offset)
        {
            if (particleSystem.GetShape() == scene::ParticleShape::Cone)
            {
                const float angle = Hash(seed + 7.0f) * glm::two_pi<float>();
                const float radial = std::tan(glm::radians(particleSystem.GetConeAngle())) * std::sqrt(Hash(seed + 8.0f));
                return glm::normalize(glm::vec3(std::cos(angle) * radial, 1.0f, std::sin(angle) * radial));
            }

            if (glm::length(offset) > 0.0001f)
            {
                return glm::normalize(offset);
            }

            return RandomDirection(seed);
        }

        std::vector<scene::ParticleGpuData> BuildSpawnParticles(const scene::ParticleSystemComponent &particleSystem,
                                                                const scene::Entity &owner,
                                                                int emitCount,
                                                                int emitSequenceStart)
        {
            std::vector<scene::ParticleGpuData> particles;
            particles.reserve(static_cast<std::size_t>(std::max(emitCount, 0)));

            const glm::mat4 emitterTransform = owner.GetWorldTransform();
            const glm::mat3 emitterBasis(emitterTransform);
            const glm::vec3 emitterPosition = owner.GetWorldPosition();
            const bool worldSpace = particleSystem.GetSimulationSpace() == scene::ParticleSimulationSpace::World;

            for (int emitIndex = 0; emitIndex < emitCount; ++emitIndex)
            {
                const float seed = static_cast<float>(emitSequenceStart + emitIndex) * 17.0f + static_cast<float>(emitIndex) * 12.9898f;
                const glm::vec3 localOffset = EmitOffset(particleSystem, seed);
                const glm::vec3 localDirection = EmitDirection(particleSystem, seed, localOffset);
                const glm::vec3 spawnPosition = worldSpace ? emitterPosition + emitterBasis * localOffset : localOffset;
                const glm::vec3 spawnDirection = worldSpace ? glm::normalize(emitterBasis * localDirection) : localDirection;

                scene::ParticleGpuData particle;
                particle.positionAge = glm::vec4(spawnPosition, 0.0f);
                particle.velocityLifetime = glm::vec4(spawnDirection * particleSystem.GetStartSpeed(), particleSystem.GetStartLifetime());
                particle.colorSize = glm::vec4(glm::vec3(particleSystem.GetStartColor()), particleSystem.GetStartSize());
                particle.seed = glm::vec4(seed, seed * 1.37f, seed * 2.11f, 1.0f);
                particles.push_back(particle);
            }

            return particles;
        }

        void WriteSpawnParticles(GLuint buffer, int capacity, int startIndex, const std::vector<scene::ParticleGpuData> &particles)
        {
            if (buffer == 0 || capacity <= 0 || particles.empty())
            {
                return;
            }

            const int emitCount = std::min(static_cast<int>(particles.size()), capacity);
            const int clampedStart = std::clamp(startIndex, 0, capacity - 1);
            const int firstBlockCount = std::min(emitCount, capacity - clampedStart);

            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glBufferSubData(GL_ARRAY_BUFFER,
                            static_cast<GLintptr>(clampedStart * static_cast<int>(sizeof(scene::ParticleGpuData))),
                            static_cast<GLsizeiptr>(firstBlockCount * sizeof(scene::ParticleGpuData)),
                            particles.data());

            const int secondBlockCount = emitCount - firstBlockCount;
            if (secondBlockCount > 0)
            {
                glBufferSubData(GL_ARRAY_BUFFER,
                                0,
                                static_cast<GLsizeiptr>(secondBlockCount * sizeof(scene::ParticleGpuData)),
                                particles.data() + firstBlockCount);
            }
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        std::vector<scene::ParticleGpuData> BuildCpuParticleRenderData(const scene::ParticleSystemComponent &particleSystem)
        {
            std::vector<scene::ParticleGpuData> particles;
            for (const auto &cpuParticle : particleSystem.GetCpuParticles())
            {
                if (!cpuParticle.active || cpuParticle.age > cpuParticle.lifetime)
                {
                    continue;
                }

                scene::ParticleGpuData particle;
                particle.positionAge = glm::vec4(cpuParticle.position, cpuParticle.age);
                particle.velocityLifetime = glm::vec4(cpuParticle.velocity, cpuParticle.lifetime);
                particle.colorSize = glm::vec4(glm::vec3(cpuParticle.color), cpuParticle.size);
                particle.seed = glm::vec4(cpuParticle.seed, cpuParticle.seed * 1.37f, cpuParticle.seed * 2.11f, 1.0f);
                particles.push_back(particle);
            }
            return particles;
        }

        void EnsureCpuParticleBuffer(GLuint &vao, GLuint &buffer)
        {
            if (vao != 0 && buffer != 0)
            {
                return;
            }

            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &buffer);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(scene::ParticleGpuData), reinterpret_cast<const void *>(offsetof(scene::ParticleGpuData, positionAge)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(scene::ParticleGpuData), reinterpret_cast<const void *>(offsetof(scene::ParticleGpuData, velocityLifetime)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(scene::ParticleGpuData), reinterpret_cast<const void *>(offsetof(scene::ParticleGpuData, colorSize)));
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(scene::ParticleGpuData), reinterpret_cast<const void *>(offsetof(scene::ParticleGpuData, seed)));
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        void EnsureTrailBuffer(GLuint &vao, GLuint &buffer)
        {
            if (vao != 0 && buffer != 0)
            {
                return;
            }

            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &buffer);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, buffer);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ParticleTrailVertex), reinterpret_cast<const void *>(offsetof(ParticleTrailVertex, position)));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ParticleTrailVertex), reinterpret_cast<const void *>(offsetof(ParticleTrailVertex, color)));
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ParticleTrailVertex), reinterpret_cast<const void *>(offsetof(ParticleTrailVertex, uv)));
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        std::vector<ParticleTrailVertex> BuildTrailVertices(const scene::ParticleSystemComponent &particleSystem,
                                                            const glm::vec3 &cameraForward,
                                                            const glm::vec3 &cameraRight)
        {
            std::vector<scene::ParticleTrailRenderSegment> segments;
            particleSystem.BuildTrailRenderSegments(segments);

            std::vector<ParticleTrailVertex> vertices;
            vertices.reserve(segments.size() * 6);
            for (const auto &segment : segments)
            {
                const glm::vec3 direction = segment.end - segment.start;
                if (glm::length(direction) <= 0.0001f || segment.width <= 0.0f)
                {
                    continue;
                }

                glm::vec3 side = glm::cross(cameraForward, glm::normalize(direction));
                if (glm::length(side) <= 0.0001f)
                {
                    side = cameraRight;
                }
                side = glm::normalize(side) * segment.width * 0.5f;

                const glm::vec3 a = segment.start - side;
                const glm::vec3 b = segment.start + side;
                const glm::vec3 c = segment.end - side;
                const glm::vec3 d = segment.end + side;
                const glm::vec4 color = segment.color;

                vertices.push_back({a, color, {0.0f, 0.0f}});
                vertices.push_back({b, color, {0.0f, 1.0f}});
                vertices.push_back({c, color, {1.0f, 0.0f}});
                vertices.push_back({c, color, {1.0f, 0.0f}});
                vertices.push_back({b, color, {0.0f, 1.0f}});
                vertices.push_back({d, color, {1.0f, 1.0f}});
            }
            return vertices;
        }
    }

    ParticlePass::~ParticlePass()
    {
        if (m_cpuParticleVao != 0)
        {
            glDeleteVertexArrays(1, &m_cpuParticleVao);
        }
        if (m_cpuParticleBuffer != 0)
        {
            glDeleteBuffers(1, &m_cpuParticleBuffer);
        }
        if (m_trailVao != 0)
        {
            glDeleteVertexArrays(1, &m_trailVao);
        }
        if (m_trailBuffer != 0)
        {
            glDeleteBuffers(1, &m_trailBuffer);
        }
    }

    void ParticlePass::Initialize()
    {
        if (!GLAD_GL_VERSION_3_3)
        {
            return;
        }

        m_updateShader = CreateParticleUpdateShader();
        m_renderShader = CreateParticleRenderShader();
        m_trailShader = CreateParticleTrailShader();
    }

    void ParticlePass::Execute(const RenderContext &ctx)
    {
        if (!ctx.scene || !ctx.hasCameraData || !m_updateShader || !m_renderShader || !m_trailShader || !GLAD_GL_VERSION_3_3)
        {
            if (!m_loggedUnsupported)
            {
                std::cerr << "GPU particles unavailable: OpenGL 3.3 transform feedback path was not initialized." << std::endl;
                m_loggedUnsupported = true;
            }
            return;
        }

        const auto &particleSystems = ctx.scene->GetParticleSystemComponents();
        if (particleSystems.empty())
        {
            return;
        }

        RenderTarget *particleRenderTarget = ctx.temporaryRenderTarget ? ctx.temporaryRenderTarget : ctx.renderTarget;
        const int targetWidth = particleRenderTarget ? particleRenderTarget->GetWidth() : 0;
        const int targetHeight = particleRenderTarget ? particleRenderTarget->GetHeight() : 0;
        if (targetWidth <= 0 || targetHeight <= 0)
        {
            return;
        }

        if (particleRenderTarget)
        {
            Graphics::BindRenderTarget(particleRenderTarget);
        }
        else
        {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, targetWidth, targetHeight);
        }

        const glm::mat4 inverseView = glm::inverse(ctx.cameraData.view);
        const glm::vec3 cameraRight = glm::normalize(glm::vec3(inverseView[0]));
        const glm::vec3 cameraUp = glm::normalize(glm::vec3(inverseView[1]));
        const glm::vec3 cameraForward = -glm::normalize(glm::vec3(inverseView[2]));

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        for (auto *particleSystem : particleSystems)
        {
            auto *owner = particleSystem ? particleSystem->GetOwner() : nullptr;
            if (!particleSystem || !particleSystem->IsEnabled() || !owner || !owner->IsActive())
            {
                continue;
            }

            GLuint particleVao = 0;
            int particleDrawCount = 0;
            const bool cpuSimulation = particleSystem->UsesCpuSimulation();

            if (cpuSimulation)
            {
                const auto cpuParticles = BuildCpuParticleRenderData(*particleSystem);
                EnsureCpuParticleBuffer(m_cpuParticleVao, m_cpuParticleBuffer);
                glBindBuffer(GL_ARRAY_BUFFER, m_cpuParticleBuffer);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(cpuParticles.size() * sizeof(scene::ParticleGpuData)),
                             cpuParticles.empty() ? nullptr : cpuParticles.data(),
                             GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                particleVao = m_cpuParticleVao;
                particleDrawCount = static_cast<int>(cpuParticles.size());
            }
            else
            {
                particleSystem->EnsureGpuResources();
                const bool clearRequested = particleSystem->ConsumeClearRequested() || particleSystem->ConsumeGpuStateDirty();
                const float deltaTime = particleSystem->ConsumePendingDeltaTime();
                const int emitCount = std::clamp(particleSystem->ConsumePendingEmitCount(), 0, particleSystem->GetGpuCapacity());
                const int emitStartIndex = particleSystem->GetNextEmitIndex();
                const int emitSequenceStart = particleSystem->GetNextEmitSequence();

                if (clearRequested || deltaTime > 0.0f)
                {
                    m_updateShader->Bind();
                    m_updateShader->SetUniform("uDeltaTime", deltaTime);
                    m_updateShader->SetUniform("uClear", clearRequested ? 1 : 0);
                    m_updateShader->SetUniform("uStartLifetime", particleSystem->GetStartLifetime());
                    m_updateShader->SetUniform("uGravityModifier", particleSystem->GetGravityModifier());

                    glEnable(GL_RASTERIZER_DISCARD);
                    glBindVertexArray(particleSystem->GetReadVao());
                    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, particleSystem->GetWriteBuffer());
                    glBeginTransformFeedback(GL_POINTS);
                    glDrawArrays(GL_POINTS, 0, particleSystem->GetGpuCapacity());
                    glEndTransformFeedback();
                    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
                    glDisable(GL_RASTERIZER_DISCARD);
                    particleSystem->SwapGpuBuffers();
                }

                if (emitCount > 0)
                {
                    const auto spawnParticles = BuildSpawnParticles(*particleSystem, *owner, emitCount, emitSequenceStart);
                    WriteSpawnParticles(particleSystem->GetReadBuffer(), particleSystem->GetGpuCapacity(), emitStartIndex, spawnParticles);
                    particleSystem->AdvanceEmitCursor(emitCount);
                }

                particleVao = particleSystem->GetReadVao();
                particleDrawCount = particleSystem->GetGpuCapacity();
            }

            m_renderShader->Bind();
            m_renderShader->SetUniform("uView", ctx.cameraData.view);
            m_renderShader->SetUniform("uProjection", ctx.cameraData.projection);
            m_renderShader->SetUniform("uCameraRight", cameraRight);
            m_renderShader->SetUniform("uCameraUp", cameraUp);
            m_renderShader->SetUniform("uEmitterTransform", owner->GetWorldTransform());
            m_renderShader->SetUniform("uSimulationSpace", cpuSimulation ? 1 : ToInt(particleSystem->GetSimulationSpace()));
            m_renderShader->SetUniform("uParticleRenderShape", ToInt(particleSystem->GetRenderShape()));
            m_renderShader->SetUniform("uStartColorAlpha", particleSystem->GetStartColor().a);
            m_renderShader->SetUniform("uColorOverLifetimeEnabled", particleSystem->GetColorOverLifetimeEnabled() ? 1 : 0);
            m_renderShader->SetUniform("uEndColor", particleSystem->GetEndColor());
            m_renderShader->SetUniform("uSizeOverLifetimeEnabled", particleSystem->GetSizeOverLifetimeEnabled() ? 1 : 0);
            m_renderShader->SetUniform("uEndSize", particleSystem->GetEndSize());

            if (!particleSystem->GetMaterialAssetReference().empty())
            {
                if (auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(particleSystem->GetMaterialAssetReference()))
                {
                    material->Bind(m_renderShader);
                }
                else
                {
                    m_renderShader->SetUniform("uColor", glm::vec4(1.0f));
                    m_renderShader->SetUniform("uHasAlbedoTexture", 0.0f);
                }
            }
            else
            {
                m_renderShader->SetUniform("uColor", glm::vec4(1.0f));
                m_renderShader->SetUniform("uHasAlbedoTexture", 0.0f);
            }

            glBindVertexArray(particleVao);
            glDrawArrays(GL_POINTS, 0, particleDrawCount);

            if (particleSystem->GetTrailsEnabled())
            {
                const auto trailVertices = BuildTrailVertices(*particleSystem, cameraForward, cameraRight);
                if (!trailVertices.empty())
                {
                    EnsureTrailBuffer(m_trailVao, m_trailBuffer);
                    glBindBuffer(GL_ARRAY_BUFFER, m_trailBuffer);
                    glBufferData(GL_ARRAY_BUFFER,
                                 static_cast<GLsizeiptr>(trailVertices.size() * sizeof(ParticleTrailVertex)),
                                 trailVertices.data(),
                                 GL_DYNAMIC_DRAW);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);

                    m_trailShader->Bind();
                    m_trailShader->SetUniform("uView", ctx.cameraData.view);
                    m_trailShader->SetUniform("uProjection", ctx.cameraData.projection);

                    if (!particleSystem->GetTrailMaterialAssetReference().empty())
                    {
                        if (auto *material = core::Engine::GetInstance().GetAssetManager().LoadMaterialAsset(particleSystem->GetTrailMaterialAssetReference()))
                        {
                            material->Bind(m_trailShader);
                        }
                        else
                        {
                            m_trailShader->SetUniform("uColor", glm::vec4(1.0f));
                            m_trailShader->SetUniform("uHasAlbedoTexture", 0.0f);
                        }
                    }
                    else
                    {
                        m_trailShader->SetUniform("uColor", glm::vec4(1.0f));
                        m_trailShader->SetUniform("uHasAlbedoTexture", 0.0f);
                    }

                    glBindVertexArray(m_trailVao);
                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(trailVertices.size()));
                }
            }
        }

        glBindVertexArray(0);
        if (m_updateShader)
        {
            m_updateShader->Unbind();
        }
        if (m_renderShader)
        {
            m_renderShader->Unbind();
        }
        if (m_trailShader)
        {
            m_trailShader->Unbind();
        }
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDepthFunc(GL_GREATER);
        Graphics::UnbindRenderTarget();
    }
}
