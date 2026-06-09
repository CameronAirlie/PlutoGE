#include "PlutoGE/ui/PanelManager.h"

#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/render/Renderer.h"

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#if defined(_WIN32)
#include <backends/imgui_impl_dx12.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#elif !defined(_WIN32)
#include <backends/imgui_impl_opengl3.h>
#endif
#include <ImGuizmo.h>

#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/NvrhiBackend.h"

#include <iostream>
#include <chrono>

namespace
{
#if defined(_WIN32)
    struct PanelManagerD3D12State
    {
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap;
        Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence;
        std::vector<Microsoft::WRL::ComPtr<ID3D12CommandAllocator>> commandAllocators;
        std::vector<UINT64> frameFenceValues;
        Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList;
        D3D12_CPU_DESCRIPTOR_HANDLE fontCpuHandle{};
        D3D12_GPU_DESCRIPTOR_HANDLE fontGpuHandle{};
        bool fontDescriptorAllocated = false;
        UINT rtvDescriptorSize = 0;
        UINT64 nextFenceValue = 1;
        HANDLE fenceEvent = nullptr;
    };

    PanelManagerD3D12State &GetD3D12State()
    {
        static PanelManagerD3D12State state;
        return state;
    }

    void AllocateSrvDescriptor(ImGui_ImplDX12_InitInfo *info,
                               D3D12_CPU_DESCRIPTOR_HANDLE *outCpuDescHandle,
                               D3D12_GPU_DESCRIPTOR_HANDLE *outGpuDescHandle)
    {
        auto &state = GetD3D12State();
        if (!state.fontDescriptorAllocated)
        {
            state.fontCpuHandle = state.srvHeap->GetCPUDescriptorHandleForHeapStart();
            state.fontGpuHandle = state.srvHeap->GetGPUDescriptorHandleForHeapStart();
            state.fontDescriptorAllocated = true;
        }

        *outCpuDescHandle = state.fontCpuHandle;
        *outGpuDescHandle = state.fontGpuHandle;
        (void)info;
    }

    void FreeSrvDescriptor(ImGui_ImplDX12_InitInfo *info,
                           D3D12_CPU_DESCRIPTOR_HANDLE,
                           D3D12_GPU_DESCRIPTOR_HANDLE)
    {
        (void)info;
    }
#endif
}

namespace PlutoGE::ui
{
    namespace
    {
        constexpr bool kEnableNativeMultiViewport = false;
        constexpr bool kEnableDx12ImguiSubmission = true;

        float DurationMs(const std::chrono::high_resolution_clock::time_point &start,
                         const std::chrono::high_resolution_clock::time_point &end)
        {
            return std::chrono::duration<float, std::milli>(end - start).count();
        }
    }

    bool PanelManager::InitializeImGui(platform::Window *window, render::Renderer *renderer)
    {
        m_renderer = renderer;
        m_backend = renderer ? renderer->GetBackend() : render::RenderBackend::OpenGL;
        m_imguiInitialized = false;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO &io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
        if (kEnableNativeMultiViewport && m_backend == render::RenderBackend::OpenGL)
        {
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigViewportsNoDecoration = false;
        }

#if defined(_WIN32)
        if (m_backend == render::RenderBackend::NvrhiD3D12)
        {
            auto *nvrhiBackend = renderer ? renderer->GetNvrhiBackend() : nullptr;
            render::NvrhiD3D12Interop interop{};
            if (!nvrhiBackend || !nvrhiBackend->GetD3D12Interop(interop))
            {
                std::cerr << "Failed to acquire D3D12 interop state for ImGui." << std::endl;
                return false;
            }

            auto *device = static_cast<ID3D12Device *>(interop.device);
            if (!device)
            {
                std::cerr << "Failed to acquire a D3D12 device for ImGui." << std::endl;
                return false;
            }

            auto &state = GetD3D12State();
            state = {};
            const auto bufferCount = (std::max)(1u, interop.bufferCount);

            D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc{};
            srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvHeapDesc.NumDescriptors = 1;
            srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&state.srvHeap))))
            {
                std::cerr << "Failed to create the D3D12 ImGui SRV descriptor heap." << std::endl;
                return false;
            }

            D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
            rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvHeapDesc.NumDescriptors = bufferCount;
            if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&state.rtvHeap))))
            {
                std::cerr << "Failed to create the D3D12 ImGui RTV descriptor heap." << std::endl;
                return false;
            }

            state.rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            state.commandAllocators.resize(bufferCount);
            state.frameFenceValues.assign(bufferCount, 0);
            for (auto &commandAllocator : state.commandAllocators)
            {
                if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator))))
                {
                    std::cerr << "Failed to create a D3D12 ImGui command allocator." << std::endl;
                    return false;
                }
            }

            if (FAILED(device->CreateCommandList(0,
                                                 D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 state.commandAllocators.front().Get(),
                                                 nullptr,
                                                 IID_PPV_ARGS(&state.commandList))))
            {
                std::cerr << "Failed to create the D3D12 ImGui command list." << std::endl;
                return false;
            }
            state.commandList->Close();

            if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&state.fence))))
            {
                std::cerr << "Failed to create the D3D12 ImGui fence." << std::endl;
                return false;
            }

            state.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!state.fenceEvent)
            {
                std::cerr << "Failed to create the D3D12 ImGui fence event." << std::endl;
                return false;
            }

            ImGui_ImplGlfw_InitForOther(static_cast<GLFWwindow *>(window->GetWindow()), true);

            ImGui_ImplDX12_InitInfo initInfo{};
            initInfo.Device = device;
            initInfo.CommandQueue = static_cast<ID3D12CommandQueue *>(interop.graphicsQueue);
            initInfo.NumFramesInFlight = static_cast<int>(bufferCount);
            initInfo.RTVFormat = interop.backBufferFormat;
            initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
            initInfo.UserData = this;
            initInfo.SrvDescriptorHeap = state.srvHeap.Get();
            initInfo.SrvDescriptorAllocFn = AllocateSrvDescriptor;
            initInfo.SrvDescriptorFreeFn = FreeSrvDescriptor;
            if (!ImGui_ImplDX12_Init(&initInfo))
            {
                std::cerr << "Failed to initialize the Dear ImGui D3D12 backend." << std::endl;
                return false;
            }
        }
#else
        ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow *>(window->GetWindow()), true);
        ImGui_ImplOpenGL3_Init("#version 330 core");
#endif

        // Setup Platform
        ImGuiStyle &style = ImGui::GetStyle();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        m_imguiInitialized = true;
        return true;
    }

    void PanelManager::AddPanel(Panel *panel)
    {
        m_panels.push_back(panel);
    }

    void PanelManager::UpdatePanels()
    {
        for (auto panel : m_panels)
        {
            panel->Update();
        }
    }

    void PanelManager::ShutdownPanels()
    {
        for (auto panel : m_panels)
        {
            panel->Shutdown();
            delete panel;
        }
        m_panels.clear();

        if (!m_imguiInitialized)
        {
            return;
        }

#if defined(_WIN32)
        if (m_backend == render::RenderBackend::NvrhiD3D12)
        {
            ImGui_ImplDX12_Shutdown();
            auto &state = GetD3D12State();
            state.commandList.Reset();
            state.commandAllocators.clear();
            state.frameFenceValues.clear();
            state.fence.Reset();
            if (state.fenceEvent)
            {
                CloseHandle(state.fenceEvent);
                state.fenceEvent = nullptr;
            }
            state.rtvHeap.Reset();
            state.srvHeap.Reset();
        }
#else
        ImGui_ImplOpenGL3_Shutdown();
#endif

        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_imguiInitialized = false;
    }

    void PanelManager::BeginPanelUpdate()
    {
        if (!m_imguiInitialized)
        {
            return;
        }

#if defined(_WIN32)
        if (m_backend == render::RenderBackend::NvrhiD3D12)
        {
            ImGui_ImplDX12_NewFrame();
        }
#else
        ImGui_ImplOpenGL3_NewFrame();
#endif

        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);
    }

    void PanelManager::EndPanelUpdate()
    {
        if (!m_imguiInitialized)
        {
            return;
        }

        const auto endPanelUpdateStart = std::chrono::high_resolution_clock::now();
        const auto imguiRenderStart = std::chrono::high_resolution_clock::now();
        ImGui::Render();

#if defined(_WIN32)
        if (m_backend == render::RenderBackend::NvrhiD3D12)
        {
            if (!kEnableDx12ImguiSubmission)
            {
                const auto imguiRenderEnd = std::chrono::high_resolution_clock::now();
                m_timingStats.imguiRenderMs = std::chrono::duration<float, std::milli>(imguiRenderEnd - imguiRenderStart).count();
                const auto endPanelUpdateEnd = std::chrono::high_resolution_clock::now();
                m_timingStats.endPanelUpdateTotalMs = std::chrono::duration<float, std::milli>(endPanelUpdateEnd - endPanelUpdateStart).count();
                return;
            }

            auto *nvrhiBackend = m_renderer ? m_renderer->GetNvrhiBackend() : nullptr;
            render::NvrhiD3D12Interop interop{};
            auto &state = GetD3D12State();
            if (nvrhiBackend && nvrhiBackend->GetD3D12Interop(interop) && state.commandList && interop.currentBackBuffer && interop.graphicsQueue && interop.swapChain)
            {
                auto *swapChain = static_cast<IDXGISwapChain3 *>(interop.swapChain);
                const auto backBufferIndex = swapChain->GetCurrentBackBufferIndex();
                if (backBufferIndex < state.commandAllocators.size())
                {
                    auto *commandAllocator = state.commandAllocators[backBufferIndex].Get();
                    auto *commandList = state.commandList.Get();
                    auto *resource = static_cast<ID3D12Resource *>(interop.currentBackBuffer);
                    auto *queue = static_cast<ID3D12CommandQueue *>(interop.graphicsQueue);
                    auto rtvHandle = state.rtvHeap->GetCPUDescriptorHandleForHeapStart();
                    rtvHandle.ptr += static_cast<SIZE_T>(backBufferIndex) * static_cast<SIZE_T>(state.rtvDescriptorSize);
                    static_cast<ID3D12Device *>(interop.device)->CreateRenderTargetView(resource, nullptr, rtvHandle);

                    const UINT64 fenceValue = state.frameFenceValues[backBufferIndex];
                    if (fenceValue != 0 && state.fence && state.fence->GetCompletedValue() < fenceValue)
                    {
                        state.fence->SetEventOnCompletion(fenceValue, state.fenceEvent);
                        WaitForSingleObject(state.fenceEvent, INFINITE);
                    }

                    commandAllocator->Reset();
                    commandList->Reset(commandAllocator, nullptr);

                    D3D12_RESOURCE_BARRIER beginBarrier{};
                    beginBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    beginBarrier.Transition.pResource = resource;
                    beginBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
                    beginBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    beginBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    commandList->ResourceBarrier(1, &beginBarrier);

                    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
                    ID3D12DescriptorHeap *descriptorHeaps[] = {state.srvHeap.Get()};
                    commandList->SetDescriptorHeaps(1, descriptorHeaps);
                    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

                    D3D12_RESOURCE_BARRIER endBarrier{};
                    endBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                    endBarrier.Transition.pResource = resource;
                    endBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
                    endBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
                    endBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                    commandList->ResourceBarrier(1, &endBarrier);

                    commandList->Close();
                    ID3D12CommandList *commandLists[] = {commandList};
                    queue->ExecuteCommandLists(1, commandLists);
                    if (state.fence)
                    {
                        const UINT64 signalValue = state.nextFenceValue++;
                        queue->Signal(state.fence.Get(), signalValue);
                        state.frameFenceValues[backBufferIndex] = signalValue;
                    }
                }
            }
        }
#else
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

        const auto imguiRenderEnd = std::chrono::high_resolution_clock::now();
        m_timingStats.imguiRenderMs = DurationMs(imguiRenderStart, imguiRenderEnd);
        m_timingStats.platformViewportCount = ImGui::GetPlatformIO().Viewports.Size;

        ImGuiIO &io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow *backup_current_context = glfwGetCurrentContext();

            const auto platformUpdateStart = std::chrono::high_resolution_clock::now();
            ImGui::UpdatePlatformWindows();
            const auto platformUpdateEnd = std::chrono::high_resolution_clock::now();

            const auto platformRenderStart = std::chrono::high_resolution_clock::now();
            ImGui::RenderPlatformWindowsDefault();
            const auto platformRenderEnd = std::chrono::high_resolution_clock::now();

            const auto contextRestoreStart = std::chrono::high_resolution_clock::now();
            glfwMakeContextCurrent(backup_current_context);
            const auto contextRestoreEnd = std::chrono::high_resolution_clock::now();

            m_timingStats.platformWindowsUpdateMs = DurationMs(platformUpdateStart, platformUpdateEnd);
            m_timingStats.platformWindowsRenderMs = DurationMs(platformRenderStart, platformRenderEnd);
            m_timingStats.contextRestoreMs = DurationMs(contextRestoreStart, contextRestoreEnd);
            m_timingStats.endPanelUpdateTotalMs = DurationMs(endPanelUpdateStart, contextRestoreEnd);
            return;
        }

        m_timingStats.platformWindowsUpdateMs = 0.0f;
        m_timingStats.platformWindowsRenderMs = 0.0f;
        m_timingStats.contextRestoreMs = 0.0f;
        m_timingStats.endPanelUpdateTotalMs = DurationMs(endPanelUpdateStart, std::chrono::high_resolution_clock::now());
    }
}