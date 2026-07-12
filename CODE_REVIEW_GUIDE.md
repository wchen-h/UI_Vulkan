# UI_Vulkan 代码阅读指南

## 项目概述

Vulkan 渲染工具，对比 SDR/HDR 显示器上同一 UI 素材的主观视觉表现，研究 Hunt Effect 补偿方案（chromaScale）。

两个独立可执行文件：`UI_Vulkan_SDR`（sRGB 输出）和 `UI_Vulkan_HDR`（PQ/ST.2084 输出）。

---

## 推荐阅读顺序

### 第一轮：理解整体架构（~15 min）

| 顺序 | 文件 | 重点 |
|------|------|------|
| 1 | `src/common.h` (97行) | 所有共享类型和常量：`UITexture`, `UIPair`, `WindowContext`, `VulkanCore` 四个核心结构体 |
| 2 | `src/vulkan_util.h` (49行) | 全部工具函数声明，建立函数名索引 |
| 3 | `src/sdr_app.h` (26行) | SDRApp 类结构 |
| 4 | `src/hdr_app.h` (30行) | HDRApp 类结构，对比 SDRApp 多了哪些成员变量 |

**读完后的理解**：项目有哪些数据结构、哪些函数、SDR 和 HDR 两个 App 类的差异。

### 第二轮：理解渲染管线（~20 min）

| 顺序 | 文件 | 重点 |
|------|------|------|
| 5 | `shaders/ui.vert` (20行) | push constant: offset + scale 控制 quad 位置大小 |
| 6 | `shaders/ui.frag` (36行) | **核心 shader**：alpha blending + chromaScale，push constant 布局 (offset 16-31) |
| 7 | `shaders/srgb_convert.vert` (15行) | 全屏三角形（无顶点缓冲） |
| 8 | `shaders/srgb_convert.frag` (31行) | sRGB OETF（SDR 用） |
| 9 | `shaders/pq_convert.frag` (43行) | PQ/ST.2084 OETF（HDR 用），注意 350.0 硬编码 |

**读完后的理解**：每帧经过 3 个 render pass（UI → Convert → ImGui），数据如何从线性中间缓冲流向 swapchain。

### 第三轮：理解核心实现（~40 min）

| 顺序 | 文件 | 重点 |
|------|------|------|
| 10 | `src/vulkan_util.cpp` (1024行) | 按以下子顺序读 |
| 11 | `src/texture.cpp` (225行) | 资源加载 |
| 12 | `src/sdr_app.cpp` (234行) | SDR 完整流程 |
| 13 | `src/hdr_app.cpp` (276行) | HDR 完整流程，重点看 Lock 机制和 chromaScale |

**`vulkan_util.cpp` 子顺序**（不需要从头到尾读）：

| 函数 | 行号 | 优先级 | 说明 |
|------|------|--------|------|
| `recordUIPass()` | 920-968 | ★★★ | push constant 组装 + draw call，连接 C++ 和 shader |
| `initVulkanCore()` | 187-406 | ★★☆ | Vulkan 初始化全流程，最长函数 |
| `createUIPipeline()` | 650-736 | ★★☆ | 图形管线配置，注意 push constant size = 32 |
| `createConvertPipeline()` | 738-810 | ★★☆ | 转换管线，SDR/HDR 用不同 frag shader |
| `createRenderPasses()` | 458-575 | ★☆☆ | 3 个 render pass 配置 |
| `initWindowSwapchain()` | 408-456 | ★☆☆ | swapchain + 线性中间图像 |
| 其余 | — | 按需 | 工具函数，看名字就知道功能 |

### 第四轮：入口和构建（~5 min）

| 顺序 | 文件 | 重点 |
|------|------|------|
| 14 | `src/sdr_main.cpp` (12行) | 入口，一行代码 |
| 15 | `src/hdr_main.cpp` (12行) | 入口 |
| 16 | `CMakeLists.txt` (145行) | 构建目标、依赖管理、shader 编译 |

---

## 文件关系图

```
common.h  ← 所有 .h/.cpp 都依赖
  ├── vulkan_util.h / .cpp  ← 共享 Vulkan 工具（初始化、管线、录制）
  ├── texture.h / .cpp       ← 纹理创建、PNG 加载
  ├── sdr_app.h / .cpp       ← SDR 应用
  │     └── sdr_main.cpp     ← SDR 入口
  ├── hdr_app.h / .cpp       ← HDR 应用
  │     └── hdr_main.cpp     ← HDR 入口
  ├── app.h                  ← Legacy（未编译）
  └── main.cpp               ← Legacy（未编译，1723行）
```

## 函数调用链

```
main()
  └── App::run()
        ├── init()
        │     ├── initVulkanCore()          // vulkan_util.cpp: GLFW + Instance + Device
        │     ├── loadAssets()              // texture.cpp: PNG → GPU 纹理
        │     ├── initWindowSwapchain()     // vulkan_util.cpp: swapchain + 中间缓冲
        │     ├── createRenderPasses()      // vulkan_util.cpp: 3 个 pass
        │     ├── createUIPipeline()        // vulkan_util.cpp: UI 管线
        │     ├── createConvertPipeline()   // vulkan_util.cpp: 转换管线
        │     └── initImGuiForWindow()      // vulkan_util.cpp: ImGui
        │
        └── drawFrame()  (每帧)
              ├── recordUIPass()            // vulkan_util.cpp: 渲染 UI quad
              ├── recordConvertPass()       // sdr/hdr_app.cpp: sRGB 或 PQ 转换
              └── recordImGuiPass()         // vulkan_util.cpp: ImGui 叠加
```

## Shader Push Constant 布局

### ui.frag（32 bytes total）

| Offset | 类型 | 名称 | 用途 |
|--------|------|------|------|
| 0-7 | vec2 | offset, scale | 顶点着色器：quad 位置和大小 |
| 16 | float | uiAlphaMultiplier | 外部 alpha 系数 |
| 20 | float | bgLinear | 背景线性灰度值 |
| 24 | float | uiLumMult | UI 亮度倍率 |
| 28 | float | chromaScale | 色彩度缩放（Hunt Effect 补偿） |

### pq_convert.frag（8 bytes）

| Offset | 类型 | 名称 | 用途 |
|--------|------|------|------|
| 0 | float | uMaxNit | PQ 最大亮度钳制 |
| 4 | float | uHDRSupported | HDR 支持标志 |

---

## 已知问题（审核时可重点关注）

1. **Lock 机制 Bug**（`hdr_app.cpp`）：`lumLock_` 只保存 UI 贡献，缺少 BG 贡献 `bgNit_*(1-effAlpha_)`。详见 `HUNT_EFFECT_COMPENSATION_ANALYSIS.md §2.3.2`
2. **PQ shader 硬编码**（`pq_convert.frag`）：`350.0` 硬编码为 paper white，与 `common.h` 的 `PAPER_WHITE_NIT` 耦合
3. **Dead code**：`main.cpp` + `app.h` 是旧版单进程双窗口实现，未被 CMakeLists 编译
4. **未使用的 sampler**：`VulkanCore::texSamplerLin`（LINEAR）被创建但未被 SDR/HDR split 版本使用
