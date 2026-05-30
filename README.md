# UI_Vulkan — SDR/HDR Visual Comparison Tool

Two separate executables for SDR and HDR visual comparison testing.

## Build

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

Produces two executables: `build/UI_Vulkan_SDR` and `build/UI_Vulkan_HDR`.

## Run

SDR device (SDR monitor):
```bash
./build/UI_Vulkan_SDR
```

HDR device (HDR10-capable monitor):
```bash
./build/UI_Vulkan_HDR
```

HDR executable will exit with an error if no HDR display is present.

## Code Layout

```
src/
  common.h              — shared types, constants
  vulkan_util.h / .cpp   — shared Vulkan utilities
  texture.h / .cpp        — asset loading
  sdr_app.h / .cpp        — SDR app class
  hdr_app.h / .cpp        — HDR app class
  sdr_main.cpp            — SDR entry point
  hdr_main.cpp            — HDR entry point
```

## Render Pipeline (per-window)

```
Pass 1 (UI):    UI quad → linear intermediate (R16G16B16A16_SFLOAT)
Pass 2 (Convert): sRGB (SDR) or PQ (HDR) → swapchain
Pass 3 (ImGui):  ImGui overlay
```

## Data Flow (HDR)

```
PNG sRGB → stb_image → VkImage (sRGB texture) → ui.frag alpha-over blend
→ linear intermediate → PQ OETF → swapchain
```

## Controls

| App      | Controls                                           |
|----------|----------------------------------------------------|
| SDR      | UI Select, Alpha (0.1–1.0)                        |
| HDR      | Max Nit, BG Nit, UI Lum Nit, Eff. Alpha, Lock     |

## Dependencies

| Package  | Version | Source       |
|----------|---------|-------------|
| Vulkan SDK | 1.3+   | Pre-installed |
| GLFW3    | 3.3.8   | Auto-fetch   |
| GLM      | 1.0.1   | Auto-fetch   |
| Dear ImGui (docking) | docking branch | Bundled under external/imgui |

## Quick Start on a Fresh Machine

```bash
git clone -b HDR_SDR_split git@github.com:wchen-h/UI_Vulkan.git
cd UI_Vulkan
mkdir -p external/imgui external/stb
# see SETUP_NEW_MACHINE.txt
``