add_library(PlutoGERender
    src/Renderer.cpp
    src/BasicRenderer.cpp
    src/RhiRenderService.cpp
    src/RhiSceneRenderer.cpp
    src/RhiPostProcessAdapter.cpp
    src/PostProcessGraph.cpp
    src/PostProcessResourcePool.cpp
    src/PostProcessGraphExecutor.cpp
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
    src/postprocess/ChromaticAberrationEffect.cpp
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
        DEPENDS "${SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Fullscreen.slang" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PostProcessParameters.slang" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ScreenSpaceCommon.slang" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMENT "Compiling ${MODULE} Slang shader for OpenGL and Vulkan"
        VERBATIM)
        set_property(GLOBAL APPEND PROPERTY PLUTO_RHI_SHADER_OUTPUTS ${outputs})
    endfunction()

    function(pluto_add_slang_compute_shader MODULE SOURCE ENTRY)
        set(normalize_arguments
            -DINPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.compute.glsl.raw
            -DOUTPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.compute.glsl)
        if (ARGV3)
            list(APPEND normalize_arguments -DIMAGE_FORMAT=${ARGV3})
        endif()
        set(outputs
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.compute.glsl"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.compute.spv")
        add_custom_command(
        OUTPUT ${outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUTO_RHI_SHADER_OUTPUT_DIR}"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${ENTRY} -target glsl -profile glsl_450 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.compute.glsl.raw"
        COMMAND ${CMAKE_COMMAND} ${normalize_arguments} -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${ENTRY} -target spirv -profile spirv_1_3 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.compute.spv"
        DEPENDS "${SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMENT "Compiling ${MODULE} Slang compute shader for OpenGL and Vulkan"
        VERBATIM)
        set_property(GLOBAL APPEND PROPERTY PLUTO_RHI_SHADER_OUTPUTS ${outputs})
    endfunction()

    function(pluto_add_slang_geometry_shader MODULE SOURCE VERTEX_ENTRY GEOMETRY_ENTRY FRAGMENT_ENTRY)
        set(outputs
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.geometry.glsl"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.spv"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.geometry.spv"
            "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.spv")
        add_custom_command(
        OUTPUT ${outputs}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PLUTO_RHI_SHADER_OUTPUT_DIR}"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${VERTEX_ENTRY} -target glsl -profile glsl_450 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl.raw"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl.raw -DOUTPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.glsl -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${GEOMETRY_ENTRY} -target glsl -profile glsl_450 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.geometry.glsl.raw"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.geometry.glsl.raw -DOUTPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.geometry.glsl -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${FRAGMENT_ENTRY} -target glsl -profile glsl_450 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl.raw"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl.raw -DOUTPUT=${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.glsl -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${VERTEX_ENTRY} -target spirv -profile spirv_1_3 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.vertex.spv"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${GEOMETRY_ENTRY} -target spirv -profile spirv_1_3 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.geometry.spv"
        COMMAND ${PLUTO_SLANGC_EXECUTABLE} "${SOURCE}" -entry ${FRAGMENT_ENTRY} -target spirv -profile spirv_1_3 -matrix-layout-column-major -o "${PLUTO_RHI_SHADER_OUTPUT_DIR}/${MODULE}.fragment.spv"
        DEPENDS "${SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/NormalizeOpenGLShader.cmake"
        COMMENT "Compiling ${MODULE} Slang geometry pipeline for OpenGL and Vulkan"
        VERBATIM)
        set_property(GLOBAL APPEND PROPERTY PLUTO_RHI_SHADER_OUTPUTS ${outputs})
    endfunction()

    pluto_add_slang_graphics_shader(BasicLit "${CMAKE_CURRENT_SOURCE_DIR}/shaders/BasicLit.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(DirectionalShadow "${CMAKE_CURRENT_SOURCE_DIR}/shaders/DirectionalShadow.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(DisplayOutput "${CMAKE_CURRENT_SOURCE_DIR}/shaders/DisplayOutput.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(ToneMapping "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ToneMapping.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(GammaCorrection "${CMAKE_CURRENT_SOURCE_DIR}/shaders/GammaCorrection.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(FXAA "${CMAKE_CURRENT_SOURCE_DIR}/shaders/FXAA.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(ColorGrading "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ColorGrading.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(ChromaticAberration "${CMAKE_CURRENT_SOURCE_DIR}/shaders/ChromaticAberration.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(LensFlare "${CMAKE_CURRENT_SOURCE_DIR}/shaders/LensFlare.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(MotionBlur "${CMAKE_CURRENT_SOURCE_DIR}/shaders/MotionBlur.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(DepthOfField "${CMAKE_CURRENT_SOURCE_DIR}/shaders/DepthOfField.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(TAA "${CMAKE_CURRENT_SOURCE_DIR}/shaders/TAA.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(AutoExposureMeter "${CMAKE_CURRENT_SOURCE_DIR}/shaders/AutoExposure.slang" vertexMain meterMain)
    pluto_add_slang_graphics_shader(AutoExposureApply "${CMAKE_CURRENT_SOURCE_DIR}/shaders/AutoExposure.slang" vertexMain applyMain)
    pluto_add_slang_graphics_shader(SSAO "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SSAO.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(SSAOResolve "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SSAOResolve.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(SSAOComposite "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SSAOComposite.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(SSGI "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SSGI.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(SSR "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SSR.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(VolumetricFog "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VolumetricFog.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(PhysicalSky "${CMAKE_CURRENT_SOURCE_DIR}/shaders/PhysicalSky.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(VolumetricCloud "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VolumetricCloud.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(SceneComposite "${CMAKE_CURRENT_SOURCE_DIR}/shaders/SceneComposite.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(BloomPrefilter "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Bloom.slang" vertexMain prefilterMain)
    pluto_add_slang_graphics_shader(BloomDownsample "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Bloom.slang" vertexMain downsampleMain)
    pluto_add_slang_graphics_shader(BloomUpsample "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Bloom.slang" vertexMain upsampleMain)
    pluto_add_slang_graphics_shader(BloomComposite "${CMAKE_CURRENT_SOURCE_DIR}/shaders/Bloom.slang" vertexMain compositeMain)
    pluto_add_slang_compute_shader(VCTResolve "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VCTResolve.slang" computeMain rgba16f)
    pluto_add_slang_compute_shader(VCTDirectionalMip "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VCTDirectionalMip.slang" computeMain rgba16f)
    pluto_add_slang_geometry_shader(VCTVoxelize "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VCTVoxelize.slang" vertexMain geometryMain fragmentMain)
    pluto_add_slang_graphics_shader(VCTConeTrace "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VCTConeTrace.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(VCTTemporal "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VCTTemporal.slang" vertexMain fragmentMain)
    pluto_add_slang_graphics_shader(VCTMetadata "${CMAKE_CURRENT_SOURCE_DIR}/shaders/VCTMetadata.slang" vertexMain fragmentMain)
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
