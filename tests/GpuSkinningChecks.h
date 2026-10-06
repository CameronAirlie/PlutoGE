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
    const auto source = kernel.CreateSource(vertices);
    auto stateA = kernel.CreateState(source), stateB = kernel.CreateState(source);
    rhi::Buffer outputA(device,device.CreateBuffer({vertices.size()*sizeof(BasicVertex),rhi::BufferUsage::VertexStorage,"Skinning oracle A"}));
    rhi::Buffer outputB(device,device.CreateBuffer({vertices.size()*sizeof(BasicVertex),rhi::BufferUsage::VertexStorage,"Skinning oracle B"}));
    auto &commands = device.GetImmediateContext();
    int completed = 0;
    for (unsigned frame = 0; frame < 8; ++frame)
    {
        std::vector<glm::mat4> current{glm::rotate(glm::mat4(1),.07f*frame,glm::normalize(glm::vec3(1,2,3))),
                                     glm::scale(glm::mat4(1),glm::vec3(frame%2 ? -2.f : 2.f,.5f,1.5f))};
        std::vector<glm::mat4> previous{glm::translate(glm::mat4(1),glm::vec3(.1f*frame,.3f,0)),glm::mat4(1)};
        if (frame == 6) current.assign(2,glm::mat4(0));
        if (frame == 7) { current.push_back(glm::mat4(1)); previous.push_back(glm::mat4(1)); }
        kernel.Queue(*stateA,current,previous);
        kernel.Queue(*stateB,previous,current);
        const auto check = [&](rhi::BufferHandle output, const auto &pose, const auto &oldPose)
        {
            const auto oldVertices = ReferenceSkinRhiVertices(vertices,oldPose);
            const auto expected = ReferenceSkinRhiVertices(vertices,pose,oldVertices);
            if (!commands.QueueBufferReadback(output,expected.size()*sizeof(BasicVertex),[&,expected](std::span<const std::byte> bytes) {
                if (bytes.size() != expected.size()*sizeof(BasicVertex)) throw std::runtime_error("GPU skinning readback size changed");
                for (std::size_t i = 0; i < expected.size(); ++i)
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
        kernel.Record(*stateA,outputA.Get()); kernel.Record(*stateB,outputB.Get());
        commands.ShaderMemoryBarrier();
        check(outputA.Get(),current,previous); check(outputB.Get(),previous,current);
        commands.Submit();
        // OpenGL diagnostic readbacks have a bounded nonblocking ring. Waiting
        // here belongs only to the oracle, never to the production skinning path.
        if (synchronizeReadback) synchronizeReadback();
    }
    for (unsigned frame = 0; frame < 3; ++frame) { commands.BeginFrame(); commands.Submit(); }
    if (completed != 16) throw std::runtime_error("GPU skinning readbacks did not finish");
    std::cout << "PASS: GPU/CPU skinning oracle, independent actors, invalid weights, reflected/degenerate transforms, UVs and in-flight motion history\n";
}
