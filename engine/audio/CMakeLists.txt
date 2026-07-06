add_library(PlutoGEAudio
    src/AudioSystem.cpp
)

add_library(PlutoGE::Audio ALIAS PlutoGEAudio)

target_include_directories(PlutoGEAudio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(PlutoGEAudio PUBLIC
    PlutoGE::glm
)

if (WIN32)
    target_link_libraries(PlutoGEAudio PUBLIC
        xaudio2
    )
endif()
