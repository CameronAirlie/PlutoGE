#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

template<class ReadPixels>
void CheckVsmMembership(PlutoGE::render::BasicRenderer &renderer, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    const auto width = renderer.GetWidth(), height = renderer.GetHeight();
    renderer.Resize(96,96);
    constexpr std::array<BasicVertex,4> vertices = {{
        {{{-1,-1,0}},{{0,0,1}},{{0,0}}}, {{{1,-1,0}},{{0,0,1}},{{1,0}}},
        {{{1,1,0}},{{0,0,1}},{{1,1}}}, {{{-1,1,0}},{{0,0,1}},{{0,1}}}
    }};
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh = renderer.CreateMesh({vertices,indices});
    BasicDraw receiver; receiver.mesh = &mesh; receiver.castsShadow = false;
    receiver.model = glm::translate(glm::mat4(1),glm::vec3(0,0,.8f)) * glm::scale(glm::mat4(1),glm::vec3(8,8,1));
    std::vector<BasicDraw> casters(3);
    const auto place = [&](BasicDraw &draw, float x, float y) {
        draw.mesh = &mesh;
        draw.model = glm::translate(glm::mat4(1),glm::vec3(x,y,.2f));
        draw.shadowBoundsCenter = {x,y,.2f}; draw.shadowBoundsRadius = 1.5f;
    };
    for (unsigned i=0;i<casters.size();++i) place(casters[i],float(i)*2-2,0);
    BasicLighting lighting;
    lighting.shadowsEnabled = true; lighting.shadowMethod = ShadowMethod::Virtual;
    lighting.directionalDirection = {0,0,1}; lighting.shadowDistance = 16;
    lighting.virtualShadowPageBudget = 256; lighting.virtualShadowTriangleBudget = 16000000;
    const auto projection = glm::scale(glm::mat4(1),glm::vec3(.125f,.125f,1));
    const auto render = [&] {
        renderer.Render(projection * lighting.view,lighting,std::span(&receiver,1),{},casters,
            PostProcessDebugView::DirectionalShadowMaskFiltered);
    };
    for (int frame=0;frame<48;++frame) render();
    unsigned changedImages = 0;
    auto previous = readPixels(renderer.GetColorTexture());
    for (unsigned scenario=0;scenario<20;++scenario)
    {
        if (scenario < 6) place(casters[0],-2 + float(scenario)*.4f,1);
        if (scenario == 6) std::swap(casters[0],casters[2]);
        if (scenario == 7) casters.erase(casters.begin());
        if (scenario == 8) { casters.emplace_back(); place(casters.back(),-3,-2); }
        if (scenario == 9) casters[0].shadowBoundsRadius = -1;
        if (scenario == 10) place(casters[0],2,-2);
        if (scenario == 11) lighting.directionalDirection = glm::normalize(glm::vec3(.3f,0,1));
        if (scenario == 12) lighting.cameraPosition = {4,1,0};
        if (scenario == 13) lighting.cameraPosition = {-3,-1,0};
        if (scenario == 14) lighting.shadowDistance = 24;
        if (scenario == 15) casters[0].castsShadow = false;
        if (scenario == 16) casters[0].castsShadow = true;
        if (scenario == 17) casters.clear();
        if (scenario == 18) { casters.emplace_back(); place(casters[0],0,0); }
        renderer.SetVirtualShadowMembershipCachingEnabled(true);
        for (int frame=0;frame<4;++frame) render();
        const auto cached = readPixels(renderer.GetColorTexture());
        if (cached != previous) ++changedImages;
        renderer.SetVirtualShadowMembershipCachingEnabled(false);
        render();
        if (cached != readPixels(renderer.GetColorTexture()))
            throw std::runtime_error("Cached VSM membership differs from re-evaluated intersections at scenario " + std::to_string(scenario));
        previous = cached;
    }
    renderer.SetVirtualShadowMembershipCachingEnabled(true);
    renderer.Resize(width,height);
    if (changedImages < 5) throw std::runtime_error("VSM membership fixture did not exercise changing shadows");
    std::cout << "VSM cached/reference membership: 20 scenarios match, " << changedImages << " changed images\n";
}
