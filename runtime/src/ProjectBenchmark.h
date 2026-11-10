#pragma once
#include "PlutoGE/render/GeometryDiagnostics.h"
#include "PlutoGE/render/GraphicsQuality.h"
#include <charconv>
#include <cmath>
#include <array>
#include <algorithm>
#include <map>
#include <optional>
#include <vector>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace PlutoGE
{
    using ProjectBenchmarkGpuScopes = std::map<std::string, std::vector<float>>;

    // Per-scope timings beside the benchmark CSV: GPU scopes (".gpu.csv") or CPU component and script costs (".cpu.csv").
    inline void WriteProjectBenchmarkGpuScopes(std::filesystem::path path, ProjectBenchmarkGpuScopes scopes,
                                               std::string_view extension = ".gpu.csv")
    {
        path.replace_extension(extension);
        std::ofstream file(path);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.imbue(std::locale::classic());
        file << "scope,observations,mean_ms,p95_ms,max_ms\n" << std::fixed << std::setprecision(6);
        for (auto &[name, values] : scopes)
        {
            if (values.empty()) continue;
            std::sort(values.begin(), values.end());
            double sum = 0;
            for (float value : values) sum += value;
            file << '"';
            for (char c : name) { if (c == '"') file << '"'; file << c; }
            file << "\"," << values.size() << ',' << sum / values.size() << ','
                 << values[(values.size() * 95 + 99) / 100 - 1] << ',' << values.back() << '\n';
        }
        file.close();
    }

    struct ProjectBenchmarkOptions
    {
        std::filesystem::path project, output;
        // Optional PNG of the final measured frame (Vulkan).
        std::filesystem::path capture;
        std::size_t frames = 600, warmup = 120;
        bool frozenRender = false;
        // Records, submits and presents on the calling thread instead of the render thread (for comparisons).
        bool serialRender = false;
        // --cpu-instance-binning: bin instanced draws on the CPU, without occlusion, rather than with GPU instance
        // culling (the default). --gpu-instance-culling, the default, is still accepted.
        bool cpuInstanceBinning = false;
        // --dynamic-resolution=<target FPS>: enables automatic render resolution for the run.
        std::optional<float> dynamicResolutionFps;
        // Geometry shading diagnostic applied to every measured frame.
        render::GeometryDiagnosticMode geometryDiagnostic = render::GeometryDiagnosticMode::None;
        // Project graphics tier applied before the scene loads. Frozen captures run
        // no scripts, so without it they render the scene's authored quality.
        std::optional<render::GraphicsPreset> quality;
        // Replaces that tier's foliage detail (GraphicsQuality::foliageDetail); needs --quality.
        std::optional<float> foliageDetail;
        // Swings the base camera's yaw by +/- this many degrees over CameraPanPeriodFrames,
        // after the scene update, so temporal reconstruction is measured under motion.
        float cameraPanDegrees = 0.0f;
        // Moves the base camera +/- this many metres along its horizontal forward axis over CameraTravelPeriodFrames,
        // so streaming, LOD and shadow caching are measured as the camera travels (panning only turns it).
        float cameraTravelMetres = 0.0f;
        // Post-process effect type names ("all" for every effect) disabled on the rendering camera each frame, as the
        // editor's Enabled toggle does, to measure what each effect costs.
        std::vector<std::string> disabledEffects;
        static constexpr std::size_t CameraPanPeriodFrames = 120;
        static constexpr std::size_t CameraTravelPeriodFrames = 600;

        [[nodiscard]] bool DisablesEffect(std::string_view type) const
        {
            return std::any_of(disabledEffects.begin(), disabledEffects.end(),
                               [&](const std::string &name) { return name == "all" || name == type; });
        }
        static constexpr float FixedDelta = 1.0f / 60.0f;

        // Yaw offset in degrees for a frame; zero when panning is off.
        [[nodiscard]] float CameraPanOffset(std::size_t frame) const noexcept
        {
            constexpr float tau = 6.28318530718f;
            return cameraPanDegrees * std::sin(tau * static_cast<float>(frame % CameraPanPeriodFrames) /
                                               static_cast<float>(CameraPanPeriodFrames));
        }
        // Forward offset in metres for a frame; zero when travel is off.
        [[nodiscard]] float CameraTravelOffset(std::size_t frame) const noexcept
        {
            constexpr float tau = 6.28318530718f;
            return cameraTravelMetres * std::sin(tau * static_cast<float>(frame % CameraTravelPeriodFrames) /
                                                 static_cast<float>(CameraTravelPeriodFrames));
        }

        static ProjectBenchmarkOptions Parse(std::span<const char *const> args)
        {
            if (args.size() < 2 || args.size() > 14) // project, output, two counts and ten switches
                throw std::invalid_argument("Usage: --benchmark-project <project> <output.csv> [frames=600] [warmup=120] [--frozen-render] "
                                            "[--geometry-diagnostic=<mode>|--scalar-shadows] [--capture=<frame.png>] [--serial-render] [--cpu-instance-binning] "
                                            "[--quality=low|medium|high|ultra] [--foliage-detail=<0.25..2>] [--camera-pan=<degrees>] [--camera-travel=<metres>] "
                                            "[--disable-effects=<Type>[,<Type>...]|all]");
            ProjectBenchmarkOptions result{args[0], args[1]};
            const auto count = [](std::string_view value, bool allowZero)
            {
                std::size_t number = 0;
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    number > 100000 || (!allowZero && number == 0))
                    throw std::invalid_argument("Benchmark counts must be integers in range (frames 1..100000, warmup 0..100000)");
                return number;
            };
            std::size_t positional = 0;
            for (std::size_t i = 2; i < args.size(); ++i)
            {
                const std::string_view value(args[i]);
                constexpr std::string_view diagnosticPrefix = "--geometry-diagnostic=";
                constexpr std::string_view capturePrefix = "--capture=";
                constexpr std::string_view qualityPrefix = "--quality=";
                constexpr std::string_view panPrefix = "--camera-pan=";
                constexpr std::string_view travelPrefix = "--camera-travel=";
                constexpr std::string_view foliagePrefix = "--foliage-detail=";
                constexpr std::string_view disablePrefix = "--disable-effects=";
                constexpr std::string_view dynamicResolutionPrefix = "--dynamic-resolution=";
                const bool diagnosticSet = result.geometryDiagnostic != render::GeometryDiagnosticMode::None;
                if (value == "--frozen-render" && !result.frozenRender) result.frozenRender = true;
                else if (value == "--serial-render" && !result.serialRender) result.serialRender = true;
                else if (value == "--cpu-instance-binning" && !result.cpuInstanceBinning) result.cpuInstanceBinning = true;
                else if (value == "--gpu-instance-culling") {}
                else if (value.starts_with(capturePrefix) && result.capture.empty())
                {
                    result.capture = std::filesystem::path(value.substr(capturePrefix.size()));
                    if (result.capture.extension() != ".png")
                        throw std::invalid_argument("Benchmark capture must have a .png extension");
                }
                else if (value.starts_with(qualityPrefix) && !result.quality)
                {
                    constexpr std::array tiers{std::pair{"low", render::GraphicsPreset::Low}, std::pair{"medium", render::GraphicsPreset::Medium},
                                               std::pair{"high", render::GraphicsPreset::High}, std::pair{"ultra", render::GraphicsPreset::Ultra}};
                    const auto tier = std::find_if(tiers.begin(), tiers.end(),
                        [&](const auto &entry) { return value.substr(qualityPrefix.size()) == entry.first; });
                    if (tier == tiers.end())
                        throw std::invalid_argument("Unknown benchmark quality: " + std::string(value));
                    result.quality = tier->second;
                }
                else if (value.starts_with(dynamicResolutionPrefix) && !result.dynamicResolutionFps)
                {
                    const auto text = value.substr(dynamicResolutionPrefix.size());
                    float fps = 0.0f;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), fps);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !(fps >= 10.0f && fps <= 500.0f))
                        throw std::invalid_argument("Dynamic resolution target must be a frame rate in [10, 500]");
                    result.dynamicResolutionFps = fps;
                }
                else if (value.starts_with(foliagePrefix) && !result.foliageDetail)
                {
                    const auto text = value.substr(foliagePrefix.size());
                    float detail = 0.0f;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), detail);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                        !(detail >= render::MinFoliageDetail && detail <= render::MaxFoliageDetail))
                        throw std::invalid_argument("Foliage detail must be a number in [0.25, 2]");
                    result.foliageDetail = detail;
                }
                else if (value.starts_with(panPrefix) && result.cameraPanDegrees == 0.0f)
                {
                    const auto text = value.substr(panPrefix.size());
                    float degrees = 0.0f;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), degrees);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                        !(degrees > 0.0f) || degrees > 180.0f)
                        throw std::invalid_argument("Camera pan must be a number of degrees in (0, 180]");
                    result.cameraPanDegrees = degrees;
                }
                else if (value.starts_with(travelPrefix) && result.cameraTravelMetres == 0.0f)
                {
                    const auto text = value.substr(travelPrefix.size());
                    float metres = 0.0f;
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), metres);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
                        !(metres > 0.0f) || metres > 10000.0f)
                        throw std::invalid_argument("Camera travel must be a number of metres in (0, 10000]");
                    result.cameraTravelMetres = metres;
                }
                else if (value.starts_with(disablePrefix) && result.disabledEffects.empty())
                {
                    auto names = value.substr(disablePrefix.size());
                    while (true)
                    {
                        const auto comma = names.find(',');
                        const auto name = names.substr(0, comma);
                        if (name.empty())
                            throw std::invalid_argument("Disabled effect names must not be empty");
                        result.disabledEffects.emplace_back(name);
                        if (comma == std::string_view::npos) break;
                        names.remove_prefix(comma + 1);
                    }
                }
                else if (value == "--scalar-shadows" && !diagnosticSet)
                    result.geometryDiagnostic = render::GeometryDiagnosticMode::ReferenceDirectionalShadows;
                else if (value.starts_with(diagnosticPrefix) && !diagnosticSet)
                {
                    const auto mode = render::ParseGeometryDiagnostic(value.substr(diagnosticPrefix.size()));
                    // Sweeps change mode mid-capture and would mix their samples.
                    if (!mode || *mode == render::GeometryDiagnosticMode::None || *mode == render::GeometryDiagnosticMode::AutomaticSweep)
                        throw std::invalid_argument("Unknown or unsupported geometry diagnostic: " + std::string(value));
                    result.geometryDiagnostic = *mode;
                }
                else if (value.starts_with("--") || positional >= 2)
                    throw std::invalid_argument("Unknown or duplicate benchmark option");
                else if (positional++ == 0) result.frames = count(value, false);
                else result.warmup = count(value, true);
            }
            if (result.geometryDiagnostic != render::GeometryDiagnosticMode::None && !result.frozenRender)
                throw std::invalid_argument("Geometry diagnostic comparisons require --frozen-render");
            if (result.foliageDetail && !result.quality)
                throw std::invalid_argument("--foliage-detail replaces a tier's value and requires --quality");
            if (result.output.extension() != ".csv")
                throw std::invalid_argument("Benchmark output must have a .csv extension");
            return result;
        }
    };

    struct ProjectBenchmarkSample
    {
        static constexpr std::array DetailNames{
            "preparation_ms", "runtime_ui_ms", "components_ms", "physics_ms", "late_scripts_ms", "audio_ms", "scene_submission_ms",
            "translation_ms", "skinning_upload_ms", "scene_setup_ms", "recording_ms", "begin_frame_ms", "shadow_recording_ms", "geometry_recording_ms", "post_recording_ms", "submit_ms",
            "scripts_ms", "animation_components_ms", "skinning_wait_ms", "skinning_caller_ms", "hud_sync_ms", "hud_update_ms", "hud_render_ms", "skinning_vertices", "geometry_draws", "shadow_draws",
            "internal_width", "internal_height", "output_width", "output_height", "opaque_triangles", "geometry_instances",
            "parallel_recording", "descriptor_cpu_ms", "descriptor_binds", "uniform_bytes", "persistent_uniform_bytes", "vsm_page_batches",
            "point_shadow_face_updates", "point_shadow_draws", "point_shadow_object_uploads",
            "point_shadow_invalid_projection", "point_shadow_invalid_casters", "point_shadow_invalid_content", "point_shadow_static_faces",
            "instance_binning_ms", "instance_preparation_ms",
            "visible_preparation_ms", "shadow_preparation_ms", "gi_preparation_ms", "batching_ms", "rebuilt_draw_packets",
            "vsm_active", "vsm_rebuilt_packets", "vsm_dirty_pages", "vsm_deferred_pages", "vsm_updated_triangles",
            "vsm_updated_pages", "vsm_caster_chunks", "vsm_clustered_chunks", "vsm_max_dirty_page_triangles", "vsm_requested_pages",
            "vsm_dirty_fine_triangles", "vsm_dirty_root_triangles", "vsm_fine_levels", "vsm_resolution_scale",
            "vsm_new_dirty_pages", "vsm_changed_dirty_pages", "vsm_evicted_pages", "vsm_cache_hits",
            "vsm_static_dirty_pages", "vsm_dynamic_chunks", "vsm_static_layer", "vsm_chunking", "vsm_lod_variant_chunks",
            "gpu_cull_instances", "gpu_cull_outside", "gpu_cull_occluded", "gpu_cull_recovered", "gpu_cull_drawn",
            "ray_traced_rebuilt", "opaque_batch_groups_rebuilt", "opaque_batch_draws_rebuilt", "visible_packets", "gpu_observation_id"};
        std::array<double, DetailNames.size()> details{};
        double frameMs = 0, updateMs = 0, renderPresentMs = 0;
        float gpuMs = 0, skinningMs = 0, uiMs = 0, shadowRequestMs = 0;
        bool gpuAvailable = false;
    };

    inline void WriteProjectBenchmark(const std::filesystem::path &path,
                                      std::span<const ProjectBenchmarkSample> samples)
    {
        if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path);
        file.exceptions(std::ios::failbit | std::ios::badbit);
        file.imbue(std::locale::classic());
        file << "sample,frame_ms,scene_update_ms,render_present_ms,scene_gpu_ms,gpu_available,skinning_ms,hud_ms,shadow_requests_gpu_ms";
        for (const auto *name : ProjectBenchmarkSample::DetailNames) file << ',' << name;
        file << '\n';
        file << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto &s = samples[i];
            file << i << ',' << s.frameMs << ',' << s.updateMs << ',' << s.renderPresentMs << ','
                 << s.gpuMs << ',' << s.gpuAvailable << ',' << s.skinningMs << ',' << s.uiMs << ',' << s.shadowRequestMs;
            for (const double value : s.details) file << ',' << value;
            file << '\n';
        }
        file.close();
    }
}
