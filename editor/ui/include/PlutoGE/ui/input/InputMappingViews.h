#pragma once

#include "PlutoGE/ui/InputMappingDocument.h"
#include "PlutoGE/ui/input/InputBindingCapture.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::platform
{
    struct InputState;
}

namespace PlutoGE::ui
{
    class InputMappingSession;
}

// The tabs of the input mapping editor. Views keep their own UI state and
// edit through InputMappingSession::Modify, so every change is undoable.
namespace PlutoGE::ui::input
{
    // Reports derived from the mapping, rebuilt only when it changes.
    struct InputMappingAnalysis
    {
        std::uint64_t revision = 0;
        std::vector<InputMappingSharedInput> sharedInputs;
        std::map<std::string, std::size_t> sharedByInputId; // index into sharedInputs
        std::vector<std::string> invalidNames;
        std::vector<std::string> unusedGamepadButtons;
        std::vector<std::string> unusedKeys;
        std::vector<int> actionProblems; // per action: bad name plus conflicting bindings
        int conflictCount = 0;           // shared inputs not told apart by triggers

        void Update(const InputMappingSession &session);
        // The shared-input entry for a binding's physical input, if others use it.
        [[nodiscard]] const InputMappingSharedInput *FindShared(const InputMappingBinding &binding) const;
        [[nodiscard]] bool IsNameInvalid(const std::string &name) const;
        [[nodiscard]] int ProblemCount() const { return static_cast<int>(invalidNames.size()) + conflictCount; }
    };

    // The physical input `inputId` (InputMappingBinding::InputId) as a binding
    // with no scale or trigger of its own, for showing it; empty when unused.
    [[nodiscard]] std::optional<InputMappingBinding> FindPhysicalInput(const std::vector<InputMappingAction> &actions,
                                                                       const std::string &inputId);

    struct ActionsViewState
    {
        int selected = 0;
        int expandedBinding = -1;
        std::string search;
        bool focusName = false; // a new action's name field takes focus
        InputBindingCapture capture;
        bool heardThisFrame = false; // a captured key must not also press the focused button
    };
    // Action list beside the selected action's bindings. `input` (null when
    // there is no window, as in tests) drives press-to-bind.
    void DrawActionsView(InputMappingSession &session, ActionsViewState &state, const InputMappingAnalysis &analysis,
                         const platform::InputState *input);

    // Shared and free inputs across the mapping. Returns an action the user
    // clicked, to show in the actions view.
    [[nodiscard]] std::optional<int> DrawOverviewView(const InputMappingSession &session, const InputMappingAnalysis &analysis);

    // What connected devices report right now, for checking a controller.
    void DrawDevicesView(const platform::InputState *input);
}
