#include "PlutoGE/render/ShaderGraphVariants.h"

#if PLUTO_HAS_SLANG_RUNTIME
#include <slang.h>
#include <slang-com-ptr.h>

#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

namespace PlutoGE::render
{
#if PLUTO_HAS_SLANG_RUNTIME
    namespace
    {
        // The Slang runtime is optional: it is loaded dynamically so builds and
        // installs without it fall back to the interpreter instead of failing to start.
        class SlangLibrary
        {
        public:
            using CreateGlobalSession = SlangResult (*)(const SlangGlobalSessionDesc *, slang::IGlobalSession **);

            static std::unique_ptr<SlangLibrary> Load()
            {
#if defined(_WIN32)
                void *handle = LoadLibraryW(L"slang.dll");
                const auto symbol = [&](const char *name) { return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name)); };
#else
                void *handle = dlopen("libslang.so", RTLD_NOW | RTLD_LOCAL);
                const auto symbol = [&](const char *name) { return dlsym(handle, name); };
#endif
                if (!handle) return {};
                auto library = std::unique_ptr<SlangLibrary>(new SlangLibrary(handle));
                library->createGlobalSession = reinterpret_cast<CreateGlobalSession>(symbol("slang_createGlobalSession2"));
                return library->createGlobalSession ? std::move(library) : nullptr;
            }

            ~SlangLibrary()
            {
#if defined(_WIN32)
                FreeLibrary(static_cast<HMODULE>(m_handle));
#else
                dlclose(m_handle);
#endif
            }

            CreateGlobalSession createGlobalSession = nullptr;

        private:
            explicit SlangLibrary(void *handle) : m_handle(handle) {}
            void *m_handle;
        };

        std::string Text(slang::IBlob *blob)
        {
            return blob ? std::string(static_cast<const char *>(blob->getBufferPointer()), blob->getBufferSize()) : std::string{};
        }

        class SlangShaderGraphCompiler final : public ShaderGraphVariantCompiler
        {
        public:
            explicit SlangShaderGraphCompiler(std::unique_ptr<SlangLibrary> library) : m_library(std::move(library)) {}

            ~SlangShaderGraphCompiler() override
            {
                // Release Slang objects before the library is unloaded.
                m_session.setNull();
            }

            std::string Identity() override { return std::string("slang ") + Session().getBuildTagString(); }

            std::vector<std::uint32_t> CompileFragment(const Request &request) override
            {
                auto &global = Session();
                // Match the offline artifact build (engine/render/CMakeLists.txt):
                // SPIR-V 1.3, column-major matrices.
                slang::TargetDesc target;
                target.format = SLANG_SPIRV;
                target.profile = global.findProfile("spirv_1_3");
                const std::string variantDirectory = request.variantDirectory.string();
                const std::string sourceDirectory = request.sourceDirectory.string();
                const char *searchPaths[] = {variantDirectory.c_str(), sourceDirectory.c_str()};
                const slang::PreprocessorMacroDesc macro{"PLUTO_SHADER_GRAPH_GENERATED", "1"};
                slang::SessionDesc description;
                description.targets = &target;
                description.targetCount = 1;
                description.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;
                description.searchPaths = searchPaths;
                description.searchPathCount = 2;
                description.preprocessorMacros = &macro;
                description.preprocessorMacroCount = 1;

                Slang::ComPtr<slang::ISession> session;
                Check(global.createSession(description, session.writeRef()), nullptr, "Cannot create a Slang session");
                Slang::ComPtr<slang::IBlob> diagnostics;
                slang::IModule *module = session->loadModule("BasicLit", diagnostics.writeRef());
                if (!module) throw std::runtime_error("BasicLit.slang failed to compile: " + Text(diagnostics));
                const std::string entryName(request.entryPoint);
                Slang::ComPtr<slang::IEntryPoint> entryPoint;
                Check(module->findEntryPointByName(entryName.c_str(), entryPoint.writeRef()), nullptr,
                      "Missing entry point " + entryName);
                slang::IComponentType *components[] = {module, entryPoint.get()};
                Slang::ComPtr<slang::IComponentType> composite;
                Check(session->createCompositeComponentType(components, 2, composite.writeRef(), diagnostics.writeRef()),
                      diagnostics, "Cannot compose " + entryName);
                Slang::ComPtr<slang::IComponentType> linked;
                Check(composite->link(linked.writeRef(), diagnostics.writeRef()), diagnostics, "Cannot link " + entryName);
                Slang::ComPtr<slang::IBlob> code;
                Check(linked->getEntryPointCode(0, 0, code.writeRef(), diagnostics.writeRef()), diagnostics,
                      "Cannot generate SPIR-V for " + entryName);
                if (!code || code->getBufferSize() == 0 || code->getBufferSize() % sizeof(std::uint32_t) != 0)
                    throw std::runtime_error("Slang produced invalid SPIR-V for " + entryName);
                std::vector<std::uint32_t> words(code->getBufferSize() / sizeof(std::uint32_t));
                std::memcpy(words.data(), code->getBufferPointer(), code->getBufferSize());
                return words;
            }

        private:
            static void Check(SlangResult result, slang::IBlob *diagnostics, const std::string &message)
            {
                if (SLANG_FAILED(result)) throw std::runtime_error(message + ": " + Text(diagnostics));
            }

            // Created on first use, on the caller's (worker) thread. Global
            // sessions are not thread-safe; the variant cache uses one thread.
            slang::IGlobalSession &Session()
            {
                if (!m_session)
                {
                    SlangGlobalSessionDesc description;
                    if (SLANG_FAILED(m_library->createGlobalSession(&description, m_session.writeRef())) || !m_session)
                        throw std::runtime_error("Cannot create the Slang global session");
                }
                return *m_session;
            }

            std::unique_ptr<SlangLibrary> m_library;
            Slang::ComPtr<slang::IGlobalSession> m_session;
        };
    }

    std::unique_ptr<ShaderGraphVariantCompiler> CreateSlangShaderGraphCompiler()
    {
        auto library = SlangLibrary::Load();
        if (!library) return {};
        return std::make_unique<SlangShaderGraphCompiler>(std::move(library));
    }
#else
    std::unique_ptr<ShaderGraphVariantCompiler> CreateSlangShaderGraphCompiler() { return {}; }
#endif
}
