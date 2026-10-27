#include "PlutoGE/ui/input/InputMappingSession.h"
#include "PlutoGE/ui/input/InputMappingViews.h"
#include "PlutoGE/ui/input/InputMappingWidgets.h"

#include <algorithm>

namespace PlutoGE::ui::input
{
    std::optional<InputMappingBinding> FindPhysicalInput(const std::vector<InputMappingAction> &actions, const std::string &inputId)
    {
        for (const auto &action : actions)
            for (const auto &binding : action.bindings)
                if (binding.InputId() == inputId)
                {
                    InputMappingBinding input;
                    input.kind = binding.kind;
                    input.value = binding.value;
                    input.gamepad = binding.gamepad;
                    return input;
                }
        return std::nullopt;
    }

    void InputMappingAnalysis::Update(const InputMappingSession &session)
    {
        if (revision == session.GetRevision())
            return;
        revision = session.GetRevision();
        const auto &document = session.GetDocument();
        sharedInputs = document.FindSharedInputs();
        invalidNames = document.FindInvalidActionNames();
        unusedGamepadButtons = document.FindUnusedGamepadButtons();
        unusedKeys = document.FindUnusedKeys();

        sharedByInputId.clear();
        conflictCount = 0;
        const auto &actions = document.Actions();
        for (std::size_t index = 0; index < sharedInputs.size(); ++index)
        {
            if (const auto input = FindPhysicalInput(actions, sharedInputs[index].inputId))
                sharedInputs[index].label = std::string(BindingKindIcon(input->kind)) + " " + BindingInputLabel(*input);
            sharedByInputId.emplace(sharedInputs[index].inputId, index);
            if (!sharedInputs[index].disambiguatedByTriggers)
                ++conflictCount;
        }

        actionProblems.assign(actions.size(), 0);
        for (std::size_t index = 0; index < actions.size(); ++index)
        {
            auto &problems = actionProblems[index];
            problems += IsNameInvalid(actions[index].name) ? 1 : 0;
            for (const auto &binding : actions[index].bindings)
                if (const auto *shared = FindShared(binding); shared && !shared->disambiguatedByTriggers)
                    ++problems;
        }
    }

    const InputMappingSharedInput *InputMappingAnalysis::FindShared(const InputMappingBinding &binding) const
    {
        const auto found = sharedByInputId.find(binding.InputId());
        return found == sharedByInputId.end() ? nullptr : &sharedInputs[found->second];
    }

    bool InputMappingAnalysis::IsNameInvalid(const std::string &name) const
    {
        return name.empty() || std::find(invalidNames.begin(), invalidNames.end(), name) != invalidNames.end();
    }
}
