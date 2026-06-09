#pragma once

#include "PlutoGE/render/RenderBackend.h"

#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::platform
{
    class Window;
}

namespace nvrhi
{
    class IDevice;
}

namespace PlutoGE::render
{
    struct CameraData;
    struct RenderCommand;

    struct NvrhiBackendConfig
    {
        RenderBackend backend = RenderBackend::NvrhiD3D12;
        platform::Window *window = nullptr;
        bool vSyncEnabled = true;
    };

    class NvrhiBackend
    {
    public:
        class Impl;

        NvrhiBackend();
        ~NvrhiBackend();

        NvrhiBackend(const NvrhiBackend &) = delete;
        NvrhiBackend &operator=(const NvrhiBackend &) = delete;

        bool Initialize(const NvrhiBackendConfig &config);
        void BeginFrame();
        void RenderFrame(int width, int height, const CameraData &cameraData, const std::vector<RenderCommand> &renderCommands);
        void EndFrame();
        void Shutdown();
        void SetVSyncEnabled(bool enabled);

        [[nodiscard]] nvrhi::IDevice *GetDevice() const;
        [[nodiscard]] RenderBackend GetBackend() const { return m_config.backend; }
        [[nodiscard]] const std::string &GetLastError() const { return m_lastError; }
        [[nodiscard]] bool IsInitialized() const { return m_impl != nullptr; }

    private:
        NvrhiBackendConfig m_config;
        std::unique_ptr<Impl> m_impl;
        std::string m_lastError;
    };
}
