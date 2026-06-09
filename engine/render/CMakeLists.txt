add_library(PlutoGERender
    src/Renderer.cpp
    src/NvrhiBackend.cpp
    src/TextureManager.cpp
    src/Shader.cpp
    src/Texture.cpp
    src/Graphics.cpp
    src/RenderTarget.cpp
    src/GBuffer.cpp
    src/Material.cpp
    src/postprocess/ShaderPostProcessEffect.cpp
    src/postprocess/AutoExposureEffect.cpp
    src/postprocess/ColorGradingEffect.cpp
    src/postprocess/SceneCompositeEffect.cpp
    src/postprocess/ToneMappingEffect.cpp
    src/postprocess/SSAOEffect.cpp
    src/postprocess/SSGIEffect.cpp
    src/postprocess/RSMEffect.cpp
    src/postprocess/VolumetricFogEffect.cpp
    src/postprocess/TAAEffect.cpp
    src/postprocess/MotionBlurEffect.cpp
    src/postprocess/DepthOfFieldEffect.cpp
    src/postprocess/FXAAEffect.cpp
    src/postprocess/GammaCorrectionEffect.cpp
    src/postprocess/PostProcessEffectFactory.cpp
    src/passes/GeometryPass.cpp
    src/passes/GridPass.cpp
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

if (PLUTO_WITH_NVRHI)
    find_program(PLUTO_SLANGC_EXECUTABLE slangc REQUIRED)

    set(PLUTO_NVRHI_SHADER_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/shaders/nvrhi/BasicGeometry.slang)
    set(PLUTO_NVRHI_SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/nvrhi_shaders)
    set(PLUTO_NVRHI_GENERATED_SHADER_SOURCES
        ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryVS_DXIL.cpp
        ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryPS_DXIL.cpp
        ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryVS_SPIRV.cpp
        ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryPS_SPIRV.cpp
    )

    file(MAKE_DIRECTORY ${PLUTO_NVRHI_SHADER_BINARY_DIR})

    function(pluto_compile_nvrhi_shader ENTRY STAGE TARGET PROFILE BINARY_NAME SYMBOL_NAME OUTPUT_CPP)
        set(OUTPUT_BINARY ${PLUTO_NVRHI_SHADER_BINARY_DIR}/${BINARY_NAME})
        add_custom_command(
            OUTPUT ${OUTPUT_BINARY}
            COMMAND ${PLUTO_SLANGC_EXECUTABLE}
                ${PLUTO_NVRHI_SHADER_SOURCE}
                -entry ${ENTRY}
                -stage ${STAGE}
                -target ${TARGET}
                -profile ${PROFILE}
                -o ${OUTPUT_BINARY}
            DEPENDS ${PLUTO_NVRHI_SHADER_SOURCE}
            VERBATIM
        )
        add_custom_command(
            OUTPUT ${OUTPUT_CPP}
            COMMAND ${CMAKE_COMMAND}
                -DINPUT_FILE=${OUTPUT_BINARY}
                -DOUTPUT_FILE=${OUTPUT_CPP}
                -DSYMBOL_NAME=${SYMBOL_NAME}
                -P ${CMAKE_SOURCE_DIR}/cmake/EmbedBinaryFile.cmake
            DEPENDS ${OUTPUT_BINARY} ${CMAKE_SOURCE_DIR}/cmake/EmbedBinaryFile.cmake
            VERBATIM
        )
    endfunction()

    pluto_compile_nvrhi_shader(vertexMain vertex dxil sm_6_0 BasicGeometry.vs.dxil BasicGeometryVS_DXIL ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryVS_DXIL.cpp)
    pluto_compile_nvrhi_shader(fragmentMain fragment dxil sm_6_0 BasicGeometry.ps.dxil BasicGeometryPS_DXIL ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryPS_DXIL.cpp)
    pluto_compile_nvrhi_shader(vertexMain vertex spirv spirv_1_5 BasicGeometry.vs.spv BasicGeometryVS_SPIRV ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryVS_SPIRV.cpp)
    pluto_compile_nvrhi_shader(fragmentMain fragment spirv spirv_1_5 BasicGeometry.ps.spv BasicGeometryPS_SPIRV ${PLUTO_NVRHI_SHADER_BINARY_DIR}/BasicGeometryPS_SPIRV.cpp)

    target_sources(PlutoGERender PRIVATE ${PLUTO_NVRHI_GENERATED_SHADER_SOURCES})
    target_compile_definitions(PlutoGERender PUBLIC PLUTOGE_WITH_NVRHI=1)
    target_link_libraries(PlutoGERender PUBLIC
        nvrhi
        nvrhi_d3d12
        nvrhi_vk
        Vulkan-Headers
    )
    if (WIN32)
        target_link_libraries(PlutoGERender PUBLIC d3d12 dxgi)
    endif()
endif()
