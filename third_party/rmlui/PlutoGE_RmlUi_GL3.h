#pragma once

#include "PlutoGE_RmlUi_Target.h"

// This macro is intentionally visible only while compiling RmlUi's stock GL3
// backend. Non-zero internal layer framebuffers pass through unchanged.
#ifdef glBindFramebuffer
#undef glBindFramebuffer
#endif
#define glBindFramebuffer PlutoGE_RmlUiBindFramebuffer
