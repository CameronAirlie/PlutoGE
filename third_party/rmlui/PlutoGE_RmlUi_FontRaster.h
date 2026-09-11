#pragma once

#include <RmlUi/Core/Header.h>

// Call after Rml::Initialise, outside Context::Render. A density change releases
// the font atlas and invalidates text geometry through RmlUi's public API.
RMLUICORE_API void PlutoGE_SetRmlUiFontRasterScale(float scale);
RMLUICORE_API int PlutoGE_GetRmlUiFontRasterScale();
