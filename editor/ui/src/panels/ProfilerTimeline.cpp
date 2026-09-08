#include "PlutoGE/ui/panels/ProfilerPanel.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <functional>
#include <map>
#include <numeric>
#include <tuple>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr std::array<const char *, 8> CategoryNames{
            "Scripts", "Rendering", "Physics", "Animation", "Audio", "UI", "Wait / Present", "Other"};
        constexpr std::array<ImU32, 8> CategoryColors{
            IM_COL32(98, 174, 205, 255), IM_COL32(173, 189, 53, 255),
            IM_COL32(205, 117, 69, 255), IM_COL32(130, 100, 187, 255),
            IM_COL32(205, 92, 137, 255), IM_COL32(72, 159, 139, 255),
            IM_COL32(208, 178, 49, 255), IM_COL32(113, 119, 129, 255)};
        static_assert(CategoryNames.size() == static_cast<std::size_t>(core::CpuCategory::Count));
        constexpr ImU32 Background = IM_COL32(35, 37, 40, 255);
        constexpr ImU32 Grid = IM_COL32(67, 70, 74, 255);
        constexpr ImU32 Text = IM_COL32(225, 228, 233, 255);

        bool Matches(const std::string &text, const char *filter)
        {
            if (!*filter) return true;
            const std::string_view query(filter);
            return std::search(text.begin(), text.end(), query.begin(), query.end(),
                [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != text.end();
        }

        float TraceDuration(const EditorProfileFrame &frame)
        {
            float end = std::max(0.001f, frame.durationMs);
            for (const auto &sample : frame.samples) end = std::max(end, sample.startMs + sample.durationMs);
            return end;
        }
    }

    void ProfilerPanel::SelectFrame(int index)
    {
        if (index != m_selectedFrame)
        {
            m_selectedSample = -1;
            m_timelineRangeMs = 0.0f;
            m_timelineStartMs = 0.0f;
        }
        m_selectedFrame = index;
    }

    void ProfilerPanel::RenderCaptureControls()
    {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const bool recording = m_profiler->IsRecording();
        ImGui::PushStyleColor(ImGuiCol_Button, recording ? ImVec4(0.65f, 0.19f, 0.20f, 1) : ImVec4(0.28f, 0.30f, 0.33f, 1));
        if (ImGui::Button(recording ? "Stop" : "Record"))
        {
            if (recording) m_profiler->StopCapture();
            else
            {
                m_profiler->StartCapture(static_cast<std::size_t>(m_captureFrameLimit));
                SelectFrame(-1);
                m_followLatest = true;
            }
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { m_profiler->ClearCapture(); SelectFrame(-1); }
        ImGui::SameLine();
        ImGui::Checkbox("Follow latest", &m_followLatest);
        if (availableWidth >= 560) ImGui::SameLine();
        ImGui::SetNextItemWidth(100);
        ImGui::InputInt("Frames", &m_captureFrameLimit);
        m_captureFrameLimit = std::clamp(m_captureFrameLimit, 1, static_cast<int>(EditorProfiler::MaxCaptureFrames));
        ImGui::SameLine();
        if (ImGui::Button("Copy metrics")) CopyMetricsToClipboard();

        const auto &frames = m_profiler->GetCapturedFrames();
        if (m_followLatest && !frames.empty()) SelectFrame(static_cast<int>(frames.size()) - 1);
        ImGui::TextDisabled("CPU Usage  |  %zu captured frames", frames.size());
        if (availableWidth >= 560) ImGui::SameLine();
        ImGui::TextDisabled("|  live %.2f ms / %.1f FPS", m_profiler->GetCurrentFrameTimeMs(), m_profiler->GetAverageFPS());
        if (m_profiler->IsCaptureMemoryLimited())
            ImGui::TextWrapped("Recording stopped at the 64 MiB CPU trace budget. Existing frames are preserved.");
        RenderFrameHistory();
        if (frames.empty())
        {
            ImGui::TextWrapped("Press Record to capture CPU activity. Select a frame in the chart to inspect its calls.");
            return;
        }
        if (ImGui::Button("<")) { m_followLatest = false; SelectFrame(std::max(0, m_selectedFrame - 1)); }
        ImGui::SameLine();
        if (ImGui::Button(">")) { m_followLatest = false; SelectFrame(std::min(static_cast<int>(frames.size()) - 1, m_selectedFrame + 1)); }
        ImGui::SameLine();
        if (ImGui::Button("Slowest"))
        {
            m_followLatest = false;
            SelectFrame(static_cast<int>(std::max_element(frames.begin(), frames.end(),
                [](const auto &a, const auto &b) { return a.durationMs < b.durationMs; }) - frames.begin()));
        }
        ImGui::SameLine();
        if (ImGui::Button("Next hitch"))
        {
            for (std::size_t step = 1; step <= frames.size(); ++step)
            {
                const auto index = static_cast<std::size_t>(m_selectedFrame + static_cast<int>(step)) % frames.size();
                if (frames[index].durationMs >= m_hitchThresholdMs)
                { m_followLatest = false; SelectFrame(static_cast<int>(index)); break; }
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        ImGui::InputFloat("ms##Hitch", &m_hitchThresholdMs, 0, 0, "%.1f");
        if (!std::isfinite(m_hitchThresholdMs)) m_hitchThresholdMs = 33.3f;
        m_hitchThresholdMs = std::max(0.1f, m_hitchThresholdMs);
        int index = std::max(0, m_selectedFrame);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderInt("##Frame", &index, 0, static_cast<int>(frames.size()) - 1, "Frame index %d"))
        { m_followLatest = false; SelectFrame(index); }
    }

    void ProfilerPanel::RenderFrameHistory()
    {
        const auto &frames = m_profiler->GetCapturedFrames();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x), 174.0f);
        const float legendWidth = std::min(150.0f, size.x * 0.30f);
        const float chartX = origin.x + legendWidth;
        const float chartWidth = std::max(1.0f, size.x - legendWidth);
        ImGui::InvisibleButton("CPU history", size);
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), Background);
        for (std::size_t c = 0; c < CategoryNames.size(); ++c)
        {
            const float y = origin.y + 8 + static_cast<float>(c) * 20;
            draw->AddRectFilled(ImVec2(origin.x + 8, y + 3), ImVec2(origin.x + 17, y + 12), CategoryColors[c]);
            draw->PushClipRect(origin, ImVec2(chartX - 2, origin.y + size.y), true);
            draw->AddText(ImVec2(origin.x + 24, y), Text, CategoryNames[c]);
            draw->PopClipRect();
        }
        float maximum = 33.334f;
        for (const auto &frame : frames) maximum = std::max(maximum, frame.durationMs * 1.05f);
        if (!frames.empty())
        {
            const float width = chartWidth / static_cast<float>(frames.size());
            draw->PushClipRect(ImVec2(chartX, origin.y), ImVec2(origin.x + size.x, origin.y + size.y), true);
            for (std::size_t i = 0; i < frames.size(); ++i)
            {
                float y = origin.y + size.y;
                for (std::size_t c = 0; c < CategoryNames.size(); ++c)
                {
                    const float height = frames[i].categoryMs[c] / maximum * size.y;
                    draw->AddRectFilled(ImVec2(chartX + width * static_cast<float>(i), y - height),
                        ImVec2(chartX + width * static_cast<float>(i + 1), y), CategoryColors[c]);
                    y -= height;
                }
            }
            if (m_selectedFrame >= 0)
            {
                const float x = chartX + width * (static_cast<float>(m_selectedFrame) + 0.5f);
                draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y), IM_COL32_WHITE, 2);
            }
            draw->PopClipRect();
            if ((ImGui::IsItemHovered() || ImGui::IsItemActive()) && ImGui::GetIO().MousePos.x >= chartX)
            {
                const int index = std::clamp(static_cast<int>((ImGui::GetIO().MousePos.x - chartX) / width), 0, static_cast<int>(frames.size()) - 1);
                ImGui::BeginTooltip();
                ImGui::Text("Frame %llu  |  %.3f ms", static_cast<unsigned long long>(frames[index].sequence), frames[index].durationMs);
                for (std::size_t c = 0; c < CategoryNames.size(); ++c)
                    ImGui::Text("%s: %.3f ms", CategoryNames[c], frames[index].categoryMs[c]);
                ImGui::EndTooltip();
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) { m_followLatest = false; SelectFrame(index); }
            }
        }
        for (const float budget : {16.667f, 33.333f})
        {
            const float y = origin.y + size.y * (1 - budget / maximum);
            draw->AddLine(ImVec2(chartX, y), ImVec2(origin.x + size.x, y), IM_COL32(225, 225, 225, 115));
            draw->AddText(ImVec2(chartX + 4, y + 2), Text, budget < 20 ? "16.7 ms (60 FPS)" : "33.3 ms (30 FPS)");
        }
    }

    void ProfilerPanel::FocusSample(const EditorProfileFrame &frame)
    {
        if (m_selectedSample < 0 || static_cast<std::size_t>(m_selectedSample) >= frame.samples.size()) return;
        const auto &sample = frame.samples[m_selectedSample];
        const float duration = TraceDuration(frame);
        m_timelineRangeMs = std::min(duration, std::max(0.01f, sample.durationMs * 1.15f));
        m_timelineStartMs = std::clamp(sample.startMs - m_timelineRangeMs * 0.05f, 0.0f, duration - m_timelineRangeMs);
    }

    void ProfilerPanel::RenderProfilerWorkspace()
    {
        const auto *frame = GetSelectedFrame();
        if (!frame) return;
        ImGui::Separator();
        ImGui::Text("Frame %llu   CPU %.3f ms   %zu samples", static_cast<unsigned long long>(frame->sequence), frame->durationMs, frame->samples.size());
        if (m_selectedFrame > 0)
        {
            if (ImGui::GetContentRegionAvail().x >= 560) ImGui::SameLine();
            ImGui::TextDisabled("(%+.3f ms)", frame->durationMs - m_profiler->GetCapturedFrames()[m_selectedFrame - 1].durationMs);
        }
        if (frame->droppedSamples)
            ImGui::TextWrapped("%u samples omitted at the per-frame limit. Parent self times include omitted work.", frame->droppedSamples);
        if (frame->samples.empty())
            ImGui::TextWrapped("No CPU trace for this frame. A recording begins tracing at the next frame boundary; aggregate metrics remain available below.");
        if (ImGui::BeginTabBar("Profiler views"))
        {
            if (ImGui::BeginTabItem("Timeline")) { RenderCpuTimeline(*frame); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Hierarchy")) { RenderCpuHierarchy(*frame); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("GPU observations"))
            {
                ImGui::TextWrapped("GPU queries are asynchronous. These durations may come from earlier CPU frames; execution intervals and a render-thread track are not available.");
                const auto show = [](const auto &values)
                {
                    for (const auto &value : values)
                        if (value.hasResult) ImGui::Text("%s: %.3f ms", value.name.c_str(), value.gpuTimeMs);
                        else ImGui::Text("%s: pending", value.name.c_str());
                };
                show(frame->gpuPasses); show(frame->postProcessGpuPasses); show(frame->gpuDetails);
                for (const auto &scope : frame->timing.rhiTimingStats.gpuScopes)
                    ImGui::Text("%s: %.3f ms GPU / %.3f ms CPU recording", scope.name.c_str(), scope.milliseconds, scope.cpuMilliseconds);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        RenderSampleDetails(*frame);
    }

    void ProfilerPanel::RenderCpuTimeline(const EditorProfileFrame &frame)
    {
        const float duration = TraceDuration(frame);
        if (m_timelineRangeMs <= 0) m_timelineRangeMs = duration;
        if (ImGui::Button("Fit frame")) { m_timelineStartMs = 0; m_timelineRangeMs = duration; }
        ImGui::SameLine();
        if (ImGui::Button("Focus sample")) { m_followLatest = false; FocusSample(frame); }
        if (ImGui::GetContentRegionAvail().x >= 660) ImGui::SameLine();
        ImGui::TextWrapped("Wheel: zoom  |  middle drag: pan  |  double click: focus");
        ImGui::BeginChild("CPU thread timeline", ImVec2(0, 290), true, ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float labelWidth = std::min(142.0f, ImGui::GetContentRegionAvail().x * 0.25f);
        const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x - labelWidth);
        int maxDepth = 0;
        for (const auto &sample : frame.samples) maxDepth = std::max(maxDepth, sample.depth);
        constexpr float rowHeight = 22.0f;
        const float height = std::max(230.0f, 30 + (maxDepth + 1) * rowHeight);
        ImGui::InvisibleButton("CPU samples", ImVec2(width + labelWidth, height), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float x0 = origin.x + labelWidth;
        auto &io = ImGui::GetIO();
        if (hovered && io.MouseWheel != 0 && io.MousePos.x >= x0)
        {
            m_followLatest = false;
            const float anchor = std::clamp((io.MousePos.x - x0) / width, 0.0f, 1.0f);
            const float time = m_timelineStartMs + anchor * m_timelineRangeMs;
            m_timelineRangeMs = std::clamp(m_timelineRangeMs * std::pow(0.8f, io.MouseWheel), std::min(0.01f, duration), duration);
            m_timelineStartMs = time - anchor * m_timelineRangeMs;
        }
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        {
            m_followLatest = false;
            m_timelineStartMs -= io.MouseDelta.x / width * m_timelineRangeMs;
        }
        m_timelineStartMs = std::clamp(m_timelineStartMs, 0.0f, std::max(0.0f, duration - m_timelineRangeMs));
        auto *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(x0 + width, origin.y + height), Background);
        draw->AddText(ImVec2(origin.x + 6, origin.y + 34), Text, "Main Thread");
        draw->PushClipRect(ImVec2(x0, origin.y), ImVec2(x0 + width, origin.y + height), true);
        const float rawStep = m_timelineRangeMs / std::max(1.0f, width / 85.0f);
        const float magnitude = std::pow(10.0f, std::floor(std::log10(rawStep)));
        const float fraction = rawStep / magnitude;
        const float step = (fraction <= 1 ? 1 : fraction <= 2 ? 2 : fraction <= 5 ? 5 : 10) * magnitude;
        for (float time = std::floor(m_timelineStartMs / step) * step; time <= m_timelineStartMs + m_timelineRangeMs; time += step)
        {
            const float x = x0 + (time - m_timelineStartMs) / m_timelineRangeMs * width;
            draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), Grid);
            char label[48]; std::snprintf(label, sizeof(label), "%.3g ms", time);
            draw->AddText(ImVec2(x + 3, origin.y + 3), Text, label);
        }
        int hoveredSample = -1;
        for (std::size_t i = 0; i < frame.samples.size(); ++i)
        {
            const auto &sample = frame.samples[i];
            if (sample.startMs + sample.durationMs < m_timelineStartMs || sample.startMs > m_timelineStartMs + m_timelineRangeMs) continue;
            const float left = x0 + (sample.startMs - m_timelineStartMs) / m_timelineRangeMs * width;
            const float right = std::max(left + 1, x0 + (sample.startMs + sample.durationMs - m_timelineStartMs) / m_timelineRangeMs * width);
            const float y = origin.y + 28 + sample.depth * rowHeight;
            const ImVec2 a(std::max(x0, left), y), b(std::min(x0 + width, right), y + rowHeight - 2);
            draw->AddRectFilled(a, b, CategoryColors[static_cast<std::size_t>(sample.category)]);
            if (static_cast<int>(i) == m_selectedSample) draw->AddRect(a, b, IM_COL32_WHITE, 0.0f, 2.0f);
            if (b.x - a.x > 24)
            {
                draw->PushClipRect(ImVec2(a.x + 3, a.y), ImVec2(b.x - 2, b.y), true);
                draw->AddText(ImVec2(a.x + 4, a.y + 2), IM_COL32(20, 25, 28, 255), sample.name.c_str());
                draw->PopClipRect();
            }
            if (hovered && io.MousePos.x >= a.x && io.MousePos.x <= b.x && io.MousePos.y >= a.y && io.MousePos.y <= b.y)
                hoveredSample = static_cast<int>(i);
        }
        draw->PopClipRect();
        if (hoveredSample >= 0)
        {
            const auto &sample = frame.samples[hoveredSample];
            ImGui::SetTooltip("%s\n%s\nStart %.3f ms  |  Duration %.3f ms", sample.name.c_str(), sample.context.c_str(), sample.startMs, sample.durationMs);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { m_followLatest = false; m_selectedSample = hoveredSample; }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) FocusSample(frame);
        }
        ImGui::EndChild();
    }

    void ProfilerPanel::RenderCpuHierarchy(const EditorProfileFrame &frame)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Sample search", "Search sample names or entities...", m_sampleSearch, sizeof(m_sampleSearch));
        const auto self = core::CpuSelfTimes(frame.samples);
        struct Node { int sample; int calls = 0; bool matches = false; float total = 0, own = 0; std::vector<int> children; };
        std::vector<Node> nodes;
        std::vector<int> mapping(frame.samples.size(), -1), roots;
        std::map<std::tuple<int, std::string, core::CpuCategory>, int> lookup;
        for (std::size_t i = 0; i < frame.samples.size(); ++i)
        {
            const auto &sample = frame.samples[i];
            const int parent = sample.parent < 0 ? -1 : mapping[static_cast<std::size_t>(sample.parent)];
            const auto key = std::make_tuple(parent, sample.name, sample.category);
            auto [entry, inserted] = lookup.try_emplace(key, static_cast<int>(nodes.size()));
            const int index = entry->second;
            if (inserted)
            {
                nodes.push_back({static_cast<int>(i)});
                if (parent < 0) roots.push_back(index); else nodes[parent].children.push_back(index);
            }
            mapping[i] = index;
            auto &node = nodes[index];
            if (Matches(sample.name, m_sampleSearch) || Matches(sample.context, m_sampleSearch))
            {
                if (!node.matches) node.sample = static_cast<int>(i);
                node.matches = true;
            }
            ++node.calls; node.total += sample.durationMs; node.own += self[i];
        }
        const auto sort = [&](auto &list) { std::stable_sort(list.begin(), list.end(), [&](int a, int b) { return nodes[a].total > nodes[b].total; }); };
        sort(roots); for (auto &node : nodes) sort(node.children);
        if (ImGui::BeginTable("CPU hierarchy", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 290)))
        {
            ImGui::TableSetupColumn("Sample", ImGuiTableColumnFlags_WidthStretch, 4);
            ImGui::TableSetupColumn("Total ms"); ImGui::TableSetupColumn("Self ms");
            ImGui::TableSetupColumn("Calls"); ImGui::TableSetupColumn("Frame %");
            ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
            std::function<void(int)> row = [&](int index)
            {
                const auto &node = nodes[index];
                const auto &sample = frame.samples[node.sample];
                const bool filtered = *m_sampleSearch != 0;
                if (filtered && !node.matches) return;
                ImGui::PushID(index);
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                const auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow
                    | (node.children.empty() || filtered ? ImGuiTreeNodeFlags_Leaf : 0)
                    | (sample.depth < 2 ? ImGuiTreeNodeFlags_DefaultOpen : 0)
                    | (m_selectedSample >= 0 && static_cast<std::size_t>(m_selectedSample) < mapping.size() && mapping[m_selectedSample] == index ? ImGuiTreeNodeFlags_Selected : 0);
                const bool open = ImGui::TreeNodeEx("##Sample", flags, "%s", sample.name.c_str());
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) { m_followLatest = false; m_selectedSample = node.sample; }
                ImGui::TableNextColumn(); ImGui::Text("%.3f", node.total);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", node.own);
                ImGui::TableNextColumn(); ImGui::Text("%d", node.calls);
                ImGui::TableNextColumn(); ImGui::Text("%.1f", frame.durationMs > 0 ? node.total / frame.durationMs * 100 : 0);
                if (open)
                {
                    if (!filtered) for (int child : node.children) row(child);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };
            if (*m_sampleSearch) for (std::size_t i = 0; i < nodes.size(); ++i) row(static_cast<int>(i));
            else for (int root : roots) row(root);
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Grouped by call path, largest total first. Selecting a group selects its first call.");
    }

    void ProfilerPanel::RenderSampleDetails(const EditorProfileFrame &frame)
    {
        if (m_selectedSample < 0 || static_cast<std::size_t>(m_selectedSample) >= frame.samples.size())
        { ImGui::TextDisabled("Select a CPU sample to inspect its duration and context."); return; }
        const auto &sample = frame.samples[m_selectedSample];
        const auto self = core::CpuSelfTimes(frame.samples);
        ImGui::Separator();
        ImGui::TextUnformatted(sample.name.c_str());
        ImGui::TextWrapped("%s  |  Total %.3f ms  |  Self %.3f ms  |  Start %.3f ms", CategoryNames[static_cast<std::size_t>(sample.category)], sample.durationMs, self[m_selectedSample], sample.startMs);
        if (!sample.context.empty()) ImGui::TextWrapped("Context: %s", sample.context.c_str());
        if (ImGui::Button("Copy sample"))
        {
            const std::string report = sample.name + "\nContext: " + sample.context + "\nFrame: " + std::to_string(frame.sequence)
                + "\nStart ms: " + std::to_string(sample.startMs) + "\nTotal ms: " + std::to_string(sample.durationMs)
                + "\nSelf ms: " + std::to_string(self[m_selectedSample]);
            ImGui::SetClipboardText(report.c_str());
        }
    }
}
