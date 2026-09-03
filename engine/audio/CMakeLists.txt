add_library(PlutoGEAudio
    src/AudioSystem.cpp
)

add_library(PlutoGE::Audio ALIAS PlutoGEAudio)

target_include_directories(PlutoGEAudio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGEAudio PUBLIC
    PlutoGE::glm
    PlutoGE::openal_soft
)

target_compile_definitions(PlutoGEAudio PRIVATE
    PLUTOGE_USE_OPENAL_SOFT=1
)

if (WIN32 AND MSVC)
    target_compile_definitions(PlutoGEAudio PRIVATE
        PLUTOGE_USE_XAUDIO2=1
    )
    target_link_libraries(PlutoGEAudio PUBLIC
        xaudio2
    )
endif()
