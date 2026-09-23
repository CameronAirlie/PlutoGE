#include "ProjectBenchmark.h"
#include <array>
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
    const std::array samples{ProjectBenchmarkSample{.frameMs = 4.5, .gpuMs = 2, .gpuAvailable = true}, ProjectBenchmarkSample{}};
    WriteProjectBenchmark(file.path, samples);
    std::ifstream input(file.path);
    std::string line;
    std::getline(input, line);
    if (!line.starts_with("sample,frame_ms,")) throw std::runtime_error("CSV header missing");
    std::getline(input, line);
    if (!line.starts_with("0,4.500000,") || line.find(",2.000000,1,") == std::string::npos)
        throw std::runtime_error("CSV metrics invalid");
    std::getline(input, line);
    if (!line.starts_with("1,")) throw std::runtime_error("Chronological sample order lost");
    std::cout << "Project benchmark tests passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
