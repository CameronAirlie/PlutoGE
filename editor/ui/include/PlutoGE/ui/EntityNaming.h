#pragma once

#include <span>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    // The name for a copy of `name` among `siblingNames`: "Crate" -> "Crate (1)", "Crate (1)" -> "Crate (2)",
    // skipping numbers a sibling already uses. Legacy " Copy" suffixes are folded into the number.
    [[nodiscard]] std::string NextDuplicateName(std::string_view name, std::span<const std::string> siblingNames);

    // A code identifier as a readable label: "moveSpeed" and "m_moveSpeed" -> "Move Speed", "HUDScale" ->
    // "HUD Scale", "jump2Height" -> "Jump 2 Height".
    [[nodiscard]] std::string NicifyFieldName(std::string_view identifier);
}
