#pragma once
#include "../engine/render/src/BasicDrawBatching.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

template<class ReadPixels>
void CheckOpaqueBatching(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto require = [](bool condition, const char *message) {
        if (!condition) throw std::runtime_error(message);
    };
    const std::array<BasicVertex, 4> vertices = {{
        {{{-.5f,-.5f,0}},{{0,0,1}},{{0,0}}}, {{{.5f,-.5f,0}},{{0,0,1}},{{1,0}}},
        {{{.5f,.5f,0}},{{0,0,1}},{{1,1}}}, {{{-.5f,.5f,0}},{{0,0,1}},{{0,1}}}
    }};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices,indices});
    std::vector<BasicDraw> original;
    for (int i=0; i<343; ++i)
    {
        BasicDraw draw;
        draw.mesh=&mesh;
        draw.model=glm::translate(glm::mat4(1),glm::vec3((i%19-9)*.1f,(i/19-8)*.1f,.5f));
        draw.model=glm::scale(draw.model,glm::vec3(.075f,.075f,1));
        draw.previousModel=draw.model;
        (*draw.previousModel)[3].x-=.012f;
        draw.baseColor={.2f+.15f*(i%4),.35f,.2f,1};
        original.push_back(draw);
    }
    auto batched=original;
    BatchOpaqueDraws(batched);
    require(batched.size()==4,"Repeated meshes did not collapse into four material groups");
    std::size_t total=0;
    for (const auto &group:batched)
    {
        require(group.instanceModels && group.previousInstanceModels,"Missing instance history");
        require(group.instanceModels->size()==group.previousInstanceModels->size(),"History count mismatch");
        for (std::size_t i=0;i<group.instanceModels->size();++i)
            require(std::abs((*group.instanceModels)[i][3].x-(*group.previousInstanceModels)[i][3].x-.012f)<.00001f,
                    "Batching lost moving-object history");
        total+=group.instanceModels->size();
    }
    require(total==343,"Batching lost visible instances");
    // Material edits, LOD ranges, transparency and packets without authoritative
    // history must not accidentally reuse an incompatible instance group.
    for (int variant=0;variant<9;++variant)
    {
        auto a=original[0], b=a;
        if (variant==0) b.emission.x=1;
        if (variant==1) b.firstIndex=3;
        if (variant==2) b.normalTexture={1,1};
        if (variant==3) b.normalizedLod=.5f;
        if (variant==4) b.alphaMode=2;
        if (variant==5) b.previousModel.reset();
        if (variant==6) b.contributesToGi=false;
        if (variant==7) b.surfaceType=1;
        if (variant==8) b.instanceModels=std::make_shared<const std::vector<glm::mat4>>(std::vector{b.model});
        std::vector<BasicDraw> separate{a,b}; BatchOpaqueDraws(separate);
        require(separate.size()==2,"Incompatible draws were batched");
    }
    BasicLighting lighting; lighting.ambientIntensity=.7f; lighting.directionalIntensity=0;
    lighting.cameraPosition={0,0,2};
    renderer.Render(glm::mat4(1),lighting,original);
    auto before=readPixels(renderer.GetColorTexture());
    require(renderer.GetFrameStats().geometryDraws==343,"Unbatched baseline count incorrect");
    renderer.Render(glm::mat4(1),lighting,batched);
    auto after=readPixels(renderer.GetColorTexture());
    require(before.size()==after.size() && !before.empty(),"Image readback failed");
    int maximumError=0;
    for (std::size_t i=0;i<before.size();++i)
        maximumError=std::max(maximumError,std::abs(int(before[i])-int(after[i])));
    require(maximumError<=1,"Opaque batching changed rendered pixels");
    require(renderer.GetFrameStats().geometryInstances==343 && renderer.GetFrameStats().geometryDraws<=8,
            "Renderer failed to use instanced geometry");
    const auto drawCount=renderer.GetFrameStats().geometryDraws;
    const auto measure=[&](const auto &draws) {
        double ms=0;
        for(int i=0;i<100;++i) {
            renderer.Render(glm::mat4(1),lighting,draws);
            if(i>=20) ms+=renderer.GetTimingStats().geometryRecordingMs;
        }
        return ms/80;
    };
    const auto unbatchedMs=measure(original), batchedMs=measure(batched);
    double batchingMs=0;
    for(int i=0;i<200;++i) {
        auto work=original;
        const auto start=std::chrono::steady_clock::now();
        BatchOpaqueDraws(work);
        batchingMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        require(work.size()==4,"Batching benchmark lost groups");
    }
    batchingMs/=200;

    std::cout<<"Opaque batching: 343 -> "<<drawCount<<" draws; 343 instances preserved; max pixel error "
             <<maximumError<<"; geometry CPU "<<unbatchedMs<<" -> "<<batchedMs<<" ms; batching CPU "<<batchingMs<<" ms (synthetic scene)\n";
}
