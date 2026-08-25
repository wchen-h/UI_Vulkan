# sRGB 域与 Linear 域 Alpha Blending 转换理论

## 1. 问题是什么

### 背景

我们的补偿模型（eff_neutral, eff_black 等曲线）是在 **linear 域** 做 alpha blending 的前提下标定的。也就是说，测试时 HDR 侧和 SDR 侧都用同一种混合方式：

```
mixed = ui_linear * alpha + bg_linear * (1 - alpha)    （linear 域混合）
```

这是计算机图形学的标准做法，物理上正确。

但经过抓帧分析发现，**实际游戏在 sRGB 域做 alpha blending**，即游戏在混合之前先做了 linear -> sRGB 转换，然后在 sRGB 域混合：

```
mixed = ui_srgb * alpha + bg_srgb * (1 - alpha)    （sRGB 域混合）
```

这不是标准做法（标准是 linear 域混合），但游戏就是这么做的。

### 为什么会出问题

在 SDR 窗口的对比测试中，同一个 UI alpha 值：
- **sRGB 域混合**的结果比 **linear 域混合**更"不透明"（更"实"）

原因是 sRGB 是一个凹函数（指数约 0.42 < 1）。关键不在于 sRGB(bg) 比 bg 大（这确实如此），而在于 **sRGB 凹性导致 sRGB(bg × (1−α)) ≥ sRGB(bg) × (1−α)**。也就是说，先在 linear 域做乘法再 sRGB encode（linear 混合）的结果，比先 sRGB encode 再做乘法（sRGB 混合）的结果更大。对黑色 UI 来说，显示值更大 = 透过来的光更多 = 更透明；显示值更小 = 透过来的光更少 = 更不透明。因此 sRGB 域混合的显示值更小，UI 看起来更不透明（更"实"）。

我们的曲线标定时，HDR 侧和 SDR 侧都用 linear 域混合，找到了一个 eff 使得两者看起来一致。但实际游戏中 SDR 侧用的是 sRGB 域混合（更不透明），而我们 HDR 视频仍然用 linear 域混合（较透明）。所以：

**HDR 视频比 SDR 游戏看起来更透明。**

### 要解决什么

我们不想改 blend_ui 的混合域（仍然用 linear 域混合，保持正确做法），但想找到一个修正后的 eff，使得 **linear 域混合用这个修正后的 eff，最终显示效果 = sRGB 域混合用原始 alpha 的显示效果**。

这样曲线的补偿逻辑（背景亮度差异、主观匹配等）仍然有效，只是把"匹配目标"从 linear 域混合的 SDR 换成 sRGB 域混合的 SDR。

---

## 2. 从最简单的情况开始：黑色 UI

黑色 UI 的 ui = 0（线性 nits 为 0），这大大简化了问题，我们先从这里推导。

### 2.1 两种混合方式的显示结果

**linear 域混合**（我们的做法，blend_ui 用的）：

```
mixed_linear = 0 * eff + bg * (1 - eff) = bg * (1 - eff)
```

混合结果在 linear 域。最终要显示到屏幕上，需要 sRGB encode：

```
display_linear = sRGB(bg * (1 - eff))
```

**sRGB 域混合**（游戏的做法）：

先把 bg 从 linear 转到 sRGB：

```
bg_srgb = sRGB(bg)
```

在 sRGB 域混合（ui=0 所以 sRGB(0)=0）：

```
mixed_srgb = 0 * alpha + bg_srgb * (1 - alpha) = sRGB(bg) * (1 - alpha)
```

混合结果已经在 sRGB 域，直接显示：

```
display_srgb = sRGB(bg) * (1 - alpha)
```

### 2.2 令两者显示相等

我们要求：用 eff 做 linear 域混合的显示 = 用 alpha 做 sRGB 域混合的显示：

```
sRGB(bg * (1 - eff)) = sRGB(bg) * (1 - alpha)
```

### 2.3 求解 eff

左边是 sRGB 函数套了一个表达式，右边是 sRGB(bg) 乘以 (1-alpha)。要对左边"脱掉" sRGB，我们对两边取 sRGB 的逆函数（即 sRGB -> linear 的解码函数，记作 sRGB_inv）：

```
bg * (1 - eff) = sRGB_inv( sRGB(bg) * (1 - alpha) )
```

两边除以 bg（假设 bg > 0）：

```
1 - eff = sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg
```

移项得到 eff：

```
eff = 1 - sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg
```

**这就是黑色 UI 的转换公式。**

它的含义是：给定背景亮度 bg 和原始 alpha，这个公式算出的 eff 用于 linear 域混合，其显示效果等同于用 alpha 做 sRGB 域混合。

### 2.4 边界验证

**alpha = 0（完全透明）**：

```
eff = 1 - sRGB_inv( sRGB(bg) * 1 ) / bg
    = 1 - sRGB_inv( sRGB(bg) ) / bg
    = 1 - bg / bg          （sRGB_inv 和 sRGB 互逆）
    = 0
```

eff = 0，完全透明。正确。

**alpha = 1（完全不透明）**：

```
eff = 1 - sRGB_inv( sRGB(bg) * 0 ) / bg
    = 1 - sRGB_inv( 0 ) / bg
    = 1 - 0 / bg           （sRGB(0) = 0, sRGB_inv(0) = 0）
    = 1
```

eff = 1，完全不透明。正确。

### 2.5 为什么 eff > alpha（linear 需要更高不透明度）

我们要证明：对于 0 < alpha < 1，有 eff > alpha。

从公式出发：

```
eff = 1 - sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg
```

要证 eff > alpha，等价于证：

```
1 - sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg > alpha
```

移项：

```
sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg < 1 - alpha
```

即：

```
sRGB_inv( sRGB(bg) * (1 - alpha) ) < bg * (1 - alpha)
```

令 t = bg, lambda = (1 - alpha)，其中 0 < lambda < 1。我们要证：

```
sRGB_inv( sRGB(t) * lambda ) < t * lambda
```

这等价于（两边套 sRGB，因为 sRGB 是单调递增的）：

```
sRGB(t) * lambda < sRGB(t * lambda)
```

即：**sRGB(t * lambda) > sRGB(t) * lambda**，其中 0 < lambda < 1。

**这正是 sRGB 凹性的定义**。sRGB 的传递函数大致是 x^(1/2.4)（指数 < 1），这是一个**凹函数**（concave function）。凹函数满足 Jensen 不等式：

> 对于凹函数 f 和 0 <= lambda <= 1：f(lambda * x + (1-lambda) * 0) >= lambda * f(x) + (1-lambda) * f(0)

取 f(0) = 0（sRGB(0) = 0），得：f(lambda * x) >= lambda * f(x)。

即 sRGB(t * lambda) >= sRGB(t) * lambda（等号仅在 lambda=0 或 lambda=1 或 t=0 时成立）。

所以 `sRGB_inv( sRGB(t) * lambda ) <= t * lambda`，即 `eff >= alpha`（等号仅在边界）。

**结论：对于 0 < alpha < 1，eff > alpha。linear 域需要更高的不透明度才能匹配 sRGB 域混合的效果。**

这与我们的观察一致：sRGB 域混合更"实"（更不透明），所以 linear 域需要提高 eff 来补偿。

---

## 3. 推广到任意颜色 UI

当 UI 颜色不为黑（ui != 0）时，公式稍复杂但仍可精确推导。

### 3.1 两种混合的显示结果

**linear 域混合，sRGB encode 显示**：

```
mixed_linear = ui * eff + bg * (1 - eff)
display_linear = sRGB( ui * eff + bg * (1 - eff) )
```

**sRGB 域混合，直接显示**：

```
mixed_srgb = sRGB(ui) * alpha + sRGB(bg) * (1 - alpha)
display_srgb = sRGB(ui) * alpha + sRGB(bg) * (1 - alpha)
```

### 3.2 令两者相等

```
sRGB( ui * eff + bg * (1 - eff) ) = sRGB(ui) * alpha + sRGB(bg) * (1 - alpha)
```

### 3.3 求解 eff

令右边的已知值为 D（sRGB 域混合的显示值）：

```
D = sRGB(ui) * alpha + sRGB(bg) * (1 - alpha)
```

对等式两边取 sRGB_inv（sRGB 逆函数）：

```
ui * eff + bg * (1 - eff) = sRGB_inv(D)
```

展开左边：

```
ui * eff + bg - bg * eff = sRGB_inv(D)
eff * (ui - bg) + bg = sRGB_inv(D)
```

移项：

```
eff * (ui - bg) = sRGB_inv(D) - bg
```

```
eff = ( sRGB_inv(D) - bg ) / ( ui - bg )
```

### 3.4 通式

把 D 展开：

```
eff = ( sRGB_inv( sRGB(ui) * alpha + sRGB(bg) * (1 - alpha) ) - bg ) / ( ui - bg )
```

其中：
- ui：UI 的线性 nits 值
- bg：背景的线性 nits 值
- alpha：原始 UI alpha
- sRGB()：linear -> sRGB 编码函数
- sRGB_inv()：sRGB -> linear 解码函数

### 3.5 退化到黑色 UI

当 ui = 0 时，sRGB(0) = 0：

```
D = 0 * alpha + sRGB(bg) * (1 - alpha) = sRGB(bg) * (1 - alpha)

eff = ( sRGB_inv( sRGB(bg) * (1 - alpha) ) - bg ) / ( 0 - bg )
    = ( bg - sRGB_inv( sRGB(bg) * (1 - alpha) ) ) / bg
    = 1 - sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg
```

与第 2 节的公式一致。正确。

### 3.6 特殊情况：ui = bg

当 UI 和背景颜色完全一样时，混合结果无论 alpha 多少都等于 ui = bg，显示没有区别。此时公式分母为 0（退化），但实际上这种情况不需要混合（视觉上无差异），eff 可以取任意值。

---

## 4. 与现有曲线的关系

### 4.1 现有曲线做了什么

现有曲线（eff_neutral, eff_black 等）在 linear-linear 域标定。标定过程是：

1. SDR 侧：用 linear 域混合，alpha = 原始 UI alpha，背景 = SDR 背景
2. HDR 侧：用 linear 域混合，eff = 可调节参数，背景 = HDR 背景
3. 调 eff 直到 HDR 显示 = SDR 显示

找到的 eff_curve 使得：

```
sRGB( bg_hdr * (1 - eff_curve) ) = sRGB( bg_sdr * (1 - alpha) )    （黑色 UI）
```

即 linear 域混合的 HDR 和 linear 域混合的 SDR，显示一致。

### 4.2 现在的目标变了

实际游戏 SDR 侧用 sRGB 域混合，显示值是：

```
display_game = sRGB(bg_sdr) * (1 - alpha)    （sRGB 域混合，直接显示）
```

而我们曲线匹配的目标是：

```
display_curve = sRGB(bg_sdr * (1 - alpha))   （linear 域混合，sRGB encode 显示）
```

两者不同！因为 sRGB 是非线性函数：

```
sRGB(bg_sdr) * (1 - alpha)  !=  sRGB(bg_sdr * (1 - alpha))
```

（左边的乘法在 sRGB 域，右边的乘法在 linear 域，sRGB 的非线性导致两者不等）

### 4.3 差异有多大

定义**修正因子 r**：

```
r = display_game / display_curve = sRGB(bg_sdr) * (1 - alpha) / sRGB(bg_sdr * (1 - alpha))
```

由 sRGB 的凹性（第 2.5 节证明）：sRGB(bg * lambda) >= sRGB(bg) * lambda（lambda = 1-alpha），所以：

```
display_curve = sRGB(bg_sdr * (1 - alpha)) >= sRGB(bg_sdr) * (1 - alpha) = display_game
```

即 display_game <= display_curve，r <= 1。

**r <= 1 意味着游戏 sRGB 域混合的显示值比我们曲线匹配的 linear 域混合显示值更小（更暗），对应黑色 UI 来说就是"更不透明"。**

### 4.4 修正方法

我们已经有 eff_curve（曲线给出的 eff），使得：

```
sRGB(bg_hdr * (1 - eff_curve)) = sRGB(bg_sdr * (1 - alpha)) = display_curve
```

现在要找 eff_adj，使得：

```
sRGB(bg_hdr * (1 - eff_adj)) = display_game = display_curve * r
```

即：

```
sRGB(bg_hdr * (1 - eff_adj)) = sRGB(bg_hdr * (1 - eff_curve)) * r
```

两边取 sRGB_inv：

```
bg_hdr * (1 - eff_adj) = sRGB_inv( sRGB(bg_hdr * (1 - eff_curve)) * r )
```

```
eff_adj = 1 - sRGB_inv( sRGB(bg_hdr * (1 - eff_curve)) * r ) / bg_hdr
```

其中：

```
r = sRGB(bg_sdr) * (1 - alpha) / sRGB(bg_sdr * (1 - alpha))
```

### 4.5 两种实现路径

**路径 A（两步法，用曲线 + 修正）**：

1. 先用曲线算 eff_curve（处理 HDR/SDR 背景亮度差异 + 主观匹配）
2. 再用上述公式算 eff_adj（处理 sRGB vs linear 混合域差异）

需要：bg_hdr（HDR 背景，adjust_ui 已有）、bg_sdr（SDR 背景，需要额外读取）、alpha（已有）、eff_curve（曲线已算）

**路径 B（一步法，跳过曲线直接算）**：

直接用第 3 节的通式，把目标从"匹配 linear 域 SDR"改成"匹配 sRGB 域 SDR"：

```
eff = ( sRGB_inv( sRGB(ui) * alpha + sRGB(bg_sdr) * (1 - alpha) ) - bg_hdr ) / ( ui - bg_hdr )
```

对于黑色 UI：

```
eff = 1 - sRGB_inv( sRGB(bg_sdr) * (1 - alpha) ) / bg_hdr
```

这跳过了曲线，直接从 sRGB 域混合的显示目标反推 linear 域 eff。但这样丢失了曲线的"主观匹配"成分（曲线不只是亮度匹配，还包含人眼感知的调节结果）。

---

## 5. 实现可行性

### 5.1 adjust_ui 已有的数据

| 数据 | 来源 | 说明 |
|------|------|------|
| bg_hdr（per-pixel HDR 背景亮度） | `hdr_nits = read_hdr_bin(...)` | 已有 |
| ui（per-pixel UI 颜色） | `rgb`（sRGB -> linear 后） | 已有 |
| alpha（per-pixel UI 不透明度） | `alpha.png` | 已有 |
| eff_curve（曲线算出的 eff） | `eff_neutral` / `eff_black` | 已有 |
| sRGB() 和 sRGB_inv() | `common.py` 中有 `linear_to_pq` 等，但需要补 `linear_to_srgb` 和 `srgb_to_linear` | `srgb_to_linear` 已有，`linear_to_srgb` 需补 |

### 5.2 还缺什么

**路径 A（两步法）**：需要 bg_sdr（SDR 背景 per-pixel）。adjust_ui 目前只读 HDR 背景，不读 SDR 背景。需要在 config 里加 SDR 背景图路径，额外读取。

**路径 B（一步法）**：同样需要 bg_sdr。或者用 `bg_sdr = bg_hdr / k` 近似（k 是 HDR/SDR 亮度比，但 k 是 per-pixel 的，adjust_ui 不知道 k）。

### 5.3 计算量

- per-pixel 计算，每个像素涉及 2-3 次 sRGB/sRGB_inv 运算（查表或解析公式）
- sRGB 函数本身很简单（一个 if-else + pow），向量化后 numpy 可以快速处理全帧
- 和现有步骤 5（Y-Scale + PQ encode）的计算量同量级

### 5.4 对现有逻辑的影响

- **路径 A**：在步骤 4（eff 计算）之后、输出 _uiAlpha.bin 之前，加一步"sRGB 域修正"。不改曲线、不改 blend_ui、不改步骤 5/6。只在 eff 上叠加一个 per-pixel 修正。
- **路径 B**：替代曲线（eff_neutral / eff_black），直接用通式算 eff。改动更大，丢失主观匹配。

**推荐路径 A**：保留曲线的主观匹配，在其上加一层 sRGB 修正。改动最小，逻辑清晰。

---

## 6. 总结

| 项目 | 内容 |
|------|------|
| 问题 | 曲线在 linear 域标定，但游戏用 sRGB 域混合，两者不匹配 |
| 修正公式（黑色 UI） | `eff = 1 - sRGB_inv( sRGB(bg) * (1 - alpha) ) / bg` |
| 修正公式（任意 UI） | `eff = ( sRGB_inv( sRGB(ui)*alpha + sRGB(bg)*(1-alpha) ) - bg ) / (ui - bg)` |
| 性质 | 精确（无近似），per-pixel，eff >= alpha（linear 需更高不透明度） |
| 与曲线关系 | 曲线给 eff_curve（linear-linear 匹配），sRGB 修正在其上叠加修正因子 r |
| 实现 | 路径 A（曲线 + sRGB 修正），需要 bg_sdr（SDR 背景），改动最小 |
