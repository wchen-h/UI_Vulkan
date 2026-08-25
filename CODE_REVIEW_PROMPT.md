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
