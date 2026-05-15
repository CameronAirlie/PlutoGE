add_library(PlutoGERender
    src/Renderer.cpp
    src/TextureManager.cpp
    src/Shader.cpp
    src/Texture.cpp
    src/Graphics.cpp
    src/RenderTarget.cpp
    src/GBuffer.cpp
    src/Material.cpp
    src/postprocess/ShaderPostProcessEffect.cpp
    src/postprocess/SceneCompositeEffect.cpp
    src/postprocess/ToneMappingEffect.cpp
    src/postprocess/SSAOEffect.cpp
    src/postprocess/SSGIEffect.cpp
    src/postprocess/RSMEffect.cpp
    src/postprocess/VolumetricFogEffect.cpp
    src/postprocess/FXAAEffect.cpp
    src/postprocess/GammaCorrectionEffect.cpp
    src/postprocess/PostProcessEffectFactory.cpp
    src/passes/GeometryPass.cpp
    src/passes/LightPropagationVolumePass.cpp
    src/passes/LightingPass.cpp
    src/passes/ShadowPass.cpp
    src/passes/PostProcessPass.cpp
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
)