// RenderGraph synchronisation: barriers between conflicting buffer accesses, async compute forks and joins, and the
// inline fallback without an async queue. A fake context records what the graph asks of it.
#include "PlutoGE/render/RenderGraph.h"
#include "LightClusterGrid.h"

#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace PlutoGE::render;
    using rhi::BarrierAccess;

    std::string Describe(BarrierAccess access)
    {
        static const std::pair<BarrierAccess, const char *> names[] = {
            {BarrierAccess::IndirectCommandRead, "indirect"}, {BarrierAccess::VertexInputRead, "vertex-input"},
            {BarrierAccess::VertexShaderRead, "vs-read"}, {BarrierAccess::FragmentShaderRead, "fs-read"},
            {BarrierAccess::FragmentShaderWrite, "fs-write"}, {BarrierAccess::ComputeShaderRead, "cs-read"},
            {BarrierAccess::ComputeShaderWrite, "cs-write"}, {BarrierAccess::TransferRead, "transfer-read"},
            {BarrierAccess::TransferWrite, "transfer-write"}};
        std::string result;
        for (const auto &[flag, name] : names)
            if (rhi::HasAny(access, flag))
                result += (result.empty() ? "" : "+") + std::string(name);
        return result.empty() ? "none" : result;
    }

    class FakeContext final : public rhi::ICommandContext
    {
    public:
        FakeContext(std::vector<std::string> &log, std::string name, FakeContext *async = nullptr)
            : m_log(log), m_name(std::move(name)), m_async(async) {}
        void BeginRendering(const rhi::RenderingInfo &) override {}
        void EndRendering() override {}
        void SetViewport(const rhi::Viewport &) override {}
        void SetScissor(const rhi::Scissor &) override {}
        void BindPipeline(rhi::PipelineHandle) override {}
        void BindVertexBuffer(rhi::BufferHandle, std::size_t) override {}
        void BindIndexBuffer(rhi::BufferHandle, rhi::Format, std::size_t) override {}
        void BindUniformBuffer(std::uint32_t, rhi::BufferHandle) override {}
        void BindTexture(std::uint32_t, rhi::TextureHandle, rhi::SamplerHandle) override {}
        void Draw(std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::int32_t) override {}
        void PipelineBarrier(BarrierAccess before, BarrierAccess after) override
        {
            m_log.push_back(m_name + " barrier " + Describe(before) + " -> " + Describe(after));
        }
        [[nodiscard]] bool SupportsAsyncCompute() const noexcept override { return m_async != nullptr; }
        rhi::ICommandContext &BeginAsyncCompute() override
        {
            if (!m_async) return *this;
            m_log.push_back("fork");
            return *m_async;
        }
        void EndAsyncCompute(BarrierAccess consumers) override
        {
            if (!m_async) { ICommandContext::EndAsyncCompute(consumers); return; }
            m_log.push_back("join at " + Describe(consumers));
        }
        [[nodiscard]] const std::string &Name() const noexcept { return m_name; }

    private:
        std::vector<std::string> &m_log;
        std::string m_name;
        FakeContext *m_async;
    };

    void Require(bool value, const std::string &message)
    {
        if (!value) throw std::runtime_error(message);
    }

    void RequireLog(const std::vector<std::string> &log, const std::vector<std::string> &expected, const char *label)
    {
        if (log == expected) return;
        std::string message = std::string(label) + ": unexpected graph recording:\n";
        for (const auto &entry : log) message += "  " + entry + "\n";
        message += "expected:\n";
        for (const auto &entry : expected) message += "  " + entry + "\n";
        throw std::runtime_error(message);
    }

    RenderGraph::Record Records(std::vector<std::string> &log, std::string name)
    {
        return [&log, name](rhi::ICommandContext &context)
        { log.push_back(name + "@" + static_cast<FakeContext &>(context).Name()); };
    }

    const rhi::BufferHandle X{1, 1}, Y{2, 1}, Z{3, 1};

    void CheckInlineFallback()
    {
        std::vector<std::string> log;
        FakeContext graphics(log, "graphics");
        RenderGraph graph;
        graph.AddPass({"A", PassQueue::AsyncCompute, {{X, BarrierAccess::ComputeShaderWrite}}, Records(log, "A")});
        graph.AddPass({"B", PassQueue::Graphics, {{X, BarrierAccess::FragmentShaderRead}}, Records(log, "B")});
        const auto stats = graph.Execute(graphics);
        RequireLog(log, {"A@graphics", "graphics barrier cs-write -> fs-read", "B@graphics"}, "Inline fallback");
        Require(stats.asyncSections == 0 && stats.barriers == 1, "Inline fallback statistics");
    }

    void CheckForkAndJoin()
    {
        std::vector<std::string> log;
        FakeContext async(log, "async");
        FakeContext graphics(log, "graphics", &async);
        RenderGraph graph;
        graph.AddPass({"A", PassQueue::AsyncCompute, {{X, BarrierAccess::ComputeShaderWrite}}, Records(log, "A")});
        // Independent graphics work overlaps the section.
        graph.AddPass({"G1", PassQueue::Graphics, {{Y, BarrierAccess::ComputeShaderWrite}}, Records(log, "G1")});
        graph.AddPass({"G2", PassQueue::Graphics,
                       {{X, BarrierAccess::FragmentShaderRead}, {Y, BarrierAccess::VertexShaderRead}}, Records(log, "G2")});
        graph.AddPass({"G3", PassQueue::Graphics, {{X, BarrierAccess::ComputeShaderRead}}, Records(log, "G3")});
        const auto stats = graph.Execute(graphics);
        // The join waits for every later use of the section's buffers, and G1's write needs its own barrier.
        RequireLog(log, {"fork", "A@async", "G1@graphics", "join at fs-read+cs-read",
                         "graphics barrier cs-write -> vs-read", "G2@graphics", "G3@graphics"}, "Fork and join");
        Require(stats.asyncSections == 1 && stats.joins == 1 && stats.barriers == 1, "Fork and join statistics");
    }

    void CheckSectionRestart()
    {
        std::vector<std::string> log;
        FakeContext async(log, "async");
        FakeContext graphics(log, "graphics", &async);
        RenderGraph graph;
        graph.AddPass({"A1", PassQueue::AsyncCompute, {{X, BarrierAccess::ComputeShaderWrite}}, Records(log, "A1")});
        graph.AddPass({"G", PassQueue::Graphics, {{Z, BarrierAccess::FragmentShaderWrite}}, Records(log, "G")});
        // Reads what graphics wrote inside the open section: the section must end and a new one follow G.
        graph.AddPass({"A2", PassQueue::AsyncCompute,
                       {{Z, BarrierAccess::ComputeShaderRead}, {X, BarrierAccess::ComputeShaderRead}}, Records(log, "A2")});
        const auto stats = graph.Execute(graphics);
        // No later graphics pass uses the first section's buffers; the second section ends with the graph and
        // joins for every stage.
        RequireLog(log, {"fork", "A1@async", "G@graphics", "join at none", "fork", "A2@async",
                         "join at indirect+vertex-input+vs-read+fs-read+fs-write+cs-read+cs-write+transfer-read+transfer-write"},
                   "Section restart");
        Require(stats.asyncSections == 2 && stats.joins == 2, "Section restart statistics");
    }

    void CheckGraphicsHazards()
    {
        std::vector<std::string> log;
        FakeContext graphics(log, "graphics");
        RenderGraph graph;
        graph.AddPass({"R1", PassQueue::Graphics, {{X, BarrierAccess::FragmentShaderRead}}, Records(log, "R1")});
        graph.AddPass({"R2", PassQueue::Graphics, {{X, BarrierAccess::VertexShaderRead}}, Records(log, "R2")});
        graph.AddPass({"W1", PassQueue::Graphics, {{X, BarrierAccess::ComputeShaderWrite}}, Records(log, "W1")});
        graph.AddPass({"W2", PassQueue::Graphics, {{X, BarrierAccess::TransferWrite}}, Records(log, "W2")});
        graph.Execute(graphics);
        // Reads after reads need nothing; a write after both reads waits for both; writes stay ordered.
        RequireLog(log, {"R1@graphics", "R2@graphics", "graphics barrier vs-read+fs-read -> cs-write", "W1@graphics",
                         "graphics barrier cs-write -> transfer-write", "W2@graphics"}, "Graphics hazards");
    }

    void CheckLightClusterFit()
    {
        const auto view = glm::lookAtRH(glm::vec3(0, 2, 5), glm::vec3(0), glm::vec3(0, 1, 0));
        const auto perspective = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.5f, 200.0f, 0.1f);
        const std::array<LocalLightBounds, 2> lights{{{{0, 0, 0}, 2.0f}, {{0, 0, -20}, 4.0f}}};
        std::array<glm::vec4, 2> spheres{};
        const auto fit = LightClusterGrid::Fit(view, perspective, lights, spheres);
        Require(fit.shading.Enabled(), "Light cluster fit was disabled for a rigid view");
        const auto depth = [&](glm::vec3 position) { return -(view * glm::vec4(position, 1.0f)).z; };
        const float near = depth(lights[0].position) - lights[0].range, far = depth(lights[1].position) + lights[1].range;
        Require(std::abs(fit.shading.depth.x - near) < 0.02f && std::abs(fit.shading.depth.z - far) < 0.02f,
                "Light cluster slices do not span the lights' depth range");
        Require(spheres[0].w > 2.0f && spheres[1].w > 4.0f, "Light cluster spheres were not inflated");
        Require(fit.sliceZeroDepth == 0.0f, "Perspective clusters must start at the eye");
        const auto orthographic = glm::orthoRH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 50.0f, -50.0f);
        const std::array<LocalLightBounds, 1> behind{{{{0, 2, 10}, 3.0f}}};
        Require(LightClusterGrid::Fit(view, orthographic, behind).sliceZeroDepth < 0.0f,
                "Orthographic clusters must reach lights behind the eye");
        Require(!LightClusterGrid::Fit(glm::scale(view, glm::vec3(2)), perspective, lights).shading.Enabled(),
                "A scaling view must evaluate every light");
        const std::array<LocalLightBounds, 1> unlit{{{{0, 0, 0}, 0.0f}}};
        Require(!LightClusterGrid::Fit(view, perspective, unlit).shading.Enabled(), "Lights without reach were clustered");
    }
}

int main()
{
    try
    {
        CheckInlineFallback();
        CheckForkAndJoin();
        CheckSectionRestart();
        CheckGraphicsHazards();
        CheckLightClusterFit();
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << "\n";
        return 1;
    }
    std::cout << "Render graph barriers, async forks and joins, and light cluster fitting passed\n";
    return 0;
}
