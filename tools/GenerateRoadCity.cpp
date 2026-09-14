// Deterministic sample city authoring tool. Writes only to the supplied staging root.
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/RoadJunction.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/CameraComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/render/Material.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <stdexcept>
using namespace PlutoGE;
using namespace PlutoGE::scene;
namespace
{
    void Require(bool ok, const std::string &error) { if (!ok) throw std::runtime_error(error); }
    struct Geometry
    {
        render::MeshData data;
        std::map<unsigned, std::vector<unsigned>> groups;
        void Box(glm::vec3 center, glm::vec3 size, unsigned material)
        {
            static std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
            const unsigned offset = static_cast<unsigned>(data.vertices.size());
            for (auto v : cube->GetMeshData().vertices)
            {
                for (int axis = 0; axis < 3; ++axis) v.position[axis] = center[axis] + v.position[axis] * size[axis];
                data.vertices.push_back(v);
            }
            for (auto index : cube->GetMeshData().indices) groups[material].push_back(offset + index);
        }
        render::MeshConfig Config()
        {
            render::MeshConfig config;
            config.data = std::move(data);
            for (auto &[material, indices] : groups)
            {
                config.submeshes.push_back({.indexOffset = static_cast<unsigned>(config.data.indices.size()),
                    .indexCount = static_cast<unsigned>(indices.size()), .materialIndex = material});
                config.data.indices.insert(config.data.indices.end(), indices.begin(), indices.end());
            }
            return config;
        }
    };
}
int main(int argc, char **argv) try
{
    Require(argc == 2, "Usage: PlutoGEGenerateRoadCity <empty staging directory>");
    const auto root = std::filesystem::absolute(argv[1]);
    Require(!std::filesystem::exists(root / "Assets/Scenes/ManhattanMini.plutoscene"), "City already exists in staging root");
    auto &assets = core::Engine::GetInstance().GetAssetManager();
    assets.SetProjectContext(root.string());
    Scene city;
    std::string error;
    const std::string prefix = "project://City/ManhattanMini/";
    std::vector<std::string> refs;
    std::vector<render::Material *> materials;
    auto material = [&](std::string name, glm::vec3 color, float roughness = .8f, float metal = 0.f, glm::vec3 emission = glm::vec3(0))
    {
        auto reference = prefix + "Materials/" + name + ".plutomaterial";
        render::MaterialConfig config;
        config.color = glm::vec4(color, 1); config.roughness = roughness; config.metallic = metal; config.emission = emission;
        config.shaderGraphReference = std::string(assets::Project::kBuiltinDefaultShaderGraphReference);
        Require(assets.SaveMaterialAsset(reference, config, &error), error);
        refs.push_back(reference); materials.push_back(assets.LoadMaterialAsset(reference));
        Require(materials.back() != nullptr, "Material failed to load");
        return static_cast<unsigned>(refs.size() - 1);
    };
    const auto asphalt = material("Asphalt", {.075f,.085f,.10f});
    const auto pavement = material("LimestoneSidewalk", {.50f,.48f,.43f});
    const auto brick = material("WarmBrick", {.32f,.14f,.09f});
    const auto stone = material("ArtDecoLimestone", {.67f,.60f,.45f});
    const auto glass = material("BlueWindows", {.09f,.23f,.30f}, .23f, .45f);
    const auto lit = material("WarmWindows", {.75f,.56f,.28f}, .4f, .1f, {.10f,.06f,.015f});
    const auto metal = material("DarkMetal", {.065f,.075f,.08f}, .4f, .55f);
    const auto white = material("RoadWhite", {.8f,.79f,.68f});
    const auto yellow = material("RoadYellow", {.95f,.58f,.07f});
    const auto grass = material("ParkGrass", {.12f,.25f,.095f});
    const auto leaves = material("Trees", {.09f,.23f,.12f});
    const auto water = material("HudsonBlue", {.07f,.21f,.28f}, .22f, .2f);
    const auto taxi = material("TaxiYellow", {.98f,.57f,.035f}, .35f);
    auto entity = [&](const std::string &name, Entity *parent = nullptr)
    { return city.AddEntity(std::make_unique<Entity>(EntityConfig{.name=name}), parent); };
    auto *roads = entity("01 - Editable spline street network");
    auto *blocks = entity("02 - Manhattan blocks");
    auto *props = entity("03 - Street furniture and markings");
    auto publish = [&](Geometry &geometry, const std::string &name, Entity *parent, glm::vec3 position = glm::vec3(0), bool collision = false)
    {
        const auto reference = prefix + "Meshes/" + name + ".plutomesh";
        Require(assets.SaveMeshAsset(reference, geometry.Config(), refs, &error), error);
        auto *mesh = assets.LoadMeshAsset(reference); Require(mesh != nullptr, "Mesh load failed");
        auto *e = entity(name, parent); e->SetPosition(position);
        auto *component = e->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh=mesh, .materials=materials});
        component->SetMeshAssetReference(reference); component->SetStatic(true);
        for (unsigned i = 0; i < refs.size(); ++i) component->SetMaterialAssetForMaterialSlot(i, refs[i]);
        if (collision) e->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape=ColliderShape::Mesh});
        return e;
    };
    Geometry foundation;
    foundation.Box({0,-.65f,0}, {340,.5f,398}, pavement);
    foundation.Box({-218,-.8f,0}, {90,.3f,430}, water);
    foundation.Box({-169,-.05f,0}, {9,.4f,400}, stone);
    publish(foundation,"Waterfront and foundation",blocks,{},true);
    std::map<std::pair<int,int>,std::vector<RoadJunctionEndpoint>> entrances;
    Geometry markings, furniture;
    int roadCount=0, junctionCount=0, buildingCount=0;
    auto road = [&](int ax,int az,int bx,int bz)
    {
        const glm::vec3 a(-144+72*ax,0,-168+56*az), b(-144+72*bx,0,-168+56*bz);
        const auto direction = glm::normalize(b-a);
        auto *e = entity((ax==bx ? "Avenue " : "Cross street ") + std::to_string(++roadCount),roads);
        SplineComponentConfig config;
        config.points = {{a+direction*11.f,{}},{b-direction*11.f,{}}};
        config.width = ax==bx ? 16.f : 12.f; config.closed=false; config.thickness=.35f;
        config.material = materials[asphalt]; config.materialAssetReference = refs[asphalt];
        auto *spline = e->CreateComponent<SplineComponent>(config); spline->Rebuild();
        entrances[{ax,az}].push_back({e->GetID(),false}); entrances[{bx,bz}].push_back({e->GetID(),true});
        const float length=glm::length(b-a);
        for(float distance=15;distance<length-13;distance+=7)
        {
            auto p=a+direction*distance;
            for(float offset : {-0.22f,0.22f})
                markings.Box(p+glm::vec3(ax==bx?offset:0,.015f,ax==bx?0:offset),ax==bx?glm::vec3(.12f,.02f,3):glm::vec3(3,.02f,.12f),yellow);
        }
        for (auto end : {a+direction*14.f,b-direction*14.f})
            for(float offset=-config.width/2+1;offset<config.width/2;offset+=1.5f)
                markings.Box(end+glm::vec3(ax==bx?offset:0,.018f,ax==bx?0:offset),ax==bx?glm::vec3(.8f,.025f,3):glm::vec3(3,.025f,.8f),white);
    };
    for(int x=0;x<5;++x) for(int z=0;z<6;++z) road(x,z,x,z+1);
    for(int z=0;z<7;++z) for(int x=0;x<4;++x) road(x,z,x+1,z);
    for(auto &[node,ends]:entrances)
    {
        auto *owner=city.FindEntityByID(ends.front().entity);
        auto *junction=BakeRoadJunction(*owner,ends,assets,prefix+"Junctions/Junction_"+std::to_string(++junctionCount)+".plutomesh",error);
        Require(junction!=nullptr,"Junction " + std::to_string(junctionCount) + ": " + error);
        junction->SetName("Baked junction " + std::to_string(node.first+1) + " / " + std::to_string(node.second+1));
    }
    for(int x=0;x<4;++x) for(int z=0;z<6;++z)
    {
        glm::vec3 center(-108+72*x,0,-140+56*z);
        auto *block=entity("Block " + std::to_string(x+1)+"-"+std::to_string(z+1),blocks);
        Geometry sidewalk; sidewalk.Box({0,.02f,0},{52,.36f,38},pavement);
        publish(sidewalk,"Sidewalk_"+std::to_string(x)+"_"+std::to_string(z),block,center,true);
        if(x==1 && (z==2 || z==3))
        {
            Geometry park;
            park.Box({0,.22f,0},{44,.08f,30},grass);
            park.Box({0,.28f,0},{5,.06f,32},stone); park.Box({0,.28f,0},{46,.06f,4},stone);
            for(int t=0;t<8;++t)
            {
                glm::vec3 p(t<4?-15.f:15.f,0,-12+8.f*(t%4));
                park.Box(p+glm::vec3(0,1.9f,0),{.5f,3.5f,.5f},brick);
                park.Box(p+glm::vec3(0,4,0),{4.3f,4.5f,4.3f},leaves);
            }
            publish(park,"Pocket_Park_"+std::to_string(z),block,center,true); continue;
        }
        for(int side=0;side<2;++side)
        {
            const int seed=x*37+z*13+side*7;
            const float h=(x>=2 && z>=2 && z<=4)?48.f+(seed%5)*11.f:15.f+(seed%6)*5.f;
            const float w=20, d=29;
            const unsigned facade=(seed%3==0)?stone:brick;
            Geometry building;
            building.Box({0,.2f+h/2,0},{w,h,d},facade);
            building.Box({0,h+.45f,0},{w+1,.7f,d+1},stone);
            building.Box({0,h+1.7f,0},{w*.70f,2.4f,d*.7f},metal);
            if(h>60)
            {
                building.Box({0,h+7,0},{14,12,21},facade);
                building.Box({0,h+16,0},{9,6,13},stone);
                building.Box({0,h+23,0},{1,10,1},metal);
            }
            for(float floor=3;floor<h-1;floor+=3.2f)
            {
                for(float wx=-8;wx<=8;wx+=3.2f) for(int face : {-1,1})
                    building.Box({wx,floor,face*(d/2+.025f)},{1.6f,1.85f,.05f},((int(floor)+int(wx)+seed)%7==0)?lit:glass);
                for(float wz=-12;wz<=12;wz+=3.2f) for(int face : {-1,1})
                    building.Box({face*(w/2+.025f),floor,wz},{.05f,1.85f,1.6f},glass);
            }
            building.Box({0,1.3f,d/2+.09f},{3.2f,2.3f,.14f},metal);
            building.Box({0,3.2f,d/2+1},{7,.25f,2},seed%2?yellow:metal);
            publish(building,"Building_"+std::to_string(++buildingCount),block,center+glm::vec3(side?12:-12,0,0));
            auto *collision=entity("Building collision",block);
            collision->SetPosition(center+glm::vec3(side?12:-12,.2f+h/2,0));
            collision->CreateComponent<ColliderComponent>(ColliderComponentConfig{.size={w,h,d}});
        }
        for(int sx : {-1,1}) for(int sz : {-1,1})
        {
            auto p=center+glm::vec3(sx*24.f,0,sz*17.f);
            furniture.Box(p+glm::vec3(0,2.8f,0),{.18f,5.4f,.18f},metal);
            furniture.Box(p+glm::vec3(0,5.5f,0),{1.3f,.2f,.7f},lit);
        }
    }
    for(int z=0;z<6;++z)
    {
        const glm::vec3 p(3,0,-140+56.f*z);
        furniture.Box(p+glm::vec3(0,.65f,0),{1.9f,1.0f,4.4f},taxi);
        furniture.Box(p+glm::vec3(0,1.4f,-.2f),{1.65f,.6f,2.1f},glass);
        for(float x:{-1.f,1.f}) for(float offset:{-1.4f,1.4f}) furniture.Box(p+glm::vec3(x,.4f,offset),{.25f,.65f,.7f},metal);
    }
    publish(markings,"Crosswalks and double yellow lines",props);
    publish(furniture,"Street lamps and yellow cabs",props);
    auto *sun=entity("Late afternoon sun"); auto *light=sun->CreateComponent<LightComponent>();
    light->SetLightType(LightType::Directional);light->SetDirection(glm::normalize(glm::vec3(-.6f,-1,-.3f)));
    light->SetColor({1,.88f,.71f});light->SetIntensity(2.5f);light->SetCastsShadows(true);
    DirectionalShadowSettings shadows;shadows.maxDistance=650;light->SetDirectionalShadowSettings(shadows);
    auto *camera=entity("City overview camera");camera->SetPosition({290,260,340});camera->SetRotation({-29,40,0});
    camera->CreateComponent<CameraComponent>(new render::Camera({.fovY=60,.nearPlane=.1f,.farPlane=1200}),true)->SetMainCamera(true);
    const auto scenePath=root/"Assets/Scenes/ManhattanMini.plutoscene";
    std::filesystem::create_directories(scenePath.parent_path());
    Require(SceneSerializer::Save(city,scenePath.string(),&error),error);
    auto loaded=SceneSerializer::Load(scenePath.string(),&error);Require(loaded!=nullptr,error);
    int loadedRoads=0,loadedJunctions=0;
    auto verify=[&](auto &&self,Entity *e)->void
    {
        if(auto *s=e->GetComponent<SplineComponent>()){s->Rebuild();Require(s->GetGeneratedMesh()!=nullptr,"Reloaded road missing mesh");++loadedRoads;}
        if(e->GetName().starts_with("Baked junction")){Require(e->GetComponent<MeshComponent>()->GetMesh()!=nullptr,"Reloaded junction missing mesh");++loadedJunctions;}
        for(auto *child:e->GetChildren())self(self,child);
    };
    for(auto *e:loaded->GetRootEntities())verify(verify,e);
    Require(loadedRoads==roadCount && loadedJunctions==junctionCount,"Scene round trip counts differ");
    std::ofstream summary(root/"CITY_README.txt");
    summary<<"Manhattan Mini: "<<roadCount<<" editable roads, "<<junctionCount<<" native thick junctions, "<<buildingCount<<" buildings, 24 blocks including two pocket parks.\n"
        <<"Open Assets/Scenes/ManhattanMini.plutoscene. Roads are editable SplineComponents. Junctions were baked with BakeRoadJunction and must be rebaked after changing entrances.\n"
        <<"The original Main scene is preserved. City buildings and street detail use generated, material-batched meshes. Buildings have box collision; sidewalks and junctions have mesh collision.\n";
    std::cout<<roadCount<<" roads, "<<junctionCount<<" junctions, "<<buildingCount<<" buildings. Scene reload validated.\n";
}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
