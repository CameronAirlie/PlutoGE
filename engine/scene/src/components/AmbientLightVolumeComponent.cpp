#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/Entity.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>

namespace PlutoGE::scene
{
    namespace {
        std::atomic<std::uint64_t> revision{1};
        void Write(std::ostream &out, glm::vec3 v) { out << v.x << ' ' << v.y << ' ' << v.z << ' '; }
        void Read(std::istream &in, glm::vec3 &v) { in >> v.x >> v.y >> v.z; }
    }
    void AmbientLightVolumeComponent::SetSize(glm::vec3 size) { m_size = glm::max(size, glm::vec3(.01f)); }
    void AmbientLightVolumeComponent::Configure(float spacing, float clearance, int budget, int directions) {
        m_spacing = std::clamp(spacing, .05f, 100.0f);
        m_clearance = std::clamp(clearance, .001f, .1f);
        m_budget = std::clamp(budget, 8, 16384);
        m_directions = std::clamp(directions, 64, 4096);
    }
    glm::mat4 AmbientLightVolumeComponent::GetVolumeTransform() const {
        return (GetOwner() ? GetOwner()->GetWorldTransform() : glm::mat4(1))*glm::scale(glm::mat4(1),m_size);
    }
    BakedProbeVolume AmbientLightVolumeComponent::GetGridLayout() const {
        BakedProbeVolume layout;
        const auto transform=GetVolumeTransform();
        layout.origin=glm::vec3(transform*glm::vec4(-.5f,-.5f,-.5f,1));
        for (int axis=0;axis<3;++axis) {
            layout.size[axis]=std::max(glm::length(glm::vec3(transform[axis])),.001f);
            layout.gridBasis[axis]=glm::vec3(transform[axis])/layout.size[axis];
        }
        return layout;
    }
    void AmbientLightVolumeComponent::GetWorldBounds(glm::vec3 &origin, glm::vec3 &size) const {
        const auto transform = (GetOwner() ? GetOwner()->GetWorldTransform() : glm::mat4(1))*glm::scale(glm::mat4(1),m_size);
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
        for (int z=0; z<2; ++z) for (int y=0; y<2; ++y) for (int x=0; x<2; ++x) {
            const glm::vec3 p(transform * glm::vec4(x-.5f,y-.5f,z-.5f,1));
            lo=glm::min(lo,p); hi=glm::max(hi,p);
        }
        origin=lo; size=glm::max(hi-lo,glm::vec3(.001f));
    }
    bool AmbientLightVolumeComponent::HasUsableBake() const {
        if (!IsEnabled() || (GetOwner() && !GetOwner()->IsActive()) || !m_baked.IsValid()) return false;
        const auto layout=GetGridLayout();
        if (glm::any(glm::greaterThan(glm::abs(layout.origin-m_baked.origin),glm::vec3(.001f))) ||
            glm::any(glm::greaterThan(glm::abs(layout.size-m_baked.size),glm::vec3(.001f)))) return false;
        for (int axis=0;axis<3;++axis)
            if (glm::any(glm::greaterThan(glm::abs(layout.gridBasis[axis]-m_baked.gridBasis[axis]),glm::vec3(.0001f)))) return false;
        return true;
    }
    void AmbientLightVolumeComponent::SetBakedVolume(BakedProbeVolume volume) {
        m_baked=std::move(volume); m_revision=revision.fetch_add(1);
    }
    void AmbientLightVolumeComponent::ClearBake() { SetBakedVolume({}); }
    std::vector<Property> AmbientLightVolumeComponent::SerializeEditableProperties() const {
        std::ostringstream size; size << m_size.x << ',' << m_size.y << ',' << m_size.z;
        return {{"Size",PropertyType::Vec3,size.str()}, {"Probe Spacing",PropertyType::Float,std::to_string(m_spacing)},
            {"Surface Clearance",PropertyType::Float,std::to_string(m_clearance)}, {"Probe Budget",PropertyType::Int,std::to_string(m_budget)},
            {"Direction Count",PropertyType::Int,std::to_string(m_directions)}};
    }
    std::vector<Property> AmbientLightVolumeComponent::Serialize() const {
        auto properties=SerializeEditableProperties();
        if (!m_baked.IsValid()) return properties;
        std::ostringstream out; out << std::setprecision(std::numeric_limits<float>::max_digits10);
        const bool directional=m_baked.HasDirectionalData(), normals=m_baked.visibilityNormals.size()==m_baked.irradiance.size();
        out << 2 << ' '; Write(out,m_baked.origin); Write(out,m_baked.size);
        for (int axis=0;axis<3;++axis) Write(out,m_baked.gridBasis[axis]);
        out << m_baked.resolution.x << ' ' << m_baked.resolution.y << ' ' << m_baked.resolution.z << ' ' << directional << ' ' << normals << ' ';
        for (std::size_t i=0; i<m_baked.irradiance.size(); ++i) {
            Write(out,m_baked.irradiance[i]);
            if (!directional) continue;
            Write(out,glm::vec3(m_baked.positions[i])); out << m_baked.positions[i].w << ' ';
            for (auto v:m_baked.sphericalHarmonics[i]) Write(out,v);
            for (float d:m_baked.visibility[i]) out << d << ' ';
            if (normals) for (auto n:m_baked.visibilityNormals[i]) Write(out,n);
        }
        properties.push_back({"Baked Data",PropertyType::String,out.str()});
        return properties;
    }
    void AmbientLightVolumeComponent::Deserialize(const std::vector<Property> &properties) {
        for (const auto &p:properties) {
            try {
                if (p.name=="Size") { auto value=p.value; std::replace(value.begin(),value.end(),',',' '); std::istringstream in(value); glm::vec3 v=m_size; Read(in,v); SetSize(v); }
                else if (p.name=="Probe Spacing") m_spacing=std::stof(p.value);
                else if (p.name=="Surface Clearance") m_clearance=std::stof(p.value);
                else if (p.name=="Probe Budget") m_budget=std::stoi(p.value);
                else if (p.name=="Direction Count") m_directions=std::stoi(p.value);
                else if (p.name=="Baked Data") {
                    std::istringstream in(p.value); BakedProbeVolume v; int version=0; bool directional=false,normals=false;
                    in >> version; Read(in,v.origin); Read(in,v.size);
                    if (version==2) for (int axis=0;axis<3;++axis) Read(in,v.gridBasis[axis]);
                    in >> v.resolution.x >> v.resolution.y >> v.resolution.z >> directional >> normals;
                    const auto r=v.resolution;
                    if (!in || (version!=1 && version!=2) || glm::any(glm::lessThan(r,glm::ivec3(1))) || glm::any(glm::greaterThan(r,glm::ivec3(128)))) continue;
                    const std::size_t count=std::size_t(r.x)*r.y*r.z;
                    if (count>16384) continue;
                    v.irradiance.resize(count);
                    if (directional) { v.positions.resize(count); v.sphericalHarmonics.resize(count); v.visibility.resize(count); if(normals) v.visibilityNormals.resize(count); }
                    for (std::size_t i=0;i<count;++i) {
                        Read(in,v.irradiance[i]); if (!directional) continue;
                        glm::vec3 position; Read(in,position); float valid=0; in >> valid; v.positions[i]=glm::vec4(position,valid);
                        for (auto &sh:v.sphericalHarmonics[i]) Read(in,sh);
                        for (float &d:v.visibility[i]) in >> d;
                        if (normals) for (auto &n:v.visibilityNormals[i]) Read(in,n);
                    }
                    if (in) SetBakedVolume(std::move(v));
                }
            } catch (const std::exception &) { /* Keep the previous result on malformed data. */ }
        }
        Configure(m_spacing,m_clearance,m_budget,m_directions);
    }
}
