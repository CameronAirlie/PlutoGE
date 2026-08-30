add_library(PlutoGERender
    src/Renderer.cpp
    src/BasicRenderer.cpp
    src/RhiRenderService.cpp
    src/RhiSceneRenderer.cpp
    src/TextureManager.cpp
    src/TexturePainter.cpp
    src/Shader.cpp
    src/ShaderArtifacts.cpp
    src/Texture.cpp
    src/ShaderGraph.cpp
    src/Graphics.cpp
    src/rhi/RenderDeviceFactory.cpp
    src/rhi/opengl/OpenGLDevice.cpp
    src/rhi/vulkan/VulkanBootstrap.cpp
    src/rhi/vulkan/VulkanDevice.cpp
    src/RenderTarget.cpp
    src/SpatialUpscaler.cpp
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

find_program(PLUTO_SLANGC_EXECUTABLE NAMES slangc)
if (PLUTO_SLANGC_EXECUTABLE)
    set(PLUTO_RHI_SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

    function(pluto_add_slang_graphics_shader MODULE SOURCE VERTEX_ENTRY FRAGMENT_ENTRY)
        set(outputs
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.spv"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.spv")
        add_custom_command(
        OUTPUT ${outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUTO_RHI_SHADER_OUTPUT_DIR}"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${VERTEX_ENTRY} -target glsl -profile glsl_450 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl.raw"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl.raw -DOUTPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${FRAGMENT_ENTRY} -target glsl -profile glsl_450 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl.raw"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl.raw -DOUTPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${VERTEX_ENTRY} -target spirv -profile spirv_1_3 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.spv"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${FRAGMENT_ENTRY} -target spirv -profile spirv_1_3 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.spv"
        DEPENDS "${SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMENT "Compiling ${MODULE} Slang shader for OpenGL and Vulkan"
        VERBATIM)
        set_property(GLOBAL APPEND PROPERTY PLUTO_RHI_SHADER_OUTPUTS ${outputs})
    endfunction()

    pluto_add_slang_graphics_shader(BasicLit "${CMAKE_CURRENT_SOURCE_DIR}/shaders/BasicLit.slang" vertexMain fragmentMain)
    get_property(PLUTO_RHI_SHADER_OUTPUTS GLOBAL PROPERTY PLUTO_RHI_SHADER_OUTPUTS)
    add_custom_target(PlutoGERhiShaders DEPENDS ${PLUTO_RHI_SHADER_OUTPUTS})
    add_dependencies(PlutoGERender PlutoGERhiShaders)
else()
    message(WARNING "slangc was not found; RHI shader artifacts will not be built")
endif()

target_include_directories(PlutoGERender PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(PlutoGERender PRIVATE
    PLUTO_RHI_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")

target_link_libraries(PlutoGERender PUBLIC
    # Add dependencies here
    PlutoGE::Platform
    PlutoGE::Assets
    PlutoGE::Core
    PlutoGE::glad
    PlutoGE::Scene
    PlutoGE::stb_image
    PlutoGE::rmlui
    PlutoGE::vulkan
)
