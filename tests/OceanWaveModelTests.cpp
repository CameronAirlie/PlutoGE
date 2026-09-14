#include "PlutoGE/scene/OceanWaveModel.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void Check(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }
}
int main() try
{
    using namespace PlutoGE::scene;
    OceanWaveSettings settings;
    settings.amplitude = 1.2f;
    const auto spectrum = BuildOceanWaveSpectrum(settings, 7.0);
    const glm::vec2 position(2.3f, -4.1f);
    const auto sample = SampleOceanSurface(spectrum, position);
    constexpr float epsilon = .001f;
    const auto height = [&](glm::vec2 p) { return SampleOceanSurface(spectrum,p).height; };
    Check(std::abs((height(position+glm::vec2(epsilon,0))-height(position-glm::vec2(epsilon,0)))/(2*epsilon)-sample.gradient.x)<.001f,
          "Analytical X slope disagrees with displaced surface");
    Check(std::abs((height(position+glm::vec2(0,epsilon))-height(position-glm::vec2(0,epsilon)))/(2*epsilon)-sample.gradient.y)<.001f,
          "Analytical Z slope disagrees with displaced surface");
    const auto before = SampleOceanSurface(BuildOceanWaveSpectrum(settings,7.0-epsilon),position);
    const auto after = SampleOceanSurface(BuildOceanWaveSpectrum(settings,7.0+epsilon),position);
    Check(std::abs((after.height-before.height)/(2*epsilon)-sample.verticalVelocity)<.001f,"Incorrect surface velocity");
    for (int i=0;i<1000;++i)
    {
        const auto value=SampleOceanSurface(spectrum,{i*.71f,i*-.39f});
        Check(std::abs(value.height)<=spectrum.heightBound+.00001f,"Wave escaped intersection envelope");
        Check(std::abs(glm::length(value.normal)-1.f)<.00001f && value.normal.y>0,"Invalid wave normal");
    }
    // Zero spread intentionally remains a planar train; nonzero spread breaks long ridges.
    auto alignedSettings=settings;
    alignedSettings.directionalSpread=0;
    const auto aligned=BuildOceanWaveSpectrum(alignedSettings,7);
    const glm::vec2 along(aligned.shape[0]);
    const glm::vec2 across(-along.y,along.x);
    Check(std::abs(SampleOceanSurface(aligned,position+across*35.f).height-
                   SampleOceanSurface(aligned,position).height)<.0001f,"Zero spread lost directional control");
    float ridgeVariation=0;
    for(int i=1;i<=8;++i)
        ridgeVariation+=std::abs(SampleOceanSurface(spectrum,position+across*float(i)*9.f).height-sample.height);
    Check(ridgeVariation>.25f,"Directional sea remains an uninterrupted ridge");
    settings.waterDepth=.2f;
    const auto shallow=BuildOceanWaveSpectrum(settings,0);
    Check(shallow.motion[0].y<spectrum.motion[0].y,"Shallow-water dispersion does not slow long waves");
    settings.speed=0;
    Check(BuildOceanWaveSpectrum(settings,1).motion==BuildOceanWaveSpectrum(settings,100000).motion,"Paused waves drift");
    settings.amplitude=0;
    const auto flat=SampleOceanSurface(BuildOceanWaveSpectrum(settings,3),position);
    Check(flat.height==0 && flat.normal==glm::vec3(0,1,0) && flat.crest==0,"Zero amplitude is not flat");
    settings.amplitude=std::numeric_limits<float>::quiet_NaN();
    Check(std::isfinite(SampleOceanSurface(BuildOceanWaveSpectrum(settings,0),position).height),"Invalid settings poison spectrum");
    OceanComponent ocean;
    ocean.AddArea({{0,0},{1,0},{0,1}});
    ocean.Deserialize({{"WindDirection",PropertyType::Float,"90"},{"WindSea",PropertyType::Float,"0.8"}});
    Check(ocean.GetAreas().size()==1,"Partial property edit destroyed area masks");
    const auto beforePreset=ocean.GetWaveSpectrum();
    const auto visibility=ocean.GetMaxVisibilityDepth();
    ocean.ApplyStylizedSeaPreset();
    Check(ocean.GetStylization()==1 && ocean.GetAreas().size()==1 && ocean.GetMaxVisibilityDepth()==visibility,
          "Stylized preset damaged masks or visibility");
    Check(ocean.GetWaveSpectrum().heightBound>beforePreset.heightBound,"Preset did not create rolling swells");
    OceanComponent styled;
    styled.Deserialize(ocean.Serialize());
    Check(styled.GetCrestColor()==ocean.GetCrestColor() && styled.GetStylization()==1,"Stylization did not round-trip");
    ocean.Deserialize({{"WindSea",PropertyType::Float,"0.8"}});
    OceanComponent restored;
    restored.Deserialize(ocean.Serialize());
    Check(restored.GetWindDirection()==90 && restored.GetWindSea()==.8f && restored.GetAreas().size()==1,"Ocean controls did not round-trip");
    ocean.Deserialize({{"WaveAmplitude",PropertyType::Float,"0.18"}});
    ocean.Update(99999.99f);
    const auto old=ocean.SampleLocalSurface(position);
    ocean.Update(.02f);
    const auto next=ocean.SampleLocalSurface(position);
    Check(std::abs(next.height-old.height)<.05f,"Wave clock jumps at the old wrap boundary");
    ocean.Update(std::numeric_limits<float>::infinity());
    Check(std::isfinite(ocean.SampleLocalSurface(position).height),"Invalid delta poisons ocean time");
    std::cout<<"Ocean wave derivatives, bounds, dispersion, pause, serialization and long-running clock passed\n";
}
catch(const std::exception &error) { std::cerr<<error.what()<<'\n';return 1; }
