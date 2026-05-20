# UI_Vulkan — Architecture (HDR_SDR branch)

Dual-window Vulkan rendering tool for SDR (benchmark) vs HDR (PQ-encoded) visual comparison.

## Window Layout

```
┌──────────────────────┐    ┌──────────────────────┐
│  SDR Window           │    │  HDR Window           │
│  1310×1498            │    │  1310×1498            │
│  swapchain:           │    │  swapchain:           │
│    B8G8R8A8_SRGB      │    │    A2B10G10R10        │
│    SRGB_NONLINEAR     │    │    HDR10_ST2084       │
│                       │    │    (fallback: SDR)    │
│  ImGui:               │    │  ImGui:               │
│    UI Select ◀▶       │    │    Max Nit            │
│    UI Alpha           │    │    BG Nit             │
│                       │    │    UI Lum Nit         │
│                       │    │    Eff Alpha          │
└──────────────────────┘    └──────────────────────┘
```

## Code Structure

```
src/
├── common.h          # Shared types (UIPair, WindowContext, VulkanCore),
│                     #   constants, utility functions
├── texture.cpp       # Image creation, texture upload, asset loading,
│                     #   UIPair precomputation (alphaAvg, lumAvg)
├── window.cpp        # Per-window Vulkan init:
│                     #   - createSwapchain (with surface format query)
│                     #   - createRenderPasses (UI + convert + ImGui)
│                     #   - createFramebuffers (linear intermediate +
│                     #       swapchain views)
│                     #   - createPipelines (UI + convert shader)
│                     #   - recordUIPass (clear+draw, handles both SDR+HDR)
│                     #   - recordConvertPass (sRGB or PQ)
│                     #   - recordImGuiPass
│                     #   - drawFrame (acquire→record→submit→present)
├── app.h             # VulkanApp class declaration
├── app.cpp           # Application orchestration:
│                     #   - initCore (instance, device, HDR detection)
│                     #   - initSDRWindow / initHDRWindow
│                     #   - initImGui (single context, InitForOther on HDR)
│                     #   - sdrImGui() / hdrImGui() panels
│                     #   - run() main loop (poll→draw both windows)
└── main.cpp          # Entry point: just creates VulkanApp and runs
```

## Render Pipeline (per window)

```
Pass 1: UI Render → linear intermediate (R16G16B16A16_SFLOAT)
        Clear with background, blend UI via alpha-over in linear domain
        Push constants: position, alpha, bgLinear, uiLumMult

Pass 2: Convert → swapchain
        Fullscreen triangle reads intermediate
        SDR window: sRGB encoding (sw or hw, auto-detected)
        HDR window: PQ/ST.2084 encoding (clamp to maxNit)

Pass 3: ImGui Overlay → swapchain
        LOAD_OP_LOAD, renders control panels
```

## Data Flow

```
PNG files (sRGB) ──→ stb_image ──→ VkImage (GPU)
  RGB: VK_FORMAT_R8G8B8A8_SRGB     Alpha: VK_FORMAT_R8_UNORM
       ↓                                    ↓
  ui.frag: hardware sRGB→linear     linear value
       ↓                                    ↓
  alpha-over blend in linear domain:
    result = UI_rgb × uiLumMult × α + bgLinear × (1-α)
       ↓
  linear intermediate (R16G16B16A16_SFLOAT)
       ↓
  convert.frag: sRGB or PQ encoding
       ↓
  swapchain → present
```

## Dependencies

| Package | Version | Auto-fetch? |
|---------|---------|:-----------:|
| Vulkan SDK | 1.3.204+ | No (pre-install) |
| GLFW3 | 3.3.8 | Yes (FetchContent) |
| GLM | 1.0.1 | Yes (FetchContent) |
| Dear ImGui | docking | Bundled in external/ |
| stb_image | latest | Bundled in external/ |
