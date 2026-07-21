#include "SharedTexturePublisher.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <glad/glad.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <sstream>
#include <vector>

namespace
{
    constexpr GLenum WglAccessWriteDiscardNv = 0x0002;
    constexpr std::size_t TextureCount = 3;

    using OpenDevice = HANDLE(WINAPI *)(void *);
    using CloseDevice = BOOL(WINAPI *)(HANDLE);
    using RegisterObject = HANDLE(WINAPI *)(HANDLE, void *, GLuint, GLenum, GLenum);
    using UnregisterObject = BOOL(WINAPI *)(HANDLE, HANDLE);
    using LockObjects = BOOL(WINAPI *)(HANDLE, GLint, HANDLE *);
    using UnlockObjects = BOOL(WINAPI *)(HANDLE, GLint, HANDLE *);

    template <typename Procedure>
    Procedure LoadWglProcedure(const char *name)
    {
        const auto procedure = wglGetProcAddress(name);
        if (procedure == nullptr || procedure == reinterpret_cast<PROC>(1) ||
            procedure == reinterpret_cast<PROC>(2) || procedure == reinterpret_cast<PROC>(3) ||
            procedure == reinterpret_cast<PROC>(-1))
        {
            return nullptr;
        }
        return reinterpret_cast<Procedure>(procedure);
    }

    void *LoadOpenGlProcedure(const char *name)
    {
        static HMODULE openGlModule = LoadLibraryA("opengl32.dll");
        PROC procedure = wglGetProcAddress(name);
        if (procedure == nullptr || procedure == reinterpret_cast<PROC>(1) ||
            procedure == reinterpret_cast<PROC>(2) || procedure == reinterpret_cast<PROC>(3) ||
            procedure == reinterpret_cast<PROC>(-1))
        {
            procedure = openGlModule ? GetProcAddress(openGlModule, name) : nullptr;
        }
        return reinterpret_cast<void *>(procedure);
    }
}

struct SharedTexturePublisher::Implementation
{
    struct Slot
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
        HANDLE localHandle = nullptr;
        HANDLE remoteHandle = nullptr;
        GLuint glTexture = 0;
        HANDLE interopObject = nullptr;
        bool inFlight = false;
    };

    struct Pool
    {
        std::uint64_t generation = 0;
        int width = 0;
        int height = 0;
        bool active = true;
        std::array<Slot, TextureCount> slots;
    };

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    HANDLE targetProcess = nullptr;
    HANDLE interopDevice = nullptr;
    CloseDevice closeDevice = nullptr;
    RegisterObject registerObject = nullptr;
    UnregisterObject unregisterObject = nullptr;
    LockObjects lockObjects = nullptr;
    UnlockObjects unlockObjects = nullptr;
    std::vector<Pool> pools;
    std::uint64_t nextGeneration = 1;
    std::uint64_t sequence = 0;

    bool Initialize(std::uint32_t electronProcessId, std::string &error)
    {
        if (!glad_glGenTextures && !gladLoadGLLoader(&LoadOpenGlProcedure))
        {
            error = "Could not initialize the editor host OpenGL dispatch table";
            return false;
        }
        const auto openDevice = LoadWglProcedure<OpenDevice>("wglDXOpenDeviceNV");
        closeDevice = LoadWglProcedure<CloseDevice>("wglDXCloseDeviceNV");
        registerObject = LoadWglProcedure<RegisterObject>("wglDXRegisterObjectNV");
        unregisterObject = LoadWglProcedure<UnregisterObject>("wglDXUnregisterObjectNV");
        lockObjects = LoadWglProcedure<LockObjects>("wglDXLockObjectsNV");
        unlockObjects = LoadWglProcedure<UnlockObjects>("wglDXUnlockObjectsNV");
        if (!openDevice || !closeDevice || !registerObject || !unregisterObject || !lockObjects || !unlockObjects)
        {
            error = "WGL_NV_DX_interop2 is unavailable on the active OpenGL driver";
            return false;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        constexpr std::array featureLevels{
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };
        D3D_FEATURE_LEVEL selectedFeatureLevel{};
        HRESULT result = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels.data(),
            static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION,
            &device,
            &selectedFeatureLevel,
            &context);
#ifndef NDEBUG
        if (FAILED(result))
        {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            result = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &device,
                &selectedFeatureLevel,
                &context);
        }
#endif
        if (FAILED(result))
        {
            std::ostringstream message;
            message << "D3D11CreateDevice failed with HRESULT 0x" << std::hex << result;
            error = message.str();
            return false;
        }

        interopDevice = openDevice(device.Get());
        if (!interopDevice)
        {
            error = "The OpenGL driver rejected the D3D11 device; the adapters may not match";
            return false;
        }

        targetProcess = OpenProcess(PROCESS_DUP_HANDLE, FALSE, electronProcessId);
        if (!targetProcess)
        {
            error = "Could not open the Electron process for shared-handle duplication";
            return false;
        }
        return true;
    }

    bool CreatePool(int width, int height, std::string &error)
    {
        for (auto &pool : pools) pool.active = false;
        Pool pool;
        pool.generation = nextGeneration++;
        pool.width = width;
        pool.height = height;

        for (auto &slot : pool.slots)
        {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(width);
            description.Height = static_cast<UINT>(height);
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            description.MiscFlags =
                D3D11_RESOURCE_MISC_SHARED |
                D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            HRESULT result = device->CreateTexture2D(&description, nullptr, &slot.texture);
            if (FAILED(result))
            {
                std::ostringstream message;
                message << "Could not create a D3D11 shared viewport texture (HRESULT 0x"
                        << std::hex << static_cast<std::uint32_t>(result) << ')';
                error = message.str();
                DestroyPool(pool);
                return false;
            }

            Microsoft::WRL::ComPtr<IDXGIResource1> resource;
            result = slot.texture.As(&resource);
            if (FAILED(result) || FAILED(resource->CreateSharedHandle(
                                      nullptr,
                                      DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                                      nullptr,
                                      &slot.localHandle)))
            {
                error = "Could not create an NT handle for the viewport texture";
                DestroyPool(pool);
                return false;
            }

            glGenTextures(1, &slot.glTexture);
            slot.interopObject = registerObject(
                interopDevice,
                slot.texture.Get(),
                slot.glTexture,
                GL_TEXTURE_2D,
                WglAccessWriteDiscardNv);
            if (!slot.interopObject)
            {
                error = "OpenGL could not register the D3D11 viewport texture";
                DestroyPool(pool);
                return false;
            }
        }

        pools.push_back(std::move(pool));
        CollectRetiredPools();
        return true;
    }

    bool CanPublish(int width, int height, std::size_t maxFramesInFlight, std::string &error)
    {
        auto active = std::find_if(pools.begin(), pools.end(), [](const Pool &pool) { return pool.active; });
        if (active == pools.end() || active->width != width || active->height != height)
        {
            if (!CreatePool(width, height, error)) return false;
            active = std::find_if(pools.begin(), pools.end(), [](const Pool &pool) { return pool.active; });
        }
        if (active == pools.end()) return false;
        const auto inFlightCount = std::count_if(
            active->slots.begin(), active->slots.end(), [](const Slot &slot) { return slot.inFlight; });
        return inFlightCount < static_cast<std::ptrdiff_t>(std::max<std::size_t>(1, maxFramesInFlight));
    }

    std::optional<Frame> Publish(int width, int height, std::string &error)
    {
        auto active = std::find_if(pools.begin(), pools.end(), [](const Pool &pool) { return pool.active; });
        if (active == pools.end() || active->width != width || active->height != height)
        {
            if (!CreatePool(width, height, error)) return std::nullopt;
            active = std::find_if(pools.begin(), pools.end(), [](const Pool &pool) { return pool.active; });
        }
        if (active == pools.end()) return std::nullopt;

        for (std::size_t index = 0; index < active->slots.size(); ++index)
        {
            auto &slot = active->slots[index];
            if (slot.inFlight) continue;
            if (!slot.remoteHandle &&
                !DuplicateHandle(
                    GetCurrentProcess(),
                    slot.localHandle,
                    targetProcess,
                    &slot.remoteHandle,
                    0,
                    FALSE,
                    DUPLICATE_SAME_ACCESS))
            {
                error = "Could not duplicate the viewport texture handle into Electron";
                return std::nullopt;
            }

            if (!lockObjects(interopDevice, 1, &slot.interopObject))
            {
                error = "Could not lock the OpenGL/D3D11 shared texture";
                CloseRemoteHandle(slot);
                return std::nullopt;
            }
            GLint previousReadFramebuffer = 0;
            GLint previousReadBuffer = GL_BACK;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
            glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glBindTexture(GL_TEXTURE_2D, slot.glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width, height);
            glBindTexture(GL_TEXTURE_2D, 0);
            glReadBuffer(static_cast<GLenum>(previousReadBuffer));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
            glFlush();
            if (!unlockObjects(interopDevice, 1, &slot.interopObject))
            {
                error = "Could not unlock the OpenGL/D3D11 shared texture";
                CloseRemoteHandle(slot);
                return std::nullopt;
            }

            slot.inFlight = true;
            return Frame{
                .sequence = ++sequence,
                .generation = active->generation,
                .slot = static_cast<std::uint32_t>(index),
                .width = width,
                .height = height,
                .remoteHandle = reinterpret_cast<std::uintptr_t>(slot.remoteHandle),
            };
        }
        return std::nullopt;
    }

    void Release(std::uint64_t generation, std::uint32_t slotIndex)
    {
        const auto pool = std::find_if(pools.begin(), pools.end(), [generation](const Pool &candidate) {
            return candidate.generation == generation;
        });
        if (pool == pools.end() || slotIndex >= pool->slots.size()) return;
        auto &slot = pool->slots[slotIndex];
        CloseRemoteHandle(slot);
        slot.inFlight = false;
        CollectRetiredPools();
    }

    void CloseRemoteHandle(Slot &slot)
    {
        if (!slot.remoteHandle || !targetProcess) return;
        HANDLE localDuplicate = nullptr;
        if (DuplicateHandle(
            targetProcess,
            slot.remoteHandle,
            GetCurrentProcess(),
            &localDuplicate,
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS | DUPLICATE_CLOSE_SOURCE))
        {
            CloseHandle(localDuplicate);
        }
        slot.remoteHandle = nullptr;
    }

    void DestroyPool(Pool &pool)
    {
        for (auto &slot : pool.slots)
        {
            CloseRemoteHandle(slot);
            if (slot.interopObject && unregisterObject && interopDevice)
                unregisterObject(interopDevice, slot.interopObject);
            slot.interopObject = nullptr;
            if (slot.glTexture) glDeleteTextures(1, &slot.glTexture);
            slot.glTexture = 0;
            if (slot.localHandle) CloseHandle(slot.localHandle);
            slot.localHandle = nullptr;
            slot.texture.Reset();
        }
    }

    void CollectRetiredPools()
    {
        for (auto iterator = pools.begin(); iterator != pools.end();)
        {
            const bool inFlight = std::any_of(iterator->slots.begin(), iterator->slots.end(), [](const Slot &slot) {
                return slot.inFlight;
            });
            if (!iterator->active && !inFlight)
            {
                DestroyPool(*iterator);
                iterator = pools.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }

    void Shutdown()
    {
        for (auto &pool : pools) DestroyPool(pool);
        pools.clear();
        if (interopDevice && closeDevice) closeDevice(interopDevice);
        interopDevice = nullptr;
        context.Reset();
        device.Reset();
        if (targetProcess) CloseHandle(targetProcess);
        targetProcess = nullptr;
    }
};

#else

struct SharedTexturePublisher::Implementation
{
};

#endif

SharedTexturePublisher::SharedTexturePublisher() = default;

SharedTexturePublisher::~SharedTexturePublisher()
{
    Shutdown();
}

bool SharedTexturePublisher::Initialize(std::uint32_t electronProcessId, std::string &error)
{
#ifdef _WIN32
    m_implementation = std::make_unique<Implementation>();
    if (m_implementation->Initialize(electronProcessId, error)) return true;
    m_implementation->Shutdown();
    m_implementation.reset();
    return false;
#else
    (void)electronProcessId;
    error = "Shared textures are currently implemented only on Windows";
    return false;
#endif
}

bool SharedTexturePublisher::CanPublish(int width, int height, std::size_t maxFramesInFlight, std::string &error)
{
#ifdef _WIN32
    return m_implementation && m_implementation->CanPublish(width, height, maxFramesInFlight, error);
#else
    (void)width;
    (void)height;
    (void)maxFramesInFlight;
    (void)error;
    return false;
#endif
}

std::optional<SharedTexturePublisher::Frame> SharedTexturePublisher::Publish(int width, int height, std::string &error)
{
#ifdef _WIN32
    return m_implementation ? m_implementation->Publish(width, height, error) : std::nullopt;
#else
    (void)width;
    (void)height;
    (void)error;
    return std::nullopt;
#endif
}

void SharedTexturePublisher::Release(std::uint64_t generation, std::uint32_t slot)
{
#ifdef _WIN32
    if (m_implementation) m_implementation->Release(generation, slot);
#else
    (void)generation;
    (void)slot;
#endif
}

void SharedTexturePublisher::Shutdown()
{
#ifdef _WIN32
    if (m_implementation) m_implementation->Shutdown();
#endif
    m_implementation.reset();
}

bool SharedTexturePublisher::IsAvailable() const
{
    return m_implementation != nullptr;
}
