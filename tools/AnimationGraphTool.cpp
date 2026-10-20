// Command-line helpers for .plutoanimgraph assets.
//
//   AnimationGraphTool report <graph>...          Duplicate transitions and problems.
//   AnimationGraphTool consolidate <in> <out>     Merge shared transitions into their groups.
//   AnimationGraphTool compare <a> <b>            Exit 0 when both graphs behave identically.
//
// "Behave identically" means every state (matched by name) checks the same
// transitions, in the same order, with the same gates and blends.

#include "PlutoGE/assets/AnimationGraphSerialization.h"
#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/assets/AnimationGraphValidation.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE::assets;
namespace ag = PlutoGE::assets::animgraph;

namespace
{
    AnimationGraphAsset Load(const std::string &path)
    {
        std::ifstream input(path);
        if (!input.is_open())
            throw std::runtime_error("Could not open " + path);
        return ParseAnimationGraph(input);
    }

    std::string Describe(const AnimationGraphAsset &graph, const ag::CompiledTransition &compiled)
    {
        const auto &transition = *compiled.source;
        std::ostringstream text;
        text << "-> " << ag::NodeName(graph, AnimationGraphNodeRef::ForState(compiled.toStateId)) << " when "
             << ag::DescribeTransitionGate(graph, transition) << " blend " << transition.duration << " offset " << transition.offset;
        return text.str();
    }

    std::map<std::string, std::vector<std::string>> Behaviour(const AnimationGraphAsset &graph)
    {
        std::map<std::string, std::vector<std::string>> behaviour;
        for (const auto &state : graph.states)
            behaviour[state.name];
        for (const auto &compiled : ag::CompileTransitions(graph))
            behaviour[ag::NodeName(graph, AnimationGraphNodeRef::ForState(compiled.fromStateId))].push_back(Describe(graph, compiled));
        return behaviour;
    }

    int Report(const std::string &path)
    {
        const auto graph = Load(path);
        std::cout << path << ": " << graph.states.size() << " states, " << graph.groups.size() << " groups, "
                  << graph.transitions.size() << " transitions\n";
        std::size_t redundant = 0;
        for (const auto &cluster : ag::FindSharedTransitionClusters(graph))
        {
            const auto *transition = ag::FindTransition(graph, cluster.front());
            std::cout << "  " << cluster.size() << " states share -> " << ag::NodeName(graph, transition->to) << " when "
                      << ag::DescribeTransitionGate(graph, *transition) << "\n";
            redundant += cluster.size() - 1;
        }
        std::cout << "  " << redundant << " duplicated state transitions\n";
        int problems = 0;
        for (const auto &diagnostic : ag::Validate(graph))
        {
            if (diagnostic.severity == ag::DiagnosticSeverity::Info)
                continue;
            ++problems;
            std::cout << "  [" << (diagnostic.severity == ag::DiagnosticSeverity::Error ? "error" : "warning") << "] "
                      << diagnostic.message << "\n";
        }
        std::cout << "  " << problems << " problems\n";
        return 0;
    }

    int Consolidate(const std::string &inputPath, const std::string &outputPath)
    {
        auto graph = Load(inputPath);
        const std::size_t before = graph.transitions.size();

        // Outer groups first: merging into a nested group would stop the
        // enclosing group from taking the same transitions later.
        std::vector<int> groups;
        for (const auto &group : graph.groups)
            groups.push_back(group.id);
        std::stable_sort(groups.begin(), groups.end(), [&graph](int a, int b)
                         { return ag::GroupPath(graph, a).size() < ag::GroupPath(graph, b).size(); });
        groups.insert(groups.begin(), kAnimationGraphRootGroupId);
        for (const int groupId : groups)
            if (const int replaced = ag::ConsolidateGroupTransitions(graph, groupId); replaced > 0)
                std::cout << "  " << ag::NodeName(graph, AnimationGraphNodeRef::ForGroup(groupId)) << ": merged " << replaced
                          << " transitions into " << ag::TransitionsFrom(graph, AnimationGraphNodeRef::ForGroup(groupId)).size()
                          << "\n";

        std::ofstream output(outputPath, std::ios::trunc);
        if (!output.is_open())
            throw std::runtime_error("Could not write " + outputPath);
        WriteAnimationGraph(output, graph);
        std::cout << "Transitions: " << before << " -> " << graph.transitions.size() << "\n";
        return 0;
    }

    int Compare(const std::string &firstPath, const std::string &secondPath)
    {
        const auto first = Behaviour(Load(firstPath));
        const auto second = Behaviour(Load(secondPath));
        int differences = 0;
        for (const auto &[state, transitions] : first)
        {
            const auto it = second.find(state);
            if (it == second.end())
            {
                std::cout << "State '" << state << "' is missing from " << secondPath << "\n";
                ++differences;
            }
            else if (it->second != transitions)
            {
                std::cout << "State '" << state << "' differs:\n";
                for (std::size_t index = 0; index < std::max(transitions.size(), it->second.size()); ++index)
                    std::cout << "  " << (index < transitions.size() ? transitions[index] : "(none)") << "\n  vs "
                              << (index < it->second.size() ? it->second[index] : "(none)") << "\n";
                ++differences;
            }
        }
        for (const auto &[state, transitions] : second)
            if (!first.contains(state))
            {
                std::cout << "State '" << state << "' is missing from " << firstPath << "\n";
                ++differences;
            }
        std::cout << (differences == 0 ? "Graphs behave identically.\n" : std::to_string(differences) + " states differ.\n");
        return differences == 0 ? 0 : 1;
    }
}

int main(int argc, char **argv)
{
    try
    {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "report" && argc >= 3)
        {
            for (int index = 2; index < argc; ++index)
                Report(argv[index]);
            return 0;
        }
        if (command == "consolidate" && argc == 4)
            return Consolidate(argv[2], argv[3]);
        if (command == "compare" && argc == 4)
            return Compare(argv[2], argv[3]);
        std::cerr << "Usage:\n  AnimationGraphTool report <graph>...\n  AnimationGraphTool consolidate <in> <out>\n"
                     "  AnimationGraphTool compare <a> <b>\n";
        return 2;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
