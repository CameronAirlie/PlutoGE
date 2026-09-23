#include "ProjectBenchmark.h"
#include <array>
#include <algorithm>
#include <chrono>
#include <iostream>

int main() try
{
    using namespace PlutoGE;
    const std::array defaults{"game.plutoproject", "results.csv"};
    const auto options = ProjectBenchmarkOptions::Parse(defaults);
    if (options.frames != 600 || options.warmup != 120) throw std::runtime_error("Wrong defaults");
    const std::array custom{"game.plutoproject", "results.csv", "12", "0"};
    if (ProjectBenchmarkOptions::Parse(custom).warmup != 0) throw std::runtime_error("Zero warmup rejected");
    for (const auto *bad : {"0", "-1", "12junk", "100001", ""})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid frame count accepted");
    }
    struct TempFile
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
            ("pluto-benchmark-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".csv");
        ~TempFile() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } file;
    std::array samples{ProjectBenchmarkSample{.frameMs = 4.5, .gpuMs = 2, .gpuAvailable = true}, ProjectBenchmarkSample{}};
    samples[0].details.back() = 123;
    WriteProjectBenchmark(file.path, samples);
    std::ifstream input(file.path);
    std::string line;
    std::getline(input, line);
    if (!line.starts_with("sample,frame_ms,")) throw std::runtime_error("CSV header missing");
    const auto columnSeparators = std::count(line.begin(), line.end(), ',');
    if (columnSeparators != 8 + ProjectBenchmarkSample::DetailNames.size() || !line.ends_with(",shadow_draws"))
        throw std::runtime_error("Detailed CSV header invalid");
    std::getline(input, line);
    if (!line.starts_with("0,4.500000,") || line.find(",2.000000,1,") == std::string::npos)
        throw std::runtime_error("CSV metrics invalid");
    if (std::count(line.begin(), line.end(), ',') != columnSeparators || !line.ends_with(",123.000000"))
        throw std::runtime_error("Detailed CSV values misaligned");
    std::getline(input, line);
    if (!line.starts_with("1,")) throw std::runtime_error("Chronological sample order lost");
    TempFile gpuFile;
    const auto gpuOutput = gpuFile.path;
    gpuFile.path.replace_extension(".gpu.csv");
    WriteProjectBenchmarkGpuScopes(gpuOutput, {{"Pass, \"quoted\"", {1, 2, 3, 4}}});
    std::ifstream gpuInput(gpuFile.path);
    std::getline(gpuInput, line);
    std::getline(gpuInput, line);
    if (line != "\"Pass, \"\"quoted\"\"\",4,2.500000,4.000000,4.000000")
        throw std::runtime_error("GPU scope CSV statistics or escaping invalid");
    std::cout << "Project benchmark tests passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
