#pragma once
#include "PlutoGE/render/GeometryDiagnostics.h"
#include <charconv>
#include <array>
#include <algorithm>
#include <map>
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
        // Geometry shading diagnostic applied to every measured frame.
        render::GeometryDiagnosticMode geometryDiagnostic = render::GeometryDiagnosticMode::None;
        static constexpr float FixedDelta = 1.0f / 60.0f;

        static ProjectBenchmarkOptions Parse(std::span<const char *const> args)
        {
            if (args.size() < 2 || args.size() > 7) // project, output, two counts and three switches
                throw std::invalid_argument("Usage: --benchmark-project <project> <output.csv> [frames=600] [warmup=120] [--frozen-render] "
                                            "[--geometry-diagnostic=<mode>|--scalar-shadows] [--capture=<frame.png>]");
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
                const bool diagnosticSet = result.geometryDiagnostic != render::GeometryDiagnosticMode::None;
                if (value == "--frozen-render" && !result.frozenRender) result.frozenRender = true;
                else if (value.starts_with(capturePrefix) && result.capture.empty())
                {
                    result.capture = std::filesystem::path(value.substr(capturePrefix.size()));
                    if (result.capture.extension() != ".png")
                        throw std::invalid_argument("Benchmark capture must have a .png extension");
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
            "parallel_recording", "descriptor_cpu_ms", "descriptor_binds", "gpu_observation_id"};
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
