#include "PlutoGE_RmlUi_Target.h"

namespace
{
    GLuint g_rmlUiFramebuffer = 0;
}

void PlutoGE_SetRmlUiFramebuffer(GLuint framebuffer)
{
    g_rmlUiFramebuffer = framebuffer;
}

void PlutoGE_RmlUiBindFramebuffer(GLenum target, GLuint framebuffer)
{
    glad_glBindFramebuffer(target, framebuffer == 0 ? g_rmlUiFramebuffer : framebuffer);
}
