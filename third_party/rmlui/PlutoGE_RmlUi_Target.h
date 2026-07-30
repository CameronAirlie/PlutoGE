#pragma once

#include <glad/glad.h>

// The stock RmlUi GL3 backend composites its final layer to framebuffer zero.
// PlutoGE also renders into editor and preview render targets, so the backend's
// notion of the backbuffer must be redirected for the duration of its pass.
void PlutoGE_SetRmlUiFramebuffer(GLuint framebuffer);
void PlutoGE_RmlUiBindFramebuffer(GLenum target, GLuint framebuffer);
void PlutoGE_CopyRmlUiBackdrop(int width, int height);
