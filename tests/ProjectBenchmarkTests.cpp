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
    const std::array frozen{"game.plutoproject", "results.csv", "10", "2", "--frozen-render", "--scalar-shadows"};
    const auto frozenOptions = ProjectBenchmarkOptions::Parse(frozen);
    if (!frozenOptions.frozenRender || frozenOptions.frames != 10 ||
        frozenOptions.geometryDiagnostic != render::GeometryDiagnosticMode::ReferenceDirectionalShadows)
        throw std::runtime_error("Frozen benchmark options lost");
    const std::array diagnostic{"game.plutoproject", "results.csv", "--frozen-render", "--geometry-diagnostic=material-only"};
    if (ProjectBenchmarkOptions::Parse(diagnostic).geometryDiagnostic != render::GeometryDiagnosticMode::MaterialOnly)
        throw std::runtime_error("Geometry diagnostic option lost");
    for (const auto *bad : {"--unknown", "--scalar-shadows", "--geometry-diagnostic=material-only"})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid benchmark switch accepted");
    }
    for (const auto *bad : {"--geometry-diagnostic=bogus", "--geometry-diagnostic=sweep", "--geometry-diagnostic=none"})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", "--frozen-render", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Unsupported geometry diagnostic accepted");
    }
    if (options.quality) throw std::runtime_error("Quality override applied by default");
    const std::array quality{"game.plutoproject", "results.csv", "--frozen-render", "--quality=low"};
    if (ProjectBenchmarkOptions::Parse(quality).quality != render::GraphicsPreset::Low)
        throw std::runtime_error("Quality option lost");
    for (const auto *bad : {"--quality=", "--quality=Low", "--quality=epic"})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid quality accepted");
    }
    if (options.cameraPanDegrees != 0.0f || options.CameraPanOffset(30) != 0.0f) throw std::runtime_error("Camera pans by default");
    const std::array pan{"game.plutoproject", "results.csv", "--camera-pan=10"};
    const auto panOptions = ProjectBenchmarkOptions::Parse(pan);
    if (panOptions.cameraPanDegrees != 10.0f || std::abs(panOptions.CameraPanOffset(ProjectBenchmarkOptions::CameraPanPeriodFrames / 4) - 10.0f) > 1e-4f ||
        panOptions.CameraPanOffset(0) != 0.0f)
        throw std::runtime_error("Camera pan option or offsets wrong");
    for (const auto *bad : {"--camera-pan=0", "--camera-pan=-5", "--camera-pan=200", "--camera-pan=x"})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid camera pan accepted");
    }
    const std::array capture{"game.plutoproject", "results.csv", "--capture=out/frame.png"};
    if (ProjectBenchmarkOptions::Parse(capture).capture != std::filesystem::path("out/frame.png"))
        throw std::runtime_error("Capture path lost");
    for (const auto *bad : {"--capture=frame.jpg", "--capture="})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Invalid capture path accepted");
    }
    if (!options.disabledEffects.empty() || options.DisablesEffect("SSAO")) throw std::runtime_error("Effects disabled by default");
    const std::array disabled{"game.plutoproject", "results.csv", "--disable-effects=SSAO,DDGI"};
    const auto disabledOptions = ProjectBenchmarkOptions::Parse(disabled);
    if (!disabledOptions.DisablesEffect("SSAO") || !disabledOptions.DisablesEffect("DDGI") || disabledOptions.DisablesEffect("SSR"))
        throw std::runtime_error("Disabled effect list wrong");
    const std::array everyEffect{"game.plutoproject", "results.csv", "--disable-effects=all"};
    if (!ProjectBenchmarkOptions::Parse(everyEffect).DisablesEffect("TAA")) throw std::runtime_error("'all' did not disable every effect");
    for (const auto *bad : {"--disable-effects=", "--disable-effects=SSAO,", "--disable-effects=,SSAO"})
    {
        bool rejected = false;
        try { const std::array args{"game.plutoproject", "results.csv", bad}; ProjectBenchmarkOptions::Parse(args); }
        catch (const std::invalid_argument &) { rejected = true; }
        if (!rejected) throw std::runtime_error("Empty disabled effect name accepted");
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
    if (columnSeparators != 8 + ProjectBenchmarkSample::DetailNames.size() || !line.ends_with(",gpu_observation_id"))
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
