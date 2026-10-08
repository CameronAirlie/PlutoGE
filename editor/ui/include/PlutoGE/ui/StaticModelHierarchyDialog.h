#pragma once
#include <memory>
#include <string>

namespace PlutoGE::ui
{
    class StaticModelHierarchyDialog
    {
    public:
        StaticModelHierarchyDialog();
        ~StaticModelHierarchyDialog();
        void Render(const std::string &sourceReference);
    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
