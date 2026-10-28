#include "DdgiVolume.h"
#include "PlutoGE/render/DdgiConfig.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    namespace
    {
        constexpr rhi::Format DataFormat = rhi::Format::R16G16B16A16Float;
        // DDGIProbeUpdate.slang bindings.
        constexpr std::uint32_t VolumeSlot = 0, ProbeListSlot = 1, StateImageSlot = 2, RayTextureSlot = 3,
                                IrradianceImageSlot = 5, DistanceImageSlot = 6;
        // DDGIApply.slang bindings.
        constexpr std::uint32_t ScreenSlot = 1, DepthSlot = 2, NormalSlot = 3, AlbedoSlot = 4, MaterialSlot = 5,
                                SceneSlot = 6, IndirectSlot = 7, StateTextureSlot = 8, IrradianceTextureSlot = 9,
                                DistanceTextureSlot = 10, EnvironmentSpecularSlot = 11;
        // Sharpness of the visibility moments' cosine lobe (RTXGI's default).
        constexpr float DistanceExponent = 50.0f;
        // Sharpness of the sky visibility lobe (half power at about 17 degrees): wide enough to hold a few rays per
        // update, narrow enough to tell a window from the wall beside it.
        constexpr float SkyVisibilityExponent = 16.0f;
        // Reflection visibility is read at least this far above the receiver's tangent plane (sine of about 24
        // degrees), clearing the sky visibility lobe's half-power width and a texel, so probes do not see the
        // receiver's own surface as an occluder.
        constexpr float ReflectionMinimumElevation = 0.4f;

        rhi::Texture CreateDataTexture(rhi::IRenderDevice &device, glm::uvec2 size, const char *name)
        {
            return rhi::Texture(device, device.CreateTexture({.width = size.x, .height = size.y, .format = DataFormat,
                .usage = rhi::TextureUsage::Sampled, .debugName = name, .sampled = true, .storage = true,
                .mipLevels = 1}));
        }

        rhi::GraphicsPipeline CreateUpdatePipeline(rhi::IRenderDevice &device, const ScreenSpaceShaderCode &shader,
                                                   const char *name)
        {
            rhi::ComputePipelineDescriptor descriptor;
            descriptor.computeShader = shader;
            descriptor.debugName = name;
            const auto bind = [&](std::uint32_t slot, rhi::ResourceBindingType type)
            { descriptor.resourceBindings.push_back({slot, 0, slot, type, rhi::ShaderStageMask::Compute}); };
            bind(VolumeSlot, rhi::ResourceBindingType::UniformBuffer);
            bind(ProbeListSlot, rhi::ResourceBindingType::StorageBuffer);
            bind(StateImageSlot, rhi::ResourceBindingType::StorageImage);
            bind(RayTextureSlot, rhi::ResourceBindingType::SampledTexture);
            bind(IrradianceImageSlot, rhi::ResourceBindingType::StorageImage);
            bind(DistanceImageSlot, rhi::ResourceBindingType::StorageImage);
            return rhi::GraphicsPipeline(device, device.CreateComputePipeline(descriptor));
        }

        rhi::GraphicsPipeline CreateScreenPipeline(rhi::IRenderDevice &device, const ScreenSpaceFullscreenShader &shader,
                                                   std::vector<rhi::Format> formats, const char *name)
        {
            rhi::GraphicsPipelineDescriptor descriptor;
            descriptor.vertexShader = shader.vertex;
            descriptor.fragmentShader = shader.fragment;
            descriptor.colorFormat = formats.front();
            if (formats.size() > 1)
                descriptor.colorFormats = std::move(formats);
            descriptor.depthFormat = rhi::Format::Undefined;
            descriptor.cullMode = rhi::CullMode::None;
            descriptor.depthTest = false;
            descriptor.depthWrite = false;
            descriptor.debugName = name;
            // The probe impostors read the parameters and probe states in the vertex stage too.
            for (const auto slot : {VolumeSlot, ScreenSlot})
                descriptor.resourceBindings.push_back(
                    {slot, 0, slot, rhi::ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::AllGraphics});
            for (const auto slot : {DepthSlot, NormalSlot, AlbedoSlot, MaterialSlot, SceneSlot, IndirectSlot,
                                    StateTextureSlot, IrradianceTextureSlot, DistanceTextureSlot,
                                    EnvironmentSpecularSlot})
                descriptor.resourceBindings.push_back(
                    {slot, 0, slot, rhi::ResourceBindingType::SampledTexture, rhi::ShaderStageMask::AllGraphics});
            return rhi::GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        }
    }

    bool DdgiVolume::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        if (!HasShader(shaders.reset) || !HasShader(shaders.state) || !HasShader(shaders.blendIrradiance) ||
            !HasShader(shaders.blendDistance) || !HasShader(shaders.apply))
            return false;
        m_reset = CreateUpdatePipeline(device, shaders.reset, "DDGI probe reset");
        m_state = CreateUpdatePipeline(device, shaders.state, "DDGI probe relocation and classification");
        m_blendIrradiance = CreateUpdatePipeline(device, shaders.blendIrradiance, "DDGI irradiance blend");
        m_blendDistance = CreateUpdatePipeline(device, shaders.blendDistance, "DDGI visibility blend");
        // Scene colour, indirect term and environment reflection.
        m_apply = CreateScreenPipeline(device, shaders.apply, {DataFormat, DataFormat, DataFormat}, "DDGI apply");
        if (HasShader(shaders.probes))
            m_probes = CreateScreenPipeline(device, shaders.probes, {DataFormat}, "DDGI probe spheres");
        m_pointSampler = rhi::Sampler(device, device.CreateSampler({false, false, "DDGI point sampler"}));
        m_linearSampler = rhi::Sampler(device, device.CreateSampler({true, false, "DDGI atlas sampler"}));
        m_volumeUniforms.Initialize(device, "DDGI volume parameters");
        m_screenUniforms.Initialize(device, "DDGI screen parameters");
        m_device = &device;
        return Available();
    }

    bool DdgiVolume::Available() const noexcept
    {
        return m_device && m_reset && m_state && m_blendIrradiance && m_blendDistance && m_apply;
    }

    void DdgiVolume::EnsureResources(const DdgiGridSettings &grid)
    {
        if (m_hasResources && grid == m_grid)
            return;
        m_grid = grid;
        m_cascades.clear();
        m_schedulers.assign(static_cast<std::size_t>(grid.cascades), {});
        for (int cascade = 0; cascade < grid.cascades; ++cascade)
        {
            m_cascades.emplace_back(grid, cascade);
            m_schedulers[static_cast<std::size_t>(cascade)].Reset(grid.ProbeCount());
        }
        const auto tiles = DdgiAtlasTiles(grid.TotalProbes());
        m_irradiance = CreateDataTexture(*m_device, tiles * glm::uvec2(PLUTO_DDGI_IRRADIANCE_TILE), "DDGI irradiance");
        m_distance = CreateDataTexture(*m_device, tiles * glm::uvec2(PLUTO_DDGI_DISTANCE_TILE), "DDGI visibility");
        m_states = CreateDataTexture(*m_device, tiles, "DDGI probe states");
        m_rays = CreateDataTexture(*m_device, {grid.TracedRays(), grid.FrameBudget()}, "DDGI rays");
        // This frame's listed probes followed by those it resets: at most a full budget plus every probe.
        m_probeList = rhi::Buffer(*m_device, m_device->CreateBuffer(
            {(static_cast<std::size_t>(grid.TotalProbes()) + grid.FrameBudget()) * sizeof(std::uint32_t),
             rhi::BufferUsage::Storage, "DDGI probe list"}));
        m_resetAll = true;
        m_hasResources = true;
    }

    DdgiGpuParameters DdgiVolume::BuildParameters(std::uint32_t listedProbes, float resetProbes) const
    {
        const auto &grid = m_grid;
        const auto tiles = DdgiAtlasTiles(grid.TotalProbes());
        DdgiGpuParameters parameters;
        for (const auto &cascade : m_cascades)
        {
            const auto index = static_cast<std::size_t>(cascade.Cascade());
            parameters.cascadeOrigins[index] = {cascade.Origin(), cascade.Spacing()};
            parameters.cascadeCells[index] = {cascade.OriginCell(), 0};
        }
        parameters.counts = {grid.counts, static_cast<int>(tiles.x)};
        parameters.volume = {static_cast<std::uint32_t>(grid.cascades), grid.ProbeCount(), grid.TotalProbes(), 0u};
        parameters.rays = {PLUTO_DDGI_FIXED_RAYS, grid.blendedRays, listedProbes, grid.TracedRays()};
        const auto rotation = DdgiRayRotation(m_updates);
        for (int row = 0; row < 3; ++row)
            parameters.rotation[row] = {rotation[0][row], rotation[1][row], rotation[2][row], 0.0f};
        // Visibility moments only need distances up to a cell diagonal plus bias (in each cascade's spacings);
        // clamping keeps them precise.
        parameters.blend = {m_settings.hysteresis, m_settings.brightnessClamp, DistanceExponent, 1.5f * std::sqrt(3.0f)};
        // Lookups must clear the probes embedded in or behind the surface. Visibility separates them only as sharply
        // as the ray source places surfaces, so the normal offset is at least one and a half times that tolerance.
        parameters.bias = {m_settings.normalBias, m_settings.viewBias, 1.5f * m_surfaceTolerance, 0.0f};
        parameters.state = {m_settings.relocation ? 1.0f : 0.0f, m_settings.classification ? 1.0f : 0.0f,
                            m_settings.minFrontfaceDistance, m_settings.multipleBounces ? 1.0f : 0.0f};
        parameters.apply = {m_settings.intensity, static_cast<float>(m_settings.debugView), m_settings.probeRadius,
                            resetProbes};
        parameters.reflection = {m_settings.reflectionOcclusion, ReflectionMinimumElevation, SkyVisibilityExponent,
                                 0.0f};
        parameters.atlas = {tiles * glm::uvec2(PLUTO_DDGI_IRRADIANCE_TILE), tiles * glm::uvec2(PLUTO_DDGI_DISTANCE_TILE)};
        // Without the physical sky the geometry pass lights a surface with albedo * ambient, which is irradiance
        // pi * ambient in this volume's convention (cosine-weighted mean radiance times albedo / pi).
        parameters.sky = {m_environment.physicalSky ? 1.0f : 0.0f,
                          3.14159265f * std::max(m_environment.ambientIntensity, 0.0f), 0.0f, 0.0f};
        parameters.physicalSkyParameters = m_environment.skyParameters;
        parameters.physicalSkySettings = {m_environment.physicalSky ? 1.0f : 0.0f,
                                          std::max(m_environment.skyExposure, 0.0f), 1.0f, 0.0f};
        return parameters;
    }

    void DdgiVolume::Update(rhi::ICommandContext &commands, const DdgiSettings &settings,
                            const DdgiEnvironment &environment, glm::vec3 cameraPosition, IDdgiRaySource &rays)
    {
        if (!Available())
            return;
        m_settings = settings.Sanitized();
        m_environment = environment;
        EnsureResources(m_settings.grid);
        m_volumeUniforms.BeginFrame();
        m_screenUniforms.BeginFrame();
        const auto &grid = m_grid;
        m_stats = {.framesPerSweep = grid.FramesPerSweep(0)};

        // Recentre every cascade; a cascade that moved by its own size (or a full reset) restarts all its probes.
        std::array<DdgiProbeGrid::Scroll, DdgiGridSettings::MaxCascades> scrolls{};
        bool everyCascadeReset = true;
        for (std::size_t index = 0; index < m_cascades.size(); ++index)
        {
            scrolls[index] = m_cascades[index].Recenter(cameraPosition);
            everyCascadeReset = everyCascadeReset && scrolls[index].reset;
        }
        const bool resetAll = m_resetAll || everyCascadeReset;
        m_entered.clear();
        for (std::size_t index = 0; index < m_cascades.size(); ++index)
        {
            const auto &cascade = m_cascades[index];
            m_scratch.clear();
            if (resetAll || scrolls[index].reset)
            {
                m_schedulers[index].Reset(grid.ProbeCount());
                if (!resetAll)
                    for (std::uint32_t probe = 0; probe < grid.ProbeCount(); ++probe)
                        m_entered.push_back(cascade.GlobalIndex(probe));
                continue;
            }
            cascade.AppendEnteredProbes(scrolls[index], m_scratch);
            m_schedulers[index].Enqueue(m_scratch);
            for (const auto probe : m_scratch)
                m_entered.push_back(cascade.GlobalIndex(probe));
        }
        m_listed.clear();
        if (rays.Ready())
        {
            for (std::size_t index = 0; index < m_cascades.size(); ++index)
            {
                m_schedulers[index].Next(grid.CascadeBudget(static_cast<int>(index)), m_scratch);
                for (const auto probe : m_scratch)
                    m_listed.push_back(m_cascades[index].GlobalIndex(probe));
            }
            m_surfaceTolerance = rays.SurfaceTolerance();
        }
        m_upload = m_listed;
        m_upload.insert(m_upload.end(), m_entered.begin(), m_entered.end());
        if (!m_upload.empty())
            m_device->UpdateBuffer(m_probeList.Get(), 0, std::as_bytes(std::span(m_upload)));
        const auto listed = static_cast<std::uint32_t>(m_listed.size());
        const auto parameters = m_volumeUniforms.Push(BuildParameters(
            listed, resetAll ? -1.0f : static_cast<float>(m_entered.size())));
        const auto bindUpdate = [&](const rhi::GraphicsPipeline &pipeline)
        {
            commands.BindPipeline(pipeline.Get());
            commands.BindUniformBuffer(VolumeSlot, parameters);
            commands.BindStorageBuffer(ProbeListSlot, m_probeList.Get());
            commands.BindStorageImage(StateImageSlot, m_states.Get());
            commands.BindTexture(RayTextureSlot, m_rays.Get(), m_pointSampler.Get());
            commands.BindStorageImage(IrradianceImageSlot, m_irradiance.Get());
            commands.BindStorageImage(DistanceImageSlot, m_distance.Get());
        };
        const std::array states{m_states.Get()};

        const auto resetCount = resetAll ? grid.TotalProbes() : static_cast<std::uint32_t>(m_entered.size());
        if (resetCount > 0)
        {
            commands.BeginGpuScope("RHI DDGI / Probe reset");
            bindUpdate(m_reset);
            commands.Dispatch((resetCount + PLUTO_DDGI_STATE_GROUP - 1) / PLUTO_DDGI_STATE_GROUP, 1, 1);
            commands.ComputeImageBarrier(states);
            commands.EndGpuScope();
            m_resetAll = false;
            m_stats.probesReset = resetCount;
        }
        if (listed == 0)
            return;

        commands.BeginGpuScope("RHI DDGI / Trace");
        rays.Trace(commands, {.volumeParameters = parameters, .probeList = m_probeList.Get(), .rays = m_rays.Get(),
                              .probeStates = m_states.Get(), .irradiance = m_irradiance.Get(),
                              .distance = m_distance.Get(), .pointSampler = m_pointSampler.Get(),
                              .linearSampler = m_linearSampler.Get(), .probes = listed,
                              .tracedRays = grid.TracedRays()});
        const std::array rayImage{m_rays.Get()};
        commands.ComputeImageBarrier(rayImage);
        commands.EndGpuScope();

        commands.BeginGpuScope("RHI DDGI / Relocate and classify");
        bindUpdate(m_state);
        commands.Dispatch((listed + PLUTO_DDGI_STATE_GROUP - 1) / PLUTO_DDGI_STATE_GROUP, 1, 1);
        commands.ComputeImageBarrier(states);
        commands.EndGpuScope();

        commands.BeginGpuScope("RHI DDGI / Blend");
        bindUpdate(m_blendIrradiance);
        commands.Dispatch(listed, 1, 1);
        const std::array irradiance{m_irradiance.Get()};
        commands.ComputeImageBarrier(irradiance);
        bindUpdate(m_blendDistance);
        commands.Dispatch(listed, 1, 1);
        const std::array blended{m_distance.Get(), m_states.Get()};
        commands.ComputeImageBarrier(blended);
        commands.EndGpuScope();

        ++m_updates;
        m_stats.probesTraced = listed;
        m_stats.raysScheduled = listed * grid.TracedRays();
    }

    ScreenSpaceLighting DdgiVolume::Apply(rhi::ICommandContext &commands, const ScreenSpaceView &view,
                                          const ScreenSpaceLighting &lighting)
    {
        if (!Available() || !m_hasResources || !lighting.color || !lighting.indirect)
            return lighting;
        const rhi::TextureDescriptor output{.width = view.width, .height = view.height, .format = DataFormat,
            .usage = rhi::TextureUsage::ColorAttachment, .debugName = "DDGI lit scene", .sampled = true};
        m_color.Ensure(*m_device, output);
        auto indirectOutput = output;
        indirectOutput.debugName = "DDGI indirect";
        m_indirect.Ensure(*m_device, indirectOutput);
        auto environmentOutput = output;
        environmentOutput.debugName = "DDGI environment specular";
        m_environmentSpecular.Ensure(*m_device, environmentOutput);
        auto parameters = BuildParameters(0, 0.0f);
        // Without the geometry pass's environment reflection there is nothing to occlude; the indirect term stands
        // in for the unread binding.
        const bool reflections = static_cast<bool>(lighting.environmentSpecular);
        if (!reflections)
            parameters.reflection.x = 0.0f;
        const auto environmentSpecular = reflections ? lighting.environmentSpecular : lighting.indirect;
        const auto volume = m_volumeUniforms.Push(parameters);
        const auto screen = m_screenUniforms.Push(MakeScreenSpaceParameters(view, 0, {}));
        const auto bindApply = [&](const rhi::GraphicsPipeline &pipeline)
        {
            commands.BindPipeline(pipeline.Get());
            commands.BindUniformBuffer(VolumeSlot, volume);
            commands.BindUniformBuffer(ScreenSlot, screen);
            commands.BindTexture(DepthSlot, view.depth, m_pointSampler.Get());
            commands.BindTexture(NormalSlot, view.normal, m_pointSampler.Get());
            commands.BindTexture(AlbedoSlot, view.albedo, m_pointSampler.Get());
            commands.BindTexture(MaterialSlot, view.material, m_pointSampler.Get());
            commands.BindTexture(SceneSlot, lighting.color, m_pointSampler.Get());
            commands.BindTexture(IndirectSlot, lighting.indirect, m_pointSampler.Get());
            commands.BindTexture(StateTextureSlot, m_states.Get(), m_pointSampler.Get());
            commands.BindTexture(IrradianceTextureSlot, m_irradiance.Get(), m_linearSampler.Get());
            commands.BindTexture(DistanceTextureSlot, m_distance.Get(), m_linearSampler.Get());
            commands.BindTexture(EnvironmentSpecularSlot, environmentSpecular, m_pointSampler.Get());
        };

        commands.BeginGpuScope("RHI DDGI / Apply");
        rhi::RenderingInfo info;
        info.colorAttachments = {m_color.Get(), m_indirect.Get(), m_environmentSpecular.Get()};
        info.width = view.width;
        info.height = view.height;
        info.clearColor = info.clearDepth = false;
        commands.BeginRendering(info);
        bindApply(m_apply);
        commands.Draw(3);
        commands.EndRendering();
        commands.EndGpuScope();

        if (m_settings.debugView == PLUTO_DDGI_VIEW_PROBES && m_probes)
        {
            commands.BeginGpuScope("RHI DDGI / Probe spheres");
            info.colorAttachments = {m_color.Get()};
            commands.BeginRendering(info);
            bindApply(m_probes);
            commands.Draw(6 * m_grid.TotalProbes());
            commands.EndRendering();
            commands.EndGpuScope();
        }
        return {m_color.Get(), m_indirect.Get(), reflections ? m_environmentSpecular.Get() : rhi::TextureHandle{}};
    }

    void DdgiVolume::ReleaseTargets() noexcept
    {
        m_color.Reset();
        m_indirect.Reset();
        m_environmentSpecular.Reset();
    }

    void DdgiVolume::ReleaseResources() noexcept
    {
        ReleaseTargets();
        m_irradiance.Reset();
        m_distance.Reset();
        m_states.Reset();
        m_rays.Reset();
        m_probeList.Reset();
        m_volumeUniforms.Reset();
        m_screenUniforms.Reset();
        m_cascades.clear();
        m_schedulers.clear();
        m_hasResources = false;
        m_resetAll = true;
    }
}
