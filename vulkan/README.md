# Gemstone Viewer — Vulkan / C

Native port of the [Three.js web viewer](../README.md) using Vulkan, C17, GLFW, and cglm.

## Status

**Phase 1–3 complete.** A spinning 57-facet brilliant-cut gemstone renders with two-pass alpha blending (back faces / front faces), Schlick-Fresnel specular shading, Blinn-Phong specular highlights, and ACES tone mapping. Geometry normals are outward-enforced in the builder; per-swapchain-image semaphore sync eliminates validation errors under MoltenVK.

Phase 4 (IBL environment map, physical transmission, bloom) is planned. See [`notes/renderer-comparison.md`](../notes/renderer-comparison.md) for a detailed gap analysis against the Three.js reference renderer.

## Prerequisites

### macOS (primary dev — M1, macOS 26.3)

1. **Xcode 26.3** (or Command Line Tools): `xcode-select --install`
2. **CMake ≥ 3.22**: `brew install cmake`
3. **Vulkan SDK for macOS** (includes MoltenVK + glslc):
   - Download from [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home#mac)
   - Run the installer; it places the SDK at `~/VulkanSDK/<version>/`
   - Run the setup script to export environment variables:
     ```sh
     source ~/VulkanSDK/<version>/setup-env.sh
     ```
   - Or add to `~/.zshrc`:
     ```sh
     export VULKAN_SDK=~/VulkanSDK/<version>/macOS
     export PATH=$VULKAN_SDK/bin:$PATH
     export DYLD_LIBRARY_PATH=$VULKAN_SDK/lib:$DYLD_LIBRARY_PATH
     export VK_ICD_FILENAMES=$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json
     export VK_LAYER_PATH=$VULKAN_SDK/share/vulkan/explicit_layer.d
     ```

### Windows 11 (secondary — RTX 4070)

1. **Visual Studio 2022** with C++ Desktop workload
2. **CMake ≥ 3.22** (included with VS2022 or via winget)
3. **Vulkan SDK for Windows**: [vulkan.lunarg.com](https://vulkan.lunarg.com/sdk/home#windows)
   - Adds `VULKAN_SDK` environment variable automatically

## Building

### macOS

```sh
# From the vulkan/ directory:
source ~/VulkanSDK/<version>/setup-env.sh    # if not already in .zshrc

cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(sysctl -n hw.logicalcpu)

./build/gemstone_viewer
```

### Windows (MSVC)

```bat
cmake -G "Visual Studio 17 2022" -A x64 -B build
cmake --build build --config Debug
build\Debug\gemstone_viewer.exe
```

## Controls

| Input | Action |
|-------|--------|
| Left-drag | Orbit camera |
| Scroll | Zoom |
| ← / → arrow keys | Cycle gemstone preset |
| Escape | Quit |

## Gemstone Presets

| Key | Name | IOR | Dispersion | Color |
|-----|------|-----|------------|-------|
| ← / → | Diamond | 2.42 | 0.044 | White |
| | Blue Sapphire | 1.77 | 0.018 | Deep blue |
| | Green Emerald | 1.57 | 0.014 | Green |
| | Amber Citrine | 1.55 | 0.013 | Amber |

## Project Layout

```
vulkan/
  CMakeLists.txt          Root build script
  src/
    main.c                App lifecycle, frame loop
    renderer/
      vk_init.c/h         Instance, device, surface (MoltenVK portability)
      vk_swapchain.c/h    Swapchain + depth buffer + image views
      vk_renderpass.c/h   Render pass + framebuffers
      vk_command.c/h      Command pool, per-frame buffers, per-swapchain-image semaphore sync
      vk_buffer.c/h       Vertex / uniform buffer creation + staging upload
      vk_pipeline.c/h     Two graphics pipelines (back-face + front-face), descriptor sets, push constants
    scene/
      camera.c/h          Orbit camera with damping + auto-rotate
      gem_geometry.c/h    Procedural 57-facet brilliant-cut geometry builder; outward-normal enforcement via centroid-dot post-process
      gem_material.c/h    Material preset structs + GPU UBO packing
    util/
      file_io.c/h         Text + binary file loading (JSON, SPIR-V)
  shaders/
    gem.vert              MVP transform, world-space pos + normal output
    gem.frag              Two-pass shader: back-face interior tint (α=0.50); front-face Schlick Fresnel + Blinn-Phong specular + ACES tone mapping
  data/
    brilliant-cut.json    Tolkowsky ideal cut parameters
  third_party/
    glfw/                 Windowing + input (git submodule)
    cglm/                 C math library (git submodule)
    vma/                  Vulkan Memory Allocator (git submodule)
    cjson/                JSON parser (git submodule)
    nuklear/              Immediate-mode GUI (git submodule)
    stb/                  stb_image.h etc. (git submodule)
```

## Submodule Setup

After cloning the repo:

```sh
git submodule update --init --recursive
```

## Implementation Phases

| Phase | Status | Description |
|-------|--------|-------------|
| 1–3 | ✅ Complete | Scaffold, device init, two-pass alpha-blend gem, Fresnel + Blinn-Phong specular, ACES tone mapping, outward-normal geometry, MoltenVK semaphore sync |
| 4 | Planned | IBL environment map (cube sampler), physical transmission (offscreen capture + IOR refraction), clearcoat lobe |
| 5 | Planned | Post-processing bloom (dual-Kawase or separable Gaussian) |
| 6 | Planned | Nuklear UI panel (material sliders + gem selector) |
| 7 | Planned | Windows / MoltenVK polish + validation layer sign-off |
