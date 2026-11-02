#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/rhi/Resource.h"
#include <glm/glm.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

// Hardware ray tracing (IRenderDevice::GetRayTracingSupport, RayQueryProbe.slang). Every device reports whether it
// has it; without it, acceleration structures cannot be created (and DDGI's hardware source stays off). With it, a
// quad's bottom level placed twice in a top level must be hit by rays from above at the right distances, with the
// instances' custom indices and the triangle's normal (position fetch), and a ray between them must miss.
inline void CheckRayTracing(PlutoGE::render::rhi::IRenderDevice &device,
                            const PlutoGE::render::rhi::ComputePipelineDescriptor::ShaderCode &probe)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const std::string &message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto support = device.GetRayTracingSupport();
    if (!support.supported)
    {
        std::cout << "Ray tracing: unsupported (" << support.reason << "); acceleration structures must be refused\n";
        require(!support.reason.empty(), "An unsupported device gave no reason for lacking ray tracing");
        require(!device.CreateTopLevelAccelerationStructure({4, "Unsupported top level"}),
                "A device without ray tracing created an acceleration structure");
        return;
    }
    require(!probe.spirv.empty(), "Ray tracing is supported but the ray query probe shader is missing");

    std::array<BasicVertex, 4> vertices{};
    const std::array<std::array<float, 3>, 4> positions{{{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}}};
    for (std::size_t i = 0; i < 4; ++i)
    {
        vertices[i].position = positions[i];
        vertices[i].normal = {0, 0, 1};
    }
    constexpr std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    rhi::Buffer vertexBuffer(device, device.CreateBuffer({sizeof(vertices), rhi::BufferUsage::Vertex, "Ray tracing quad", true},
                                                         std::as_bytes(std::span(vertices))));
    rhi::Buffer indexBuffer(device, device.CreateBuffer({sizeof(indices), rhi::BufferUsage::Index, "Ray tracing quad indices", true},
                                                        std::as_bytes(std::span(indices))));
    rhi::AccelerationStructure bottom(device, device.CreateBottomLevelAccelerationStructure(
        {{{.vertices = vertexBuffer.Get(), .vertexStride = sizeof(BasicVertex), .vertexCount = 4,
           .indices = indexBuffer.Get(), .indexCount = 6}}, "Ray tracing quad"}));
    rhi::AccelerationStructure top(device, device.CreateTopLevelAccelerationStructure({2, "Ray tracing scene"}));
    require(bottom && top, "Ray tracing is supported but acceleration structures could not be created");
    const std::array instances{
        rhi::AccelerationStructureInstance{bottom.Get(), {1, 0, 0, -3, 0, 1, 0, 0, 0, 0, 1, 0}, 0},
        rhi::AccelerationStructureInstance{bottom.Get(), {1, 0, 0, 3, 0, 1, 0, 0, 0, 0, 1, -2}, 7}};
    const std::array<glm::vec4, 3> origins{glm::vec4(-3, 0, 5, 0), glm::vec4(3, 0, 5, 0), glm::vec4(0, 0, 5, 0)};
    rhi::Buffer originBuffer(device, device.CreateBuffer({sizeof(origins), rhi::BufferUsage::Storage, "Ray origins"},
                                                         std::as_bytes(std::span(origins))));
    rhi::Buffer resultBuffer(device, device.CreateBuffer({sizeof(origins), rhi::BufferUsage::Storage, "Ray results"}));
    rhi::GraphicsPipeline pipeline(device, device.CreateComputePipeline(
        {probe,
         {{0, 0, 0, rhi::ResourceBindingType::AccelerationStructure, rhi::ShaderStageMask::Compute},
          {1, 0, 1, rhi::ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Compute},
          {2, 0, 2, rhi::ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Compute}},
         "Ray query probe"}));
    require(static_cast<bool>(pipeline), "Ray query probe pipeline could not be created");

    auto &commands = device.GetImmediateContext();
    std::array<glm::vec4, 3> results{};
    bool delivered = false;
    commands.BeginFrame();
    commands.BuildBottomLevelAccelerationStructure(bottom.Get());
    commands.BuildTopLevelAccelerationStructure(top.Get(), instances);
    commands.BindPipeline(pipeline.Get());
    commands.BindAccelerationStructure(0, top.Get());
    commands.BindStorageBuffer(1, resultBuffer.Get());
    commands.BindStorageBuffer(2, originBuffer.Get());
    commands.Dispatch(3, 1, 1);
    commands.PipelineBarrier(rhi::BarrierAccess::ComputeShaderWrite, rhi::BarrierAccess::TransferRead);
    require(commands.QueueBufferReadback(resultBuffer.Get(), sizeof(results), [&](std::span<const std::byte> bytes) {
                std::memcpy(results.data(), bytes.data(), std::min(bytes.size(), sizeof(results)));
                delivered = true;
            }), "Ray query results cannot be read back");
    commands.Submit();
    for (int frame = 0; frame < 4 && !delivered; ++frame)
    {
        commands.BeginFrame();
        commands.Submit();
    }
    require(delivered, "Ray query results never arrived");
    std::cout << "Ray tracing: supported; hits " << results[0].x << " (instance " << results[0].z << ", normal z "
              << results[0].y << "), " << results[1].x << " (instance " << results[1].z << "), miss " << results[2].w << '\n';
    const auto near = [](float a, float b) { return std::abs(a - b) < 1.e-3f; };
    require(results[0].w > 0.5f && near(results[0].x, 5.0f) && near(results[0].z, 0.0f) && results[0].y > 0.99f,
            "A ray did not hit the first instance at its distance with its normal");
    require(results[1].w > 0.5f && near(results[1].x, 7.0f) && near(results[1].z, 7.0f) && results[1].y > 0.99f,
            "A ray did not hit the moved instance at its distance with its custom index");
    require(results[2].w < 0.0f, "A ray between the instances hit something");
}
