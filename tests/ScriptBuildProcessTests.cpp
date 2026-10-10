#include "PlutoGE/scripting/ScriptBuildProcess.h"
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) try
{
    if (argc > 1)
    {
        if (std::string_view(argv[1]) == "--large") { std::cout << std::string(5 * 1024 * 1024, 'x'); return 7; }
        for (int i = 1; i < argc; ++i) std::cout << argv[i] << '\n';
        std::cerr << "stderr\n";
        return 0;
    }
    using PlutoGE::scripting::RunBuildProcess;
    const auto executable = std::filesystem::absolute(argv[0]).string();
    const auto result = RunBuildProcess({executable, "space argument", "literal\"quote", "trailing\\", "%PATH% & $(echo unexpected)"});
    auto normalized = result.text;
    normalized.erase(std::remove(normalized.begin(), normalized.end(), '\r'), normalized.end());
    if (result.exitCode != 0 || normalized != "space argument\nliteral\"quote\ntrailing\\\n%PATH% & $(echo unexpected)\nstderr\n")
        throw std::runtime_error("Argument quoting or combined output failed: " + result.text);
    const auto large = RunBuildProcess({executable, "--large"});
    if (large.exitCode != 7 || large.text.size() != 4 * 1024 * 1024) throw std::runtime_error("Output bound or exit code failed");
    if (RunBuildProcess({"pluto-nonexistent-executable-91283"}).exitCode == 0) throw std::runtime_error("Missing executable reported success");
    std::cout << "Script build process tests passed.\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
