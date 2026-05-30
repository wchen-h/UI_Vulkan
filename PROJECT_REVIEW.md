# Vulkan SDR/HDR 双窗口 UI 渲染器 — 项目说明与测试目的

**仓库**：`github.com:wchen-h/UI_Vulkan.git`，分支 `HDR_SDR_test`

---

## 一、测试目的

本项目的核心目标是对比 **SDR（标准动态范围）** 和 **HDR（高动态范围）** 两种显示模式下，同一组 UI 素材的主观视觉表现。

### 测试流程

1. **SDR 窗口**（基准）：
   - 背景为 18% 灰（对应 paper white = 500 nit 下约 90 nit 实际亮度）
   - UI 素材以固定 Alpha 叠加（Alpha 滑动条 0.1~1.0，步长 0.1）
   - 这是基准画面——UI 亮度不可调，由素材本身的 sRGB 像素值决定

2. **HDR 窗口**（可调）：
   - 背景亮度可调（BG Nit，0~1000 nit）
   - UI 亮度可调（UI Lum Nit，0~1000 nit，相对素材平均亮度的缩放因子）
   - 叠加透明度可调（Eff. Alpha，0~1.0）
   - Lock 功能：锁定 `UI亮度 × Alpha` 的乘积，调整 Alpha 时自动反向补偿亮度，保持主观视觉亮度不变
   - Max Nit 钳位限制整体输出上限

3. **对比目标**：
   - 观察者同时注视 SDR 窗口和 HDR 窗口
   - 在 SDR 窗口选定一个 UI 素材和 Alpha 值作为基准
   - 在 HDR 窗口调节 BG Nit、UI Lum Nit、Eff. Alpha，使 HDR 窗口中的 UI **主观视觉感受与 SDR 窗口一致**
   - 记录此时 HDR 的参数组合（BG Nit、UI Lum Nit、Eff. Alpha）
   - 分析：在 HDR 高亮度显示能力下，达到与 SDR 相同主观感受所需的亮度和透明度参数关系

### 研究问题

- 在 HDR 显示器上，用户是否倾向于用更高的背景亮度 + 更低的 UI 不透明度来达到与 SDR 相同的主观效果？
- SDR 中固定的 18% 灰背景在 HDR 中被映射到什么主观等效亮度？
- UI 素材的亮度感知在 HDR 和 SDR 之间是否存在非线性的映射关系？

---

## 二、渲染管线架构

每个窗口三个 render pass，共享一个 linear intermediate（R16G16B16A16_SFLOAT）：

```
[UI Pass]                    [Convert Pass]              [ImGui Pass]
UI quad → linear intermediate   linear → swapchain        ImGui → swapchain
(ui.vert/frag, push constants)  (srgb_convert 或 pq_convert)  (ImGui backend)
bg blend in linear domain       sRGB OETF 或 PQ OETF        overlay on top
clear = bgLinear                读取 linear intermediate    loadOp=LOAD
```

两个窗口各有一套 swapchain + linear image + pipeline + command buffer（定义在 `common.h` 的 `WindowContext` 结构体），共享 `VulkanCore`（instance、device、queue、sampler、descriptor pool）。

---

## 三、素材

- 源数据：`python/pic/cropped_images/` 下 24 个 PNG，命名格式 `{id}_{type}_{x}_{y}.png`
- `type=rgb` 和 `type=alpha` 自动配对为 `UIPair`
- 加载时通过 `stb_image` + `vkCmdCopyBufferToImage` 上传为 `VK_FORMAT_R8G8B8A8_SRGB`（RGB）和 `VK_FORMAT_R8_UNORM`（Alpha）
- 预计算每对的平均 alpha（`alphaAvg`）和线性亮度均值（`lumAvg`）

---

## 四、关键文件

| 文件 | 作用 |
|------|------|
| `src/common.h` | `WindowContext`、`VulkanCore`、`UIPair`、`UITexture` 结构体，工具函数声明 |
| `src/app.h` | `VulkanApp` 类声明（双窗口成员、ImGui 状态变量） |
| `src/main.cpp` | ~1700 行，全部实现：Vulkan 初始化、swapchain/管线/render pass 创建、UI 混合、convert、ImGui 集成、主循环 |
| `src/texture.cpp` | `createImage`、`uploadTexture`、`loadAssets`（含 stb_image） |
| `shaders/ui.vert/frag` | UI quad 渲染 + push constant 控制的 alpha-over 混合 |
| `shaders/srgb_convert.vert/frag` | 全屏三角形 + sRGB OETF |
| `shaders/pq_convert.frag` | ST.2084 PQ OETF（含 MaxNit clamp） |
| `CMakeLists.txt` | 自动 fetch GLFW/glm，编译 shader 为 SPIR-V |

---

## 五、UI 融合公式（ui.frag）

```glsl
uiAlpha     = texture(texAlpha, uv).r * uAlphaMultiplier;
uiAdj       = texture(texRGB,  uv).rgb * uLumMult;
blended     = uiAdj * uiAlpha + uBgLinear * (1.0 - uiAlpha);
outColor    = vec4(blended, 1.0);
```

### Push constant 参数（28 字节总长）

| 字节偏移 | 参数 | 描述 |
|---------|------|------|
| 0-7 | `offset` (vec2) | quad 在 NDC 中的左下角位置（固定 0,0 居中） |
| 8-15 | `scale` (vec2) | quad 在 NDC 中的尺寸 = `2 × uiSize / winSize` |
| 16-19 | `uAlphaMultiplier` | 外部透明度系数 |
| 20-23 | `uBgLinear` | 背景线性亮度 |
| 24-27 | `uLumMult` | UI 亮度缩放因子 |

### SDR 参数设置

| 参数 | 值 | 来源 |
|------|-----|------|
| `uAlphaMultiplier` | `sdrAlpha_` (0.1~1.0) | ImGui 滑动条 |
| `uBgLinear` | `0.18` | 常量 `BG_GRAY`（18% 灰，paper white 500 nit → 90 nit） |
| `uLumMult` | `1.0` | 硬编码，不缩放 UI 亮度 |

### HDR 参数设置

| 参数 | 值 | 来源 |
|------|-----|------|
| `uAlphaMultiplier` | `effAlpha_` (0.0~1.0) | ImGui 滑动条 |
| `uBgLinear` | `bgNit_ / 500.0` | ImGui 滑动条 BG Nit ÷ 500 |
| `uLumMult` | `uiLumNit_ / (lumAvg × 500.0)` | ImGui 滑动条 UI Lum Nit ÷ 素材平均亮度(nit) |

---

## 六、Convert Pass（线性 → 输出色彩空间）

### SDR 窗口

使用 `srgb_convert.frag`，push constant `uSRGBEncode`：
- swapchain 为 `VK_FORMAT_B8G8R8A8_SRGB` → `uSRGBEncode = 0`，硬件自动编码
- swapchain 为 `VK_FORMAT_B8G8R8A8_UNORM` → `uSRGBEncode = 1`，shader 手动编码

### HDR 窗口

使用 `pq_convert.frag`（HDR 支持时）或 `srgb_convert.frag`（降级），push constants：
- `uMaxNit`：输出钳位上限（100~2000 nit）
- `uHDRSupported`：1.0 = HDR 激活，0.0 = 降级为灰色

```glsl
nitVal   = linear × 500.0;           // [0,1] 归一化 → nit
clamped  = clamp(nitVal, 0.0, uMaxNit);
outColor = vec4(linearToPQ(clamped), 1.0);
```

### PQ 诊断面板

HDR ImGui 面板实时计算并显示当前背景对应的 10-bit PQ 码值：
```
BG PQ: 521/1023  (1000 nit → clamped 100)
  ^ clamped by MaxNit     ← 橙色提示
```

---

## 七、HDR Lock/Unlock 逻辑

- **Lock**：保存 `lumLock_ = uiLumNit × effAlpha`（亮度 × 透明度的乘积）
- **锁定后**：调整 `effAlpha` 时自动计算 `uiLumNit = lumLock_ / effAlpha`，保持视觉亮度不变
- **状态指示**：锁定态按钮变绿色 + 文字 "Unlock"，右侧显示 `LOCKED` / `unlocked`

---

## 八、UI quad 顶点坐标

对象空间 `[-0.5, 0.5] × [-0.5, 0.5]`，通过 push constant 的 offset/scale 映射到 NDC 居中位置。

**Vulkan viewport Y 轴注意**：NDC Y=-1 → framebuffer Y=0（顶部），NDC Y=+1 → framebuffer Y=height（底部）。顶点 UV 的 V 坐标已适配此变换：
- 屏幕上方顶点 → UV V=0（纹理顶部）
- 屏幕下方顶点 → UV V=1（纹理底部）

---

## 九、当前状态

- Linux x86_64（Intel UHD 630 集显）上编译运行正常，SDR 窗口可用，HDR 窗口因无 HDR 显示器被自动隐藏
- Windows HDR 设备上测试过基本功能，ImGui 交互、Lock 逻辑、PQ 诊断已迭代到可用状态
- MaxNit clamp 效果待 Windows HDR 设备上通过 PQ 诊断面板验证 shader 侧是否生效

---

## 十、构建

```bash
git clone -b HDR_SDR_test git@github.com:wchen-h/UI_Vulkan.git
cd UI_Vulkan
mkdir -p external && cd external
git clone --depth 1 --branch docking https://github.com/ocornut/imgui.git
curl -o stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
cd ..
cmake -B build -S .
cmake --build build -j$(nproc)
./build/UI_Vulkan   # 需要 ASSET_DIR 指向 python/pic/cropped_images
```
