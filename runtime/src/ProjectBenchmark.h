#pragma once
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <stdexcept>
#include <string_view>

namespace PlutoGE
{
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
        file << "sample,frame_ms,scene_update_ms,render_present_ms,scene_gpu_ms,gpu_available,skinning_ms,hud_ms,shadow_requests_gpu_ms\n";
        file << std::fixed << std::setprecision(6);
        for (std::size_t i = 0; i < samples.size(); ++i)
        {
            const auto &s = samples[i];
            file << i << ',' << s.frameMs << ',' << s.updateMs << ',' << s.renderPresentMs << ','
                 << s.gpuMs << ',' << s.gpuAvailable << ',' << s.skinningMs << ',' << s.uiMs << ',' << s.shadowRequestMs << '\n';
        }
        file.close();
    }
}
