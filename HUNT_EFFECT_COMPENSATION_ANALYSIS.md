# Hunt Effect 补偿分析：CIECAM02 视角下的 SDR/HDR UI 主观一致性

## 1. 调节目标

本项目对比 SDR 显示器（ThinkVision T27q-20, 350 nit 峰值）和 HDR 显示器（ProArt PA32UCX-P, 1000 nit 峰值）上同一 UI 素材的主观视觉表现。

调节目标为：在 HDR 窗口的绝对 brightness 高于 SDR 窗口（这是必然的，因为 HDR 动态范围更大）的前提下，使两个窗口中 **UI 与背景之间的三个相对关系** 保持主观一致：

| # | 目标 | CIECAM02 对应量 | 说明 |
|---|------|-----------------|------|
| 1 | 相对 brightness 一致 | $J_{\text{ui}} / J_{\text{bg}} \approx$ 相同 | UI 比 BG 看起来亮多少倍 |
| 2 | 主观不透明度一致 | **不要求物理 $\alpha$ 相同**，只要求 UI"浮在"BG上的主观程度一致 | 物理 $\alpha$ 可不同 |
| 3 | 相对 colorfulness 一致 | $C_{\text{ui}} \approx$ 相同（BG 为灰色，$C_{\text{bg}} \approx 0$） | UI 比 BG 看起来多彩多少 |

**不要求**绝对 brightness（Q）一致——HDR 窗口整体更亮是可接受的。
**不要求**物理 α 一致——SDR和HDR的α可以不同，只要求主观不透明度看起来一致。

---

## 2. 基于 CIECAM02 的理论分析

### 2.1 CIECAM02 关键公式引用

以下公式均来自 CIE 159:2004 / Fairchild Ch.16 / Wikipedia "CIECAM02"。

**亮度水平适应因子 $F_L$**（CIECAM02 §16.3）：

$$
k = \frac{1}{5L_A + 1}
$$

$$
F_L = \frac{1}{5}k^4(5L_A) + \frac{1}{10}(1-k^4)^2(5L_A)^{1/3}
$$

其中 $L_A$ 为适应场亮度，通常取 $L_W / 5$（"中等灰"假设）。

**锥体响应压缩**（CIECAM02 §16.3，post-adaptation）：

$$
R_a' = \frac{400 \cdot (F_L \cdot R'/100)^{0.42}}{27.13 + (F_L \cdot R'/100)^{0.42}} + 0.1
$$

$$
G_a' = \frac{400 \cdot (F_L \cdot G'/100)^{0.42}}{27.13 + (F_L \cdot G'/100)^{0.42}} + 0.1
$$

$$
B_a' = \frac{400 \cdot (F_L \cdot B'/100)^{0.42}}{27.13 + (F_L \cdot B'/100)^{0.42}} + 0.1
$$

$F_L$ 出现在压缩函数输入端，随 $L_A$ 增大而增大，直接影响色差信号 $a, b$ 的幅度。

**明度 J**（CIECAM02 §16.6）：

$$
A = \left[2R_a' + G_a' + \frac{1}{20}B_a' - 0.305\right] \cdot N_{bb}
$$

$$
J = 100 \cdot \left(\frac{A}{A_w}\right)^{cz}
$$

**亮度 Q**（CIECAM02 §16.7）：

$$
Q = \frac{4}{c} \cdot \sqrt{\frac{J}{100}} \cdot (A_w + 4) \cdot F_L^{0.25}
$$

**彩度 C**（CIECAM02 §16.8）：

$$
t = \frac{\frac{50000}{13} \cdot N_c \cdot N_{cb} \cdot e_t \cdot \sqrt{a^2 + b^2}}{R_a' + G_a' + \frac{21}{20}B_a'}
$$

$$
C = t^{0.9} \cdot \sqrt{\frac{J}{100}} \cdot (1.64 - 0.29^n)^{0.73}
$$

**色彩度 M**（CIECAM02 §16.9）：

$$
M = C \cdot F_L^{0.25}
$$

**饱和度 s**（CIECAM02 §16.10）：

$$
s = 100 \cdot \sqrt{\frac{M}{Q}}
$$

### 2.2 同一刺激在 SDR vs HDR 观视条件下的 CIECAM02 输出

同一 sRGB 颜色（同一 XYZ 三刺激值），两种观视条件：

| 参数 | SDR (350 nit) | HDR (1000 nit) |
|------|---------------|-----------------|
| $L_W$ | 350 cd/m² | 1000 cd/m² |
| $L_A$（$\approx L_W/5$） | 70 cd/m² | 200 cd/m² |
| $F_L$ | $\approx 0.69$ | $\approx 0.86$ |
| 白点 | D65 | D65 |

推理（基于上述公式）：

| CIECAM02 输出 | 推理过程 | SDR vs HDR 比较 |
|---------------|---------|-----------------|
| $J$ (lightness) | $J = 100(A/A_w)^{cz}$，$A$ 和 $A_w$ 都受 $F_L$ 影响，但比值 $A/A_w$ 在完全适应下近似不变 | **≈相同** — $J$ 是相对属性 |
| $Q$ (brightness) | $Q$ 含 $F_L^{0.25}$，$F_{L,\text{HDR}} > F_{L,\text{SDR}}$ $\to$ $Q_{\text{HDR}} > Q_{\text{SDR}}$ | **不同** — HDR 更亮 |
| $C$ (chroma) | $C = t^{0.9} \cdot \sqrt{J/100} \cdot (1.64 - 0.29^n)^{0.73}$。$t$ 中分子 $\sqrt{a^2+b^2}$ 和分母 $[R_a'+G_a'+\frac{21}{20}B_a']$ 均随 $F_L$ 近似线性缩放，比值近似不变 $\to$ $t \approx$ 常数 $\to$ $C \approx$ 常数 | **≈相同** — $C$ 是相对属性（相对于参考白），设计为亮度不变 |
| $M$ (colorfulness) | $M = C \cdot F_L^{0.25}$，$C$ 近似不变但 $F_{L,\text{HDR}} > F_{L,\text{SDR}}$ $\to$ $M_{\text{HDR}} > M_{\text{SDR}}$（比值 $= (0.86/0.69)^{0.25} \approx 1.056$） | **不同** — HDR 更高（**Hunt Effect 体现在 $M$，不是 $C$**） |
| $s$ (saturation) | $s = 100\sqrt{M/Q}$，$F_L^{0.25}$ 在 $M$ 和 $Q$ 中同时出现并抵消 | **≈相同** |

**关键发现**：CIECAM02 预测 saturation ($s$) 和 chroma ($C$) 在两种亮度下均近似不变（$s$ 中 $F_L^{0.25}$ 在 $M/Q$ 中抵消；$C$ 中锥体信号缩放比例在分子分母中抵消）。**Hunt Effect 体现在 colorfulness ($M$)**：$M_{\text{HDR}} / M_{\text{SDR}} \approx (F_{L,\text{HDR}} / F_{L,\text{SDR}})^{0.25} \approx 1.056$，即 HDR 下主观鲜艳度高约 5.6%。

### 2.3 为什么亮度和不透明度调节无法同时满足三个目标

#### 2.3.1 当前代码中的 alpha blending

ui.frag 中的 blending 公式：

```glsl
vec3 blended = uiAdj * uiAlpha + fpc.bgLinear * (1.0 - uiAlpha);
```

其中 `bgLinear` 是纯灰色（$\text{bgNit}/350$ for HDR），$\text{uiAdj} = \text{uiRGB} \times \text{uiLumMult}$。

blended 的 luminance（Rec.709）：

$$
L_{\text{blended}} = \text{lumAvg} \times \text{uiLumMult} \times \text{effAlpha} + \text{bgLinear} \times (1 - \text{effAlpha})
$$

$$
= \frac{\text{uiLumNit} \times \text{effAlpha}}{\text{PAPER\_WHITE\_NIT}} + \frac{\text{bgNit}}{\text{PAPER\_WHITE\_NIT}} \times (1 - \text{effAlpha})
$$

#### 2.3.2 Lock 机制的正确行为与代码 Bug

**Lock 的预期行为**：当 Lock 被按下时，调节 effAlpha 时 blended luminance 应保持不变。

由 $L_{\text{blended}}$ 公式，要使 $L_{\text{blended}}$ 恒定，需要：

$$
\text{uiLumNit} \times \text{effAlpha} + \text{bgNit} \times (1 - \text{effAlpha}) = L_{\text{target}} \times \text{PAPER\_WHITE\_NIT} = \text{恒定}
$$

Lock 时记录：

$$
\text{lumLock} = \text{uiLumNit} \times \text{effAlpha} + \text{bgNit} \times (1 - \text{effAlpha})
$$

effAlpha 变化后计算 uiLumNit：

$$
\text{uiLumNit} = \frac{\text{lumLock} - \text{bgNit} \times (1 - \text{effAlpha})}{\text{effAlpha}}
$$

**推理**：例如 uiLumNit=500, effAlpha=1.0, bgNit=500 时：
- lumLock = 500×1.0 + 500×0 = 500
- effAlpha 降到 0.5：uiLumNit = (500 - 500×0.5) / 0.5 = 500（不变！）
- 因为 BG 贡献增加了（500×0.5=250 nit），UI 贡献减少了（500×0.5=250 nit），总 blended luminance = 250+250 = 500 nit，与原来（500+0 = 500 nit）相同。

**代码 Bug**（hdr_app.cpp line 183-189）：当前代码存储 `lumLock_ = uiLumNit_ × effAlpha_`，计算 `uiLumNit_ = lumLock_ / effAlpha_`。这只保持了 UI 对 blended 的贡献不变，**忽略了 BG 贡献 `bgNit × (1 - effAlpha)` 的变化**，导致 blended luminance 不恒定。

用同样的例子（uiLumNit=500, effAlpha=1.0, bgNit=500）：
- 当前代码 lumLock = 500×1 = 500
- effAlpha 降到 0.5：uiLumNit = 500 / 0.5 = **1000**（错误！翻倍了！）
- blended luminance = 1000×0.5 + 500×0.5 = 500+250 = 750 nit（原为 500 nit，增加了 50%）

**代码需要修正**为：

```cpp
// Lock pressed:
lumLock_ = uiLumNit_ * effAlpha_ + bgNit_ * (1.0f - effAlpha_);

// effAlpha changed while locked:
uiLumNit_ = (lumLock_ - bgNit_ * (1.0f - effAlpha_)) / effAlpha_;
```

此外，当 bgNit 也在 Lock 状态下被调节时，需要同步更新 uiLumNit 以保持 blended 恒定（因为 lumLock 依赖 bgNit）。

#### 2.3.3 修正后 Lock 机制下的 CIECAM02 分析（完整推导）

本节从 shader blending 公式出发，逐步推导到 CIECAM02 chroma C，证明 Lock 机制下 C 和主观不透明度仍然耦合。

##### Step 1: Shader blending 公式（逐通道展开）

ui.frag 中的 alpha blending（`bgLinear` 在所有通道相同 = 纯灰 BG）：

$$
\begin{aligned}
\text{blended}_R &= \text{uiR} \times \text{uiLumMult} \times \alpha + \text{bgLinear} \times (1-\alpha) \\
\text{blended}_G &= \text{uiG} \times \text{uiLumMult} \times \alpha + \text{bgLinear} \times (1-\alpha) \\
\text{blended}_B &= \text{uiB} \times \text{uiLumMult} \times \alpha + \text{bgLinear} \times (1-\alpha)
\end{aligned}
$$

其中 $\text{uiLumMult} = \frac{\text{uiLumNit}}{\text{lumAvg} \times \text{PAPER\_WHITE\_NIT}}$，$\text{bgLinear} = \frac{\text{bgNit}}{\text{PAPER\_WHITE\_NIT}}$。

BG 的关键性质：bgRGB = vec3(bgLinear)，即所有三通道值相同——这是一个 D65 灰色。

##### Step 2: 分解 blended 为 luminance + chrominance

对任意线性 RGB 颜色，定义：
- Luminance: $L = 0.2126 R + 0.7152 G + 0.0722 B$（Rec.709）
- Chrominance vector: $(R-L,\ G-L,\ B-L)$

**blended 的 luminance**：

$$
L_{\text{blended}} = 0.2126 \cdot \text{blended}_R + 0.7152 \cdot \text{blended}_G + 0.0722 \cdot \text{blended}_B
$$

代入 Step 1，逐项展开：

$$
L_{\text{blended}} = 0.2126[\text{uiR} \cdot M \cdot \alpha + \text{bg}(1-\alpha)] + 0.7152[\text{uiG} \cdot M \cdot \alpha + \text{bg}(1-\alpha)] + 0.0722[\text{uiB} \cdot M \cdot \alpha + \text{bg}(1-\alpha)]
$$

（$M = \text{uiLumMult}$，$\text{bg} = \text{bgLinear}$，下同）

将 $\text{uiLumMult} \cdot \alpha$ 和 $\text{bgLinear} \cdot (1-\alpha)$ 各自提取公因子：

$$
L_{\text{blended}} = [0.2126 \cdot \text{uiR} + 0.7152 \cdot \text{uiG} + 0.0722 \cdot \text{uiB}] \cdot M \alpha + \text{bg}(1-\alpha) \cdot [0.2126 + 0.7152 + 0.0722]
$$

由于 Rec.709 权重之和为 1.0（$0.2126 + 0.7152 + 0.0722 = 1.000$），第一项 $= \text{lumAvg} \cdot M \cdot \alpha$，第二项 $= \text{bg} \cdot (1-\alpha)$：

$$
L_{\text{blended}} = \text{lumAvg} \times \text{uiLumMult} \times \alpha + \text{bgLinear} \times (1-\alpha)
$$

**blended 的 chrominance**：

$$
\text{blended}_R - L_{\text{blended}} = [\text{uiR} \cdot M \alpha + \text{bg}(1-\alpha)] - [\text{lumAvg} \cdot M \alpha + \text{bg}(1-\alpha)] = (\text{uiR} - \text{lumAvg}) \cdot M \cdot \alpha
$$

推导关键：bgLinear 在所有通道相同，所以 $\text{bg}(1-\alpha)$ 对 $\text{blended}_R$ 和 $L_{\text{blended}}$ 的贡献完全相同，相减时**完美抵消**。

同理：

$$
\text{blended}_G - L_{\text{blended}} = (\text{uiG} - \text{lumAvg}) \cdot \text{uiLumMult} \cdot \alpha
$$

$$
\text{blended}_B - L_{\text{blended}} = (\text{uiB} - \text{lumAvg}) \cdot \text{uiLumMult} \cdot \alpha
$$

**核心结论**：BG（纯灰色）贡献零 chrominance。blended 的全部 chrominance 来自 UI，缩放因子为 $\text{uiLumMult} \cdot \alpha$。

##### Step 3: Lock 约束与 uiLumMult·α 的推导

Lock 机制保持 blended luminance 恒定：

$$
\text{lumAvg} \cdot \text{uiLumMult} \cdot \alpha + \text{bgLinear} \cdot (1-\alpha) = L_{\text{target}} \quad (\text{常数})
$$

由此推导 uiLumMult：

$$
\text{uiLumMult} = \frac{L_{\text{target}} - \text{bgLinear} \cdot (1-\alpha)}{\text{lumAvg} \cdot \alpha}
$$

或者更方便的形式（chrominance 缩放因子）：

$$
\text{uiLumMult} \cdot \alpha = \frac{L_{\text{target}} - \text{bgLinear} \cdot (1-\alpha)}{\text{lumAvg}}
$$

##### Step 4: uiLumMult·α 不是常数——它是 α 的线性函数

展开 $(1-\alpha) = 1 - \alpha$：

$$
\text{uiLumMult} \cdot \alpha = \frac{L_{\text{target}} - \text{bgLinear} + \text{bgLinear} \cdot \alpha}{\text{lumAvg}} = \frac{L_{\text{target}} - \text{bgLinear}}{\text{lumAvg}} + \frac{\text{bgLinear}}{\text{lumAvg}} \cdot \alpha
$$

这是 $\alpha$ 的线性函数：
- 截距：$(L_{\text{target}} - \text{bgLinear}) / \text{lumAvg}$
- 斜率：$\text{bgLinear} / \text{lumAvg} > 0$（因为 $\text{bgLinear} > 0$ 且 $\text{lumAvg} > 0$）

因此：$\alpha$ 增大 $\to$ $\text{uiLumMult} \cdot \alpha$ 增大；$\alpha$ 减小 $\to$ $\text{uiLumMult} \cdot \alpha$ 减小。

**注意 uiLumMult 本身的变化方向与 $\alpha$ 相反**：从公式 $\text{uiLumMult} = (L_{\text{target}} - \text{bgLinear}(1-\alpha)) / (\text{lumAvg} \cdot \alpha)$，$\alpha$ 增大时 uiLumMult 减小（$\alpha$ 出现在分母中），$\alpha$ 减小时 uiLumMult 增大。但 $\text{uiLumMult} \cdot \alpha$ 的变化方向与 $\alpha$ 相同，因为 BG 贡献的变化主导了总量。

数值验证（$L_{\text{target}}=0.5$, $\text{bgLinear}=0.18$, $\text{lumAvg}=0.5$）：

| $\alpha$ | $\text{uiLumMult} \cdot \alpha$ | $\text{uiLumMult}$ | 说明 |
|---|-------------|-----------|------|
| 1.0 | 1.000 | 1.000 | baseline：纯 UI，无 BG |
| 0.9 | 0.964 | 1.071 | 9% BG 混入，chrominance $\downarrow$ 3.6% |
| 0.5 | 0.820 | 1.640 | 50% BG 混入，chrominance $\downarrow$ 18% |

##### Step 5: 从线性 RGB chrominance 到 CIECAM02 opponent color (a, b)

blended stimulus 经以下变换链产生 CIECAM02 的 $a, b$：

$$
\text{linear RGB} \xrightarrow{M_{\text{srgb2xyz}}} \text{CIE XYZ} \xrightarrow{M_{\text{hpe}}} R',G',B' \xrightarrow{\text{compression}} R_a',G_a',B_a' \xrightarrow{\text{linear}} a, b
$$

**5a: 线性 RGB → XYZ → HPE cone responses**

两个矩阵变换都是线性的，所以 blended stimulus 线性分解为 UI 和 BG 两部分：

$$
\begin{aligned}
X_{\text{blended}} &= X_{\text{ui}} \cdot \text{uiLumMult} \cdot \alpha + X_{\text{bg}} \cdot (1-\alpha) \\
Y_{\text{blended}} &= Y_{\text{ui}} \cdot \text{uiLumMult} \cdot \alpha + Y_{\text{bg}} \cdot (1-\alpha) = L_{\text{target}} \quad (\text{Lock 保持恒定}) \\
Z_{\text{blended}} &= Z_{\text{ui}} \cdot \text{uiLumMult} \cdot \alpha + Z_{\text{bg}} \cdot (1-\alpha)
\end{aligned}
$$

$$
\begin{aligned}
R'_{\text{blended}} &= R'_{\text{ui}} \cdot \text{uiLumMult} \cdot \alpha + R'_{\text{bg}} \cdot (1-\alpha) \\
G'_{\text{blended}} &= G'_{\text{ui}} \cdot \text{uiLumMult} \cdot \alpha + G'_{\text{bg}} \cdot (1-\alpha) \\
B'_{\text{blended}} &= B'_{\text{ui}} \cdot \text{uiLumMult} \cdot \alpha + B'_{\text{bg}} \cdot (1-\alpha)
\end{aligned}
$$

其中 $\text{XYZ}_{\text{ui}} = M_{\text{srgb2xyz}} \times \text{uiRGB}$，$R'_{\text{ui}} = M_{\text{hpe}} \times \text{XYZ}_{\text{ui}}$，类似地计算 BG 分量。

**5b: D65 灰色 BG 的 cone responses 全部相等**

BG 是 D65 灰色（所有 RGB 通道 = bgLinear）。经 sRGB→XYZ 变换后，XYZ_bg 与 D65 白点 (X_n, Y_n, Z_n) 成比例。经 HPE 变换后，R'_bg = G'_bg = B'_bg（HPE 矩阵在 D65 adaptation 下将中性灰映射到等值 cone response）。

证明：设 bgLinear 对应的 XYZ 为 (k·X_n, k·Y_n, k·Z_n)，其中 k = bgLinear/Y_n。CIECAM02 的 HPE 矩阵对 D65 白点的变换结果为 R'_n = G'_n = B'_n（这是 D65 adaptation 的定义性质）。因此 R'_bg = k·R'_n = k·G'_n = k·B'_n = G'_bg = B'_bg。

**5c: 压缩函数是非线性的**

CIECAM02 对每个 cone response 应用相同的压缩函数：

$$
R_a' = \frac{400 \cdot (F_L \cdot R'/100)^{0.42}}{27.13 + (F_L \cdot R'/100)^{0.42}} + 0.1
$$

这是一个 sigmoid-like 函数。它是非线性的，因此我们**不能**在压缩之后将 UI 和 BG 的贡献分离：

$$
R_{a,\text{blended}}' = f\!\left(\frac{F_L \cdot R'_{\text{blended}}}{100}\right) \neq f\!\left(\frac{F_L \cdot R'_{\text{ui}} \cdot M \cdot \alpha}{100}\right) + f\!\left(\frac{F_L \cdot R'_{\text{bg}} \cdot (1-\alpha)}{100}\right)
$$

压缩函数将 UI 和 BG 的贡献非线性地混合在一起。

**5d: BG 在压缩后仍贡献零 opponent color**

尽管压缩是非线性的，BG 对 a, b 的贡献仍为零。这是因为：

从 Step 5b，$R'_{\text{bg}} = G'_{\text{bg}} = B'_{\text{bg}}$。压缩函数对所有三个 cone type 相同，所以：

$$
R_{a,\text{bg}}' = f\!\left(\frac{F_L \cdot R'_{\text{bg}}}{100}\right), \quad G_{a,\text{bg}}' = R_{a,\text{bg}}', \quad B_{a,\text{bg}}' = R_{a,\text{bg}}'
$$

计算 opponent color：

$$
a_{\text{bg}} = R_{a,\text{bg}}' - \frac{12}{11} G_{a,\text{bg}}' + \frac{1}{11} B_{a,\text{bg}}' = R_{a,\text{bg}}' \times \left(1 - \frac{12}{11} + \frac{1}{11}\right) = 0
$$

$$
b_{\text{bg}} = \frac{R_{a,\text{bg}}' + G_{a,\text{bg}}' - 2 B_{a,\text{bg}}'}{9} = R_{a,\text{bg}}' \times \frac{1 + 1 - 2}{9} = 0
$$

结论：**D65 灰色 BG 在 CIECAM02 中贡献零 $a$ 和零 $b$**，与它在线性 RGB 中贡献零 chrominance 一致。

**5e: α 变化对 blended 的 a, b 的影响**

Lock 保持 L_blended = L_target 恒定。从 Step 2，blended 的全部 chrominance 来自 UI，缩放因子 uiLumMult·α。

从 Step 4，uiLumMult·α 与 α 同方向变化（α 增大 → uiLumMult·α 增大）。

问题：线性 RGB 的 chrominance 变化如何传递到 CIECAM02 的 a, b？

- 在线性变换阶段（RGB→XYZ→HPE）：chrominance 变化完全保留，按 uiLumMult·α 缩放
- 在非线性压缩阶段：无法精确分解，但方向性明确——stimulus 的 chrominance 增大时，各锥体通道在压缩函数中的输入差异增大，经非线性压缩后对立色信号 $a, b$ 的绝对值通常增大（取决于具体刺激）

对于小变化（3-5% Hunt Effect 补偿范围），压缩函数在局部可近似为线性，a, b 的变化大致与 uiLumMult·α 成正比。精确比例系数取决于压缩函数在当前工作点的局部斜率，该斜率由 F_L 和绝对信号水平决定。

定性结论：
- α 增大 → uiLumMult·α 增大 → stimulus chrominance 增大 → a, b 增大
- α 减小 → uiLumMult·α 减小 → stimulus chrominance 减小 → a, b 减小

##### Step 6: 从 a, b 到 CIECAM02 chroma C

CIECAM02 chroma 公式（§2.1）：

$$
t = \frac{\frac{50000}{13} \cdot N_c \cdot N_{cb} \cdot e_t \cdot \sqrt{a^2 + b^2}}{R_a' + G_a' + \frac{21}{20} B_a'}
$$

$$
C = t^{0.9} \cdot \sqrt{\frac{J}{100}} \cdot (1.64 - 0.29^n)^{0.73}
$$

逐因子分析哪些在 Lock 下变化，哪些不变：

1. $\sqrt{a^2+b^2}$：opponent color 信号幅度。从 Step 5e，$\alpha$ 变化时 $a,b$ 变化 $\to$ $\sqrt{a^2+b^2}$ 变化。**变化。**

2. $[R_a' + G_a' + \frac{21}{20} B_a']$：分母。CIECAM02 中 $A = [2R_a' + G_a' + \frac{1}{20}B_a' - 0.305] \cdot N_{bb}$，权重 $(2, 1, 1/20)$ 与此分母的权重 $(1, 1, 21/20)$ 不同，但两者都是 cone 信号的加权求和。Lock 保持 luminance 恒定 $\to$ $Y$ 恒定 $\to$ cone 信号总和近似恒定 $\to$ 此分母近似恒定。**≈ 不变。**

3. $\sqrt{J/100}$：$J = 100 \cdot (A/A_w)^{cz}$。Lock 保持 $A \approx$ 恒定（因为 luminance 恒定 $\to$ $A$ 恒定），$A_w$ 由 viewing condition 固定。所以 $J \approx$ 恒定 $\to$ $\sqrt{J/100} \approx$ 恒定。**≈ 不变。**

4. $(1.64 - 0.29^n)^{0.73}$：surround factor $n$ 由观视环境决定，不随 $\alpha$ 变化。**不变。**

5. $N_c, N_{cb}, e_t$：viewing condition 参数，不随 $\alpha$ 变化。**不变。**

因此 $C$ 中唯一随 $\alpha$ 变化的因子是 $\sqrt{a^2+b^2}$（通过 $t$）：

$$
t \approx \text{const} \times \sqrt{a^2 + b^2}, \qquad C \approx \left(\sqrt{a^2 + b^2}\right)^{0.9} \times \text{const}
$$

变化方向：
- $\alpha$ 增大 $\to$ $\sqrt{a^2+b^2}$ 增大 $\to$ $t$ 增大 $\to$ $C$ 增大
- $\alpha$ 减小 $\to$ $\sqrt{a^2+b^2}$ 减小 $\to$ $t$ 减小 $\to$ $C$ 减小

##### Step 7: C 与主观不透明度的耦合——完整推理链

总结从 $\alpha$ 变化到 $C$ 变化的完整因果链：

$$
\alpha \uparrow \;\to\; \text{bgLinear}(1-\alpha) \downarrow \;\to\; \text{uiLumMult} \cdot \alpha \uparrow \;\to\; \text{blended chrominance} \uparrow \;\to\; a,b \uparrow \;\to\; t \uparrow \;\to\; C \uparrow
$$

同时：

$$
\alpha \uparrow \;\to\; \text{主观不透明度} \uparrow \quad (\text{UI 更不透明，更多 UI 覆盖 BG})
$$

**$C$ 和主观不透明度同方向变化——无法只降低 $C$ 而不降低主观不透明度。**

反向推理（$\alpha$ 减小）同理：$C$ 降低的同时主观不透明度也降低。

##### Step 8: 为什么 uiLumNit 调节也无法解耦

uiLumNit 通过 uiLumMult = uiLumNit / (lumAvg × PAPER_WHITE_NIT) 等比例缩放整个 uiRGB：

$$
\text{uiAdj} = \text{uiRGB} \times \text{uiLumMult}
$$

这等比例缩放 luminance 和 chrominance（$\text{uiR} \cdot \text{uiLumMult},\ \text{uiG} \cdot \text{uiLumMult},\ \text{uiB} \cdot \text{uiLumMult}$）。在 CIECAM02 中：

- $A_{\text{ui}}$ = luminance-based weighted sum $\to$ 按 uiLumMult 缩放
- $a_{\text{ui}}, b_{\text{ui}}$ = opponent color difference $\to$ 也按 uiLumMult 缩放
- 但 $a_{\text{ui}} / A_{\text{ui}}$ 的比值不变（两者被同一因子缩放）

因此 $t \approx \sqrt{a^2+b^2} / [R_a'+G_a'+\frac{21}{20}B_a']$ 中，分子和分母同比例缩放 $\to$ $t$ 不变 $\to$ $C$ 的变化仅来自 $\sqrt{J/100}$：

$$
\text{降低 uiLumNit} \;\to\; A \downarrow \;\to\; J \downarrow \;\to\; \sqrt{J/100} \downarrow \;\to\; C \downarrow \quad \checkmark
$$

$$
\text{降低 uiLumNit} \;\to\; J_{\text{ui}} \downarrow \;\to\; J_{\text{ui}}/J_{\text{bg}} \text{ 比值改变} \quad \times \quad (\text{相对 brightness 不再一致})
$$

**$J$ 和 $C$ 通过 uiLumNit 耦合——无法只改变 $C$ 而不改变 $J$。**

##### 总结

Lock 机制下，blended luminance 恒定 → A 恒定 → J 恒定，保证了目标 #1（相对 brightness）。但 α 的变化同时影响：
- 主观不透明度（目标 #2）：α 直接控制
- C（目标 #3）：α → uiLumMult·α → chrominance → a,b → √(a²+b²) → t → C

两个目标通过 α 耦合，无法用现有参数（uiLumNit, effAlpha, bgNit）同时满足 3 个独立约束。

#### 2.3.4 Hunt Effect 是观视条件效应，不是刺激效应

**推理**（核心论点，非直接引用标准）：

CIECAM02 中 C 的计算依赖两个与亮度水平相关的因素：

1. **$F_L$ 依赖的锥体压缩函数**：$F_L$ 由 $L_A$（适应场亮度）决定，$L_A$ 由整个观视环境决定（$L_A \approx L_W/5$），不是由单个刺激的亮度决定。即使我们把 HDR UI 的亮度降到 SDR 水平，观察者的眼睛仍然适应于 HDR 显示器的峰值亮度（1000 nit），$F_L$ 仍然 $\approx 0.86$。

2. $\sqrt{J/100}$：$J$ 是相对属性，近似不随亮度水平变化（因为 $A/A_w$ 的比值近似不变）。所以 $\sqrt{J/100}$ 在 SDR 和 HDR 之间近似相同。

因此：**Hunt Effect 的机制是 $L_A \to F_L \to M = C \cdot F_L^{0.25}$ 中 $F_L^{0.25}$ 的直接放大**（$C$ 本身近似不变，因为 $t$ 的分子分母同步变化抵消）。$F_L$ 由观视环境决定，无法通过改变刺激参数（uiLumNit, effAlpha）来消除。

即使我们让 HDR UI 的物理亮度与 SDR UI 完全相同，CIECAM02 仍然预测 $M_{\text{hdr}} > M_{\text{sdr}}$，因为 $F_{L,\text{HDR}} \approx 0.86 > F_{L,\text{SDR}} \approx 0.69$，导致 colorfulness 更高。

#### 2.3.5 Lock + effAlpha 能否补偿 Hunt Effect？

修正后的 Lock 机制保持 blended luminance 恒定。降低 α 时，uiLumMult 增大（§2.3.3 分析），但 uiLumMult×α 减小，a,b 降低，C 降低。

**推理**：降低 α 会使主观不透明度降低（UI 更透明），同时 C 也降低。C 的降低幅度取决于 bgLinear 相对于 UI luminance 的比例。

对于典型场景（$\text{bgLinear}=0.18$, $\text{lumAvg}=0.5$, $L_{\text{target}}=0.5$）：

$$
\begin{aligned}
\alpha = 1.0 &\;\to\; \text{uiLumMult} \cdot \alpha = 1.000 &\;\to\; C = C_{\text{original}} \times 1.000 \\
\alpha = 0.9 &\;\to\; \text{uiLumMult} \cdot \alpha = 0.964 &\;\to\; C \approx C_{\text{original}} \times 0.964 \quad (\downarrow 3.6\%) \\
\alpha = 0.5 &\;\to\; \text{uiLumMult} \cdot \alpha = 0.820 &\;\to\; C \approx C_{\text{original}} \times 0.820 \quad (\downarrow 18\%)
\end{aligned}
$$

注意：$\text{uiLumMult} \cdot \alpha \approx$ chromatic 信号缩放因子（$a_{\text{blended}} = a_{\text{ui}} \times \text{uiLumMult} \times \alpha$），$C$ 的降低大致与此成正比（CIECAM02 中 $C \approx (\text{uiLumMult} \cdot \alpha)^{0.9} \times \text{const}$，略小于线性）。

**推理**：要补偿 3-5% Hunt Effect（$M$ 降低 3-5%），需要将 $\text{uiLumMult} \cdot \alpha$ 从 1.0 降到约 0.96-0.97。由公式 $\text{uiLumMult} \cdot \alpha = (L_{\text{target}} - \text{bgLinear} \times (1-\alpha)) / \text{lumAvg}$，解出对应的 $\alpha$：

$$
\begin{aligned}
\frac{L_{\text{target}} - \text{bgLinear} \times (1-\alpha)}{\text{lumAvg}} &= 0.96 \\
L_{\text{target}} - \text{bgLinear} \times (1-\alpha) &= 0.96 \times \text{lumAvg} \\
\text{bgLinear} \times (1-\alpha) &= L_{\text{target}} - 0.96 \times \text{lumAvg} = 0.5 - 0.48 = 0.02 \\
(1-\alpha) &= 0.02 / 0.18 = 0.111 \\
\alpha &\approx 0.89
\end{aligned}
$$

将 $\alpha$ 从 1.0 降到 0.89 会使主观不透明度明显降低（11% 更多 BG 穿透），违反目标 #2。

**结论**：修正后的 Lock 机制 + effAlpha 调节可以在小范围内降低 $C$，但**无法在不破坏主观不透明度匹配的前提下完全补偿 Hunt Effect**。$C$ 和主观不透明度仍然耦合——降低 $C$ 必然同时降低主观不透明度。

uiLumNit 通过 $\text{uiLumMult} = \text{uiLumNit} / (\text{lumAvg} \times \text{PAPER\_WHITE\_NIT})$ 按比例缩放整个 uiRGB：

$$
\text{uiAdj} = \text{uiRGB} \times \text{uiLumMult}
$$

这等比例缩放 $A_{\text{ui}}$ 和 $a_{\text{ui}}, b_{\text{ui}}$。CIECAM02 的 $C$ 依赖 $\sqrt{J/100}$ 和 $t$，$t$ 依赖 $\sqrt{a^2+b^2}/(R_a'+G_a'+B_a')$。

**推理**：按比例缩放 uiRGB 改变了刺激的绝对亮度，但不改变色差信号与 achromatic 信号的相对比例（$a_{\text{ui}}/A_{\text{ui}}$ 不变）。然而 $C$ 的变化主要来自 **$F_L$ 依赖的锥体压缩函数**（在不同工作点上斜率不同）和 $\sqrt{J/100}$（$J$ 随绝对亮度变化）。因此：

- 降低 uiLumNit $\to$ $J \downarrow$ $\to$ $\sqrt{J/100} \downarrow$ $\to$ $C \downarrow$ $\checkmark$
- 降低 uiLumNit $\to$ $J_{\text{ui}} \downarrow$ $\to$ $J_{\text{ui}}/J_{\text{bg}}$ 比值改变 $\times$（相对 brightness 不再一致）

**$J$ 和 $C$ 是耦合的——无法只改变 $C$ 而不改变 $J$。**

#### 2.3.6 数学本质：参数空间分析

自由参数与约束的关系：

| 方案 | 自由参数 | 约束 | 可满足？ |
|------|---------|------|---------|
| 无Lock：$\text{uiLumNit} + \text{effAlpha}$ | 2 | $J_{\text{ratio}}$ + 主观opacity + $C$ = 3 | ✗ |
| 有Lock：$\text{effAlpha} + \text{bgNit}$ | 2 | $J_{\text{ratio}}$ + 主观opacity + $C$ = 3 | ✗ |
| 有Lock + chromaScale：$\text{effAlpha} + \text{bgNit} + \text{chromaScale}$ | 3 | $J_{\text{ratio}}$ + 主观opacity + $C$ = 3 | **✓** |

即使加入了 Lock 机制（将 uiLumNit 和 effAlpha 从两个独立参数变为一个耦合参数），自由参数仍然只有 2 个（加上 bgNit），无法满足 3 个独立约束。chromaScale 提供第 3 个自由度。

---

## 3. chromaScale 方案的原理与可行性

### 3.1 基本原理

核心思想：**在亮度不变的条件下，缩放色差信号**。

CIECAM02 中，C 的计算基于对手色维度 a 和 b（色差信号），而 J 的计算基于 A（achromatic 信号）。如果能独立缩放 a, b 而不改变 A，就能独立调节 C 而不改变 J。

在 shader 的线性 RGB 空间中，不存在直接的 $a, b$ 维度。但可以用以下等价操作：

$$
\begin{aligned}
1.&\quad L = 0.2126 R + 0.7152 G + 0.0722 B \quad &\text{(Rec.709 luminance)} \\
2.&\quad \text{gray} = (L, L, L) \quad &\text{(同亮度纯灰)} \\
3.&\quad \text{chromatic} = \text{blended} - \text{gray} \quad &\text{(色差)} \\
4.&\quad \text{result} = \text{gray} + \text{chromaScale} \times \text{chromatic} \quad &\text{(缩放色差)}
\end{aligned}
$$

### 3.2 为什么这在原理上可行

**引用 CIECAM02 §16.6-16.8**：$J$ 依赖 $A = [2R_a' + G_a' + \frac{1}{20}B_a' - 0.305] \cdot N_{bb}$。$A$ 是 cone 信号的加权求和，其中权重 $(2, 1, 1/20)$ 接近 Rec.709 luminance 权重 $(0.2126, 0.7152, 0.0722)$ 的比例。

**推理**：在 ui.frag 中计算的 $L = 0.2126 R + 0.7152 G + 0.0722 B$ 近似对应 CIECAM02 中的 $A$ 信号（两者都是 cone 信号加权和）。$\text{gray} = (L,L,L)$ 是同 luminance 的 achromatic 信号。$\text{chromatic} = \text{blended} - \text{gray}$ 近似对应 CIECAM02 中的色差信号 $(a, b)$。

因此：

$$
\text{result} = \text{gray} + \text{chromaScale} \times (\text{blended} - \text{gray})
$$

- **gray 不变** $\to$ $A$ 不变 $\to$ $J$ 不变 $\to$ 相对 brightness 保持 $\checkmark$
- **chromatic 被 chromaScale 缩放** $\to$ $a, b$ 被缩放 $\to$ $t$ 被 chromaScale 近似缩放 $\to$ $C$ 被 $\text{chromaScale}^{0.9}$ 近似缩放 $\to$ 相对 colorfulness 可调节 $\checkmark$
- $\alpha$ **不变** $\to$ 不透明度不变 $\checkmark$

三个目标同时满足。

### 3.3 chromaScale 的目标值

**推理**（基于 §2.2 修正后的 CIECAM02 分析）：

目标：M_hdr ≈ M_sdr，即补偿 Hunt Effect 在 colorfulness 上的 ~5.6% 差异。

由 §2.2，CIECAM02 预测 $C_{\text{hdr}} \approx C_{\text{sdr}}$（chroma 近似不变），而 $M = C \times F_L^{0.25}$，因此 $M$ 的差异完全来自 $F_L$：

$$
\frac{M_{\text{hdr}}}{M_{\text{sdr}}} = \left(\frac{F_{L,\text{HDR}}}{F_{L,\text{SDR}}}\right)^{0.25} = \left(\frac{0.86}{0.69}\right)^{0.25} \approx 1.056
$$

chromaScale 缩放 blended 的 chrominance $\to$ 缩放 $C$ $\to$ 缩放 $M$（$M = C \times F_L^{0.25}$，$F_L$ 由观视条件固定，不受 chromaScale 影响）。设 chromaScale 对 $C$ 的影响为 $C_{\text{after}} = C \times \text{chromaScale}^{0.9}$，则：

$$
M_{\text{hdr,after}} = C_{\text{hdr}} \times \text{chromaScale}^{0.9} \times F_{L,\text{HDR}}^{0.25}
$$

要求 $M_{\text{hdr,after}} \approx M_{\text{sdr}}$：

$$
\text{chromaScale}^{0.9} \approx \frac{M_{\text{sdr}}}{M_{\text{hdr}}} = \frac{1}{1.056} \approx 0.947
$$

$$
\text{chromaScale} \approx 0.947^{1/0.9} \approx 0.947^{1.11} \approx 0.94
$$

交叉验证：Kawashima & Ohno (CIE x047:2020 OP01) 报告 10:1 照度比下 perceived chroma（对应 CIECAM02 的 $M$，即 colorfulness）差 8-15%。本项目 F_L 比 1.24:1（远小于 10:1），推算 perceived chroma 差约 4-6%，与上述 M 的 5.6% 分析一致。

初始参考值：**chromaScale ≈ 0.93~0.96**。

**注意**：此值为推理估计，未经实验验证。精确值需要：
1. 用 CIECAM02 对具体 UI 颜色和观视条件进行离线计算
2. 或在 HDR 面板中提供 chromaScale 滑动条，通过视觉匹配实验确定

### 3.4 实现方案（代码修改思路记录）

##### 3.4.1 Shader 修改：ui.frag

当前 push constant 结构（28 bytes）：

```
offset 0-7:   vec2 offset (vertex)
offset 8-15:  vec2 scale  (vertex)
offset 16-19: float uiAlphaMultiplier
offset 20-23: float bgLinear
offset 24-27: float uiLumMult
```

新增 chromaScale（offset 28-31，push constant 总大小从 28→32 bytes）：

```glsl
layout(push_constant) uniform FragPush {
    layout(offset = 16) float uiAlphaMultiplier;
    layout(offset = 20) float bgLinear;
    layout(offset = 24) float uiLumMult;
    layout(offset = 28) float chromaScale;   // NEW: SDR=1.0, HDR=可调
} fpc;
```

Fragment shader 核心逻辑修改：

```glsl
vec3  uiRGB   = texture(texRGB, fragUV).rgb;
float uiAlpha = texture(texAlpha, fragUV).r * fpc.uiAlphaMultiplier;
vec3  uiAdj   = uiRGB * fpc.uiLumMult;

vec3 blended = uiAdj * uiAlpha + fpc.bgLinear * (1.0 - uiAlpha);

// chromaScale: 在 luminance 不变的条件下缩放 chrominance
float lum = 0.2126 * blended.r + 0.7152 * blended.g + 0.0722 * blended.b;
vec3 gray = vec3(lum);
vec3 result = gray + fpc.chromaScale * (blended - gray);

outColor = vec4(result, 1.0);
```

原理对应 §3.1：`gray = vec3(lum)` 保持 luminance（≈A）不变 → J 不变；`chromaScale × (blended - gray)` 缩放 chrominance（≈a,b）→ C 可独立调节。

##### 3.4.2 C++ 修改清单

**vulkan_util.h**：`recordUIPass` 函数签名增加 `float chromaScale` 参数：

```cpp
void recordUIPass(WindowContext& wc, VkCommandBuffer cmd, uint32_t imageIdx,
                  const std::vector<UIPair>& uiPairs, int currentUI,
                  VkBuffer quadVB, float bgLinear, float alpha, float uiLum,
                  float chromaScale);
```

**vulkan_util.cpp**：

1. `recordUIPass`：pcData 数组从 7→8 floats，增加 chromaScale；push constant 大小从 28→32：

```cpp
float pcData[8] = {
    0.0f, 0.0f,
    scaleX, scaleY,
    alpha, bgLinear, uiLum,
    chromaScale,     // NEW
};
vkCmdPushConstants(cmd, wc.uiPipeLayout,
                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                   0, 32, pcData);  // 28 → 32
```

2. `createUIPipeline`：push constant range size 从 28→32：

```cpp
pcRange.size = 32;  // 原为 28
```

**hdr_app.h**：增加成员变量：

```cpp
float chromaScale_ = 1.0f;
```

**hdr_app.cpp**：

1. `hdrImGui()`：增加 DragFloat 控件：

```cpp
ImGui::DragFloat("Chroma Scale", &chromaScale_, 0.001f, 0.0f, 2.0f, "%.3f");
```

2. `drawFrame()`：传递 chromaScale_ 到 recordUIPass：

```cpp
recordUIPass(wc, cmd, imageIdx, uiPairs_, currentUI_, quadVB_,
             bgLinear, effAlpha_, uiLumMult, chromaScale_);
```

**sdr_app.cpp**：`drawFrame()` 中传递 chromaScale = 1.0f（硬编码，SDR 不补偿）：

```cpp
recordUIPass(wc, cmd, imageIdx, uiPairs_, currentUI_, quadVB_,
             bgLinear, sdrAlpha_, 1.0f, 1.0f);
```

##### 3.4.3 PQ convert shader 无需修改

result 仍然是线性 RGB（chromaScale 只改变 RGB 值的 chrominance 部分，不改变 luminance）。PQ 编码只关心绝对亮度值 → PQ pipeline 不受影响。

##### 3.4.4 验证步骤

1. **编译验证**：确保 push constant 大小匹配（shader 32 bytes ↔ C++ 32 bytes）
2. **功能验证**：
   - SDR：chromaScale=1.0 → 输出应与修改前完全相同
   - HDR：chromaScale=1.0 → 输出应与修改前完全相同
   - HDR：chromaScale=0.95 → UI 颜色应比 chromaScale=1.0 时更接近灰色（更低彩度），但亮度不变
3. **视觉匹配实验**：
   - SDR 显示器（ThinkVision）上运行 SDR.exe（chromaScale=1.0）
   - HDR 显示器（ProArt）上运行 HDR.exe（chromaScale 从 1.0 调到 ~0.95-0.97）
   - 对比两侧 UI 的主观彩度是否一致
   - 如果 Hunt Effect 补偿正确，chromaScale≈0.95-0.97 时两侧主观彩度应匹配
4. **边界情况检查**：
   - chromaScale=0 → result=gray（纯灰，零彩度）
   - chromaScale>1 → 彩度增强（可用于验证方向是否正确）
   - BG 区域（uiAlpha=0）→ blended=bgLinear → lum=bgLinear → gray=vec3(bgLinear) → result=bgLinear ✓（BG 不受 chromaScale 影响）

##### 3.4.5 实现优先级

当前计划：先实现 chromaScale 作为**唯一新增调节手段**（不加其他参数），验证一版效果。具体步骤：
1. 修改 ui.frag（增加 chromaScale push constant + chrominance scaling）
2. 修改 vulkan_util.h/cpp（recordUIPass 签名 + push constant 大小）
3. 修改 hdr_app.h/cpp（chromaScale_ 成员 + ImGui DragFloat + 传递参数）
4. 修改 sdr_app.cpp（硬编码 chromaScale=1.0f）
5. 编译、运行、视觉对比

### 3.5 限制与注意事项

1. **Rec.709 luminance ≠ CIECAM02 A**：权重比例相似但不完全相同。luminance-based chroma scaling 在 CIECAM02 意义上是近似操作，不是精确的 a,b 缩放。误差量级估计 < 2%（对于常见 UI 颜色）。

2. **chromaScale 对不同色调的影响不同**：Hunt Effect 对绿色的影响最大、黄色最小（Kawashima 论文）。单一 chromaScale 参数对所有颜色做均匀缩放，无法精确补偿色调依赖的 Hunt Effect 差异。对于 UI 素材中的混合色调场景，chromaScale 是一个折衷参数。

3. **BG 不是纯灰时的边界情况**：当 BG 含色差时（如偏绿的 ProArt），gray = vec3(L) 不是 BG 的精确 achromatic 等价物。此时 chroma scaling 会微量影响 BG 的色差。本项目 BG 为灰色（BG_GRAY=0.18），此问题影响极小。

4. **chromaScale 的物理意义**：此参数是感知补偿，不是物理校正。它人为降低了 HDR 端的彩度以抵消 Hunt Effect，使得 SDR 和 HDR 的主观彩度一致。项目目标已明确为"使主观相对 colorfulness 一致"（§1 目标 #3），因此 chromaScale 是实现该目标的必要手段。

### 3.6 进阶方案：Hue-dependent chromaScale（未来研究方向）

单一 chromaScale 对所有像素均匀缩放 chrominance，无法精确补偿 Hunt Effect 的色调依赖性（§3.5 #2）。如果视觉匹配实验中发现某些 hue（特别是绿色）仍明显偏彩，可升级为 hue-dependent 方案。

##### 3.6.1 原理

将 uniform chromaScale 替换为 hue-dependent 函数：

```
result = gray + chromaScale(h) × (blended - gray)
```

其中 `h` 是该像素的 chrominance hue angle。chrominance 方向不变 → hue 不变，只是按 hue 调整缩放幅度。绿色方向缩放更多，黄色方向缩放更少，匹配 Kawashima 论文的色调依赖数据。

##### 3.6.2 确定 chromaScale(h) 曲线

最严谨方式：用 CIECAM02 离线计算。对一系列代表性 hue（覆盖 0°~360°），分别在 SDR 观视条件（L_A=70, F_L≈0.69）和 HDR 观视条件（L_A=200, F_L≈0.86）下计算 M：

```
r(h) = M_hdr(h) / M_sdr(h)
chromaScale(h) = (1/r(h))^{1/0.9}
```

Kawashima 论文只提供了少数 hue 的实验数据点（绿最大、黄最小），需要 CIECAM02 补全整条 hue 曲线。

##### 3.6.3 Shader 实现思路

```glsl
// 从 blended 计算 luminance-relative 色差（用于 hue 估计）
float lum = dot(blended, vec3(0.2126, 0.7152, 0.0722));
vec3 gray = vec3(lum);
vec3 chromatic = blended - gray;

// 简化 opponent color（近似 CIECAM02 a, b，基于 luminance-relative 色差）
float a_simple = 0.5 * (chromatic.r - chromatic.b);
float b_simple = 0.21 * (2.0 * chromatic.g - chromatic.r - chromatic.b);
float h = atan(b_simple, a_simple);  // hue angle

// hue-dependent chromaScale: parametric curve 或 small lookup table
// 示例：以 baseChromaScale 为基准，正弦调制，peak 在绿色方向
float cs = baseChromaScale + hueAmplitude * cos(h - huePeakAngle);

vec3 result = gray + cs * chromatic;
```

##### 3.6.4 局限性

1. **RGB 空间 hue angle ≠ CIECAM02 hue angle**：简化 a_simple, b_simple 的权重是近似的，与 CIECAM02 精确 hue 有偏差。常见 UI 颢色误差估计 < 5°。

2. **Hunt Effect 也受 stimulus chroma magnitude 影响**：高彩度色和低彩度色在 CIECAM02 中的补偿比例不同（因为压缩函数在不同工作点上斜率不同）。hue-dependent 只解决了色调依赖这一维度，chroma magnitude 依赖是第二维度。

3. **参数化曲线需要离线计算或视觉实验确定**：tuning 成本比单一 chromaScale 高。需要 CIECAM02 离线计算或逐 hue 视觉匹配实验来确定 baseChromaScale、hueAmplitude、huePeakAngle 等参数。

##### 3.6.5 建议路径

1. 先用单一 chromaScale 验证基础效果（§3.4）
2. 如果视觉匹配时某些 hue（特别是绿色）仍明显偏彩，升级为 hue-dependent 方案
3. hue-dependent 曲线先用 CIECAM02 离线计算生成参考值，再通过视觉实验微调

---

## 参考文献

- CIE 159:2004, *A Color Appearance Model for Color Management Systems: CIECAM02*
- Fairchild, M.D., *Color Appearance Models*, 3rd ed., Wiley-IS&T, 2013, Chapter 16
- Kawashima, Y., Ohno, Y., "Change of Perceived Chroma and Hue of Object Colours at Different Lighting Levels Due to Hunt Effect", CIE x047:2020 OP01
- Wikipedia, "CIECAM02" (公式引用已与 CIE 159:2004 交叉验证)

---

## 修改记录

### 2026-06-11 — 首次审查修正（AI 辅助审查）

**1. F_L 数值修正（§2.2）**

原文档表中 F_L 值为 SDR≈0.57、HDR≈1.0。用文档自身列出的 CIECAM02 F_L 公式重新计算：

| $L_A$ | 原值 | 修正值 |
|-----|------|--------|
| 70（SDR 350 nit） | $\approx 0.57$ | **$\approx 0.69$** |
| 200（HDR 1000 nit） | $\approx 1.0$ | **$\approx 0.86$** |

$F_L$ 比值从原文 1.75 修正为 1.24。后续依赖 $F_L$ 比值的推导同步更新。

**2. Hunt Effect 归属修正（§2.2）**

原文档声称 $C_{\text{hdr}} > C_{\text{sdr}}$（"$F_L$ 更大时色差信号幅度更大 $\to$ $t$ 更大 $\to$ $C$ 更大"）。此推理在方向上是合理的，但对 CIECAM02 的具体结论有误：

- CIECAM02 的 $t$ 公式中，分子 $\sqrt{a^2+b^2}$ 和分母 $[R_a'+G_a'+\frac{21}{20}B_a']$ 均随 $F_L$ 近似线性缩放，比值近似不变 $\to$ **$t \approx$ 常数 $\to$ $C \approx$ 常数**
- 这是 $C$（chroma）作为"相对彩度"（相对于参考白亮度）的设计本意
- **Hunt Effect 实际体现在 $M$（colorfulness）**：$M = C \times F_L^{0.25}$，$C$ 不变但 $F_L$ 增大 $\to$ $M$ 增大

修正后的对比表：$C$ 在 SDR/HDR 下≈相同，$M_{\text{HDR}} / M_{\text{SDR}} \approx (0.86/0.69)^{0.25} \approx 1.056$（+5.6%）。

此修正不影响 chromaScale 方案的工程正确性（shader 代码相同，补偿目标从"补偿 $C$ 差异"调整为"补偿 $M$ 差异"，数值结果几乎一致）。

**3. chromaScale 初始值更新（§3.3）**

原推导基于"$C_{\text{hdr}} > C_{\text{sdr}}$，$r \approx 1.03\text{-}1.05$"。修正为基于 $M$ 的精确比值：

$$
\frac{M_{\text{hdr}}}{M_{\text{sdr}}} = \left(\frac{F_{L,\text{HDR}}}{F_{L,\text{SDR}}}\right)^{0.25} = 1.056, \quad \text{chromaScale}^{0.9} \approx \frac{1}{1.056} \approx 0.947, \quad \text{chromaScale} \approx 0.94
$$

初始参考值从 **0.95~0.97** 修正为 **0.93~0.96**。

**4. §3.6.3 简化 opponent color 公式修正**

原公式在绝对 RGB 值上计算 opponent color，对非单位 luminance 的颜色 hue 估计有偏。修正为先计算 luminance-relative 色差（chromatic = blended - gray），再在色差上计算 opponent color，使 hue 估计在亮度变化时保持稳定。

**5. §3.6.2 $F_L$ 数值同步更新**

SDR $F_L$ 从 0.57 更新为 0.69，HDR $F_L$ 从 1.0 更新为 0.86。

### 2026-06-11 — LaTeX 公式排版

将文档中所有纯数学公式从 plain text code block 转换为 LaTeX 数学公式（GitHub Markdown 兼容的 `$...$` 和 `$$...$$` 语法）。涉及的公式包括：

- §2.1 CIECAM02 核心公式（$F_L$、锥体压缩、$J$、$Q$、$C$、$M$、$s$）
- §2.3.1~2.3.2 blending 公式、Lock 机制公式
- §2.3.3 Step 1~8 完整推导链中的所有数学公式
- §3.1~3.2 chromaScale 原理公式
- §3.3 chromaScale 目标值推导
- 修改记录中的公式

代码块（`glsl`、`cpp` 等带语言标识的 block）保持不变，因为它们是代码而非数学公式。
