add_library(PlutoGERender
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
    src/postprocess/SurfaceCacheGIEffect.cpp
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
    src/surfacecache/SurfaceCache.cpp
    src/surfacecache/SurfaceCacheAtlas.cpp
    src/surfacecache/SurfaceCardSpatialIndex.cpp
    src/surfacecache/SurfaceCardGpuIndex.cpp
    src/passes/GeometryPass.cpp
    src/passes/DecalPass.cpp
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
    src/RmlUiRuntime.cpp
    src/passes/RmlUiPass.cpp
)

add_library(PlutoGE::Render ALIAS PlutoGERender)

target_include_directories(PlutoGERender PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGERender PUBLIC
    # Add dependencies here
    PlutoGE::Platform
    PlutoGE::Assets
    PlutoGE::Core
    PlutoGE::glad
    PlutoGE::Scene
    PlutoGE::stb_image
    PlutoGE::rmlui
)
