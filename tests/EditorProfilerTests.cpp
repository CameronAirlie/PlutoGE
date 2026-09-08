#include "PlutoGE/ui/EditorProfiler.h"

#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}

int main()
{
    using namespace PlutoGE::ui;
    try
    {
        EditorProfiler profiler;
        EditorProfileFrame frame;
        frame.sequence = 42;
        frame.durationMs = 80.0f;
        frame.timing.sceneUpdateMs = 60.0f;
        frame.timing.componentTimings.emplace_back();
        frame.timing.componentTimings.back().name = "Expensive component";
        frame.timing.componentTimings.back().callCount = 17;
        profiler.RecordFrame(frame);
        Require(profiler.GetCapturedFrames().empty(), "Idle profiler must not capture");
        profiler.StartCapture(2);
        profiler.RecordFrame(frame);
        frame.timing.componentTimings.back().name = "Changed";
        frame.timing.sceneUpdateMs = 1.0f;
        frame.durationMs = 12.0f;
        ++frame.sequence;
        profiler.RecordFrame(frame);
        Require(!profiler.IsRecording(), "Capture must stop at its limit");
        profiler.RecordFrame(frame);
        const auto &frames = profiler.GetCapturedFrames();
        Require(frames.size() == 2, "Completed capture must remain bounded");
        Require(frames[0].sequence == 42 && frames[1].sequence == 43, "Frame order must be retained");
        Require(frames[0].timing.componentTimings[0].name == "Expensive component", "Snapshot must own dynamic data");
        Require(frames[0].timing.componentTimings[0].callCount == 17, "Capture must retain work counts");
        const auto &saved = frames[0];
        profiler.AddFrameSample(2.0f);
        const auto report = profiler.BuildMetricsReport(saved.panels, saved.timing, saved.cpuPasses,
            saved.renderer, saved.gpuPasses, saved.postProcessGpuPasses, saved.gpuDetails,
            saved.totalCpuMs, saved.totalGpuMs, saved.lighting, saved.durationMs);
        Require(report.find("Captured CPU frame: 80.00 ms") != std::string::npos, "Report must use captured duration");
        Require(report.find("Scene update: 60.00 ms") != std::string::npos, "Report must use captured metrics");
        Require(report.find("Session peak") == std::string::npos, "Capture report must not mix live session metrics");
        profiler.StartCapture(0);
        frame.durationMs = std::numeric_limits<float>::quiet_NaN();
        profiler.RecordFrame(frame);
        Require(profiler.GetCapturedFrames().empty(), "Invalid duration must be ignored");
        frame.durationMs = 1.0f;
        profiler.RecordFrame(frame);
        Require(!profiler.IsRecording(), "Zero frame limit must clamp to one");
        profiler.StartCapture(EditorProfiler::MaxCaptureFrames + 10);
        for (std::size_t i = 0; i < EditorProfiler::MaxCaptureFrames + 1; ++i)
            profiler.RecordFrame(frame);
        Require(profiler.GetCapturedFrames().size() == EditorProfiler::MaxCaptureFrames, "Maximum capture limit must be enforced");
        profiler.StartCapture(10);
        profiler.RecordFrame(frame);
        profiler.StopCapture();
        profiler.RecordFrame(frame);
        Require(profiler.GetCapturedFrames().size() == 1, "Manual stop must preserve capture");
        profiler.ClearCapture();
        Require(!profiler.IsRecording() && profiler.GetCapturedFrames().empty(), "Clear must stop and empty capture");
        profiler.AddFrameSample(-1.0f);
        profiler.AddFrameSample(std::numeric_limits<float>::infinity());
        Require(profiler.GetSampleCount() == 1, "Invalid samples must not contaminate live history");
        profiler.StartCapture(1);
        frame.samples = {{"Root", "", 0.0f, 10.0f, -1, 0, PlutoGE::core::CpuCategory::Other},
                         {"Script", "Player", 1.0f, 6.0f, 0, 1, PlutoGE::core::CpuCategory::Scripts}};
        frame.durationMs = 10.0f;
        profiler.RecordFrame(frame);
        const auto &categories = profiler.GetCapturedFrames()[0].categoryMs;
        Require(categories[0] == 6.0f && categories[7] == 4.0f, "Stacked categories must use exclusive time");
        profiler.StartCapture(1);
        frame.samples.clear();
        frame.samples.reserve(EditorProfiler::MaxTraceBytes / sizeof(PlutoGE::core::CpuSample) + 1);
        profiler.RecordFrame(std::move(frame));
        Require(profiler.IsCaptureMemoryLimited() && !profiler.IsRecording(), "Trace memory budget must stop recording");
        Require(profiler.GetCapturedFrames().empty(), "An oversized trace must not be retained");
        std::cout << "Editor profiler tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
