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

void PlutoGE_CopyRmlUiBackdrop(int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    // BeginFrame leaves RmlUi's base layer bound. Copy the already-rendered
    // scene into it so backdrop-filter operates on scene pixels as well as
    // previously rendered UI content.
    GLint rmlFramebuffer = 0;
    glad_glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &rmlFramebuffer);
    if (static_cast<GLuint>(rmlFramebuffer) == g_rmlUiFramebuffer)
        return;

    glad_glBindFramebuffer(GL_READ_FRAMEBUFFER, g_rmlUiFramebuffer);
    glad_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(rmlFramebuffer));
    glad_glBlitFramebuffer(
        0, 0, width, height,
        0, 0, width, height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glad_glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(rmlFramebuffer));
}
