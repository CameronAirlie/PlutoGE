#pragma once
#include "../engine/render/src/BasicDrawBatching.h"
#include "../engine/render/src/CanonicalGeometry.h"
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
    // Exporters can duplicate vertex/index storage for otherwise identical
    // primitives. Deduplicate ranges without changing topology or attributes.
    std::vector<BasicVertex> duplicateVertices(vertices.begin(), vertices.end());
    duplicateVertices.insert(duplicateVertices.end(), vertices.begin(), vertices.end());
    std::vector<std::uint32_t> duplicateIndices(indices.begin(), indices.end());
    for (const auto index : indices) duplicateIndices.push_back(index + 4);
    const std::array ranges{GeometryRange{0, 6}, GeometryRange{6, 6}};
    auto canonical = FindCanonicalGeometry({duplicateVertices, duplicateIndices}, ranges);
    require(canonical.at(GeometryRangeKey(ranges[1])) == 0, "Duplicate geometry was not canonicalized");
    for (int variant = 0; variant < 5; ++variant)
    {
        auto modified = duplicateVertices;
        if (variant == 0) modified[4].normal[0] = .1f;
        if (variant == 1) modified[4].uv[0] = .1f;
        if (variant == 2) modified[4].tangent[3] = -1;
        if (variant == 3) modified[4].position[0] += .1f;
        if (variant == 4) modified[4].previousPosition[3] = 1;
        require(FindCanonicalGeometry({modified, duplicateIndices}, ranges).at(GeometryRangeKey(ranges[1])) == 6,
                "Canonical geometry discarded distinct vertex attributes");
    }
    auto reversed = duplicateIndices;
    std::swap(reversed[6], reversed[7]);
    require(FindCanonicalGeometry({duplicateVertices, reversed}, ranges).at(GeometryRangeKey(ranges[1])) == 6,
            "Canonical geometry changed triangle winding");
    auto duplicateMesh = renderer.CreateMesh({duplicateVertices, duplicateIndices});
    std::vector<BasicDraw> duplicateDraws(2);
    for (size_t i = 0; i < duplicateDraws.size(); ++i)
    {
        auto &draw = duplicateDraws[i];
        draw.mesh = &duplicateMesh;
        draw.firstIndex = ranges[i].firstIndex;
        draw.indexCount = 6;
        draw.model = glm::translate(glm::mat4(1), glm::vec3(i == 0 ? -.5f : .5f, 0, .5f));
        draw.previousModel = draw.model;
        draw.emission = {.4f, .2f, .1f};
    }
    BasicLighting duplicateLighting;
    duplicateLighting.ambientIntensity = 0;
    duplicateLighting.directionalIntensity = 0;
    renderer.Render(glm::mat4(1), duplicateLighting, duplicateDraws);
    const auto duplicateReference = readPixels(renderer.GetColorTexture());
    for (auto &draw : duplicateDraws)
        draw.firstIndex = canonical.at(GeometryRangeKey({draw.firstIndex, draw.indexCount}));
    BatchOpaqueDraws(duplicateDraws);
    require(duplicateDraws.size() == 1, "Duplicate exported primitives did not become instances");
    renderer.Render(glm::mat4(1), duplicateLighting, duplicateDraws);
    require(readPixels(renderer.GetColorTexture()) == duplicateReference, "Geometry canonicalization changed rendered pixels");
    // Different primitives sharing an object transform may merge after packing,
    // but their geometry, visibility gaps and motion histories must survive.
    auto distinctVertices = duplicateVertices;
    for (size_t i = 4; i < distinctVertices.size(); ++i) distinctVertices[i].position[0] += 1.0f;
    const auto packed = PackGeometryRanges({distinctVertices, duplicateIndices}, ranges);
    require(packed.indices.size() == 24, "Packed geometry lost original whole-mesh indices");
    const std::array isolated{GeometryRange{0,6,0}, GeometryRange{6,6,1}};
    require(PackGeometryRanges({distinctVertices, duplicateIndices}, isolated).indices.size() == duplicateIndices.size(),
            "Unmergeable material/LOD groups wasted index-buffer storage");
    auto packedMesh = renderer.CreateMesh({distinctVertices, packed.indices});
    std::vector<BasicDraw> separate(2);
    for (size_t i = 0; i < separate.size(); ++i)
    {
        separate[i].mesh = &packedMesh;
        separate[i].firstIndex = packed.firstIndices.at(GeometryRangeKey(ranges[i]));
        separate[i].indexCount = 6;
        separate[i].previousModel = separate[i].model;
        separate[i].emission = {.2f,.4f,.6f};
    }
    renderer.Render(glm::mat4(1), duplicateLighting, separate);
    const auto separatePixels = readPixels(renderer.GetColorTexture());
    auto merged = separate;
    MergeAdjacentOpaqueDraws(merged);
    require(merged.size() == 1 && merged[0].indexCount == 12, "Adjacent compatible geometry did not merge");
    renderer.Render(glm::mat4(1), duplicateLighting, merged);
    require(readPixels(renderer.GetColorTexture()) == separatePixels, "Range merging changed rendered pixels");
    for (int variant = 0; variant < 6; ++variant)
    {
        auto incompatible = separate;
        if (variant == 0) incompatible[1].firstIndex += 3;
        if (variant == 1) incompatible[1].model[3].x += 1;
        if (variant == 2) (*incompatible[1].previousModel)[3].x += 1;
        if (variant == 3) incompatible[1].emission.x += 1;
        if (variant == 4) incompatible[1].normalizedLod = .5f;
        if (variant == 5) incompatible[1].alphaMode = 2;
        MergeAdjacentOpaqueDraws(incompatible);
        require(incompatible.size() == 2, "Range merging crossed a visibility, material, LOD or history boundary");
    }
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
    for (int variant=0;variant<11;++variant)
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
        if (variant==9) b.outlineWidth=.1f;
        if (variant==10) b.outlineColor={1,0,0};
        if (variant==8) b.instanceModels=std::make_shared<const std::vector<glm::mat4>>(std::vector{b.model});
        std::vector<BasicDraw> separate{a,b}; BatchOpaqueDraws(separate);
        require(separate.size()==2,"Incompatible draws were batched");
    }
    BasicLighting lighting; lighting.ambientIntensity=.7f; lighting.directionalIntensity=0;
    lighting.cameraPosition={0,0,2};
    renderer.Render(glm::mat4(1),lighting,original);
    auto before=readPixels(renderer.GetColorTexture());
    require(renderer.GetFrameStats().geometryDraws==343,"Unbatched baseline count incorrect");
    require(renderer.GetFrameStats().geometryTriangles[0] == 686, "Unbatched triangle count incorrect");
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
    require(renderer.GetFrameStats().geometryTriangles[0] == 686, "Instanced triangle count incorrect");
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
