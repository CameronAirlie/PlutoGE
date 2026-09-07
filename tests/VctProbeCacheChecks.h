#pragma once
#include "PlutoGE/render/VctProbeCache.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "../engine/render/src/postprocess/VctProbeSampling.h"
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

inline bool CheckVctProbeCache()
{
    using namespace PlutoGE::render;
    VctProbeSchedule schedule;
    schedule.Refresh();
    for (unsigned frame = 0; frame < 63; ++frame) schedule.Advance(schedule.Budget(64));
    if (schedule.blend != 0.0f || schedule.initialized != 4032) return false;
    schedule.Advance(schedule.Budget(64));
    if (schedule.blend <= 0.0f || schedule.cursor != 0) return false;
    while (schedule.remaining) schedule.Advance(schedule.Budget(255));
    if (schedule.Budget(64) != 0 || schedule.blend != 1.0f) return false;
    schedule.Refresh();
    if (schedule.blend != 1.0f || schedule.initialized != 4096) return false;
    schedule.Reset();
    if (!schedule.clear || schedule.blend != 0.0f || schedule.initialized != 0) return false;

    ShaderSource source;
    source.computeSource = ShaderArtifactLibrary().Load("VCTProbeUpdate", "compute").glsl;
    if (source.computeSource.empty()) return false;
    auto *shader = Shader::Create(source);
    shader->Bind(); GLint program = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    GLint linked = 0; glGetProgramiv(GLuint(program), GL_LINK_STATUS, &linked);
    if (!linked) return false;
    shader->Bind();
    GLuint field = 0, radiance = 0, visibility = 0, parameters = 0;
    glGenTextures(1, &field); glBindTexture(GL_TEXTURE_3D, field);
    glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA16F, 32, 32, 32);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    std::vector<glm::vec4> voxels(32 * 32 * 32, glm::vec4(0.1f, 0.0f, 0.0f, 0.1f));
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 32, 32, 32, GL_RGBA, GL_FLOAT, voxels.data());
    for (GLuint slot = 7; slot <= 12; ++slot)
    { glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(GL_TEXTURE_3D, field); }
    for (auto *texture : {&radiance, &visibility})
    {
        glGenTextures(1, texture); glBindTexture(GL_TEXTURE_3D, *texture);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA16F, 16, 16, 96);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    // Allocation used the active unit; restore its source field before tracing.
    glBindTexture(GL_TEXTURE_3D, field);
    glBindImageTexture(1, radiance, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(2, visibility, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);
    glGenBuffers(1, &parameters); glBindBuffer(GL_UNIFORM_BUFFER, parameters);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(VctProbeParameters), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, parameters);
    const auto dispatch = [&](unsigned first, unsigned budget, bool clear)
    {
        VctProbeParameters pass{glm::vec4(0, 0, 0, 32), glm::uvec4(0, 1, 32, 0), glm::uvec4(first, budget, clear ? 1u : 0u, 0)};
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(pass), &pass);
        glDispatchCompute((budget + 63) / 64, 1, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    };
    std::vector<glm::vec4> values(16 * 16 * 96);
    const auto read = [&]()
    {
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_3D, radiance);
        glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, values.data());
    };
    dispatch(0, 4096, true); read();
    bool passed = true;
    for (const auto &v : values) passed &= v == glm::vec4(0);
    dispatch(0, 64, false); read();
    unsigned valid = 0;
    for (const auto &v : values)
    {
        if (v.a > 0.0f) { ++valid; passed &= v.r > 0.0f && v.r <= 1.001f && v.g == 0.0f && v.b == 0.0f; }
        passed &= std::isfinite(v.r) && std::isfinite(v.a);
    }
    passed &= valid == 64 * 6;
    const auto firstBatch = values;
    dispatch(64, 64, false); read();
    valid = 0;
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        valid += values[i].a > 0.0f ? 1 : 0;
        if (firstBatch[i].a > 0.0f) passed &= firstBatch[i] == values[i];
    }
    passed &= valid == 128 * 6;
    // An empty source must not manufacture blockers at its boundary.
    std::fill(voxels.begin(), voxels.end(), glm::vec4(0));
    glBindTexture(GL_TEXTURE_3D, field);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 32, 32, 32, GL_RGBA, GL_FLOAT, voxels.data());
    dispatch(0, 4096, true);
    dispatch(0, 64, false);
    glBindTexture(GL_TEXTURE_3D, visibility);
    glGetTexImage(GL_TEXTURE_3D, 0, GL_RGBA, GL_FLOAT, values.data());
    bool unoccluded = true;
    for (std::size_t i = 0; i < values.size(); ++i)
        if (firstBatch[i].a > 0.0f)
            unoccluded &= std::abs(values[i].r - std::sqrt(3.0f)) < 0.002f &&
                          std::abs(values[i].g - 3.0f) < 0.004f;
    if (!unoccluded) std::cerr << "Empty VCT field created false visibility blockers.\n";
    passed &= unoccluded;
    // Geometry moving over probes must invalidate their old illumination.
    std::fill(voxels.begin(), voxels.end(), glm::vec4(0, 0, 0, 1));
    glBindTexture(GL_TEXTURE_3D, field);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 32, 32, 32, GL_RGBA, GL_FLOAT, voxels.data());
    dispatch(0, 64, false); read();
    for (std::size_t i = 0; i < values.size(); ++i)
        if (firstBatch[i].a > 0.0f) passed &= values[i] == glm::vec4(0);
    passed &= glGetError() == GL_NO_ERROR;
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0); glDeleteBuffers(1, &parameters);
    const GLuint textures[] = {field, radiance, visibility}; glDeleteTextures(3, textures);
    glUseProgram(0); glDeleteProgram(GLuint(program)); delete shader;
    if (!passed) std::cerr << "VCT probe budget, persistence, or invalidation check failed.\n";
    return passed;
}

// Local radiance and blockers must survive cache warm-up and convergence.
// The near field is red, the stationary field blue, and the cache green.
inline bool CheckVctStationaryLighting()
{
    using namespace PlutoGE::render;
    ShaderArtifactLibrary artifacts;
    ShaderSource source;
    source.vertexSource = artifacts.Load("VCTConeTrace", "vertex").glsl;
    source.fragmentSource = artifacts.Load("VCTConeTrace", "fragment").glsl;
    auto *shader = Shader::Create(source);
    if (!shader) return false;
    shader->Bind(); GLint program = 0, linked = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program); glGetProgramiv(GLuint(program), GL_LINK_STATUS, &linked);
    if (!linked) return false;
    struct alignas(16) Parameters
    {
        glm::mat4 inverseViewProjection{1}, view{1};
        std::array<glm::vec4, 3> cascades{glm::vec4(-8,-8,-8,16), glm::vec4(-16,-16,-16,32), glm::vec4(-16,-16,-16,32)};
        glm::vec4 settings{1, 0.5f, 16, 0.35f};
        glm::uvec4 counts{2,2,1,0}, flags{0,0,1,1};
        glm::vec4 cacheOrigin{-16,-16,-16,32}, cacheSettings{1,1,0,0};
    } pass;
    static_assert(sizeof(Parameters) == 256);
    std::vector<GLuint> textures;
    const auto texture = [&](GLuint slot, GLenum target, int width, int height, int depth, const std::vector<glm::vec4>& values)
    {
        GLuint id = 0; glGenTextures(1, &id); textures.push_back(id);
        glActiveTexture(GL_TEXTURE0 + slot); glBindTexture(target, id);
        if (target == GL_TEXTURE_3D)
            glTexImage3D(target, 0, GL_RGBA32F, width, height, depth, 0, GL_RGBA, GL_FLOAT, values.data());
        else glTexImage2D(target, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, values.data());
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        return id;
    };
    texture(1, GL_TEXTURE_2D, 1,1,1, {glm::vec4(0)});
    texture(2, GL_TEXTURE_2D, 1,1,1, {glm::vec4(0.5f)});
    texture(3, GL_TEXTURE_2D, 1,1,1, {glm::vec4(0.5f,1,0.5f,1)});
    texture(4, GL_TEXTURE_2D, 1,1,1, {glm::vec4(0)});
    texture(5, GL_TEXTURE_2D, 1,1,1, {glm::vec4(1)});
    std::vector<glm::vec4> voxels(32*32*64, glm::vec4(0,0,0.2f,0.2f));
    std::fill(voxels.begin(), voxels.begin()+32*32*32, glm::vec4(0.4f,0,0,1));
    std::array<GLuint,6> fields;
    for (GLuint slot = 7; slot <= 12; ++slot) fields[slot-7] = texture(slot, GL_TEXTURE_3D, 32,32,64, voxels);
    const GLuint probeRadiance = texture(13, GL_TEXTURE_3D,16,16,96, std::vector<glm::vec4>(16*16*96, glm::vec4(0,0.6f,0,1)));
    const GLuint probeVisibility = texture(14, GL_TEXTURE_3D,16,16,96, std::vector<glm::vec4>(16*16*96, glm::vec4(2,4,0,0)));
    GLuint output = texture(0, GL_TEXTURE_2D,1,1,1, {glm::vec4(0)});
    GLuint framebuffer=0, vao=0, buffer=0;
    glGenFramebuffers(1,&framebuffer); glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    glGenBuffers(1,&buffer); glBindBuffer(GL_UNIFORM_BUFFER,buffer);
    glBufferData(GL_UNIFORM_BUFFER,sizeof(pass),nullptr,GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,buffer);
    glViewport(0,0,1,1); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    const auto render = [&]()
    {
        glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(pass),&pass);
        glDrawArrays(GL_TRIANGLES,0,3);
        glm::vec4 pixel; glReadPixels(0,0,1,1,GL_RGBA,GL_FLOAT,&pixel); return pixel;
    };
    bool passed = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    pass.cacheSettings = glm::vec4(0);
    const auto uncached = render();
    for (float blend : {0.0f, 0.5f, 1.0f})
    {
        pass.cacheSettings=glm::vec4(1,blend,0,0); pass.cascades[0]=glm::vec4(-8,-8,-8,16);
        // Move the volume while retaining the same world-space local field.
        const glm::vec4 before=render(); pass.cascades[0].x += 1;
        const glm::vec4 after=render();
        for (int channel=0;channel<3;++channel)
            passed &= std::isfinite(before[channel]) && std::abs(before[channel]-after[channel])<0.0001f &&
                      std::abs(before[channel]-uncached[channel])<0.0001f;
        passed &= before.r>0.3f && before.g<0.001f && before.b<0.001f;
    }
    const auto uploadField = [&]()
    {
        for (GLuint slot = 7; slot <= 12; ++slot)
        {
            glActiveTexture(GL_TEXTURE0+slot); glBindTexture(GL_TEXTURE_3D,fields[slot-7]);
            glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,32,32,64,GL_RGBA,GL_FLOAT,voxels.data());
        }
    };
    // A dark local blocker must occlude the bright distant cache completely.
    std::fill(voxels.begin(), voxels.begin()+32*32*32, glm::vec4(0,0,0,1));
    uploadField();
    const auto blocked = render();
    passed &= glm::length(glm::vec3(blocked)) < 0.001f;
    // With no blocker or local radiance, the distant cache must still be used.
    std::fill(voxels.begin(), voxels.end(), glm::vec4(0));
    uploadField();
    const auto distant = render();
    passed &= distant.g > 0.1f && distant.r < 0.001f && distant.b < 0.001f;
    pass.cacheSettings.y = 0;
    passed &= glm::length(glm::vec3(render())) < 0.001f;

    // Test the legacy lookup itself independently of where a cone hands off.
    glUseProgram(0); glDeleteProgram(GLuint(program)); delete shader;
    source.vertexSource = "#version 430 core\nvoid main(){vec2 p[3]=vec2[3](vec2(-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);}";
    source.fragmentSource = std::string("#version 430 core\nuniform sampler3D uProbeRadiance,uProbeVisibility;uniform vec4 uCacheOriginSize;out vec4 color;\n") +
        kVctProbeSampling + "\nvoid main(){vec4 c=cachedIrradiance(vec3(0,0,.5),vec3(0,1,0),uCacheOriginSize);color=vec4(c.rgb*c.a,1);}";
    shader = Shader::Create(source); Shader::ResetStateCache(); shader->Bind();
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);
    glGetProgramiv(GLuint(program),GL_LINK_STATUS,&linked); passed &= linked != 0;
    shader->SetUniform("uProbeRadiance",13); shader->SetUniform("uProbeVisibility",14);
    // Isolate one probe with different X/Y visibility, then cross its face
    // diagonal by a tiny amount. Lighting must vary continuously across it.
    std::vector<glm::vec4> probeValues(16*16*96, glm::vec4(0));
    for (int face = 0; face < 6; ++face)
        probeValues[8 + 16 * (8 + 16 * (8 + face * 16))] = glm::vec4(0, 1, 0, 1);
    glActiveTexture(GL_TEXTURE13); glBindTexture(GL_TEXTURE_3D, probeRadiance);
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,16,16,96,GL_RGBA,GL_FLOAT,probeValues.data());
    for (int face = 0; face < 6; ++face)
        std::fill(probeValues.begin() + face * 4096, probeValues.begin() + (face + 1) * 4096,
                  face < 2 ? glm::vec4(0,0,0,0) : glm::vec4(2,4,0,0));
    glActiveTexture(GL_TEXTURE14); glBindTexture(GL_TEXTURE_3D, probeVisibility);
    glTexSubImage3D(GL_TEXTURE_3D,0,0,0,0,16,16,96,GL_RGBA,GL_FLOAT,probeValues.data());
    pass.cacheSettings.y = 1.0f;
    // Receiver is at (0,0,0.5), facing +Y. Shift the probe grid across its
    // X/Y diagonal without depending on backend matrix layout conventions.
    pass.inverseViewProjection = glm::mat4(1);
    pass.cacheOrigin = glm::vec4(-16.0001f,-16,-16.5f,32);
    shader->SetUniform("uCacheOriginSize",pass.cacheOrigin);
    const auto diagonalBefore = render();
    pass.cacheOrigin.x = -15.9999f;
    shader->SetUniform("uCacheOriginSize",pass.cacheOrigin);
    const auto diagonalAfter = render();
    const bool continuous = diagonalBefore.g > 0.01f && std::abs(diagonalBefore.g - diagonalAfter.g) < 0.001f;
    if (!continuous) std::cerr << "VCT probe direction boundary changed GI from "
                               << diagonalBefore.g << " to " << diagonalAfter.g << ".\n";
    passed &= continuous;
    passed &= glGetError()==GL_NO_ERROR;
    glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
    glBindVertexArray(0); glDeleteVertexArrays(1,&vao);
    glBindBufferBase(GL_UNIFORM_BUFFER,0,0); glDeleteBuffers(1,&buffer);
    glDeleteTextures(GLsizei(textures.size()),textures.data());
    glUseProgram(0); glDeleteProgram(GLuint(program)); delete shader;
    if (!passed) std::cerr << "VCT stationary lighting or probe continuity check failed.\n";
    return passed;
}
