cmake_minimum_required(VERSION 3.22)
project(gemstone_viewer VERSION 0.1.0 LANGUAGES C)

# ---------------------------------------------------------------------------
# C standard
# ---------------------------------------------------------------------------
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Build type default
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
        STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---------------------------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /wd4100)   # /wd4100 = suppress unreferenced param
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Find Vulkan SDK
# ---------------------------------------------------------------------------
find_package(Vulkan REQUIRED)
message(STATUS "Vulkan found: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan lib:   ${Vulkan_LIBRARIES}")

# Locate glslc for offline SPIR-V compilation
find_program(GLSLC_EXECUTABLE glslc
    HINTS "$ENV{VULKAN_SDK}/bin"
          "$ENV{VULKAN_SDK}/Bin"
)
if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc not found. Install the Vulkan SDK or add it to PATH.")
endif()
message(STATUS "glslc:        ${GLSLC_EXECUTABLE}")

# ---------------------------------------------------------------------------
# Vendored dependencies
# ---------------------------------------------------------------------------
add_subdirectory(third_party)

# ---------------------------------------------------------------------------
# Shader compilation (GLSL -> SPIR-V)
# ---------------------------------------------------------------------------
set(SHADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SHADER_BIN_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${SHADER_BIN_DIR}")

file(GLOB SHADER_SOURCES
    "${SHADER_SRC_DIR}/*.vert"
    "${SHADER_SRC_DIR}/*.frag"
    "${SHADER_SRC_DIR}/*.comp"
)

set(SHADER_SPIRV_FILES "")
foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME "${SHADER_SRC}" NAME)
    set(SHADER_SPV "${SHADER_BIN_DIR}/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT  "${SHADER_SPV}"
        COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_SRC}" -o "${SHADER_SPV}"
        DEPENDS "${SHADER_SRC}"
        COMMENT "Compiling shader: ${SHADER_NAME} -> ${SHADER_NAME}.spv"
    )
    list(APPEND SHADER_SPIRV_FILES "${SHADER_SPV}")
endforeach()

add_custom_target(shaders ALL DEPENDS ${SHADER_SPIRV_FILES})

# ---------------------------------------------------------------------------
# Application sources
# ---------------------------------------------------------------------------
set(APP_SOURCES
    src/main.c
    src/renderer/vk_init.c
    src/renderer/vk_swapchain.c
    src/renderer/vk_renderpass.c
    src/renderer/vk_command.c
    src/renderer/vk_buffer.c
    src/renderer/vk_pipeline.c
    src/renderer/vk_environment.c
    src/renderer/vk_background.c
    src/scene/gem_geometry.c
    src/scene/camera.c
    src/scene/gem_material.c
    src/util/file_io.c
)

add_executable(gemstone_viewer ${APP_SOURCES})
add_dependencies(gemstone_viewer shaders)

target_include_directories(gemstone_viewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${Vulkan_INCLUDE_DIRS}"
)

target_link_libraries(gemstone_viewer PRIVATE
    Vulkan::Vulkan
    glfw
    cglm
    cjson
)

# VMA is header-only; expose its include path
target_include_directories(gemstone_viewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vma/include"
)

# Nuklear is header-only; expose its path
target_include_directories(gemstone_viewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear"
)

# stb is header-only
target_include_directories(gemstone_viewer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb"
)

# ---------------------------------------------------------------------------
# macOS / MoltenVK specifics
# ---------------------------------------------------------------------------
if(APPLE)
    target_compile_definitions(gemstone_viewer PRIVATE GV_PLATFORM_MACOS=1)
    # MoltenVK portability flags are handled at runtime in vk_init.c
endif()

# ---------------------------------------------------------------------------
# Windows specifics
# ---------------------------------------------------------------------------
if(WIN32)
    target_compile_definitions(gemstone_viewer PRIVATE GV_PLATFORM_WINDOWS=1)
    # Link Vulkan loader from SDK
    target_link_libraries(gemstone_viewer PRIVATE ${Vulkan_LIBRARIES})
endif()

# ---------------------------------------------------------------------------
# Debug / validation layer hint
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_compile_definitions(gemstone_viewer PRIVATE GV_ENABLE_VALIDATION=1)
endif()

# ---------------------------------------------------------------------------
# Pass shader binary directory to C code
# ---------------------------------------------------------------------------
target_compile_definitions(gemstone_viewer PRIVATE
    GV_SHADER_DIR="${SHADER_BIN_DIR}"
    GV_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

# ---------------------------------------------------------------------------
# Install (optional)
# ---------------------------------------------------------------------------
install(TARGETS gemstone_viewer RUNTIME DESTINATION bin)
install(DIRECTORY "${SHADER_BIN_DIR}/" DESTINATION share/gemstone_viewer/shaders)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/" DESTINATION share/gemstone_viewer/data)
