#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include "PlutoGE/render/postprocess/AutoExposureEffect.h"
#include "PlutoGE/render/postprocess/BloomEffect.h"
#include "PlutoGE/render/postprocess/ColorGradingEffect.h"
#include "PlutoGE/render/postprocess/ChromaticAberrationEffect.h"
#include "PlutoGE/render/postprocess/DDGIEffect.h"
#include "PlutoGE/render/postprocess/DepthOfFieldEffect.h"
#include "PlutoGE/render/postprocess/FXAAEffect.h"
#include "PlutoGE/render/postprocess/GammaCorrectionEffect.h"
#include "PlutoGE/render/postprocess/LensFlareEffect.h"
#include "PlutoGE/render/postprocess/LPVEffect.h"
#include "PlutoGE/render/postprocess/LSAOEffect.h"
#include "PlutoGE/render/postprocess/MotionBlurEffect.h"
#include "PlutoGE/render/postprocess/RSMEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/postprocess/SSREffect.h"
#include "PlutoGE/render/postprocess/TAAEffect.h"
#include "PlutoGE/render/postprocess/ToneMappingEffect.h"
#include "PlutoGE/render/postprocess/VolumetricFogEffect.h"
#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"

#include <algorithm>
#include <array>
#include <map>
#include <mutex>

namespace PlutoGE::render
{
    namespace
    {
        template <typename TEffect>
        std::unique_ptr<IPostProcessEffect> Create()
        {
            return std::make_unique<TEffect>();
        }

        using Stage = PostProcessStage;

        // Pipeline order: by stage, then the order a new preset lists them in.
        constexpr std::array kDescriptors = {
            PostProcessEffectDescriptor{"DDGI", Stage::ProbeLighting, "Probe-based diffuse light that bounces around the whole scene and updates as lights and objects move.", &Create<DDGIEffect>},
            PostProcessEffectDescriptor{"SSGI", Stage::LightingComposite, "Adds bounced light traced against what is on screen; cheap, but misses anything off screen.", &Create<SSGIEffect>},
            PostProcessEffectDescriptor{"VCTGI", Stage::LightingComposite, "Indirect light cone-traced through a voxelized copy of the static scene.", &Create<VoxelConeTracingEffect>},
            PostProcessEffectDescriptor{"LPV", Stage::LightingComposite, "Light propagation volume indirect light (OpenGL renderer only).", &Create<LPVEffect>},
            PostProcessEffectDescriptor{"RSM", Stage::LightingComposite, "Reflective shadow map indirect light (OpenGL renderer only).", &Create<RSMEffect>},
            PostProcessEffectDescriptor{"SceneComposite", Stage::LightingComposite, "Combines the lighting buffers into the final scene colour, and shows debug views of them.", &Create<SceneCompositeEffect>},
            PostProcessEffectDescriptor{"SSAO", Stage::AmbientOcclusion, "Darkens creases and contact points where ambient light is blocked.", &Create<SSAOEffect>},
            PostProcessEffectDescriptor{"LSAO", Stage::AmbientOcclusion, "Ambient occlusion tuned for large-scale shapes; an alternative to SSAO.", &Create<LSAOEffect>},
            PostProcessEffectDescriptor{"SSR", Stage::ScreenSpaceAtmosphere, "Reflections of what is on screen on glossy and wet surfaces.", &Create<SSREffect>},
            PostProcessEffectDescriptor{"VolumetricFog", Stage::FogComposite, "Height fog that scatters light and catches shafts from the sun and local lights.", &Create<VolumetricFogEffect>},
            PostProcessEffectDescriptor{"TAA", Stage::TemporalResolve, "Smooths jagged edges and shimmering by blending each frame with the previous ones.", &Create<TAAEffect>},
            PostProcessEffectDescriptor{"MotionBlur", Stage::CameraOptics, "Blurs fast-moving objects and camera turns, as a real shutter would.", &Create<MotionBlurEffect>},
            PostProcessEffectDescriptor{"DepthOfField", Stage::CameraOptics, "Blurs what is nearer or farther than the focus distance.", &Create<DepthOfFieldEffect>},
            PostProcessEffectDescriptor{"Bloom", Stage::CameraOptics, "Makes bright areas glow softly into their surroundings.", &Create<BloomEffect>},
            PostProcessEffectDescriptor{"LensFlare", Stage::CameraOptics, "Streaks and ghosts from bright lights, as seen through a camera lens.", &Create<LensFlareEffect>},
            PostProcessEffectDescriptor{"AutoExposure", Stage::Exposure, "Adapts brightness to the scene over time, like an eye adjusting between dark and light.", &Create<AutoExposureEffect>},
            PostProcessEffectDescriptor{"ToneMapping", Stage::ToneAndColor, "Maps the scene's high dynamic range onto the screen's range.", &Create<ToneMappingEffect>},
            PostProcessEffectDescriptor{"ColorGrading", Stage::ToneAndColor, "Adjusts the final look: contrast, saturation, white balance and tint.", &Create<ColorGradingEffect>},
            PostProcessEffectDescriptor{"ChromaticAberration", Stage::ToneAndColor, "Splits colour fringes towards the screen edges, like a cheap lens.", &Create<ChromaticAberrationEffect>},
            PostProcessEffectDescriptor{"FXAA", Stage::ToneAndColor, "Fast edge smoothing applied to the finished image.", &Create<FXAAEffect>},
            PostProcessEffectDescriptor{"GammaCorrection", Stage::ToneAndColor, "Converts linear colour for display. Only needed when the output is not already sRGB.", &Create<GammaCorrectionEffect>},
        };

        std::vector<std::string> BuildRegisteredTypes()
        {
            std::vector<std::string> types;
            types.reserve(kDescriptors.size());
            for (const auto &descriptor : kDescriptors)
                types.emplace_back(descriptor.typeName);
            return types;
        }
    }

    const char *GetPostProcessStageName(PostProcessStage stage)
    {
        switch (stage)
        {
        case Stage::ProbeLighting: return "Global Illumination";
        case Stage::LightingComposite: return "Lighting";
        case Stage::AmbientOcclusion: return "Ambient Occlusion";
        case Stage::ScreenSpaceAtmosphere: return "Reflections";
        case Stage::FogComposite: return "Atmosphere";
        case Stage::TemporalResolve: return "Anti-Aliasing";
        case Stage::CameraOptics: return "Lens & Camera";
        case Stage::Exposure: return "Exposure";
        case Stage::ToneAndColor: return "Tone & Colour";
        }
        return "Other";
    }

    std::span<const PostProcessEffectDescriptor> GetPostProcessEffectDescriptors()
    {
        return kDescriptors;
    }

    const PostProcessEffectDescriptor *FindPostProcessEffectDescriptor(std::string_view typeName)
    {
        const auto found = std::ranges::find(kDescriptors, typeName, &PostProcessEffectDescriptor::typeName);
        return found == kDescriptors.end() ? nullptr : &*found;
    }

    const PostProcessEffectDefaults *GetPostProcessEffectDefaults(std::string_view typeName)
    {
        static std::mutex mutex;
        static std::map<std::string, PostProcessEffectDefaults, std::less<>> cache;
        const std::scoped_lock lock(mutex);
        if (const auto cached = cache.find(typeName); cached != cache.end())
            return &cached->second;
        const auto effect = CreatePostProcessEffect(typeName);
        if (!effect)
            return nullptr;
        return &cache.emplace(std::string(typeName), PostProcessEffectDefaults{effect->GetDisplayName(), effect->GetParameters()})
                    .first->second;
    }

    std::unique_ptr<IPostProcessEffect> CreatePostProcessEffect(std::string_view typeName)
    {
        const auto *descriptor = FindPostProcessEffectDescriptor(typeName);
        return descriptor ? descriptor->create() : nullptr;
    }

    const std::vector<std::string> &GetRegisteredPostProcessEffectTypes()
    {
        static const std::vector<std::string> types = BuildRegisteredTypes();
        return types;
    }
}
