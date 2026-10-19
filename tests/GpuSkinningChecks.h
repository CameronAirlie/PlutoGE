#pragma once
#include "../engine/render/src/RhiGpuSkinning.h"
#include "ReferenceSkinning.h"
#include <cstring>
#include <cmath>
#include <iostream>
#include <limits>
#include <functional>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

template<class Device>
void CheckGpuSkinning(Device &device, const PlutoGE::render::rhi::ComputePipelineDescriptor::ShaderCode &shader,
                      const std::function<void()> &synchronizeReadback = {})
{
    using namespace PlutoGE::render;
    RhiGpuSkinning kernel;
    if (!kernel.Initialize(device,shader)) throw std::runtime_error("GPU skinning shader unavailable");
    std::vector<MeshVertexData> vertices(257);
    for (std::size_t i = 0; i < vertices.size(); ++i)
    {
        auto &v = vertices[i];
        v.position = {.01f * float(i),-.3f,.7f}; v.normal = {.2f,.8f,.4f};
        v.tangent = {.7f,-.2f,.1f,i%2 ? -1.f : 1.f};
        v.uv = {.2f,.7f}; v.uv2 = {.8f,.1f};
        v.joints = {0,1,-1,999}; v.weights = {.3f,.7f,2,1};
        if (i%7 == 0) v.weights = {0,0,0,0};
        if (i%7 == 1) v.weights = {2,3,0,0};
        if (i%7 == 2) v.weights[0] = std::numeric_limits<float>::quiet_NaN();
        if (i%7 == 3) { v.normal = {0,0,0}; v.tangent = {0,0,0,0}; }
        if (i%7 == 4) v.weights = {.00001f,0,0,0};
    }
    // The kernel reads a packed source; the CPU reference sees the same values.
    std::vector<MeshVertexData> quantized;
    for (const auto &vertex : vertices) quantized.push_back(RhiGpuSkinning::QuantizeSource(vertex));

    // B deforms through a reversed layout so slot order differs from source order.
    SkinningLodLayout reversed;
    for (std::size_t i = vertices.size(); i-- > 0;) reversed.sourceVertexOrder.push_back(static_cast<std::uint32_t>(i));
    reversed.levelVertexCounts = {static_cast<std::uint32_t>(vertices.size()), 193};
    std::vector<MeshVertexData> reversedQuantized(quantized.rbegin(), quantized.rend());

    const auto sourceA = kernel.CreateSource(vertices), sourceB = kernel.CreateSource(vertices,reversed);
    auto stateA = kernel.CreateState(sourceA), stateB = kernel.CreateState(sourceB);
    rhi::Buffer outputA(device,device.CreateBuffer({vertices.size()*sizeof(BasicVertex),rhi::BufferUsage::VertexStorage,"Skinning oracle A"}));
    rhi::Buffer outputB(device,device.CreateBuffer({vertices.size()*sizeof(BasicVertex),rhi::BufferUsage::VertexStorage,"Skinning oracle B"}));
    auto &commands = device.GetImmediateContext();
    int completed = 0;
    std::vector<BasicVertex> lastA, lastB;
    std::size_t lastJointsA = 0, lastJointsB = 0;
    for (unsigned frame = 0; frame < 8; ++frame)
    {
        std::vector<glm::mat4> poseA{glm::rotate(glm::mat4(1),.07f*frame,glm::normalize(glm::vec3(1,2,3))),
                                     glm::scale(glm::mat4(1),glm::vec3(frame%2 ? -2.f : 2.f,.5f,1.5f))};
        std::vector<glm::mat4> poseB{glm::translate(glm::mat4(1),glm::vec3(.1f*frame,.3f,0)),glm::mat4(1)};
        if (frame == 6) poseA.assign(2,glm::mat4(0));
        if (frame == 7) { poseA.push_back(glm::mat4(1)); poseB.push_back(glm::mat4(1)); }
        // B covers only its coarse prefix for frames 3-6. On frame 6 a later
        // batch (as a finer view would) extends it to the full stream: the tail
        // restarts history because it is stale, while the prefix keeps its own.
        const auto prefixB = reversed.VertexCount(1), fullB = reversed.VertexCount(0);
        const auto countB = frame >= 3 && frame <= 6 ? prefixB : fullB;
        const bool growB = frame == 6;
        const bool historyA = frame > 0 && lastJointsA == poseA.size();
        const bool historyB = frame > 0 && lastJointsB == poseB.size();
        kernel.Queue(*stateA,poseA,0,sourceA->vertexCount,historyA);
        kernel.Queue(*stateB,poseB,0,countB,historyB);

        auto expectedA = ReferenceSkinRhiVertices(quantized,poseA,historyA ? std::span<const BasicVertex>(lastA) : std::span<const BasicVertex>{});
        auto expectedB = ReferenceSkinRhiVertices(reversedQuantized,poseB,historyB ? std::span<const BasicVertex>(lastB) : std::span<const BasicVertex>{});
        const auto restartedB = ReferenceSkinRhiVertices(reversedQuantized,poseB);
        // Slots beyond B's deformed range keep last frame's output; grown slots restart history.
        for (std::size_t i = countB; i < expectedB.size(); ++i)
            expectedB[i] = growB || lastB.empty() ? restartedB[i] : lastB[i];

        const auto check = [&](rhi::BufferHandle output, const std::vector<BasicVertex> &expected, std::size_t count)
        {
            if (!commands.QueueBufferReadback(output,expected.size()*sizeof(BasicVertex),[&,expected,count](std::span<const std::byte> bytes) {
                if (bytes.size() != expected.size()*sizeof(BasicVertex)) throw std::runtime_error("GPU skinning readback size changed");
                for (std::size_t i = 0; i < count; ++i)
                {
                    std::array<float,18> actual{}, reference{};
                    std::memcpy(actual.data(),bytes.data()+i*sizeof(BasicVertex),sizeof(BasicVertex));
                    std::memcpy(reference.data(),&expected[i],sizeof(BasicVertex));
                    for (unsigned field = 0; field < actual.size(); ++field)
                        if (!std::isfinite(actual[field]) || std::abs(actual[field]-reference[field]) > 1e-4f + std::abs(reference[field])*1e-5f)
                            throw std::runtime_error("GPU skinning differs from CPU reference at vertex " + std::to_string(i) + " field " + std::to_string(field));
                }
                ++completed;
            })) throw std::runtime_error("GPU skinning readback unavailable");
        };
        commands.BeginFrame();
        commands.ShaderMemoryBarrier();
        kernel.BeginFrame();
        const std::array<RhiGpuSkinningDispatch,2> dispatches{{{stateA.get(),outputA.Get()},{stateB.get(),outputB.Get()}}};
        if (kernel.Record(dispatches) != 2) throw std::runtime_error("GPU skinning batch skipped a queued actor");
        commands.ShaderMemoryBarrier();
        if (growB)
        {
            kernel.Queue(*stateB,poseB,prefixB,fullB,false);
            if (kernel.Record(std::span(dispatches).subspan(1)) != 1) throw std::runtime_error("GPU skinning growth was not recorded");
            commands.ShaderMemoryBarrier();
        }
        check(outputA.Get(),expectedA,expectedA.size()); check(outputB.Get(),expectedB,expectedB.size());
        commands.Submit();
        // OpenGL diagnostic readbacks have a bounded nonblocking ring. Waiting
        // here belongs only to the oracle, never to the production skinning path.
        if (synchronizeReadback) synchronizeReadback();
        lastA = std::move(expectedA); lastB = std::move(expectedB);
        lastJointsA = poseA.size(); lastJointsB = poseB.size();
    }
    for (unsigned frame = 0; frame < 3; ++frame) { commands.BeginFrame(); commands.Submit(); }
    if (completed != 16) throw std::runtime_error("GPU skinning readbacks did not finish");
    std::cout << "PASS: GPU/CPU skinning oracle, batched palettes, LOD layout prefixes, invalid weights, "
                 "reflected/degenerate transforms, UVs and in-place motion history\n";
}
