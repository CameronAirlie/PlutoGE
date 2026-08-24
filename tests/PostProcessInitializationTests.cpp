#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/TextureManager.h"
#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/LSAOEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"

#include <glad/glad.h>

#include <iostream>

int main()
{
    PlutoGE::platform::Window window;
    if (!window.Create({
            .title = "PlutoGE post-process initialization test",
            .width = 64,
            .height = 64,
            .resizable = false,
            .visible = false,
        }))
    {
        std::cerr << "Failed to create the hidden OpenGL test window.\n";
        return 1;
    }

    if (!window.EnsureOpenGLContextCurrent(true))
    {
        std::cerr << "Failed to prepare the OpenGL dispatch table.\n";
        window.Close();
        return 1;
    }

    while (glGetError() != GL_NO_ERROR)
    {
    }

    {
        // Simulate an editor subsystem leaving pixel-unpack state active before a scene texture load.
        // A CPU pointer passed with this buffer still bound would be interpreted as a buffer offset.
        GLuint unpackBuffer = 0;
        glGenBuffers(1, &unpackBuffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        glBufferData(GL_PIXEL_UNPACK_BUFFER, 16, nullptr, GL_STREAM_DRAW);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 37);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 5);

        constexpr unsigned char pixels[] = {
            255, 0, 0, 0, 255, 0,
            0, 0, 255, 255, 255, 255,
        };
        PlutoGE::render::TextureManager textureManager;
        textureManager.SetWindow(&window);
        PlutoGE::render::Texture *texture = textureManager.LoadTextureFromMemory(
            "pixel-unpack-state-regression", pixels, 2, 2, 3);

        GLint unpackBinding = -1;
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBinding);
        const GLenum error = glGetError();
        if (!texture || unpackBinding != 0 || error != GL_NO_ERROR)
        {
            std::cerr << "Texture upload did not isolate inherited pixel-unpack state; binding="
                      << unpackBinding << ", error=0x" << std::hex << error << ".\n";
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glDeleteBuffers(1, &unpackBuffer);
            window.Close();
            return 1;
        }

        const GLuint textureId = texture->GetTextureID();
        glDeleteTextures(1, &textureId);
        delete texture;
        glDeleteBuffers(1, &unpackBuffer);
    }

    {
        PlutoGE::render::ColorGradingEffect effect;
        effect.EnsureInitialized();
        if (!effect.IsInitialized() || glGetError() != GL_NO_ERROR)
        {
            std::cerr << "Color grading shader did not initialize cleanly.\n";
            window.Close();
            return 1;
        }
    }

    {
        PlutoGE::render::LSAOEffect effect;
        effect.EnsureInitialized();
        if (!effect.IsInitialized())
        {
            std::cerr << "LSAO did not initialize.\n";
            window.Close();
            return 1;
        }

        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            std::cerr << "LSAO initialization produced OpenGL error 0x" << std::hex << error << ".\n";
            window.Close();
            return 1;
        }
    }

    {
        PlutoGE::render::SSGIEffect effect;
        effect.EnsureInitialized();
        if (!effect.IsInitialized())
        {
            std::cerr << "SSGI did not initialize.\n";
            window.Close();
            return 1;
        }

        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
        {
            std::cerr << "SSGI initialization produced OpenGL error 0x" << std::hex << error << ".\n";
            window.Close();
            return 1;
        }
    }

    window.Close();
    return 0;
}
