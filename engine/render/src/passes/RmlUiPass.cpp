#include "PlutoGE/render/passes/RmlUiPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/scene/Scene.h"

#include <PlutoGE_RmlUi_Target.h>

namespace PlutoGE::render
{
    RmlUiPass::~RmlUiPass()
    {
        if (m_surfaceVao)
            glDeleteVertexArrays(1, &m_surfaceVao);
    }

    void RmlUiPass::Initialize()
    {
        ShaderSource source;
        source.vertexSource = R"(
            #version 330 core
            uniform mat4 uModel;
            uniform mat4 uView;
            uniform mat4 uProjection;
            out vec2 vUv;
            void main()
            {
                const vec2 positions[6] = vec2[6](
                    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
                    vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5));
                const vec2 uvs[6] = vec2[6](
                    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
                    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0));
                vUv = uvs[gl_VertexID];
                gl_Position = uProjection * uView * uModel * vec4(positions[gl_VertexID], 0.0, 1.0);
            })";
        source.fragmentSource = R"(
            #version 330 core
            uniform sampler2D uSurface;
            in vec2 vUv;
            out vec4 fragColor;
            void main()
            {
                vec4 color = texture(uSurface, vUv);
                if (color.a <= 0.001) discard;
                fragColor = color;
            })";
        m_surfaceShader.reset(Shader::Create(source));
        glGenVertexArrays(1, &m_surfaceVao);
    }

    void RmlUiPass::DrawWorldSurfaces(const RenderContext &ctx)
    {
        if (!m_surfaceShader || !m_surfaceVao)
            return;

        if (ctx.renderTarget)
            Graphics::BindRenderTarget(ctx.renderTarget);
        else
            glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_surfaceShader->Bind();
        m_surfaceShader->SetUniform("uView", ctx.cameraData.view);
        m_surfaceShader->SetUniform("uProjection", ctx.cameraData.projection);
        m_surfaceShader->SetUniform("uSurface", 0);
        glBindVertexArray(m_surfaceVao);
        glActiveTexture(GL_TEXTURE0);
        for (const auto &surface : RmlUiRuntime::Get().GetWorldSurfaces())
        {
            m_surfaceShader->SetUniform("uModel", surface.model);
            glBindTexture(GL_TEXTURE_2D, surface.texture);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindVertexArray(0);
        m_surfaceShader->Unbind();
        glDepthMask(GL_TRUE);
    }

    void RmlUiPass::Execute(const RenderContext &ctx)
    {
        if (!ctx.scene || !ctx.renderer || !ctx.scene->HasRmlRuntimeUI())
            return;

        int width = 0;
        int height = 0;
        if (ctx.renderTarget)
        {
            width = ctx.renderTarget->GetWidth();
            height = ctx.renderTarget->GetHeight();
            Graphics::BindRenderTarget(ctx.renderTarget);
            PlutoGE_SetRmlUiFramebuffer(ctx.renderTarget->GetFramebufferID());
        }
        else if (ctx.temporaryRenderTarget)
        {
            width = ctx.temporaryRenderTarget->GetWidth();
            height = ctx.temporaryRenderTarget->GetHeight();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            PlutoGE_SetRmlUiFramebuffer(0);
        }

        if (width > 0 && height > 0)
            RmlUiRuntime::Get().Render(*ctx.scene, width, height, ctx.frameSequence,
                                      ctx.cameraData.view, ctx.cameraData.projection,
                                      [this, &ctx]() { DrawWorldSurfaces(ctx); });
    }
}
