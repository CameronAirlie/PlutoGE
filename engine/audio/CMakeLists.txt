pluto_add_engine_module(PlutoGEAudio PlutoGE::Audio PLUTO_AUDIO_TARGET
    src/AudioSystem.cpp
)

target_include_directories(${PLUTO_AUDIO_TARGET} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(${PLUTO_AUDIO_TARGET} PRIVATE
    PLUTOGE_USE_OPENAL_SOFT=1
)

if (NOT PLUTO_BUILD_ENGINE_SHARED)
    target_link_libraries(${PLUTO_AUDIO_TARGET} PUBLIC
        PlutoGE::glm
        PlutoGE::openal_soft
    )

    if (WIN32)
        target_link_libraries(${PLUTO_AUDIO_TARGET} PUBLIC xaudio2)
    endif()
endif()
