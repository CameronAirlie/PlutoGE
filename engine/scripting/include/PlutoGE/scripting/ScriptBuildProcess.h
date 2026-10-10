#pragma once
#include <string>
#include <vector>

namespace PlutoGE::scripting
{
    struct ProcessOutput { int exitCode = -1; std::string text; };
    // Direct process creation, never a shell. Output is bounded while pipes are
    // drained completely so a verbose compiler cannot deadlock the child.
    ProcessOutput RunBuildProcess(const std::vector<std::string> &arguments);
}
