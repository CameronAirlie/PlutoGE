#include "PlutoGE/core/CpuTrace.h"
#include <iostream>
#include <stdexcept>
#include <chrono>

namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
}

int main()
{
    using namespace PlutoGE::core;
    using namespace std::chrono;
    try
    {
        const CpuTrace::Clock::time_point zero{};
        {
            CpuTrace trace(true, zero);
            const int root = trace.Begin("Root", CpuCategory::Other, {}, zero);
            const int child = trace.Begin("Child", CpuCategory::Scripts, "Entity", zero + milliseconds(1));
            const int grandchild = trace.Begin("Grandchild", CpuCategory::Physics, {}, zero + milliseconds(2));
            trace.End(grandchild, zero + milliseconds(4));
            trace.End(child, zero + milliseconds(7));
            const int sibling = trace.Begin("Sibling", CpuCategory::UI, {}, zero + milliseconds(8));
            trace.End(sibling, zero + milliseconds(9));
            trace.End(root, zero + milliseconds(10));
            auto samples = trace.TakeSamples();
            Require(samples.size() == 4, "All samples must be captured");
            Require(samples[2].parent == 1 && samples[2].depth == 2, "Nested parentage must be retained");
            Require(samples[3].parent == 0 && samples[3].depth == 1, "Ending children must restore the parent");
            Require(samples[1].startMs == 1 && samples[1].durationMs == 6, "Timestamps must remain relative to frame origin");
            const auto self = CpuSelfTimes(samples);
            Require(self[0] == 3 && self[1] == 4 && self[2] == 2 && self[3] == 1, "Exclusive times must subtract direct children only");
            Require(trace.Begin("After take", CpuCategory::Other) == -1, "Taking a trace must disable subsequent samples");
        }
        Require(CpuTrace::current == nullptr, "Recorder must detach on destruction");
        {
            CpuTrace trace(true);
            try { CpuScope root("Exception scope"); CpuScope child("Nested"); throw std::runtime_error("test"); }
            catch (const std::runtime_error &) {}
            { CpuScope sibling("After exception"); sibling.End(); sibling.End(); }
            const auto samples = trace.TakeSamples();
            Require(samples.size() == 3 && samples[2].parent == -1, "RAII must restore nesting through exceptions and repeated End");
        }
        {
            CpuTrace outer(true);
            { CpuTrace disabled(false); CpuScope sample("Disabled"); Require(CpuTrace::current == nullptr, "Disabled recording must bypass scopes"); }
            Require(CpuTrace::current == &outer, "Nested recorder must restore the previous recorder");
            { CpuScope sample("Enabled"); }
            Require(outer.TakeSamples().size() == 1, "Disabled scope must not leak into outer recorder");
        }
        {
            CpuTrace trace(true);
            for (std::size_t i = 0; i < CpuTrace::MaxSamples; ++i)
            { const int index = trace.Begin(std::string(300, 'x'), CpuCategory::Other); trace.End(index); }
            Require(trace.Begin("Overflow", CpuCategory::Other) == -1, "Trace must cap sample count");
            Require(trace.GetDroppedCount() == 1, "Overflow must be reported");
            const auto samples = trace.TakeSamples();
            Require(samples.size() == CpuTrace::MaxSamples && samples[0].name.size() == CpuTrace::MaxLabelLength, "Trace labels and storage must be bounded");
        }
        std::cout << "CPU trace tests passed\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
