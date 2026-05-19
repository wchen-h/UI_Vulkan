cmake_minimum_required(VERSION 3.20)
project(UI_Vulkan VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Vulkan
find_package(Vulkan REQUIRED)

# GLFW3
find_package(glfw3 REQUIRED)

# glm (for future use)
find_package(glm REQUIRED)

# ImGui sources
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

# Shader build directory
set(SHADER_SRC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BUILD_DIR})

# Collect shader outputs
set(SHADER_SPV_FILES
    ${SHADER_BUILD_DIR}/ui.vert.spv
    ${SHADER_BUILD_DIR}/ui.frag.spv
    ${SHADER_BUILD_DIR}/srgb_convert.vert.spv
    ${SHADER_BUILD_DIR}/srgb_convert.frag.spv
    ${SHADER_BUILD_DIR}/pq_convert.frag.spv
)

# Custom commands: compile each shader
add_custom_command(
    OUTPUT ${SHADER_BUILD_DIR}/ui.vert.spv
    COMMAND glslangValidator -V ${SHADER_SRC_DIR}/ui.vert -o ${SHADER_BUILD_DIR}/ui.vert.spv
    DEPENDS ${SHADER_SRC_DIR}/ui.vert
    COMMENT "SPIR-V: ui.vert"
)
add_custom_command(
    OUTPUT ${SHADER_BUILD_DIR}/ui.frag.spv
    COMMAND glslangValidator -V ${SHADER_SRC_DIR}/ui.frag -o ${SHADER_BUILD_DIR}/ui.frag.spv
    DEPENDS ${SHADER_SRC_DIR}/ui.frag
    COMMENT "SPIR-V: ui.frag"
)
add_custom_command(
    OUTPUT ${SHADER_BUILD_DIR}/srgb_convert.vert.spv
    COMMAND glslangValidator -V ${SHADER_SRC_DIR}/srgb_convert.vert -o ${SHADER_BUILD_DIR}/srgb_convert.vert.spv
    DEPENDS ${SHADER_SRC_DIR}/srgb_convert.vert
    COMMENT "SPIR-V: srgb_convert.vert"
)
add_custom_command(
    OUTPUT ${SHADER_BUILD_DIR}/srgb_convert.frag.spv
    COMMAND glslangValidator -V ${SHADER_SRC_DIR}/srgb_convert.frag -o ${SHADER_BUILD_DIR}/srgb_convert.frag.spv
    DEPENDS ${SHADER_SRC_DIR}/srgb_convert.frag
    COMMENT "SPIR-V: srgb_convert.frag"
)
add_custom_command(
    OUTPUT ${SHADER_BUILD_DIR}/pq_convert.frag.spv
    COMMAND glslangValidator -V ${SHADER_SRC_DIR}/pq_convert.frag -o ${SHADER_BUILD_DIR}/pq_convert.frag.spv
    DEPENDS ${SHADER_SRC_DIR}/pq_convert.frag
    COMMENT "SPIR-V: pq_convert.frag"
)

# Custom target for shader compilation
add_custom_target(Shaders DEPENDS ${SHADER_SPV_FILES})

# Main executable (no SPV files in sources)
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${IMGUI_SOURCES}
)

add_dependencies(${PROJECT_NAME} Shaders)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${Vulkan_INCLUDE_DIRS}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Vulkan::Vulkan
    glfw
    glm::glm
)

# Platform-specific
if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
endif()

# Asset / shader paths injected at compile time
set(ASSET_ABS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../python/pic/cropped_images)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSET_DIR="${ASSET_ABS_DIR}"
    SHADER_DIR="${SHADER_BUILD_DIR}/"
)
