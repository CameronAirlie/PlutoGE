#pragma once
#include "../engine/render/src/postprocess/VctVoxelizationShaders.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

// Read the real floating-point voxel field, avoiding tone mapping, screen-space
// filtering and 8-bit display quantization when checking energy conservation.
inline bool CheckVctEmissionCoverage()
{
    using namespace PlutoGE::render;
    bool passed = true;
    for (const bool legacy : {false, true})
    {
        ShaderSource voxelSource, resolveSource, mipSource;
        mipSource.computeSource = ShaderArtifactLibrary().Load("VCTDirectionalMip", "compute").glsl;
        if (legacy)
        {
            voxelSource = detail::VctVoxelizationShaderSource();
            resolveSource = detail::VctResolveShaderSource();

        }
        else
        {
            ShaderArtifactLibrary artifacts;
            voxelSource.vertexSource = artifacts.Load("VCTVoxelize", "vertex").glsl;
            voxelSource.geometrySource = artifacts.Load("VCTVoxelize", "geometry").glsl;
            voxelSource.fragmentSource = artifacts.Load("VCTVoxelize", "fragment").glsl;
            resolveSource.computeSource = artifacts.Load("VCTResolve", "compute").glsl;
        }
        std::unique_ptr<Shader> voxel(Shader::Create(voxelSource)), resolve(Shader::Create(resolveSource)), mip(Shader::Create(mipSource));
        if (!voxel || !resolve || !mip) return false;
        for (auto* shader : {voxel.get(), resolve.get(), mip.get()})
        {
            shader->Bind(); GLint program = 0, linked = 0;
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetProgramiv(GLuint(program), GL_LINK_STATUS, &linked);
            if (!linked) return false;
        }
        GLuint vao = 0, vbo = 0, framebuffer = 0;
        std::array<GLuint, 3> buffers{};
        std::array<GLuint, 8> textures{};
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        struct Vertex { glm::vec3 position, normal; glm::vec2 uv; };
        for (GLuint attribute = 0; attribute < 3; ++attribute)
        {
            glEnableVertexAttribArray(attribute);
            glVertexAttribPointer(attribute, attribute == 2 ? 2 : 3, GL_FLOAT, GL_FALSE,
                sizeof(Vertex), reinterpret_cast<void*>(attribute * sizeof(glm::vec3)));
        }
        glGenBuffers(GLsizei(buffers.size()), buffers.data());
        glGenTextures(GLsizei(textures.size()), textures.data());
        glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, 32);
        glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, 32);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        const auto upload = [&](unsigned slot, const void* data, std::size_t size)
        {
            glBindBuffer(GL_UNIFORM_BUFFER, buffers[slot]);
            glBufferData(GL_UNIFORM_BUFFER, GLsizeiptr(size), data, GL_DYNAMIC_DRAW);
            glBindBufferBase(GL_UNIFORM_BUFFER, slot, buffers[slot]);
        };
        const auto measure = [&](float side, float phase, int subdivisions, int resolution, int axis, bool reversed, float emission = 8.0f, float bounce = 0.0f, float metallic = 0.0f, float sourceRadiance = 2.0f, int repetitions = 1)
        {
            constexpr float volumeSize = 16.0f;
            const float voxelSize = volumeSize / float(resolution);
            std::vector<Vertex> vertices;
            const auto vertex = [&](float x, float y)
            {
                glm::vec3 p(4.0f + phase + x * side, 4.0f + phase + y * side, 5.125f);
                glm::vec3 n(0, 0, 1);
                if (axis == 0) { std::swap(p.x, p.z); std::swap(n.x, n.z); }
                if (axis == 1) { std::swap(p.y, p.z); std::swap(n.y, n.z); }
                return Vertex{p, n, {x,y}};
            };
            for (int y = 0; y < subdivisions; ++y)
                for (int x = 0; x < subdivisions; ++x)
                {
                    float x0 = float(x)/subdivisions, x1 = float(x+1)/subdivisions;
                    float y0 = float(y)/subdivisions, y1 = float(y+1)/subdivisions;
                    std::array<Vertex,6> cell{vertex(x0,y0),vertex(x1,y0),vertex(x0,y1),
                                              vertex(x0,y1),vertex(x1,y0),vertex(x1,y1)};
                    if (reversed) { std::swap(cell[0],cell[2]); std::swap(cell[3],cell[5]); }
                    vertices.insert(vertices.end(),cell.begin(),cell.end());
                }
            glBindBuffer(GL_ARRAY_BUFFER,vbo);
            glBufferData(GL_ARRAY_BUFFER,GLsizeiptr(vertices.size()*sizeof(Vertex)),vertices.data(),GL_DYNAMIC_DRAW);
            glDeleteTextures(GLsizei(textures.size()), textures.data());
            glGenTextures(GLsizei(textures.size()), textures.data());
            const int count = resolution*resolution*resolution;
            std::vector<unsigned> zeros(count,0);
            for (unsigned i=0;i<5;++i)
            {
                glBindTexture(GL_TEXTURE_3D,textures[i]);
                glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,0);
                glTexImage3D(GL_TEXTURE_3D,0,GL_R32UI,resolution,resolution,resolution,0,GL_RED_INTEGER,GL_UNSIGNED_INT,zeros.data());
                if (legacy || i<4) glBindImageTexture(legacy?i:i+4,textures[i],0,GL_TRUE,0,GL_READ_WRITE,GL_R32UI);
            }
            glBindTexture(GL_TEXTURE_3D,textures[5]);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,0);
            glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,resolution,resolution,resolution,0,GL_RGBA,GL_FLOAT,nullptr);
            // Immutable synthetic field: opaque incident radiance is returned
            // at the first sample. This isolates the shared bounce integral and
            // material response from mip/filter and scene coverage errors.
            std::vector<glm::vec4> incident(count,glm::vec4(sourceRadiance,sourceRadiance,sourceRadiance,1));
            for (unsigned direction=0;direction<6;++direction)
            {
                glActiveTexture(GL_TEXTURE0+(legacy?10:14)+direction);
                glBindTexture(GL_TEXTURE_3D,textures[7]);
            }
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,0);
            glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,resolution,resolution,resolution,0,GL_RGBA,GL_FLOAT,incident.data());
            glActiveTexture(GL_TEXTURE0);
            voxel->Bind();
            if (legacy)
            {
                voxel->SetUniform("uModel",glm::mat4(1)); voxel->SetUniform("uUVScale",glm::vec2(1));
                voxel->SetUniform("uVolumeOrigin",glm::vec3(0)); voxel->SetUniform("uVolumeSize",volumeSize);
                voxel->SetUniform("uVoxelResolution",resolution); voxel->SetUniform("uColor",glm::vec4(1));
                voxel->SetUniform("uEmission",glm::vec3(emission,emission*.5f,emission*.25f));
                voxel->SetUniform("uSecondaryBounce",bounce);
                voxel->SetUniform("uMetallicFactor",metallic);
            }
            else
            {
                struct VoxelPass { glm::vec4 originSize; glm::uvec4 counts; std::array<glm::vec4,78> unused{}; } pass{{0,0,0,volumeSize},{resolution,0,std::bit_cast<unsigned>(bounce),0}};
                struct MaterialPass { glm::vec4 color{1}; glm::vec2 uv{1}; float metallic=0,cutoff=0; glm::vec3 emission{8,4,2}; unsigned alpha=0; glm::uvec4 flags{0}; } material;
                static_assert(sizeof(MaterialPass)==64);
                material.emission = {emission,emission*.5f,emission*.25f};
                material.metallic = metallic;
                glm::mat4 model(1);
                upload(0,&pass,sizeof(pass)); upload(1,&model,sizeof(model)); upload(2,&material,sizeof(material));
            }
            glViewport(0,0,resolution,resolution);
            glDrawArraysInstanced(GL_TRIANGLES,0,GLsizei(vertices.size()),repetitions);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            resolve->Bind();
            if (legacy)
            {
                resolve->SetUniform("uResolution",resolution); resolve->SetUniform("uDestinationZOffset",0);
            }
            else
            {
                for (unsigned i=0;i<4;++i) glBindImageTexture(i+1,textures[i],0,GL_TRUE,0,GL_READ_ONLY,GL_R32UI);
                const glm::uvec4 pass(resolution,0,0,0); upload(0,&pass,sizeof(pass));
            }
            glBindImageTexture(5,textures[5],0,GL_TRUE,0,GL_WRITE_ONLY,GL_RGBA16F);
            glDispatchCompute((resolution+3)/4,(resolution+3)/4,(resolution+3)/4);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
            std::vector<glm::vec4> field(count);
            glBindTexture(GL_TEXTURE_3D,textures[5]);
            glGetTexImage(GL_TEXTURE_3D,0,GL_RGBA,GL_FLOAT,field.data());
            double energy=0;
            for (const auto& value:field) energy+=value.r*voxelSize*voxelSize;
            // Compare the actual mip against premultiplied front-to-back
            // compositing. Resolve's existing opacity dilation can absorb light
            // before it reaches a mip, so equality with unoccluded energy is not
            // expected; generating additional energy is never valid.
            const int mipSize = resolution/2;
            glBindTexture(GL_TEXTURE_3D,textures[6]);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_3D,GL_TEXTURE_MAX_LEVEL,0);
            glTexImage3D(GL_TEXTURE_3D,0,GL_RGBA16F,mipSize,mipSize,mipSize,0,GL_RGBA,GL_FLOAT,nullptr);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_3D,textures[5]);
            glActiveTexture(GL_TEXTURE0);
            glBindImageTexture(2,textures[6],0,GL_TRUE,0,GL_WRITE_ONLY,GL_RGBA16F);
            const std::array<glm::uvec4,2> mipPass{glm::uvec4(axis,1,0,mipSize),glm::uvec4(0)};
            upload(0,mipPass.data(),sizeof(mipPass));
            mip->Bind();
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
            glDispatchCompute((mipSize+3)/4,(mipSize+3)/4,(mipSize+3)/4);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
            std::vector<glm::vec4> mipField(mipSize*mipSize*mipSize);
            glBindTexture(GL_TEXTURE_3D,textures[6]);
            glGetTexImage(GL_TEXTURE_3D,0,GL_RGBA,GL_FLOAT,mipField.data());
            double mipEnergy = 0;
            for (const auto& value:mipField) mipEnergy+=value.r*voxelSize*voxelSize*4;
            double referenceEnergy = 0;
            const auto at = [&](glm::ivec3 coordinate) -> const glm::vec4&
            {
                return field[(coordinate.z*resolution+coordinate.y)*resolution+coordinate.x];
            };
            for (int z=0;z<resolution;++z)
                for (int y=0;y<resolution;++y)
                    for (int x=0;x<resolution;++x)
                    {
                        glm::ivec3 near(x,y,z);
                        if (near[axis]%2 != 0) continue;
                        glm::ivec3 far = near; ++far[axis];
                        referenceEnergy += (at(near).r+(1-at(near).a)*at(far).r)*voxelSize*voxelSize;
                    }
            if (std::abs(mipEnergy-referenceEnergy)>std::max(.002,referenceEnergy*.002) || mipEnergy>energy+.002)
            {
                std::cerr << "VCT directional mip energy: expected=" << referenceEnergy << " actual=" << mipEnergy << '\n';
                passed = false;
            }
            return energy;
        };
        // The integrated projected energy is Le * area, independent of cell
        // alignment, dominant axis, winding, tessellation and voxel resolution.
        for (int resolution : {16,32})
            for (float side : {.25f,.5f,2.0f})
                for (float phase : {.0f,.13f,.49f,.91f})
                    for (int subdivisions : {1,4})
                        for (int axis : {0,1,2})
                        {
                            const double actual=measure(side,phase,subdivisions,resolution,axis,axis==1);
                            const double expected=8.0*side*side;
                            // Integer atomic and half-float storage quantize each contribution.
                            if (std::abs(actual-expected)>std::max(.025,expected*.04))
                            {
                                std::cerr << "VCT " << (legacy?"legacy":"Slang") << " area conservation failed: side=" << side
                                    << " phase=" << phase << " subdivisions=" << subdivisions << " resolution=" << resolution
                                    << " axis=" << axis << " expected=" << expected << " actual=" << actual << '\n';
                                passed=false;
                            }
                        }
        // Unit radiance, matching ordinary emissive materials, must also scale
        // with area. Keep these footprints above fixed-point quantization noise.
        for (int resolution : {16,32})
            for (float side : {.5f,1.0f})
            {
                const double actual = measure(side,.13f,1,resolution,2,false,1.0f);
                if (std::abs(actual-side*side) > .025) passed = false;
            }
        for (const float gain : {0.0f,0.5f,1.0f})
        {
            const double actual=measure(2,.13f,1,32,2,false,0,gain);
            const double expected=4.0*2.0*.95*gain;
            if (std::abs(actual-expected)>.15)
            {
                std::cerr << "VCT " << (legacy?"legacy":"Slang") << " secondary material integral: expected=" << expected << " actual=" << actual << '\n';
                passed=false;
            }
        }
        const double faint = measure(2,.13f,1,32,2,false,0,1,0,.001f);
        if (std::abs(faint - .0038) > .0008)
        {
            std::cerr << "Faint secondary radiance was lost: " << faint << '\n';
            passed = false;
        }
        passed &= measure(2,.13f,1,32,2,false,0,1,1)<.01;
        passed &= measure(2,.13f,1,32,2,false,0,1,0,0)<.01;
        const double saturated = measure(2,0,1,32,2,false,16,0,0,2,70000);
        if (std::abs(saturated-64.0)>1.0)
        {
            std::cerr << "Dense voxel accumulation wrapped or changed source energy: " << saturated << '\n';
            passed=false;
        }
        passed &= glGetError()==GL_NO_ERROR;
        glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&framebuffer);
        glBindVertexArray(0); glDeleteVertexArrays(1,&vao); glDeleteBuffers(1,&vbo);
        glDeleteBuffers(GLsizei(buffers.size()),buffers.data());
        glDeleteTextures(GLsizei(textures.size()),textures.data());
    }
    return passed;
}
