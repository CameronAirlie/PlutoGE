#include "PlutoGE/ui/ListEditor.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE::ui;
namespace
{
    void Require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
    std::vector<std::string> Moved(std::size_t from, std::size_t to)
    {
        std::vector<std::string> items = {"a", "b", "c", "d"};
        MoveListElement(items, from, to);
        return items;
    }
}
int main()
try
{
    Require(Moved(0, 2) == std::vector<std::string>{"b", "c", "a", "d"}, "Moving down must land at the target index");
    Require(Moved(3, 1) == std::vector<std::string>{"a", "d", "b", "c"}, "Moving up must land at the target index");
    Require(Moved(0, 3) == std::vector<std::string>{"b", "c", "d", "a"}, "Moving to the end failed");
    Require(Moved(2, 2) == std::vector<std::string>{"a", "b", "c", "d"}, "Moving onto itself must not change order");
    Require(Moved(1, 9) == std::vector<std::string>{"a", "b", "c", "d"}, "Out-of-range moves must be ignored");
    std::cout << "List editor checks passed.\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
