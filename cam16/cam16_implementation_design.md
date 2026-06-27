# CAM16 实现方案设计

> 前置：cam16/cam16_derivation.md（公式链）
> 目标：在 UI_Vulkan HDR 管线中实现 CAM16 感知模型亮度提亮

## 1. 实现位置选择

**选定方案：CPU 逐像素（方案 C）**

理由：
1. cam16_derivation.md 的公式链涉及大量 pow/sqrt/atan2，GLSL 实现复杂且调试困难
2. UI 像素量不大（~5万），CPU 逐像素可接受（~1ms 级别）
3. CPU 端可用 C++ 直接实现，方便调试和数值验证
4. 后续验证正确性后可迁移到 Shader（方案 A）

## 2. 管线架构

### 当前管线（chromaScale_YUV 分支）
```
hdr_ui.frag:
  采样UI → ×350 → BT.2020 → 混合 → PQ encode → ×1023 → YCbCr → Y-Scale/CbCr-Scale → 逆YCbCr → ÷1023 → 输出PQ
```

### 新管线（chromaScale_cam16 分支）
```
hdr_ui.frag (简化):
  采样UI → ×350 → BT.2020 → 混合 → PQ encode → ×1023 → 输出PQ（直接输出，不调节）

CPU 端 (drawFrame 里, fence wait 后读 readback buffer):
  对每个 alpha!=0 像素:
    读 RGB_PQ [0,1023] (from readback buffer, 已含混合结果)
    → PQ decode → nit
    → XYZ
    → CAM16 正向 → (Q, h, s)
    → 调节: Q' = Q * k_scale
    → 桥接: (Q', s) → (J', C')
    → CAM16 逆向 → XYZ'
    → BT.2020 nit'
    → PQ encode → RGB_PQ' [0,1023]
    → clamp + 色域外计数

  将结果写回一个 host-visible buffer → GPU 拷到 swapchain (或直接用 CPU 结果覆盖)
```

**简化方案**：不修改 shader。在 CPU 端读回 readback buffer（已有的 PQ 值），做 CAM16 调节后，用一个新的 buffer 传回 GPU 显示。

但这需要 GPU→CPU→GPU 往返，性能不好。

**更实际的方案**：在 CPU 端预计算 CAM16 调节后的结果，存为一个 LUT 或直接逐像素写入一个 staging buffer，然后 GPU 拷到 swapchain。

考虑到首轮实现验证正确性为主，采用最简单的方式：

### 最终方案：CPU 全流程 + staging buffer → GPU 拷贝

```
1. hdr_ui.frag 不变（输出混合后的 PQ 值到 linear intermediate）
2. GPU 拷 linear intermediate → readback buffer（已有）
3. CPU 读 readback buffer → 对 alpha!=0 像素做 CAM16 调节 → 写入 staging buffer
4. CPU 拷 staging buffer → 另一个 image → 显示
```

但这太复杂。更简单：

### 最终方案（修正）：CPU 逐像素直接生成输出 image

```
1. CPU 端持有 rawRGBA + rawAlpha（已有）
2. CPU 对每个像素做完整管线：sRGB→BT.2020→混合→PQ→CAM16调节→PQ'→输出
3. 写入一个 host-visible staging image/buffer
4. GPU 拷到 swapchain
```

这其实就是把 shader 的工作搬到 CPU。性能差但正确性有保证。

**问题**：需要每帧都算，性能可能不够。

**折中方案**：只在参数变化时重算（k_scale, bgNit, effAlpha, 切换 UI 时），结果缓存。帧间不变化时直接用缓存。

## 3. 最终选定方案

**CPU 逐像素 + 脏标记缓存**

- 新建 `cam16.h` / `cam16.cpp`：CAM16 正向 + 逆向 C++ 实现
- 在 `hdr_app.cpp` 中：
  - 新增 `float qScale_ = 1.0f`（替代 yScale_）
  - 新增 `bool cam16Dirty_ = true`（参数变化标记）
  - drawFrame 里：如果 dirty，CPU 逐像素算完整管线 → 写入 staging buffer → 拷到 swapchain
  - 如果不 dirty，直接用上一帧的 staging buffer
- hdr_ui.frag：简化为只做混合 + PQ encode（不做 YCbCr 调节），输出到 linear intermediate
- readback buffer：读回混合后的 PQ 值作为 CAM16 输入
- staging buffer：CAM16 调节后的 PQ 值，拷到 swapchain

## 4. 文件改动清单

| 文件 | 改动 |
|---|---|
| `src/cam16.h` | 新建：CAM16 正向/逆向函数声明 + 结构体 |
| `src/cam16.cpp` | 新建：CAM16 正向/逆向实现 |
| `src/hdr_app.h` | 加 `qScale_`, `cam16Dirty_`, staging buffer 成员；删 `yScale_`, `cbcrScale_` |
| `src/hdr_app.cpp` | ImGui 加 Q-Scale 滑条；drawFrame 加 CAM16 逐像素处理 |
| `shaders/hdr_ui.frag` | 简化：删 YCbCr 调节段，只保留混合 + PQ encode |
| `CMakeLists.txt` | 加 cam16.cpp |

## 5. cam16.h / cam16.cpp 接口设计

```cpp
// cam16.h

struct CAM16ViewingConditions {
    float XYZ_w[3];  // 适应白 XYZ (scale=100)
    float L_A;        // 适应场亮度
    float Y_b;        // 背景亮度因子
    float F, c, N_c;  // surround 参数
};

struct CAM16Appearance {
    float J, C, h, s, Q, M;
};

// 预计算观看环境依赖的中间参数
struct CAM16Intermediate {
    float n, F_L, N_bb, N_cb, z, D;
    float D_RGB[3];
    float RGB_aw[3];
    float A_w;
};

void cam16_precompute(const CAM16ViewingConditions& vc, CAM16Intermediate& im);
CAM16Appearance cam16_forward(const float XYZ[3], const CAM16ViewingConditions& vc, const CAM16Intermediate& im);
void cam16_inverse(float XYZ_out[3], float J, float C, float h, const CAM16ViewingConditions& vc, const CAM16Intermediate& im);

// 桥接
void cam16_Qs_to_JC(float& J_out, float& C_out, float Q, float s, const CAM16ViewingConditions& vc, const CAM16Intermediate& im);
```

## 6. 逐像素处理流程（hdr_app.cpp drawFrame）

```
1. fence wait
2. 读 readback buffer（混合后的 PQ 值，上一帧的）
3. 如果 cam16Dirty_:
   a. 预计算 CAM16Intermediate（从 bgNit 推 L_A, Y_b）
   b. 对每个 alpha!=0 像素:
      - 读 RGB_PQ [0,1023] from readback buffer
      - PQ decode → nit
      - BT.2020→XYZ
      - CAM16 正向 → (Q, h, s)
      - Q' = Q * qScale_
      - 桥接 (Q', s) → (J', C')
      - CAM16 逆向 → XYZ'
      - XYZ→BT.2020 nit'
      - PQ encode → RGB_PQ' [0,1023]
      - clamp + 色域外计数
      - 写入 staging buffer
   c. cam16Dirty_ = false
4. 拷 staging buffer → swapchain（通过一个 image copy）
5. recordImGuiPass（叠加 UI 控件）
```

## 7. 复用清单

| 已有代码 | 复用方式 |
|---|---|
| rawRGBA / rawAlpha | 提供原始像素数据 |
| readbackBuf_ | 读回混合后的 PQ 值 |
| linearToPQ / PQ decode | PQ 编解码 |
| BT.709→BT.2020 矩阵 | 色域转换 |
| ImGui 框架 | Q-Scale 滑条 + avgMixedNit 显示 |
