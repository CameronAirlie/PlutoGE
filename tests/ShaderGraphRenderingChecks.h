#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Mesh.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

inline PlutoGE::render::ShaderGraph ParameterTestGraph()
{
    using namespace PlutoGE::render;
    ShaderGraph g;
    g.unlit=true;
    g.variables={{"Tint",ShaderGraphValueType::Vec3,{.4f,.8f,.2f,1}}};
    g.nodes={{.id=1,.kind=ShaderGraphNodeKind::Parameter,.name="Tint",.parameter="Tint"},
             {.id=2,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(.5f)},
             {.id=3,.kind=ShaderGraphNodeKind::Multiply},
             {.id=100,.kind=ShaderGraphNodeKind::Output}};
    g.links={{1,1,"Out",3,"A"},{2,2,"Out",3,"B"},{3,3,"Out",100,"Albedo"}};
    return g;
}
// A graph-built three-band response, evaluated separately for every light.
inline PlutoGE::render::ShaderGraph ToonTestGraph()
{
    using namespace PlutoGE::render;
    ShaderGraph g;
    g.nodes={{.id=1,.kind=ShaderGraphNodeKind::WorldNormal},
        {.id=2,.kind=ShaderGraphNodeKind::LightDirection},
        {.id=3,.kind=ShaderGraphNodeKind::Dot},
        {.id=4,.kind=ShaderGraphNodeKind::Expression,.parameter="floor(saturate(A)*2.0+0.5)/2.0"},
        {.id=5,.kind=ShaderGraphNodeKind::LightColor},
        {.id=6,.kind=ShaderGraphNodeKind::LightAttenuation},
        {.id=7,.kind=ShaderGraphNodeKind::ShadowAttenuation},
        {.id=8,.kind=ShaderGraphNodeKind::Expression,.parameter="A*B*C*D"},
        {.id=100,.kind=ShaderGraphNodeKind::Output}};
    g.links={{1,1,"Out",3,"A"},{2,2,"Out",3,"B"},{3,3,"Out",4,"A"},
        {4,4,"Out",8,"A"},{5,5,"Out",8,"B"},{6,6,"Out",8,"C"},{7,7,"Out",8,"D"},
        {8,8,"Out",100,"Direct Lighting"}};
    return g;
}
inline void CheckToonGraphCompiler()
{
    using namespace PlutoGE::render;
    auto require=[](bool v,const char *message){if(!v)throw std::runtime_error(message);};
    std::string error;
    auto graph=ToonTestGraph();
    auto program=BuildShaderGraphProgram(graph,{},&error);
    require(bool(program),error.c_str());
    require(program->data.outputs1.w!=0,"Custom lighting output lost");
    ShaderGraphSample sample;sample.color={.2f,.3f,.4f,1};sample.lightColor={.4f,.8f,.2f};
    sample.lightAttenuation=.5f;sample.shadowAttenuation=.25f;
    for(float facing:{0.0f,.2f,.3f,.6f,.8f,1.0f}) {
        sample.lightDirection={std::sqrt(1-facing*facing),0,facing};
        auto result=EvaluateShaderGraph(*program,sample);
        const float band=facing<.25f?0.0f:facing<.75f?.5f:1.0f;
        require(glm::length(result.directLighting-sample.lightColor*(band*.5f*.25f))<.00001f,"Per-light CPU result differs");
        require(result.color==sample.color,"Custom lighting changed surface albedo");
    }
    auto invalid=graph;invalid.links.back().toPin="Albedo";
    require(!ValidateShaderGraph(invalid),"Light input accepted in surface stage");
    invalid=graph;invalid.links.back().toPin="Vertex Offset";
    require(!ValidateShaderGraph(invalid),"Light input accepted in vertex stage");
    invalid=graph;invalid.unlit=true;
    require(!ValidateShaderGraph(invalid),"Unlit custom lighting silently accepted");
    // Shared subgraphs preserve lighting stage validation after flattening.
    ShaderGraph parent;parent.nodes={{.id=1,.kind=ShaderGraphNodeKind::Subgraph,.subgraph=std::make_shared<const ShaderGraph>(graph)},
        {.id=100,.kind=ShaderGraphNodeKind::Output}};
    parent.links={{1,1,"Direct Lighting",100,"Direct Lighting"}};
    require(ValidateShaderGraph(parent,&error),error.c_str());
    parent.links[0].toPin="Emission";
    require(!ValidateShaderGraph(parent),"Nested light input escaped stage validation");
    for(const char *expression:{"step(0.5,A)","smoothstep(0.5,0.5,A)","smoothstep(1.0,0.0,A)"}) {
        ShaderGraph math;math.unlit=true;
        math.nodes={{.id=1,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(.5f)},
            {.id=2,.kind=ShaderGraphNodeKind::Expression,.parameter=expression},{.id=100,.kind=ShaderGraphNodeKind::Output}};
        math.links={{1,1,"Out",2,"A"},{2,2,"Out",100,"Albedo"}};
        auto code=BuildShaderGraphProgram(math,{},&error);require(bool(code),error.c_str());
        const float expected=std::string_view(expression)=="smoothstep(1.0,0.0,A)"?.5f:1.0f;
        require(std::abs(EvaluateShaderGraph(*code,{}).color.x-expected)<.00001f,"Threshold edge semantics differ");
    }
}
inline void CheckShaderGraphCompiler()
{
    using namespace PlutoGE::render;
    auto require=[](bool v,const char *message){if(!v) throw std::runtime_error(message);};
    auto graph=ParameterTestGraph();
    std::string error;
    auto program=BuildShaderGraphProgram(graph,{},&error);
    require(bool(program),error.c_str());
    auto cpu=EvaluateShaderGraph(*program,{});
    require(glm::length(glm::vec3(cpu.color)-glm::vec3(.2f,.4f,.1f))<.0001f,"CPU graph colour differs");
    SetShaderGraphTimeSeconds(12.5f);
    require(ShaderGraphTimeSeconds()==12.5f,"Explicit shader clock ignored");
    ResetShaderGraphClock();
    ShaderGraph parent;
    parent.nodes={{.id=1,.kind=ShaderGraphNodeKind::Subgraph,.parameter="test",.subgraph=std::make_shared<const ShaderGraph>(graph)},
        {.id=2,.kind=ShaderGraphNodeKind::Output}};
    parent.links={{1,1,"Albedo",2,"Albedo"}};
    auto nested=BuildShaderGraphProgram(parent,{},&error);
    require(bool(nested),error.c_str());
    require(glm::length(glm::vec3(EvaluateShaderGraph(*nested,{}).color)-glm::vec3(.2f,.4f,.1f))<.0001f,"Subgraph default failed");
    parent.nodes.push_back({.id=3,.kind=ShaderGraphNodeKind::Vec3,.value={1,0,0,1}});
    parent.links.push_back({2,3,"Vec3",1,"A"});
    nested=BuildShaderGraphProgram(parent,{},&error);
    require(bool(nested),error.c_str());
    require(glm::length(glm::vec3(EvaluateShaderGraph(*nested,{}).color)-glm::vec3(.5f,0,0))<.0001f,"Subgraph input ignored");
    ShaderGraph expression;
    expression.nodes={{.id=1,.kind=ShaderGraphNodeKind::Expression,.parameter="return float3(sin(A), 0.25, 0.5) * B;"},
        {.id=2,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(0)},
        {.id=3,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(2)}, {.id=4,.kind=ShaderGraphNodeKind::Output}};
    expression.links={{1,2,"Out",1,"A"},{2,3,"Out",1,"B"},{3,1,"Out",4,"Albedo"}};
    const auto code=BuildShaderGraphProgram(expression,{},&error);
    require(bool(code),error.c_str());
    require(glm::length(glm::vec3(EvaluateShaderGraph(*code,{}).color)-glm::vec3(0,.5f,1))<.0001f,"Code expression differs");
    expression.nodes[0].parameter="sin(";
    require(!ValidateShaderGraph(expression),"Invalid code expression accepted");
    expression.nodes[0].parameter="float3(0.5)";
    expression.links.back().toPin="Vertex Offset";
    const auto broadcast=BuildShaderGraphProgram(expression,{},&error);
    require(bool(broadcast),error.c_str());
    require(glm::length(EvaluateShaderGraph(*broadcast,{}, {},true).vertexOffset-glm::vec3(.5f))<.0001f,"Scalar vector constructor lost its width");
    MeshConfig triangle;
    triangle.data.vertices.resize(3);
    triangle.data.vertices[0].position={0,0,0};
    triangle.data.vertices[1].position={1,0,0};
    triangle.data.vertices[2].position={0,1,0};
    triangle.data.vertices[1].uv={1,0};
    triangle.data.vertices[2].uv={0,1};
    triangle.data.indices={0,1,2};
    std::unique_ptr<Mesh> sourceMesh(Mesh::FromConfig(std::move(triangle)));
    const auto *subdivided=sourceMesh->GetTessellated(1);
    require(subdivided->GetMeshData().indices.size()==12 && subdivided->GetMeshData().vertices.size()==6,"Triangle subdivision topology is wrong");
    require(sourceMesh->GetTessellated(1)==subdivided,"Tessellation cache was not reused");
    require(sourceMesh->GetTessellated(2)->GetMeshData().indices.size()==48,"Second subdivision level is wrong");
    for(const auto &vertex:subdivided->GetMeshData().vertices)
        require(std::abs(vertex.position[0]-vertex.uv[0])<.0001f && std::abs(vertex.position[1]-vertex.uv[1])<.0001f,"Subdivision UV interpolation changed");
    const std::array overrides{ShaderGraphVariable{"Tint",ShaderGraphValueType::Vec3,{.8f,.2f,.6f,1}}};
    auto changed=BuildShaderGraphProgram(graph,overrides,&error);
    require(changed && changed->hash!=program->hash,"Material parameter override ignored");
    require(std::memcmp(changed->data.instructions.data(),program->data.instructions.data(),sizeof(program->data.instructions))==0,"Overrides changed register layout");
    require(BuildShaderGraphProgram(graph)->hash==program->hash,"Graph compilation is nondeterministic");
    for(int variant=0;variant<9;++variant)
    {
        auto invalid=graph;
        if(variant==0) invalid.nodes.push_back({.id=101,.kind=ShaderGraphNodeKind::Output});
        if(variant==1) invalid.nodes.push_back(invalid.nodes[0]);
        if(variant==2) invalid.links[0].fromNodeId=999;
        if(variant==3) invalid.links[0].fromPin="NoSuchPin";
        if(variant==4) invalid.links.push_back({4,3,"Out",3,"B"});
        if(variant==5) invalid.variables.clear();
        if(variant==6) invalid.links.push_back({4,3,"Out",3,"A"});
        if(variant==7) invalid.nodes[1].kind=static_cast<ShaderGraphNodeKind>(999);
        if(variant==8) {invalid.nodes[1].kind=ShaderGraphNodeKind::Vec2;invalid.links[1].fromPin="Vec2";}
        require(!ValidateShaderGraph(invalid,&error) && !error.empty(),"Invalid graph accepted");
    }
    auto cyclic=graph;
    cyclic.nodes.push_back({.id=4,.kind=ShaderGraphNodeKind::Add});
    cyclic.links[0]={1,4,"Out",3,"A"};
    cyclic.links.push_back({4,3,"Out",4,"A"});
    require(!ValidateShaderGraph(cyclic,&error),"Cycle accepted");
    auto badType=overrides;badType[0].type=ShaderGraphValueType::Float;
    require(!BuildShaderGraphProgram(graph,badType,&error),"Changed parameter type accepted");
    auto excess=graph;
    for(int i=4;i<80;++i)
    {
        excess.nodes.push_back({.id=i,.kind=ShaderGraphNodeKind::Sine});
        excess.links.push_back({i,i-1,"Out",i,"Value"});
    }
    excess.links[2].fromNodeId=79;
    require(!ValidateShaderGraph(excess,&error),"Oversized graph accepted");
}
inline void CheckShaderGraphAssets()
{
    using namespace PlutoGE;
    auto require=[](bool v,const std::string &message){if(!v) throw std::runtime_error(message);};
    struct Scratch
    {
        std::filesystem::path root=std::filesystem::temp_directory_path()/("PlutoGE-shader-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Scratch(){if(root.parent_path()==std::filesystem::temp_directory_path() && root.filename().string().starts_with("PlutoGE-shader-")){std::error_code e;std::filesystem::remove_all(root,e);}}
    } scratch;
    assets::AssetManager manager;
    manager.SetProjectContext(scratch.root.string());
    auto graph=ParameterTestGraph();graph.outline.enabled=true;
    std::string error;
    const std::string reference="project://Graph.plutoshadergraph";
    require(manager.SaveShaderGraphAsset(reference,graph,&error),error);
    assets::AssetManager fresh;fresh.SetProjectContext(scratch.root.string());
    bool loaded=false;
    auto restored=fresh.LoadShaderGraphAsset(reference,&loaded);
    require(loaded && render::HashShaderGraph(restored)==render::HashShaderGraph(graph),"Shader graph disk roundtrip lost data");
    render::MaterialConfig config;
    config.shaderGraphReference=reference;
    config.shaderGraphVariables={{"Tint",render::ShaderGraphValueType::Vec3,{.8f,.2f,.6f,1}}};
    require(manager.SaveMaterialAsset("project://Material.plutomaterial",config,&error),error);
    auto *material=manager.LoadMaterialAsset("project://Material.plutomaterial");
    require(material && material->GetConfig().shaderGraphProgram,"Material has no portable graph");
    auto hash=material->GetConfig().shaderGraphProgram->hash;
    graph.nodes[1].value=glm::vec4(.25f);
    require(manager.SaveShaderGraphAsset(reference,graph,&error),error);
    require(material->GetConfig().shaderGraphProgram->hash!=hash,"Saving shader failed to refresh loaded material");
    const auto goodHash=material->GetConfig().shaderGraphProgram->hash;
    graph.links[0].fromNodeId=999;
    require(!manager.SaveShaderGraphAsset(reference,graph,&error),"Invalid shader was saved");
    require(material->GetConfig().shaderGraphProgram->hash==goodHash,"Invalid edit replaced working material");
    auto recursive=ParameterTestGraph();recursive.passes={reference};
    require(!manager.SaveShaderGraphAsset(reference,recursive,&error),"Recursive material pass was saved");
    auto expression=ParameterTestGraph();expression.tessellation=2;expression.textures={{"Detail",{},true,true}};
    require(manager.SaveShaderGraphAsset(reference,expression,&error),error);
    auto roundtrip=manager.LoadShaderGraphAsset(reference);
    require(roundtrip.tessellation==2&&roundtrip.textures.size()==1&&roundtrip.textures[0].nearest&&roundtrip.textures[0].clamp,"Graph settings did not roundtrip");
    assets::AssetManager examples;
    const auto sampleRoot=std::filesystem::path(__FILE__).parent_path().parent_path()/"samples"/"RocketLeg";
    if(std::filesystem::exists(sampleRoot)) {
        examples.SetProjectContext(sampleRoot.string());
        for(const char *name:{"DynamicWaves","SceneTint","LayeredRim","ScrollingTexture","Toon"}) {
            bool valid=false;auto example=examples.LoadShaderGraphAsset(std::string("project://Shaders/")+name+".plutoshadergraph",&valid);
            const bool compiled=valid&&render::ValidateShaderGraph(example,&error);
            require(compiled,std::string(name)+": "+error);
            auto *material=examples.LoadMaterialAsset(std::string("project://Materials/")+name+".plutomaterial");
            require(material && material->GetConfig().shaderGraphProgram,std::string(name)+" material did not resolve");
            if(std::string_view(name)=="LayeredRim")require(material->GetConfig().additionalPasses.size()==1,"Additional pass not resolved");
        }
    }
}

template<class Device,class ReadPixels>
void CheckShaderGraphRendering(PlutoGE::render::BasicRenderer &renderer,Device &device,ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    CheckShaderGraphCompiler();
    CheckToonGraphCompiler();
    CheckShaderGraphAssets();
    auto require=[](bool v,const char *message){if(!v) throw std::runtime_error(message);};
    const std::array<BasicVertex,4> vertices{{
        {{{-.8f,-.8f,.5f}},{{0,0,1}},{{0,0}}},{{{.8f,-.8f,.5f}},{{0,0,1}},{{1,0}}},
        {{{.8f,.8f,.5f}},{{0,0,1}},{{1,1}}},{{{-.8f,.8f,.5f}},{{0,0,1}},{{0,1}}}}};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh=renderer.CreateMesh({vertices,indices});
    BasicLighting lighting;lighting.ambientIntensity=lighting.directionalIntensity=0;lighting.cameraPosition={0,0,2};
    BasicDraw draw;draw.mesh=&mesh;draw.baseColor={.9f,0,0,1};
    auto graph=ParameterTestGraph();
    auto sample=[&](std::span<const BasicDraw> draws, float x=.5f)
    {
        renderer.Render(glm::mat4(1),lighting,draws);
        auto pixels=readPixels(renderer.GetColorTexture());
        size_t at=(renderer.GetHeight()/2*renderer.GetWidth()+unsigned(renderer.GetWidth()*x))*4;
        require(pixels.size()>at+3,"Shader graph readback is empty");
        return glm::ivec3(int(pixels[at]),int(pixels[at+1]),int(pixels[at+2]));
    };
    int pixelCheck=0;
    auto expect=[&](glm::ivec3 actual,glm::ivec3 expected)
    {
        ++pixelCheck;
        if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::ivec3(3))))
            throw std::runtime_error("Shader graph pixel check " + std::to_string(pixelCheck) + " mismatch: expected " + std::to_string(expected.x) + "," + std::to_string(expected.y) + "," + std::to_string(expected.z) +
                                     " got " + std::to_string(actual.x) + "," + std::to_string(actual.y) + "," + std::to_string(actual.z));
    };
    // Scene lights drive the graph, with no second PBR/N.L multiplication.
    draw.shaderGraphProgram=BuildShaderGraphProgram(ToonTestGraph());
    require(bool(draw.shaderGraphProgram),"Toon graph compilation failed");
    lighting.directionalIntensity=.5f;lighting.directionalColor={.4f,.8f,.2f};
    for(float facing:{.1f,.5f,.9f}) {
        lighting.directionalDirection={-std::sqrt(1-facing*facing),0,-facing};
        const float band=facing<.25f?0.0f:facing<.75f?.5f:1.0f;
        expect(sample(std::span(&draw,1)),glm::ivec3(glm::round(glm::vec3(.4f,.8f,.2f)*(.5f*band*255))));
    }
    lighting.directionalIntensity=0;
    expect(sample(std::span(&draw,1)),{0,0,0});
    lighting.pointLights.push_back({.position={0,0,2.5f},.range=10,.color={.4f,.8f,.2f},.intensity=1});
    expect(sample(std::span(&draw,1)),{26,51,13});
    lighting.pointLights.push_back(lighting.pointLights.front());
    expect(sample(std::span(&draw,1)),{51,102,26});
    lighting.pointLights.clear();
    draw.shaderGraphProgram.reset();
    // A branch shared by Albedo and Direct Lighting reads the original input.
    // Replaying it with the already tinted surface would square the tint.
    ShaderGraph shared;
    shared.nodes={{.id=1,.kind=ShaderGraphNodeKind::MaterialInput},
        {.id=2,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(.5f)},
        {.id=3,.kind=ShaderGraphNodeKind::Multiply},{.id=100,.kind=ShaderGraphNodeKind::Output}};
    shared.links={{1,1,"Out",3,"A"},{2,2,"Out",3,"B"},{3,3,"Out",100,"Albedo"},{4,3,"Out",100,"Direct Lighting"}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(shared);
    lighting.directionalIntensity=1;
    expect(sample(std::span(&draw,1)),{115,0,0});
    lighting.directionalIntensity=0;draw.shaderGraphProgram.reset();
    // GPU threshold semantics, including coincident/reversed smoothstep edges.
    for(const char *expression:{"step(0.5,0.5)","smoothstep(0.5,0.5,0.5)","smoothstep(1.0,0.0,0.5)","floor(-0.2)+1.25"}) {
        ShaderGraph math;math.unlit=true;
        math.nodes={{.id=1,.kind=ShaderGraphNodeKind::Expression,.parameter=expression},{.id=100,.kind=ShaderGraphNodeKind::Output}};
        math.links={{1,1,"Out",100,"Albedo"}};
        draw.shaderGraphProgram=BuildShaderGraphProgram(math);
        require(bool(draw.shaderGraphProgram),"Threshold expression failed");
        auto expected=EvaluateShaderGraph(*draw.shaderGraphProgram,{}).color;
        expect(sample(std::span(&draw,1)),glm::ivec3(glm::round(glm::vec3(expected)*255.0f)));
    }
    draw.shaderGraphProgram.reset();
    // White emission preserves texture detail even with no external lighting.
    const std::array<std::byte,8> emissiveTexels{std::byte{64},std::byte{128},std::byte{32},std::byte{255},
                                               std::byte{0},std::byte{32},std::byte{192},std::byte{255}};
    rhi::Texture emissiveTexture(device,device.CreateTexture({2,1,rhi::Format::R8G8B8A8Unorm,rhi::TextureUsage::Sampled,"Textured emission"},emissiveTexels));
    draw.baseColorTexture=emissiveTexture.Get(); draw.emission={1,1,1};
    expect(sample(std::span(&draw,1),.3f),{64,128,32});
    expect(sample(std::span(&draw,1),.7f),{0,32,192});
    draw.shaderGraphProgram=BuildShaderGraphProgram(CreateDefaultShaderGraph());
    expect(sample(std::span(&draw,1),.3f),{64,128,32});
    draw.emission={.5f,.25f,1};
    expect(sample(std::span(&draw,1),.3f),{32,32,32});
    ShaderGraph explicitEmission;
    explicitEmission.nodes={{.id=1,.kind=ShaderGraphNodeKind::Vec3,.value={.2f,.4f,.1f,1}},
                            {.id=100,.kind=ShaderGraphNodeKind::Output}};
    explicitEmission.links={{1,1,"Vec3",100,"Emission"}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(explicitEmission);
    expect(sample(std::span(&draw,1),.3f),{51,102,26});
    draw.shaderGraphProgram=BuildShaderGraphProgram(CreateDefaultShaderGraph());
    draw.baseColorTexture={}; draw.emission={.2f,.4f,.1f};
    expect(sample(std::span(&draw,1)),{51,102,26});
    // Exercise the shipped toon asset, including its shared textured RGB branch.
    PlutoGE::assets::AssetManager toonAssets;
    toonAssets.SetProjectContext((std::filesystem::path(__FILE__).parent_path().parent_path()/"samples"/"RocketLeg").string());
    bool toonLoaded=false;
    const auto texturedToon=toonAssets.LoadShaderGraphAsset("project://Shaders/Toon.plutoshadergraph",&toonLoaded);
    require(toonLoaded,"Toon example asset did not load");
    std::string toonError;
    draw.shaderGraphProgram=BuildShaderGraphProgram(texturedToon,{},&toonError);
    require(bool(draw.shaderGraphProgram),toonError.c_str());
    draw.baseColor={.5f,.25f,1,1};draw.emission={0,0,0};draw.baseColorTexture=emissiveTexture.Get();
    lighting.directionalDirection={0,0,-1};lighting.directionalIntensity=1;
    lighting.directionalColor={1,1,1};
    expect(sample(std::span(&draw,1),.3f),{32,32,32});
    expect(sample(std::span(&draw,1),.7f),{0,8,192});
    // Ambient and direct illumination must share the same textured albedo.
    lighting.ambientIntensity=.25f;lighting.directionalIntensity=.5f;
    expect(sample(std::span(&draw,1),.3f),{24,24,24});
    lighting.ambientIntensity=0;lighting.directionalIntensity=1;
    draw.baseColorTexture={};
    expect(sample(std::span(&draw,1)),{128,64,255});
    const std::array tintOverride{ShaderGraphVariable{"Tint",ShaderGraphValueType::Vec3,{.5f,1,.25f,1}}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(texturedToon,tintOverride);
    require(bool(draw.shaderGraphProgram),"Toon tint override failed");
    expect(sample(std::span(&draw,1)),{64,64,64});
    lighting.directionalIntensity=0;draw.baseColor={.9f,0,0,1};
    draw.emission={0,0,0};
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    expect(sample(std::span(&draw,1)),{51,102,26});
    const std::array overrides{ShaderGraphVariable{"Tint",ShaderGraphValueType::Vec3,{.8f,.2f,.6f,1}}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph,overrides);
    expect(sample(std::span(&draw,1)),{102,26,77});
    draw.instanceModels=std::make_shared<const std::vector<glm::mat4>>(std::vector{
        glm::scale(glm::translate(glm::mat4(1),glm::vec3(-.45f,0,0)),glm::vec3(.4f,1,1)),
        glm::scale(glm::translate(glm::mat4(1),glm::vec3(.45f,0,0)),glm::vec3(.4f,1,1))});
    expect(sample(std::span(&draw,1),.25f),{102,26,77});
    expect(sample(std::span(&draw,1),.75f),{102,26,77});
    draw.instanceModels.reset();
    const auto clear=sample({});
    graph.nodes.push_back({.id=4,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(0)});
    graph.links.push_back({4,4,"Out",100,"Opacity"});
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    draw.alphaMode=1;
    expect(sample(std::span(&draw,1)),clear);
    draw.alphaMode=2;
    expect(sample(std::span(&draw,1)),clear);
    draw.alphaMode=0;
    graph.nodes={{.id=1,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(-.4f)},
                 {.id=2,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(-2)},
                 {.id=3,.kind=ShaderGraphNodeKind::Divide},{.id=100,.kind=ShaderGraphNodeKind::Output}};
    graph.links={{1,1,"Out",3,"A"},{2,2,"Out",3,"B"},{3,3,"Out",100,"Albedo"}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    expect(sample(std::span(&draw,1)),{51,51,51});
    // Recombine scalar inputs, then split the resulting vector again.
    graph.nodes={{.id=1,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(.3f)},
                 {.id=2,.kind=ShaderGraphNodeKind::Vec3,.value=glm::vec4(.2f,.4f,.6f,1),.componentPins=true},
                 {.id=3,.kind=ShaderGraphNodeKind::Vec3},
                 {.id=100,.kind=ShaderGraphNodeKind::Output}};
    graph.links={{1,1,"Out",2,"X"},{2,2,"Vec3",3,"Vec3"},{3,3,"Y",100,"Albedo"}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    require(bool(draw.shaderGraphProgram),"Vector split/combine graph failed to compile");
    expect(sample(std::span(&draw,1)),{102,102,102});
    std::array<std::byte,16> texels;
    for(int i=0;i<4;++i){texels[i*4]=std::byte{64};texels[i*4+1]=std::byte{128};texels[i*4+2]=std::byte{192};texels[i*4+3]=std::byte{255};}
    rhi::Texture texture(device,device.CreateTexture({2,2,rhi::Format::R8G8B8A8Unorm,rhi::TextureUsage::Sampled,"Shader graph sample"},texels));
    draw.baseColorTexture=texture.Get();
    graph.nodes={{.id=1,.kind=ShaderGraphNodeKind::TextureSample},{.id=100,.kind=ShaderGraphNodeKind::Output}};
    graph.links={{1,1,"Color",100,"Albedo"}};
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    expect(sample(std::span(&draw,1)),{64,128,192});
    graph.textures={{"Detail",{}}};
    graph.nodes[0].parameter="Detail";
    draw.graphTextures[0]=texture.Get();
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    require(bool(draw.shaderGraphProgram),"Named texture program failed");
    expect(sample(std::span(&draw,1)),{64,128,192});
    graph.nodes.push_back({.id=2,.kind=ShaderGraphNodeKind::Vec3,.value={1.5f,0,0,1}});
    graph.links.push_back({2,2,"Vec3",100,"Vertex Offset"});
    draw.shaderGraphProgram=BuildShaderGraphProgram(graph);
    require(draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z>0,"Vertex graph did not compile");
    if(device.GetApi()==rhi::GraphicsApi::OpenGL) {
        std::unique_ptr<Shader> legacy(CompileShaderGraphToGeometryShader(graph));
        std::unique_ptr<Shader> shadow(Shader::CreateShadowPassShader());
        std::unique_ptr<Shader> transparency(Shader::CreateTransparentPassShader());
        require(legacy && shadow && transparency,"Legacy graph pass shader compilation failed");
    }
    expect(sample(std::span(&draw,1)),clear);
    expect(sample(std::span(&draw,1),.95f),{64,128,192});
    draw.instanceModels=std::make_shared<const std::vector<glm::mat4>>(std::vector{glm::mat4(1),glm::mat4(1)});
    expect(sample(std::span(&draw,1)),clear);
    expect(sample(std::span(&draw,1),.95f),{64,128,192});
    graph.nodes.back().kind=ShaderGraphNodeKind::Float;
    graph.links.back().fromPin="Out";
    require(!BuildShaderGraphProgram(graph),"Scalar vertex offset accepted");
    BasicDraw background; background.mesh=&mesh;background.shaderGraphProgram=BuildShaderGraphProgram(ParameterTestGraph());
    ShaderGraph scene;scene.unlit=true;
    scene.nodes={{.id=1,.kind=ShaderGraphNodeKind::SceneColor},
        {.id=2,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(.5f)},
        {.id=3,.kind=ShaderGraphNodeKind::Multiply},{.id=100,.kind=ShaderGraphNodeKind::Output}};
    scene.links={{1,1,"Color",3,"A"},{2,2,"Out",3,"B"},{3,3,"Out",100,"Albedo"}};
    BasicDraw foreground;foreground.mesh=&mesh;foreground.alphaMode=2;
    foreground.model=glm::translate(glm::mat4(1),glm::vec3(0,0,.1f));
    foreground.shaderGraphProgram=BuildShaderGraphProgram(scene);
    require(foreground.shaderGraphProgram && foreground.shaderGraphProgram->requiresSceneTextures,"Scene color program failed");
    const std::array sceneDraws{background,foreground};
    expect(sample(sceneDraws),{26,51,13});
    scene.nodes[0].kind=ShaderGraphNodeKind::SceneDepth;
    scene.links[0].fromPin="Out";
    foreground.shaderGraphProgram=BuildShaderGraphProgram(scene);
    require(bool(foreground.shaderGraphProgram),"Scene depth did not compile");
    // SceneDepth exposes raw device depth. With this identity projection,
    // OpenGL maps clip Z from [-1, 1], while Vulkan uses [0, 1].
    const int expectedDepth = device.GetApi() == rhi::GraphicsApi::OpenGL ? 96 : 64;
    expect(sample(std::array{background,foreground}),glm::ivec3(expectedDepth));
    ShaderGraph overlay;overlay.unlit=true;
    overlay.nodes={{.id=1,.kind=ShaderGraphNodeKind::Vec3,.value={1,0,0,1}},
        {.id=2,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(.5f)},
        {.id=100,.kind=ShaderGraphNodeKind::Output}};
    overlay.links={{1,1,"Vec3",100,"Albedo"},{2,2,"Out",100,"Opacity"}};
    BasicDraw red=foreground;red.graphPassOrder=1;red.shaderGraphProgram=BuildShaderGraphProgram(overlay);
    overlay.nodes[0].value={0,0,1,1};
    BasicDraw blue=red;blue.graphPassOrder=2;blue.shaderGraphProgram=BuildShaderGraphProgram(overlay);
    // Submit in reverse order to exercise the equal-depth pass ordering.
    expect(sample(std::array{background,blue,red}),{77,26,134});
    BasicDraw receiver;receiver.mesh=&mesh;receiver.model[3].z=.3f;
    BasicDraw caster;caster.mesh=&mesh;caster.model[3].z=-.3f;caster.alphaMode=1;
    ShaderGraph mask;mask.nodes={{.id=1,.kind=ShaderGraphNodeKind::Float,.value=glm::vec4(0)},
        {.id=100,.kind=ShaderGraphNodeKind::Output}};
    mask.links={{1,1,"Out",100,"Opacity"}};
    BasicLighting shadows;shadows.shadowMethod=ShadowMethod::Cascaded;shadows.shadowsEnabled=true;shadows.shadowCascadeCount=1;shadows.shadowResolution=256;
    shadows.shadowMatrices[0]=glm::mat4(1);shadows.directionalDirection={0,0,-1};shadows.cameraPosition={0,0,2};
    auto shadowPixel=[&](){
        renderer.Render(glm::mat4(1),shadows,std::span(&receiver,1),{},std::span(&caster,1),PostProcessDebugView::DirectionalShadowMaskFiltered);
        auto pixels=readPixels(renderer.GetColorTexture());
        return int(pixels[(renderer.GetHeight()/2*renderer.GetWidth()+renderer.GetWidth()/2)*4]);
    };
    caster.shaderGraphProgram=BuildShaderGraphProgram(mask);
    require(shadowPixel()>240,"Procedural zero opacity still casts a shadow");
    mask.nodes[0].value=glm::vec4(1);caster.shaderGraphProgram=BuildShaderGraphProgram(mask);
    require(shadowPixel()<15,"Opaque graph failed to cast a shadow");
    mask.nodes.push_back({.id=2,.kind=ShaderGraphNodeKind::Vec3,.value={2,0,0,1}});
    mask.links.push_back({2,2,"Vec3",100,"Vertex Offset"});caster.shaderGraphProgram=BuildShaderGraphProgram(mask);
    require(shadowPixel()>240,"Shadow ignored vertex displacement");
    // The same shadow visibility must reach the graph's per-light input.
    receiver.shaderGraphProgram=BuildShaderGraphProgram(ToonTestGraph());
    shadows.ambientIntensity=0;shadows.directionalIntensity=.5f;
    auto toonShadowPixel=[&](){
        renderer.Render(glm::mat4(1),shadows,std::span(&receiver,1),{},std::span(&caster,1));
        auto pixels=readPixels(renderer.GetColorTexture());
        return int(pixels[(renderer.GetHeight()/2*renderer.GetWidth()+renderer.GetWidth()/2)*4]);
    };
    require(std::abs(toonShadowPixel()-128)<=3,"Unshadowed toon light is not applied exactly once");
    mask.links.pop_back();caster.shaderGraphProgram=BuildShaderGraphProgram(mask);
    require(toonShadowPixel()<15,"Scene shadow visibility did not reach custom lighting");
    mask.nodes[0].value=glm::vec4(0);caster.shaderGraphProgram=BuildShaderGraphProgram(mask);
    require(std::abs(toonShadowPixel()-128)<=3,"Alpha-masked caster incorrectly darkened custom lighting");

}
