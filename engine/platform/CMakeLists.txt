add_library(PlutoGEContent src/ContentPack.cpp)
add_library(PlutoGE::Content ALIAS PlutoGEContent)
target_include_directories(PlutoGEContent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(PlutoGEContent PRIVATE
    $<TARGET_PROPERTY:zlibstatic,SOURCE_DIR> $<TARGET_PROPERTY:zlibstatic,BINARY_DIR>)
target_link_libraries(PlutoGEContent PRIVATE zlibstatic)
target_compile_features(PlutoGEContent PUBLIC cxx_std_20)

add_library(PlutoGEPlatform
    src/Window.cpp
)

add_library(PlutoGE::Platform ALIAS PlutoGEPlatform)


target_link_libraries(PlutoGEPlatform PUBLIC
    PlutoGE::Content
    PlutoGE::glfw
    PlutoGE::glad
)

target_include_directories(PlutoGEPlatform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
