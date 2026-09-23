#pragma once
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
#include <string_view>

namespace PlutoGE
{
    using ProjectBenchmarkGpuScopes = std::map<std::string, std::vector<float>>;

    inline void WriteProjectBenchmarkGpuScopes(std::filesystem::path path, ProjectBenchmarkGpuScopes scopes)
    {
        path.replace_extension(".gpu.csv");
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
        std::size_t frames = 600, warmup = 120;
        static constexpr float FixedDelta = 1.0f / 60.0f;

        static ProjectBenchmarkOptions Parse(std::span<const char *const> args)
        {
            if (args.size() < 2 || args.size() > 4)
                throw std::invalid_argument("Usage: --benchmark-project <project> <output.csv> [frames=600] [warmup=120]");
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
            if (args.size() > 2) result.frames = count(args[2], false);
            if (args.size() > 3) result.warmup = count(args[3], true);
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
            "scripts_ms", "animation_components_ms", "skinning_wait_ms", "skinning_caller_ms", "hud_sync_ms", "hud_update_ms", "hud_render_ms", "skinning_vertices", "geometry_draws", "shadow_draws"};
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
