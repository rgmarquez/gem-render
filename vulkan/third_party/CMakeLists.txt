# ---------------------------------------------------------------------------
# third_party/CMakeLists.txt
#
# Build configuration for vendored dependencies (git submodules).
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# GLFW — windowing / input / Vulkan surface creation
# ---------------------------------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
set(GLFW_VULKAN_STATIC  OFF CACHE BOOL "" FORCE)
add_subdirectory(glfw)

# ---------------------------------------------------------------------------
# cglm — C math library (header-only, but provides a CMake target)
# ---------------------------------------------------------------------------
set(CGLM_SHARED  OFF CACHE BOOL "" FORCE)
set(CGLM_STATIC  ON  CACHE BOOL "" FORCE)
set(CGLM_USE_C99 OFF CACHE BOOL "" FORCE)
add_subdirectory(cglm)

# ---------------------------------------------------------------------------
# cJSON — lightweight JSON parser
# ---------------------------------------------------------------------------
set(ENABLE_CJSON_TEST    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
set(CJSON_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(cjson)

# ---------------------------------------------------------------------------
# VMA (Vulkan Memory Allocator) — header-only, no subdirectory needed.
# The main CMakeLists.txt adds its include path directly.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Nuklear — header-only immediate-mode GUI.
# The main CMakeLists.txt adds its include path directly.
# We just need the headers present in third_party/nuklear/.
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# stb — header-only utilities (stb_image.h, etc.).
# The main CMakeLists.txt adds its include path directly.
# ---------------------------------------------------------------------------
