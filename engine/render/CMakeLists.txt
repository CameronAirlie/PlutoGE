pluto_add_engine_module(PlutoGERender PlutoGE::Render PLUTO_RENDER_TARGET
    src/Renderer.cpp
    src/TextureManager.cpp
    src/TexturePainter.cpp
    src/Shader.cpp
    src/Texture.cpp
    src/ShaderGraph.cpp
    src/Graphics.cpp
    src/RenderTarget.cpp
    src/GBuffer.cpp
    src/Material.cpp
    src/postprocess/ShaderPostProcessEffect.cpp
    src/postprocess/AutoExposureEffect.cpp
    src/postprocess/BloomEffect.cpp
    src/postprocess/ColorGradingEffect.cpp
    src/postprocess/SceneCompositeEffect.cpp
    src/postprocess/ToneMappingEffect.cpp
    src/postprocess/SSAOEffect.cpp
    src/postprocess/SSGIEffect.cpp
    src/postprocess/SSREffect.cpp
    src/postprocess/RSMEffect.cpp
    src/postprocess/VoxelConeTracingEffect.cpp
    src/postprocess/VolumetricFogEffect.cpp
    src/postprocess/TAAEffect.cpp
    src/postprocess/MotionBlurEffect.cpp
    src/postprocess/DepthOfFieldEffect.cpp
    src/postprocess/LensFlareEffect.cpp
    src/postprocess/FXAAEffect.cpp
    src/postprocess/GammaCorrectionEffect.cpp
    src/postprocess/PostProcessEffectFactory.cpp
    src/passes/GeometryPass.cpp
    src/passes/GridPass.cpp
    src/passes/LightPropagationVolumePass.cpp
    src/passes/LightingPass.cpp
    src/passes/OceanPass.cpp
    src/passes/ParticlePass.cpp
    src/passes/ShadowPass.cpp
    src/passes/TransparentPass.cpp
    src/passes/VolumetricCloudPass.cpp
    src/passes/PostProcessPass.cpp
    src/passes/PhysicalSkyPass.cpp
    src/passes/RuntimeUIPass.cpp
)

target_include_directories(${PLUTO_RENDER_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_RENDER_TARGET} PUBLIC
        PlutoGE::Platform
        PlutoGE::Assets
        PlutoGE::Core
        PlutoGE::Scene
        PlutoGE::glad
        PlutoGE::stb_image
    )
endif()
