#pragma once

// The FSR2 2.2.1 Vulkan backend relies on <codecvt> transitively including
// std::wstring_convert and on Vulkan's global entry points. Keep those legacy
// assumptions contained in the vendor target instead of leaking them into the
// engine or patching downloaded source.
#include <locale>
#include <volk.h>
