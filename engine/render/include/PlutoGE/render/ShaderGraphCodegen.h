#pragma once

#include "PlutoGE/render/ShaderGraph.h"

#include <cstdint>
#include <string>

namespace PlutoGE::render
{
    // Increment when generated code changes meaning or shape, so cached
    // variants compiled by an older generator are never reused.
    inline constexpr std::uint32_t kShaderGraphCodegenVersion = 1;

    // Identifies the code a program generates: instructions, stage counts and
    // output registers. Constant and parameter values are excluded because the
    // generated code reads them from the material's values[] at runtime, so
    // materials that only differ in parameters share one compiled variant.
    [[nodiscard]] std::uint64_t ShaderGraphStructureHash(const ShaderGraphProgramData &data);
    // The program's cached structureHash, computed when the program was built by hand.
    [[nodiscard]] inline std::uint64_t ShaderGraphStructureHash(const ShaderGraphProgram &program)
    {
        return program.structureHash ? program.structureHash : ShaderGraphStructureHash(program.data);
    }

    // Emits ShaderGraphGenerated.slang for BasicLit.slang: straight-line code
    // with the same API as the interpreter in ShaderGraphEvaluation.slang.
    // Throws std::invalid_argument for malformed bytecode.
    [[nodiscard]] std::string GenerateShaderGraphSlang(const ShaderGraphProgramData &data);
}
