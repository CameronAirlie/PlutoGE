#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FastTextNumbers.h"
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
        void Read(std::istream &in, glm::vec3 &v) { in >> v.x >> v.y >> v.z; }

        void WriteNumber(std::string &out, float value) { AppendNumber(out, value); out.push_back(' '); }
        void WriteNumber(std::string &out, int value) { AppendNumber(out, static_cast<std::uint64_t>(value)); out.push_back(' '); }
        void WriteVec3(std::string &out, const glm::vec3 &v) { WriteNumber(out, v.x); WriteNumber(out, v.y); WriteNumber(out, v.z); }

        std::string EncodeBake(const BakedProbeVolume &baked)
        {
            const bool directional = baked.HasDirectionalData();
            const bool normals = baked.visibilityNormals.size() == baked.irradiance.size();
            const bool sky = baked.HasSkyVisibility();
            constexpr std::size_t kTypicalProbeBytes = 8192;
            std::string out;
            out.reserve(baked.irradiance.size() * (directional ? kTypicalProbeBytes : 64) + 256);
            // Version 3 appends sky visibility and the live sky scale; 1 and 2 remain readable.
            WriteNumber(out, 3);
            WriteVec3(out, baked.origin);
            WriteVec3(out, baked.size);
            for (int axis = 0; axis < 3; ++axis) WriteVec3(out, baked.gridBasis[axis]);
            WriteNumber(out, baked.resolution.x);
            WriteNumber(out, baked.resolution.y);
            WriteNumber(out, baked.resolution.z);
            WriteNumber(out, directional ? 1 : 0);
            WriteNumber(out, normals ? 1 : 0);
            WriteNumber(out, sky ? 1 : 0);
            WriteNumber(out, sky ? baked.skyIrradianceScale : 0.0f);
            for (std::size_t i = 0; i < baked.irradiance.size(); ++i)
            {
                WriteVec3(out, baked.irradiance[i]);
                if (!directional) continue;
                WriteVec3(out, glm::vec3(baked.positions[i]));
                WriteNumber(out, baked.positions[i].w);
                for (const auto &v : baked.sphericalHarmonics[i]) WriteVec3(out, v);
                for (const float d : baked.visibility[i]) WriteNumber(out, d);
                if (normals) for (const auto &n : baked.visibilityNormals[i]) WriteVec3(out, n);
                if (sky)
                {
                    WriteVec3(out, glm::vec3(baked.skyVisibility[i]));
                    WriteNumber(out, baked.skyVisibility[i].w);
                }
            }
            return out;
        }

        // Reads the space-separated numbers of a bake; any malformed or missing one fails the whole read.
        class BakeReader
        {
        public:
            explicit BakeReader(std::string_view text) : m_text(text) {}
            explicit operator bool() const { return m_good; }

            template <typename T>
            BakeReader &operator>>(T &value)
            {
                std::string_view token;
                while (m_good)
                {
                    if (!NextToken(m_text, ' ', token)) { m_good = false; break; }
                    if (token.empty()) continue;
                    if (const auto parsed = ParseNumberToken<T>(token)) value = *parsed;
                    else m_good = false;
                    break;
                }
                return *this;
            }

            BakeReader &operator>>(bool &value)
            {
                int number = 0;
                *this >> number;
                m_good = m_good && (number == 0 || number == 1);
                value = number == 1;
                return *this;
            }

            BakeReader &operator>>(glm::vec3 &v) { return *this >> v.x >> v.y >> v.z; }

        private:
            std::string_view m_text;
            bool m_good = true;
        };
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
        m_baked=std::move(volume); m_baked.blendDistance=m_blendDistance; m_baked.reflectionOcclusion=m_reflectionOcclusion;
        m_revision=revision.fetch_add(1);
        m_bakeRevision=m_revision;
    }
    bool AmbientLightVolumeComponent::SkyLightingNeedsRebake() const {
        // A Live request baked without a physical sky stays baked by design (see AmbientSkyLighting).
        return m_baked.IsValid() && (!m_baked.HasSkyVisibility() ||
            (m_skyLighting==AmbientSkyLighting::Baked && m_baked.HasLiveSky()));
    }
    // These change the packed volume order or parameters, so they bump the revision.
    void AmbientLightVolumeComponent::SetPriority(int priority) {
        if (priority==m_priority) return;
        m_priority=priority; m_revision=revision.fetch_add(1);
    }
    void AmbientLightVolumeComponent::SetBlendDistance(float distance) {
        distance=std::max(distance,0.0f);
        if (distance==m_blendDistance) return;
        m_blendDistance=m_baked.blendDistance=distance; m_revision=revision.fetch_add(1);
    }
    void AmbientLightVolumeComponent::SetReflectionOcclusion(float strength) {
        strength=std::isfinite(strength) ? std::clamp(strength,0.0f,1.0f) : 1.0f;
        if (strength==m_reflectionOcclusion) return;
        m_reflectionOcclusion=m_baked.reflectionOcclusion=strength; m_revision=revision.fetch_add(1);
    }
    void AmbientLightVolumeComponent::ClearBake() { SetBakedVolume({}); }
    std::vector<Property> AmbientLightVolumeComponent::SerializeEditableProperties() const {
        std::ostringstream size; size << m_size.x << ',' << m_size.y << ',' << m_size.z;
        return {{"Size",PropertyType::Vec3,size.str()}, {"Probe Spacing",PropertyType::Float,std::to_string(m_spacing)},
            {"Surface Clearance",PropertyType::Float,std::to_string(m_clearance)}, {"Probe Budget",PropertyType::Int,std::to_string(m_budget)},
            {"Direction Count",PropertyType::Int,std::to_string(m_directions)},
            {"Priority",PropertyType::Int,std::to_string(m_priority)}, {"Blend Distance",PropertyType::Float,std::to_string(m_blendDistance)},
            {"Sky Lighting",PropertyType::Enum,std::to_string(static_cast<int>(m_skyLighting)),{"Live","Baked"}},
            {"Reflection Occlusion",PropertyType::Float,std::to_string(m_reflectionOcclusion)}};
    }
    std::vector<Property> AmbientLightVolumeComponent::Serialize() const {
        return MaterializeSharedProperties(SerializeShared());
    }
    std::vector<Property> AmbientLightVolumeComponent::SerializeShared() const {
        auto properties=SerializeEditableProperties();
        if (!m_baked.IsValid()) return properties;
        Property bake{"Baked Data",PropertyType::String};
        bake.sharedValue=m_bakeText.Get(m_bakeRevision,[this] { return EncodeBake(m_baked); });
        properties.push_back(std::move(bake));
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
                else if (p.name=="Priority") SetPriority(std::stoi(p.value));
                else if (p.name=="Blend Distance") SetBlendDistance(std::stof(p.value));
                else if (p.name=="Sky Lighting") m_skyLighting=std::stoi(p.value)==1 ? AmbientSkyLighting::Baked : AmbientSkyLighting::Live;
                else if (p.name=="Reflection Occlusion") SetReflectionOcclusion(std::stof(p.value));
                else if (p.name=="Baked Data") {
                    BakeReader in(p.Text()); BakedProbeVolume v; int version=0; bool directional=false,normals=false,sky=false;
                    in >> version >> v.origin >> v.size;
                    if (version>=2) for (int axis=0;axis<3;++axis) in >> v.gridBasis[axis];
                    in >> v.resolution.x >> v.resolution.y >> v.resolution.z >> directional >> normals;
                    if (version>=3) in >> sky >> v.skyIrradianceScale;
                    const auto r=v.resolution;
                    if (!in || version<1 || version>3 || glm::any(glm::lessThan(r,glm::ivec3(1))) || glm::any(glm::greaterThan(r,glm::ivec3(128)))) continue;
                    const std::size_t count=std::size_t(r.x)*r.y*r.z;
                    if (count>16384) continue;
                    sky=sky && directional;
                    if (!sky || !std::isfinite(v.skyIrradianceScale)) v.skyIrradianceScale=0;
                    v.irradiance.resize(count);
                    if (directional) { v.positions.resize(count); v.sphericalHarmonics.resize(count); v.visibility.resize(count); if(normals) v.visibilityNormals.resize(count); }
                    if (sky) v.skyVisibility.resize(count);
                    for (std::size_t i=0;i<count;++i) {
                        in >> v.irradiance[i]; if (!directional) continue;
                        glm::vec3 position; in >> position; float valid=0; in >> valid; v.positions[i]=glm::vec4(position,valid);
                        for (auto &sh:v.sphericalHarmonics[i]) in >> sh;
                        for (float &d:v.visibility[i]) in >> d;
                        if (normals) for (auto &n:v.visibilityNormals[i]) in >> n;
                        if (sky) { glm::vec3 gradient; in >> gradient; float share=0; in >> share; v.skyVisibility[i]=glm::vec4(gradient,share); }
                    }
                    if (in) SetBakedVolume(std::move(v));
                }
            } catch (const std::exception &) { /* Keep the previous result on malformed data. */ }
        }
        Configure(m_spacing,m_clearance,m_budget,m_directions);
    }
}
