# UI_Vulkan — SDR & HDR UI Rendering Tool

GPU-accelerated UI rendering comparison tool using Vulkan 1.3.

## Features

- **SDR Rendering** (current): Render UI elements on 18% gray background with alpha blending
- **HDR Rendering** (planned): PQ/ST.2084 encoding, background brightness traversal, UI brightness & opacity adjustment
- **Interactive Controls**: Dear ImGui overlay with UI selection (12 pairs), alpha slider, brightness sliders
- **Correct Color Pipeline**: sRGB↔linear conversion, precision intermediate buffer (R16G16B16A16_SFLOAT)
- **Cross-Platform**: Linux & Windows via CMake

## Project Structure

```
UI_Vulkan/
├── src/main.cpp              # ~1450 lines, VulkanApp class (all logic)
├── shaders/
│   ├── ui.vert               # UI quad vertex shader
│   ├── ui.frag               # UI alpha blending (linear domain)
│   ├── srgb_convert.vert     # Fullscreen triangle (no vertex buffer)
│   └── srgb_convert.frag     # Linear → sRGB encoding (IEC 61966-2-1)
├── external/
│   ├── imgui/                # Dear ImGui (docking branch, fetched via git)
│   └── stb_image.h           # Single-header PNG loader
├── CMakeLists.txt            # Build configuration
└── README.md
```

### Render Pipeline (3 Passes)

```
Pass 1: UI Render → linear intermediate (R16G16B16A16_SFLOAT)
        Clear with 18% gray, blend UI RGB × alpha + BG × (1-alpha)

Pass 2: sRGB Convert → swapchain
        Fullscreen triangle reads intermediate, applies sRGB encoding

Pass 3: ImGui Overlay → swapchain
        LOAD_OP_LOAD preserves pass 2 output, draws control panel
```

## Dependencies

### Runtime

| Dependency | Version | Notes |
|-----------|---------|-------|
| Vulkan SDK | 1.3.204+ | Headers + loader |
| GLFW3 | 3.3.6 | Window & surface creation |
| GLM | 0.9.9.8 | Math library (future use) |
| Dear ImGui | docking branch | GUI controls |

### Build Tools

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.20+ | |
| g++ / MSVC | 11.4 / VS2022+ | C++17 |
| glslangValidator | 11.8.0 | GLSL → SPIR-V |

### Shader Compiler

GLSL shaders are compiled to SPIR-V at build time using `glslangValidator`.
On Ubuntu 22.04+, install with:

```bash
sudo apt install glslang-tools
```

## Build

### Linux (Ubuntu 22.04+)

```bash
# Install dependencies
sudo apt install libvulkan-dev vulkan-tools libglfw3-dev libglm-dev glslang-tools cmake g++

# Clone & build
git clone git@github.com:wchen-h/UI_Vulkan.git
cd UI_Vulkan
cmake -B build -S .
cmake --build build -j$(nproc)
./build/UI_Vulkan
```

### Windows

```powershell
# Requirements: Vulkan SDK 1.3+, GLFW3, CMake 3.20+, Visual Studio 2022

cmake -B build -S .
cmake --build build --config Release
.\build\Release\UI_Vulkan.exe
```

### External Dependencies

Before first build, fetch submodules or download manually:

```bash
# Dear ImGui (docking branch)
cd external
git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git

# stb_image.h
cd external
curl -o stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
```

## Input Data

UI images are loaded from `../python/pic/cropped_images/` relative to the project root.
12 pairs of RGB + Alpha PNGs are auto-detected by filename convention:

```
{id}_{type}_{x}_{y}.png
 e.g.: 1_rgb_148_476.png  ←→  1_alpha_148_476.png
```

## Controls

| Action | Key / UI |
|--------|----------|
| Next UI | Button "Next >" |
| Previous UI | Button "< Prev" |
| UI Alpha | Slider (0.1–1.0), arrow keys |
| Quit | Close window / Esc |

## Known Limitations

- Intel UHD 630 does not support HDR swapchain (no 10-bit output)
- HDR rendering validated via PQ-encoded pixel analysis on SDR swapchain
- True HDR display requires external GPU with VK_EXT_hdr_metadata support
