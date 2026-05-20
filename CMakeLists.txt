cmake_minimum_required(VERSION 3.20)
project(UI_Vulkan VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)  # 不联网检查依赖更新

# Vulkan SDK (must be pre-installed)
find_package(Vulkan REQUIRED)

# === GLFW3: auto-fetch if not installed ===
find_package(glfw3 QUIET)
if(NOT glfw3_FOUND)
    include(FetchContent)
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
    )
    FetchContent_MakeAvailable(glfw)
    set(glfw3_FOUND TRUE)
endif()

# === glm: auto-fetch if not installed ===
find_package(glm QUIET)
if(NOT glm_FOUND)
    include(FetchContent)
    FetchContent_Declare(glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

# === ImGui sources (bundled in external/) ===
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

# === Shader compiler detection ===
find_program(GLSLC glslc)                  # Vulkan SDK (preferred)
find_program(GLSLC glslangValidator)       # Linux apt fallback
if(NOT GLSLC)
    message(FATAL_ERROR "Shader compiler not found. Install Vulkan SDK (glslc) or glslang-tools.")
endif()
message(STATUS "Shader compiler: ${GLSLC}")

# Determine compilation flags per compiler
if(GLSLC MATCHES "glslc")
    set(GLSL_FLAG_VERT "-fshader-stage=vert")
    set(GLSL_FLAG_FRAG "-fshader-stage=frag")
else()
    set(GLSL_FLAG_VERT "-V")
    set(GLSL_FLAG_FRAG "-V")
endif()

# === Shader compilation ===
set(SHADER_SRC_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_BUILD_DIR})

set(SHADER_SPV_FILES)

function(add_shader NAME STAGE)
    set(OUT ${SHADER_BUILD_DIR}/${NAME}.spv)
    list(APPEND SHADER_SPV_FILES ${OUT})
    set(SHADER_SPV_FILES ${SHADER_SPV_FILES} PARENT_SCOPE)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${GLSLC} ${GLSL_FLAG_${STAGE}} ${SHADER_SRC_DIR}/${NAME} -o ${OUT}
        DEPENDS ${SHADER_SRC_DIR}/${NAME}
        COMMENT "SPIR-V: ${NAME}"
    )
endfunction()

add_shader(ui.vert           VERT)
add_shader(ui.frag           FRAG)
add_shader(srgb_convert.vert VERT)
add_shader(srgb_convert.frag FRAG)
add_shader(pq_convert.frag   FRAG)

add_custom_target(Shaders DEPENDS ${SHADER_SPV_FILES})

# === Main executable ===
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/texture.cpp
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

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
endif()

# Asset / shader paths
set(ASSET_ABS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../python/pic/cropped_images)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    ASSET_DIR="${ASSET_ABS_DIR}"
    SHADER_DIR="${SHADER_BUILD_DIR}/"
)
