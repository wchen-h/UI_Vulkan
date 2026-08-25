# Code Review Prompt: mixed_ui_test branch

## Project Background

This is a Vulkan-based HDR/SDR dual-window UI compensation testing tool. The SDR window (Eizo monitor, 350nit, BT.709) serves as reference; the HDR window (Sony BVM X3110, 4000nit peak, PQ+BT.2020) is adjusted by an operator to match SDR appearance. The tool tests UI brightness/opacity compensation curves for HDR display.

The parent branch `chromaScale_YUV_GameBG` implements the **separated approach**: UI RGB+alpha are available separately. The shader adjusts UI brightness (Y-Scale) BEFORE alpha-blending with background.

## This Branch's Purpose

`mixed_ui_test` implements the **non-separated approach**: simulate the case where only the mixed SDR frame (bg+UI already blended) is available, not separated UI RGB/alpha. The compensation changes from "adjust UI then mix" to "mix first, then adjust the mixed result".

## Files Changed (3 files, 3 commits)

### 1. `shaders/hdr_ui.frag` — HDR fragment shader (core change)

**Before (parent branch):**
```
UI -> PQ encode -> YCbCr -> Y*Y-Scale -> RGB -> PQ decode -> adjusted UI nits
-> mix with bg: mixed = adjustedUI * effAlpha + bg * (1-effAlpha)
-> PQ encode -> output
```

**After (this branch):**
```
UI + bg -> mix FIRST: mixed = uiNit * alpha + bgNit_raw * (1-alpha)
-> PQ encode -> YCbCr -> Y*Y-Scale -> RGB -> PQ output
```

Key changes:
- **Mix order reversed**: Y-Scale now applied AFTER mixing (on mixed result), not before (on UI alone)
- **Alpha is plain mix alpha** (not "eff alpha" compensation curve) — the slider directly controls mixing opacity
- **Raw bg in UI area**: `bgNit_raw` (no bgMultiplier) used for mixing inside UI pixels (alpha!=0). `bgNit_scaled` (with bgMultiplier) only for non-UI pixels. This lets BG Nit slider change non-UI background without affecting UI area — operator can independently set B and adjust Y-Scale.
- **Linear/sRGB blending toggle** (offset 52, repurposed from bgYScale): sRGB blend normalizes to [0,1], applies sRGB OETF, blends in sRGB domain, applies sRGB EOTF, back to nits. Matches SDR shader's existing implementation.
- **Y-Scale unified**: only `fgYScale` for all UI pixels (bgYScale slot repurposed as blendingMode, same as SDR shader does)
- `pqDecode()` function retained but now unused (was needed for decode-then-mix in old flow)

### 2. `src/hdr_app.h` — HDR app header

- `float bgYScale_` replaced with `int blendingMode_ = 0` (0=linear, 1=sRGB)

### 3. `src/hdr_app.cpp` — HDR app

- ImGui: removed "BG Y-Scale" slider, added "Linear Blend"/"sRGB Blend" radio buttons (matching SDR window)
- "FG Y-Scale (mixed)" renamed to "Y-Scale (mixed)" (single Y-Scale for all UI)
- `recordUIPass()` call: passes `(float)blendingMode_` instead of `bgYScale_`

## Unchanged (intentionally)

- `shaders/sdr_ui.frag` — SDR reference shader (already has blending toggle from parent branch)
- `src/sdr_app.h/cpp` — SDR app (already has blending radio buttons from parent branch)
- `src/vulkan_util.h/cpp` — `recordUIPass` signature unchanged (bgYScale parameter repurposed, same as SDR already does)
- Push constant layout: still 56 bytes / 14 floats (no extension needed)

## Review Checklist

Please verify:

1. **sRGB blending math**: The sRGB blend path normalizes linear nits to [0,1] by dividing by PAPER_WHITE_NIT (350), applies sRGB OETF, blends, applies sRGB EOTF, multiplies back by PAPER_WHITE_NIT. Is this correct? Does the clamp to [0,1] before sRGB encode lose information for HDR values >350 nit?

2. **Raw bg in UI area**: UI pixels (alpha!=0) use `bgNit_raw` (no multiplier), non-UI pixels use `bgNit_scaled` (with multiplier). Is the boundary at alpha==0 correct? Could there be a visible seam at the UI edge where alpha transitions from 0 to non-zero?

3. **Push constant compatibility**: HDR shader reads offset 52 as `blendingMode`, SDR shader also reads offset 52 as `blendingMode`. Both apps pass `(float)blendingMode_` as the bgYScale parameter to `recordUIPass()`. Is this consistent?

4. **Y-Scale unification**: Only `fgYScale` is used for all UI pixels (was previously `texAlpha > 0.5 ? fgYScale : bgYScale`). Is losing the fg/bg Y-Scale distinction acceptable for the non-separated test? (The fg/bg alpha distinction is still kept.)

5. **Unused function**: `pqDecode()` is defined but no longer called in the shader. Should it be removed or kept for future use?

6. **sRGB clamp**: `clamp(uiNit2020 / PAPER_WHITE_NIT, 0.0, 1.0)` — UI nits can exceed 350 (bright UI). Clamping to 1.0 before sRGB encode loses HDR highlight info. Is this acceptable for the test, or should a different normalization be used?

---

## Review Findings (2026-08-25)

> 核对结果:清单第 3 项(推送常量兼容性)完全通过。以下为审阅发现的问题,按严重度排序。

### 🔴 P1 — 注释与代码矛盾:alpha 实为 eff-alpha,非 "plain mix alpha"

- **文件**:`shaders/hdr_ui.frag:17`、`:23`(以及 commit `15092a4` 的描述)
- **问题**:push constant 注释写的是 `mix alpha, not eff`,但第 147-149 行实际仍使用 eff-alpha 补偿公式:
  ```glsl
  float alpha = clamp(texAlpha * min(sliderAlpha, 1.0)
                    + max(0.0, sliderAlpha - 1.0), 0.0, 1.0);
  ```
  这是旧的补偿公式,允许 `sliderAlpha ∈ (1,2]` 额外提升 alpha。且 `src/hdr_app.cpp:262,266` 的滑条范围仍为 `0.0f ~ 2.0f`。所以它**不是 plain mix alpha**,注释/commit 描述有误导。
- **二选一**:若 eff-alpha 是有意保留 → 删掉 "not eff";若真想要纯 mix → 改成 `texAlpha * sliderAlpha` 并把滑条上限改回 1.0。

### 🟡 P2 — UI 区 raw bg 导致亮度接缝(视觉伪影)

- **文件**:`shaders/hdr_ui.frag:141-186`(分支逻辑,关键在 `:155-164` 用 `bgNit_raw`,而 `:124-127` 与 `:183-185` 用 `bgNit_scaled`)
- **问题**:只要 `bgMultiplier != 1`,背景亮度就在 alpha 轮廓处硬跳变 —— `texAlpha>0` 用 raw bg,`texAlpha==0`/UI 区外用 scaled bg。半透明 UI 边缘会出现明显"方框"。
- **说明**:这是 commit `de4fb5f` 刻意做的解耦(让 BG Nit 只影响非 UI 背景),但副作用是操作者一动 BG Nit 就必然看到接缝。建议在 ImGui 旁提示,或给 UI 区单独 bg 参考值。

### 🟡 P3 — 头部注释自相矛盾

- **文件**:`shaders/hdr_ui.frag:5-6`
- **问题**:注释写 "use **separate** Alpha and Y-Scale controls",但第 173 行实际只用统一 `fpc.fgYScale`,Y-Scale 已不再 fg/bg 分离(只有 Alpha 仍分离)。注释陈旧。

### 🟡 P4 — sRGB 路径 clamp 到 [0,1] 的 HDR 丢失(潜在陷阱)

- **文件**:`shaders/hdr_ui.frag:155-156`
- **问题**:`clamp(uiNit2020 / PAPER_WHITE_NIT, 0.0, 1.0)` 把 >350 nit 截到 1.0。当前输入(uiRGB/bgRGB 均为 sRGB [0,1])下基本无害(仅高饱和色经 709→2020 矩阵超界会被截);但若未来给 sRGB 路径喂真 HDR(>350 nit)UI/背景,高光会静默丢失。建议补注释说明"该路径仅限 ≤350 nit 输入"。

### 🟡 P5 — sRGB 混合的色域错配

- **文件**:`shaders/hdr_ui.frag:153-165`
- **问题**:`linearToSRGB`/`sRGBToLinear`(IEC 61966-2-1,基于 sRGB/BT.709)作用在 `uiNit2020`/`bgNit_raw` 这些 **BT.2020 线性** 值上,本质是"在 BT.2020 上做 gamma 2.4 混合",非真 sRGB 混合。与 SDR 窗口(`sdr_ui.frag:82-87`,对 BT.709 `uiRGB` 做真 sRGB 混合)结果不一致。注释 "Matches SDR shader's existing implementation" 只是近似。

### 🟢 P6 — 死代码:未用的 `pqDecode()`

- **文件**:`shaders/hdr_ui.frag:54-65`
- **问题**:`pqDecode()` 已无调用点。留着无妨,但严格 GLSL 编译器会报 unused-function 警告;建议删除或加注释说明留待后用。

### 🟢 P7 — `recordUIPass` 参数名与注释过期

- **文件**:`src/vulkan_util.cpp:1069`(参数名 `bgYScale`)、`:1102`(注释 `[52-55] bgYScale (background Y-Scale)`)、`:1109`
- **问题**:该参数已被复用作 `blendingMode`,但函数参数名和 push constant 注释块仍叫 "bgYScale / background Y-Scale"。布局未变(不是 bug),但误导后续维护者。建议改参数名或更新注释。
