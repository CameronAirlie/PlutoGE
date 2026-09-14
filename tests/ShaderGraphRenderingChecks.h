#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
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
inline void CheckShaderGraphCompiler()
{
    using namespace PlutoGE::render;
    auto require=[](bool v,const char *message){if(!v) throw std::runtime_error(message);};
    auto graph=ParameterTestGraph();
    std::string error;
    auto program=BuildShaderGraphProgram(graph,{},&error);
    require(bool(program),error.c_str());
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
}

template<class Device,class ReadPixels>
void CheckShaderGraphRendering(PlutoGE::render::BasicRenderer &renderer,Device &device,ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    CheckShaderGraphCompiler();
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
    auto expect=[&](glm::ivec3 actual,glm::ivec3 expected)
    {
        if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::ivec3(3))))
            throw std::runtime_error("Shader graph pixel mismatch: expected " + std::to_string(expected.x) + "," + std::to_string(expected.y) + "," + std::to_string(expected.z) +
                                     " got " + std::to_string(actual.x) + "," + std::to_string(actual.y) + "," + std::to_string(actual.z));
    };
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
}
