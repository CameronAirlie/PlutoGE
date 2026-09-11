#pragma once
#include "../engine/render/src/RhiSkinning.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <stb_image_write.h>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

template<class Device>
void CheckSkinningRendering(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value, const char *message) { if (!value) throw std::runtime_error(message); };
    MeshConfig config;
    config.data.vertices = {
        {{-.15f,-.15f,-.5f},{0,0,1},{0,0},{1,0,0,1}},
        {{ .15f,-.15f,-.5f},{0,0,1},{1,0},{1,0,0,1}},
        {{ .15f, .15f,-.5f},{0,0,1},{1,1},{1,0,0,1}},
        {{-.15f, .15f,-.5f},{0,0,1},{0,1},{1,0,0,1}}};
    for (auto &v : config.data.vertices) v.weights = {1,0,0,0};
    config.data.indices = {0,1,2,0,2,3};
    config.submeshes = {{.indexOffset=0,.indexCount=3}, {.indexOffset=3,.indexCount=3}};
    Mesh mesh(config);
    std::vector<glm::mat4> poseA{glm::mat4(1)}, poseB{glm::mat4(1)};
    auto redConfig = MaterialConfig{}; redConfig.emission = {1,0,0};
    auto greenConfig = MaterialConfig{}; greenConfig.emission = {0,1,0};
    Material red(redConfig), green(greenConfig);
    RenderCommand a; a.mesh=&mesh; a.material=&red; a.jointMatrices=&poseA;
    a.model=glm::translate(glm::mat4(1),{-.45f,0,0}); a.previousModel=a.model;
    RenderCommand b=a; b.material=&green; b.jointMatrices=&poseB;
    b.model=glm::translate(glm::mat4(1),{.45f,0,0}); b.previousModel=b.model;
    auto a2=a, b2=b; a2.submeshIndex=b2.submeshIndex=1;
    std::array commands{a,a2,b,b2};
    CameraData camera{glm::mat4(1),glm::mat4(1),.1f,4};
    BasicLighting light; light.ambientIntensity=light.directionalIntensity=0;
    light.shadowsEnabled=true; light.shadowDistance=2; light.shadowResolution=128;
    light.shadowCascadeCount=1;
    RhiSceneRenderer renderer;
    require(renderer.Initialize(device,shaders),"Skinning renderer initialization failed");
    const auto render = [&] {
        require(renderer.Render(128,128,camera,light,commands,commands),"Skinning render failed");
        return device.ReadTextureRgba8(renderer.GetColorTexture());
    };
    const auto initial=render();
    require(renderer.GetTimingStats().skinningUpdateCount==2,"Submeshes/passes did not share per-actor deformation");
    const auto centroid = [&](const auto &pixels, unsigned channel) {
        glm::vec2 sum(0); unsigned count=0;
        for(unsigned y=0;y<128;++y) for(unsigned x=0;x<128;++x) {
            const auto at=(y*128+x)*4;
            if(int(pixels[at+channel])>100 && int(pixels[at+1-channel])<30) { sum+=glm::vec2(x,y); ++count; }
        }
        require(count>100,"Skinned actor missing from Vulkan color buffer");
        return sum/float(count);
    };
    auto redBefore=centroid(initial,0), greenBefore=centroid(initial,1);
    // Only a bone moves. Model matrices stay fixed, reproducing the T-pose bug.
    poseA[0]=glm::translate(glm::mat4(1),{.10f,.35f,0});
    const auto moved=render();
    require(glm::distance(centroid(moved,0),redBefore)>15,"Bone animation did not move rendered vertices");
    require(glm::distance(centroid(moved,1),greenBefore)<.01f,"Shared model mixed two actors' poses");
    require(renderer.GetTimingStats().skinningUpdateCount==1,"Unchanged actor was deformed again");
    require(renderer.GetTimingStats().shadowCascadeUpdateCount>0,"Bone movement failed to invalidate shadow cache");
    const auto motion=device.ReadTextureRgba8(renderer.GetMotionTexture());
    unsigned moving=0;
    for(std::size_t i=0;i<motion.size();i+=4) if(int(motion[i])>5) ++moving;
    require(moving>100,"Skeletal motion missing from motion-vector attachment");
    const auto paused=render();
    require(paused==moved,"Paused skeletal geometry changed");
    require(renderer.GetTimingStats().skinningUpdateCount==0,"Paused pose repeated CPU deformation");
    const auto stopped=device.ReadTextureRgba8(renderer.GetMotionTexture());
    for(std::size_t i=0;i<stopped.size();i+=4) require(int(stopped[i])<=1,"Pause retained stale skeletal velocity");
    render();
    require(renderer.GetTimingStats().shadowCascadeUpdateCount==0,"Stationary pose invalidated shadow cache");
    renderer.ResetTemporalHistory();
    poseA[0]=glm::mat4(1);
    std::swap(commands[0],commands[2]); std::swap(commands[1],commands[3]);
    const auto reset=render();
    require(glm::distance(centroid(reset,0),redBefore)<.01f,"Reset/draw-order change corrupted actor pose");
    const auto resetMotion=device.ReadTextureRgba8(renderer.GetMotionTexture());
    for(std::size_t i=0;i<resetMotion.size();i+=4) require(int(resetMotion[i])<=1,"Reset retained skeletal motion history");
    // More frames than Vulkan's in-flight count, without readbacks/fence waits
    // between them: recorded uploads must not overwrite preceding GPU work.
    for(int frame=0;frame<12;++frame) { poseA[0]=glm::translate(glm::mat4(1),{.02f*frame,0,0}); renderer.Render(128,128,camera,light,commands,commands); }
    require(glm::distance(centroid(device.ReadTextureRgba8(renderer.GetColorTexture()),0),redBefore)>10,"In-flight dynamic vertex uploads lost the final pose");
    renderer.InvalidateAssetCache();
    render();
    require(renderer.GetTimingStats().skinningUpdateCount==2,"Cache invalidation retained actor buffers");

    // Non-normalized/invalid weights and normals under nonuniform bone scale.
    auto vertex=config.data.vertices[0]; vertex.position={1,0,0}; vertex.normal={1,1,0};
    vertex.joints={0,1,-1,999}; vertex.weights={2,2,0,0};
    std::array joints{glm::mat4(1),glm::translate(glm::mat4(1),{2,0,0})};
    const auto weighted=SkinRhiVertices(std::span(&vertex,1),joints);
    require(std::abs(weighted[0].position[0]-2)<1e-5f,"Joint weights were not blended/normalized");
    vertex.weights={0,0,0,0};
    require(SkinRhiVertices(std::span(&vertex,1),joints)[0].position==vertex.position,"Unweighted vertex lost bind position");
    vertex.weights={1,0,0,0}; joints[0]=glm::scale(glm::mat4(1),{2,1,1});
    const auto scaled=SkinRhiVertices(std::span(&vertex,1),joints);
    require(std::abs(scaled[0].normal[1]/scaled[0].normal[0]-2)<1e-5f,"Skinned normals ignored nonuniform scale");
    std::cout << "PASS Vulkan skinning: pixels, independent actors, submeshes, shadows, motion, pause/reset, in-flight uploads and weights\n";
}

template<class Device>
void CheckImportedSkinning(Device &device, const PlutoGE::render::BasicRendererShaderPackage &shaders,
                           const char *path, const char *outputDirectory)
{
    using namespace PlutoGE::render;
    const auto imported = PlutoGE::assetimport::MeshImporter{}.ImportMeshSourceAsset(path);
    if (imported.skeleton.joints.empty() || imported.animations.empty()) throw std::runtime_error("Fixture has no skeleton/animations");
    MeshConfig config;
    config.data=imported.meshData; config.submeshes=imported.submeshes;
    config.skeleton=imported.skeleton; config.animationNodes=imported.animationNodes;
    Mesh mesh(config);
    MaterialConfig materialConfig;
    materialConfig.color={.75f,.55f,.25f,1}; materialConfig.roughness=.65f;
    Material material(materialConfig);
    PlutoGE::scene::AnimationComponent animation;
    animation.SetClipsFromImportedAnimations(imported.animations);
    std::vector<glm::mat4> palette(imported.skeleton.joints.size(),glm::mat4(1));
    std::vector<RenderCommand> commands;
    for (unsigned i=0;i<mesh.GetSubmeshCount();++i) {
        RenderCommand command; command.mesh=&mesh; command.material=&material; command.jointMatrices=&palette; command.submeshIndex=i;
        commands.push_back(command);
    }
    CameraData camera{glm::lookAtRH(glm::vec3(3,2.6f,4.5f),glm::vec3(0,.9f,0),glm::vec3(0,1,0)),glm::perspective(glm::radians(35.0f),1.0f,.1f,20.0f),.1f,20};
    BasicLighting light; light.ambientIntensity=.6f; light.directionalIntensity=1;
    RhiSceneRenderer renderer;
    if(!renderer.Initialize(device,shaders)) throw std::runtime_error("Fixture renderer initialization failed");
    const auto capture=[&](const char *name) {
        if(!renderer.Render(512,512,camera,light,commands,{})) throw std::runtime_error("Fixture render failed");
        auto pixels=device.ReadTextureRgba8(renderer.GetColorTexture());
        if(outputDirectory) {
            std::filesystem::create_directories(outputDirectory);
            if(!stbi_write_png((std::filesystem::path(outputDirectory)/(std::string(name)+".png")).string().c_str(),512,512,4,pixels.data(),512*4))
                throw std::runtime_error("Could not save fixture image");
        }
        return pixels;
    };
    const auto bind=capture("bind");
    if(!animation.Play("sword and shield idle")) throw std::runtime_error("Fixture idle clip missing");
    animation.Update(.2f); palette=animation.GetJointMatrices(mesh.GetSkeleton(),mesh.GetAnimationNodes());
    const auto idle=capture("idle");
    if(!animation.Play("Gameplay.Player.heavy")) throw std::runtime_error("Fixture attack clip missing");
    animation.Update(.52f); palette=animation.GetJointMatrices(mesh.GetSkeleton(),mesh.GetAnimationNodes());
    const auto attack=capture("attack");
    const auto changed=[](const auto &a,const auto &b) {
        unsigned count=0;
        for(std::size_t i=0;i<a.size();i+=4)
            if(std::abs(int(a[i])-int(b[i]))+std::abs(int(a[i+1])-int(b[i+1]))+std::abs(int(a[i+2])-int(b[i+2]))>30) ++count;
        return count;
    };
    const auto bindDifference=changed(bind,idle), attackDifference=changed(idle,attack);
    std::cout<<"Imported Vulkan skinning: bind/idle changed pixels="<<bindDifference<<", idle/attack="<<attackDifference<<'\n';
    if(bindDifference<1500 || attackDifference<1500) throw std::runtime_error("Imported actor remained in bind/unchanged pose");
}
