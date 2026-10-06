#pragma once
#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

// Multiple copies of mutable CPU input, partial destination updates, arena
// rollover and more submissions than in-flight slots. Every GPU snapshot must
// retain the values from its own submission rather than a later CPU write.
template<class Device>
void CheckLargeBufferUploads(Device &device)
{
    using namespace PlutoGE::render::rhi;
    constexpr std::size_t partSize = 9 * 1024 * 1024;
    constexpr int frames = 7;
    Buffer destination(device, device.CreateBuffer({partSize * 2, BufferUsage::Storage, "Upload regression"}));
    std::vector<std::byte> input(partSize);
    auto &context = device.GetImmediateContext();
    int completed = 0;
    double recordingMs = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        context.BeginFrame();
        const auto start = std::chrono::steady_clock::now();
        for (unsigned part = 0; part < 2; ++part)
        {
            std::fill(input.begin(), input.end(), std::byte(frame * 2 + part + 1));
            device.UpdateBuffer(destination.Get(), part * partSize, input);
        }
        std::fill(input.begin(), input.end(), std::byte{255});
        recordingMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!context.QueueBufferReadback(destination.Get(), partSize * 2, [&, frame](std::span<const std::byte> snapshot) {
            if (snapshot.size() != partSize * 2) throw std::runtime_error("Upload readback size changed");
            for (unsigned part = 0; part < 2; ++part)
                if (!std::all_of(snapshot.begin() + part * partSize, snapshot.begin() + (part + 1) * partSize,
                                [=](std::byte value) { return value == std::byte(frame * 2 + part + 1); }))
                    throw std::runtime_error("In-flight staging upload was overwritten or copied to the wrong offset");
            ++completed;
        })) throw std::runtime_error("Upload readback could not be queued");
        context.Submit();
    }
    for (int frame = 0; frame < 3; ++frame) { context.BeginFrame(); context.Submit(); }
    if (completed != frames) throw std::runtime_error("Upload callbacks did not complete");
    std::cout << "PASS: staged uploads, offsets, CPU source lifetime, arena rollover and in-flight reuse; "
              << recordingMs / frames << " ms CPU recording per 18 MiB\n";
}
