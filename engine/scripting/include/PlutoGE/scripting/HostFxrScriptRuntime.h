#pragma once

#include "PlutoGE/scripting/ScriptRuntime.h"

#include <memory>

namespace PlutoGE::scripting
{
    class HostFxrScriptRuntime final : public IScriptRuntime
    {
    public:
        struct Impl;

        HostFxrScriptRuntime();
        ~HostFxrScriptRuntime() override;

        bool LoadAssembly(const std::filesystem::path &assemblyPath) override;
        [[nodiscard]] bool IsLoaded() const override;
        [[nodiscard]] std::vector<ScriptClassDefinition> GetScriptClasses() const override;
        [[nodiscard]] std::unique_ptr<ScriptInstance> CreateInstance(const ScriptClassDefinition &scriptClass) const override;

    private:
        std::shared_ptr<Impl> m_impl;
    };
}