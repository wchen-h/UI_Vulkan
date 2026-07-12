# 代码审查指南 — chromaScale_YUV_GameBG & chromaScale_ICtCp_GameBG

## 1. 工程目的

本工程是一个 Vulkan SDR/HDR 双窗口 UI 一致性测试工具。SDR 窗口运行在艺卓 SDR 显示器（350nit，sRGB，BT.709），HDR 窗口运行在 Sony BVM X3110 HDR 监视器（峰值 4000nit，PQ，BT.2020）。两个窗口同时显示同一个 UI 纹理和同一个游戏背景图，通过调节 HDR 滑动条使 HDR 窗口的 UI 主观亮度/不透明度与 SDR 窗口一致，记录匹配参数。

本文档描述两个并行分支的差异，供代码审查人员和 AI Agent 快速理解。

---

## 2. 两分支对比

| | `chromaScale_YUV_GameBG` | `chromaScale_ICtCp_GameBG` |
|---|---|---|
| **HDR 亮度调节域** | YCbCr (PQ 域 Y 分量) | ICtCp (PQ 域 I 分量) |
| **UI 亮度滑动条** | Y-Scale (乘 Y 分量) | I-Scale (乘 I 分量) |
| **色度滑动条** | CbCr-Scale | CtCp-Scale |
| **BG multiplier 域** | 线性 nit (bgNit_ / localAvgNit_) | PQ 域 (PQ(bgNit_) / localAvgI_) |
| **localAvg 计算** | avg(BT.2020 luma Y) 线性域 | avg(ICtCp I) PQ 域 |
| **SDR 管线** | 完全相同 | 完全相同 |
| **前景/背景分离** | 完全相同 | 完全相同 |
| **背景切换** | 完全相同 | 完全相同 |

**仅 HDR shader 和 CPU 侧的 computeLocalAvg / drawFrame 不同。** SDR shader、vulkan_util、texture、push constant 布局（56B）两分支完全一致。

---

## 3. 代码模块结构

### 3.1 编译目标

```
ui_vulkan_common (静态库)
├── src/vulkan_util.cpp   — Vulkan 核心、pipeline、recordUIPass
├── src/texture.cpp        — 纹理加载、createSolidTexture
└── external/imgui/       — ImGui 后端

UI_Vulkan_HDR (可执行)
├── src/hdr_app.cpp        — HDR 窗口主逻辑
└── src/hdr_main.cpp       — 入口

UI_Vulkan_SDR (可执行)
├── src/sdr_app.cpp        — SDR 窗口主逻辑
└── src/sdr_main.cpp       — 入口
```

### 3.2 渲染管线（三 pass）

```
Pass 1: UI Pass (hdr_ui.frag / sdr_ui.frag)
  全屏 quad → 采样背景纹理 + UI 纹理 → ICtCp/YCbCr 调整 → 线性混合 → PQ/sRGB 编码
  → 输出到 R16G16B16A16_SFLOAT 中间图

Pass 2: Convert Pass (pq_convert.frag / srgb_convert.frag)
  全屏三角形 → 采样中间图 → 直通(PQ) 或 sRGB 编码 → 输出到 swapchain

Pass 3: ImGui Pass
  绘制控制面板（滑动条、按钮）
```

### 3.3 Push Constant 布局（56 字节，两分支共享）

```
[0-7]    vertex offset (vec2) = (0, 0)    全屏
[8-15]   vertex scale  (vec2) = (2, 2)    全屏（顶点 ±0.5 × 2 = ±1 NDC）
[16-19]  fgAlpha       (float)            前景 UI Eff.Alpha [0-2]
[20-23]  bgMultiplier  (float)            YCbCr: 线性域 multiplier
                                          ICtCp: PQ 域 I multiplier
[24-27]  fgScale       (float)            YCbCr: fgYScale
                                          ICtCp: fgIScale
[28-31]  ctCpScale     (float)            YCbCr: cbcrScale (共享)
                                          ICtCp: ctCpScale (共享)
[32-39]  uiOffset      (vec2)             UI 区域左下角在 screen UV [0,1]
[40-47]  uiScale       (vec2)             UI 区域大小在 screen UV [0,1]
[48-51]  bgAlpha       (float)            背景UI Eff.Alpha [0-2]
[52-55]  bgScale       (float)            YCbCr: bgYScale
                                          ICtCp: bgIScale
```

---

## 4. 核心模块功能说明

### 4.1 `texture.h / texture.cpp`

| 函数 | 功能 |
|---|---|
| `loadAssets()` | 扫描 UI 素材目录，加载 RGB+Alpha 纹理对 |
| `loadBackgroundTexture()` | 加载背景 PNG，上传为 SRGB 纹理，计算全局平均亮度（BT.2020 for HDR / BT.709 for SDR），输出原始 RGBA 数据 |
| `createSolidTexture()` | 创建纯色纹理（用于白色背景），输出原始 RGBA |
| `uploadTexture()` (static) | 底层纹理上传（staging buffer → image → layout transition） |

### 4.2 `vulkan_util.h / vulkan_util.cpp`

| 函数 | 功能 |
|---|---|
| `initVulkanCore()` | 创建 instance/device/sampler/descriptor layout(3 bindings)/pool |
| `initWindowSwapchain()` | 创建窗口+swapchain+中间图，设置 pxPerMm |
| `createUIPipeline()` | UI pipeline（动态 viewport/scissor，push constant 56B） |
| `recordUIPass()` | 录制 UI pass：全屏 quad，push 56B，计算 uiOffset/uiScale |
| `recordImGuiPass()` | 录制 ImGui pass |
| `recreateSwapchain()` | 窗口 resize 时重建 swapchain |

### 4.3 `hdr_app.h / hdr_app.cpp`

#### 成员变量

| 变量 | 功能 |
|---|---|
| `bgTextures_` / `bgRawList_` / `bgWList_` / `bgHList_` / `bgNames_` | 多背景纹理（vector），含原始像素数据 |
| `currentBG_` | 当前背景索引 |
| `localAvgI_` (ICtCp) / `localAvgNit_` (YCbCr) | local 区域平均亮度 |
| `fgAlpha_` / `bgAlpha_` | 前景/背景 UI Eff.Alpha |
| `fgIScale_` / `bgIScale_` (ICtCp) / `fgYScale_` / `bgYScale_` (YCbCr) | 前景/背景 UI 亮度缩放 |
| `ctCpScale_` (ICtCp) / `cbcrScale_` (YCbCr) | 色度缩放（共享） |

#### 方法

| 方法 | 功能 |
|---|---|
| `init()` | 初始化 Vulkan → 加载 UI → 扫描+加载背景纹理 → 创建 descriptor sets → 创建 swapchain/pipeline → `computeLocalAvg()` + `updateBGDescriptorSets()` |
| `computeLocalAvgI()` (ICtCp) / `computeLocalAvgNit()` (YCbCr) | 计算 local 区域（2× UI quad）的平均亮度 |
| `switchBackground()` | 切换背景：`currentBG_++` → 更新 descriptor binding 2 → 重算 localAvg |
| `updateBGDescriptorSets()` | 更新所有 UIPair 的 binding 2 指向当前背景纹理 |
| `drawFrame()` | 每帧：算 multiplier → `recordUIPass` → `recordConvertPass` → `recordImGuiPass` |
| `hdrImGui()` | 控制面板：UI 切换 / BG Nit / Next BG / FG+BG 滑动条 |

### 4.4 `sdr_app.h / sdr_app.cpp`

SDR 管线无 ICtCp/YCbCr 调整。shader 直接在线性域混合 UI 和 BG。SDR 的 `computeLocalAvgNit()` 用 BT.709 luma，`bgMultiplier_ = 63 / localAvgNit_`（固定目标 63nit）。

### 4.5 Shader 文件

| Shader | 功能 |
|---|---|
| `ui.vert` | 全屏 quad vertex shader（push constant: offset + scale） |
| `hdr_ui.frag` | **两分支不同** — HDR UI shader（见下） |
| `sdr_ui.frag` | SDR UI shader（两分支相同） |
| `srgb_convert.vert` | 全屏三角形 vertex |
| `srgb_convert.frag` | SDR: 线性→sRGB 编码（条件） |
| `pq_convert.frag` | HDR: PQ 直通 |

### 4.6 `hdr_ui.frag` 两分支对比

#### YCbCr 分支

```glsl
// UI 调整管线:
bt2020_nit → PQ encode → ×1023 → YCbCr(10bit)
→ Y × yScale, (Cb-512) × cbcrScale, (Cr-512) × cbcrScale
→ inv YCbCr → /1023 → PQ decode → linear nit

// BG 调整:
bgNit = bt2020_nit × bgMultiplier  (线性域)
```

#### ICtCp 分支

```glsl
// UI 调整管线:
bt2020_nit → LMS(normalized) → ×10000 → PQ → ICtCp
→ I × iScale, Ct × ctCpScale, Cp × ctCpScale
→ inv ICtCp → LMS(PQ) → PQ decode → LMS → RGB (nit)

// BG 调整:
bgICtCp = rgbToICtCp(bgNit)
bgICtCp.x = clamp(bgICtCp.x × bgMultiplierI, 0, 1)  (PQ 域)
bgNitAdj = ictcpToRGB(bgICtCp)
```

两分支在 ICtCp 调整后都转回线性 nit，与 BG 在线性域混合，最后 PQ encode 输出。

---

## 5. 背景图片切换

### 循环列表

`pic/` 目录下所有 `*_rotate.png`（按字母排序）+ 全白背景（64×64，sRGB 255,255,255）。

### 切换流程

1. ImGui "Next BG" 按钮 → `switchBackground()`
2. `currentBG_ = (currentBG_ + 1) % bgTextures_.size()`
3. `updateBGDescriptorSets()` — 更新所有 UIPair 的 binding 2
4. `computeLocalAvg()` — 重算 local 区域平均亮度
5. `drawFrame()` — multiplier 自动更新（每帧从 localAvg 和 bgNit_ 计算）

### 文件扫描

```cpp
for (auto& e : fs::directory_iterator(BG_IMAGE_DIR)) {
    std::string fn = e.path().filename().string();
    if (fn.find("_rotate.png") != std::string::npos)
        bgFiles.push_back(fn);
}
```

使用 `find()` 而非 `substr` + `tolower`（MSVC 兼容）。

---

## 6. Local 区域计算

### 区域定义

- Local 区域 = UI quad 线性尺寸 × 2（面积 × 4）
- 居中于窗口
- Clamp 到 [0, 1]（不超过屏幕）

### UV 计算

```
localMin = max(0.5 - uiScale, 0.0)
localMax = min(0.5 + uiScale, 1.0)
```

其中 `uiScale = (fracX, fracY)` = UI 尺寸 / 窗口尺寸。

### 渲染

- Local 区域内：绘制背景纹理 + UI 混合
- Local 区域外：输出黑色（硬边界）

### CPU 侧计算

`computeLocalAvgI()` / `computeLocalAvgNit()` 遍历 local 区域对应的背景图像素，计算平均亮度。触发时机：init / 切换 UI / 切换背景 / 窗口 resize。

---

## 7. 前景/背景 UI 分离

### 判定

```
texAlpha > 0.5  → 前景 UI (fgAlpha / fgScale)
texAlpha ≤ 0.5  → 背景UI (bgAlpha / bgScale)
texAlpha == 0   → 完全透明，只显示背景
```

### 原因

纹理背景下，低 alpha 像素对 BG Nit 更敏感（`Δmixed = (1-α)·Δbg`），高亮度像素对 Y-Scale/I-Scale 更敏感（PQ 非线性）。同一组滑动条无法同时匹配两部分，需分离控制。CbCr-Scale/CtCp-Scale 保持共享。

---

## 8. 关键 Bug 记录

### 8.1 BT.709 luma vs BT.2020 luma（已修复）

CPU 用 BT.709 luma 算 avg=124.31nit，shader 用 BT.2020 luma 感知=129.83nit。multiplier 偏高 4.4%。

### 8.2 computeLocalAvg 在 swapchain 初始化前调用（已修复）

`swapchainExt={0,0}` → `frac=inf` → local 区域=全图 → localAvg=globalAvg。修复：移到 `initWindowSwapchain()` 之后。

### 8.3 白色纹理 1×1 导致空像素范围（已修复）

`bx0=0, bx1=0` → early return → localAvg=0 → multiplier=NaN → 全黑。修复：64×64 + fallback 全图。

### 8.4 MSVC tolower 行为异常（已修复）

`std::transform` + `::tolower`（未 include `<cctype>`）在 MSVC 上 `_rotate.png` 匹配失败。修复：`fn.find("_rotate.png")`。

### 8.5 CPU 矩阵 M 行值错误（已修复）

`computeLocalAvgI()` 中 M 行第三列 `0.08044665`（S 行值）误用为 `0.07667981`（M 行正确值）。导致 gray 350nit 时 M=351.32（应 350），BG Nit=1000 实际输出 997.8nit。

---

## 9. 编译与运行

### 依赖

- Vulkan SDK
- GLFW（FetchContent 自动下载）
- GLM（FetchContent 自动下载）
- ImGui（源码在 `external/imgui/`）

### 编译

```bat
REM Windows (Developer Command Prompt)
build.bat           REM clean Release
build.bat inc        REM incremental
```

```bash
# Linux
./build.sh           # clean Release
./build.sh inc        # incremental
```

### 两分支独立编译

```bat
git checkout chromaScale_YUV_GameBG
mkdir build_ycbcr && cd build_ycbcr && cmake .. && cmake --build . --config Release

git checkout chromaScale_ICtCp_GameBG
mkdir build_ictcp && cd build_ictcp && cmake .. && cmake --build . --config Release
```

**注意：** 两分支 shader .spv 不兼容，必须用独立 build 目录。
