#include "PlutoGE/ui/ComponentInspectors.h"

#include <unordered_map>

namespace PlutoGE::ui
{
    namespace
    {
        std::unordered_map<scene::ComponentTypeID, ComponentInspector> &Storage()
        {
            static std::unordered_map<scene::ComponentTypeID, ComponentInspector> inspectors;
            return inspectors;
        }

        // Built-in inspectors register on first lookup. Kept apart from
        // Storage() because registration itself writes to the storage.
        void EnsureBuiltins()
        {
            static const bool registered = (RegisterUIComponentInspectors(), true);
            (void)registered;
        }
    }

    void RegisterComponentInspector(scene::ComponentTypeID type, ComponentInspector inspector)
    {
        Storage()[type] = inspector;
    }

    ComponentInspector FindComponentInspector(scene::ComponentTypeID type)
    {
        EnsureBuiltins();
        const auto found = Storage().find(type);
        return found == Storage().end() ? nullptr : found->second;
    }
}
