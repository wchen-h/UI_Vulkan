# UI 跨背景亮度一致性算法 — 改进路径

> 问题陈述：SDR/HDR 双窗口 UI 匹配实验中，希望从 HDR 背景中提取的统计量能概括"背景对半透明 UI 的感知影响"，使得一组 UI 亮度/透明度匹配参数在不同色彩组成的背景图片间仍然有效。
>
> 当前一阶近似：控制 HDR 背景的 `avg(Y)`（YUV 分支）或 `avg(I)`（ICtCp 分支），假定均值相同时背景对 UI 的感知影响相同。

---

## 目录

1. [第一阶修正：局部加权与分位数](#1-第一阶修正局部加权与分位数)
2. [第二阶修正：色度方向补偿 H-K 效应](#2-第二阶修正色度方向补偿-h-k-效应)
3. [第三阶修正：局部对比度与纹理 Masking](#3-第三阶修正局部对比度与纹理-masking)
4. [数据驱动方法：从统计量到回归模型](#4-数据驱动方法从统计量到回归模型)
5. [参考：各方案对比总表](#5-参考各方案对比总表)

---

## 1. 第一阶修正：局部加权与分位数

### 1.1 UI-Alpha 加权的局部统计（推荐优先实现）

**当前做法：** `computeLocalAvgI()` 对 UI quad 的 2× 外接矩形做均匀像素平均。

**问题：** UI 纹理有 alpha mask。背景被 UI 完全遮盖的区域不影响混合结果；UI 不覆盖的区域也不参与。均匀平均混入了无关像素。

**改进：** 只统计 UI 实际覆盖的像素，用 texAlpha 推导**背景透视权重**：

```cpp
double totalI = 0.0;
double totalWeight = 0.0;
for (每个位于 UI 区域的像素) {
    float uiAlpha = ui.rawAlpha[uiY * ui.width + uiX];
    if (uiAlpha == 0) continue;   // 完全透明，不参与
    
    // 透视权重：背景在混合中的贡献比例 = (1 - effAlpha)
    // 粗略近似直接用 1.0 - uiAlpha
    float weight = 1.0f - uiAlpha;
    if (weight <= 0.0f) continue; // 不透明区域背景不显示
    
    totalI += I_pixel * weight;
    totalWeight += weight;
}
avgI_effective = totalWeight > 0 ? totalI / totalWeight : 0.0f;
```

**优势：** 改动局限在 `computeLocalAvgI()` 的循环内，不涉及 shader。CPU 端计算量增加 ~10%，可接受。

**预期收益：** 高。排除了大量不影响 UI 的背景区域，使 `bgMultiplierI` 更精准地作用于 UI 实际感知的背景部分。

### 1.2 I 分位数模型（P10, P50, P90）

**当前做法：** `avg(I)` 一个标量。

**改进：** 统计 I 在 UI 区域内的分布特征——用分位数替代平均值。

**直觉：** 半透明 UI（texAlpha ≈ 0.3）主要"透过"背景的暗部看出去。不透明 UI 几乎不关心背景。两张图 `avg(I)` 相同但暗部/亮部差异很大的时候，半透明 UI 的感知完全不一样。

```cpp
// 收集 UI 区域内所有像素的 I 值
std::vector<float> I_values;
for (每个位于 UI 区域的像素) {
    if (uiAlpha > 0) I_values.push_back(I);
}
// 排序后取分位数
std::sort(I_values.begin(), I_values.end());
float P10  = I_values[(int)(0.10 * I_values.size())];
float P50  = I_values[(int)(0.50 * I_values.size())];  // 中位数
float P90  = I_values[(int)(0.90 * I_values.size())];
float mean = I_values.size() > (int)0 ? I_values.size() : 0.0f;
```

**如何用：** 记录每个匹配点的 `(P10, P50, P90, alpha_match, scale_match)`。数据积累后可以观察：
- 半透明 UI（texAlpha 小）的匹配是否与 P10 相关更强？
- 不透明 UI（texAlpha 大）的匹配是否与 P50/P90 相关更强？

**实现成本：** 低。CPU 端加一个 `vector::push_back` 和 `std::sort`。

### 1.3 同时记录的辅助统计量

建议在每次匹配记录中加入以下变量（不改 shader，只在 CPU 端追加记录）：

```
localAvgI_     // 当前均值（保持不变）
avgI_weighted  // UI-alpha 加权均值 (#1.1)
P10_I, P50_I, P90_I  // I 分位数
avgCt, avgCp   // 色度均值
stdCt, stdCp   // 色度标准差
```

---

## 2. 第二阶修正：色度方向补偿 H-K 效应

### 2.1 理论背景：Helmholtz-Kohlrausch (H-K) 效应

人眼对**彩色亮度的感知高于**同亮度中性色。饱和度越高的颜色看起来越亮。

ICtCp 的 I 分量 `= 0.5·L' + 0.5·M'` 完全忽略 S 锥体贡献（系数 0）。因此，**高饱和蓝紫色背景的感知亮度被 I 低估**——等 I 下高饱和背景看起来更亮。

### 2.2 方案一：I 的 H-K 补偿项（推荐）

利用已有 ICtCp 的 Ct、Cp 分量估算色度幅度，对 I 做经验修正：

```cpp
// 在 CPU 统计循环中
float saturation = sqrt(Ct*Ct + Cp*Cp);      // 色度幅度 [0, ~0.5]
float hkFactor = 1.0f + k_hk * saturation;    // k_hk 为经验系数
float perceivedI = I * hkFactor;
```

**系数 k_hk 的标定：** 可在实验中让被试匹配一组饱和度渐变的纯色块与中性灰色块的亮度感知，拟合最佳 `k_hk`。预估取值范围 0.05–0.3。

**优势：** 不修改 shader，不改变 ICtCp 管线。CPU 端对每个像素多算一次 `sqrt(Ct² + Cp²)`。

### 2.3 方案二：扩展 I' 重新定义（激进，不建议）

在 shader 中修改 M2 矩阵的 I 行，加入少量 S' 贡献：

```glsl
// 替代标准 M2 的第一行（仅为实验用）
const mat3 LMS2ICTCP_EXP = mat3(
    0.50,     1.6138,   4.3782,    // I' = 0.5*L' + 1.613*M' + 4.378*S'
    0.50,    -3.3235,  -4.2456,    // Ct 不变
    0.00,     1.7097,  -0.1326);   // Cp 不变
```

上例中 I' 给 S' 加了一个 4.378/4096 = 0.00107 的小系数（仅用单精度时非常微小）。要产生实际效果需大幅调整。**但任何对 M2 的改动都使结果无法与标准 ICtCp 文献对比，不推荐。**

---

## 3. 第三阶修正：局部对比度与纹理 Masking

### 3.1 问题：Contrast Masking

人眼对叠加在高纹理内容（如草地、树叶、复杂图案）上的透明度变化不敏感——视觉系统将纹理视为"噪声"，对叠加在其上的半透明信号阈值升高。

这是**与背景有关、但和平均亮度无关**的效应。两张图 `avg(I)` 相同且色度相同，但如果一张是平滑的天空、另一张是密集的树叶纹理，同一组 UI（alpha, scale）在两者上感知差异巨大。

### 3.2 局部标准差 std(I)（推荐）

最简单的 masking 度量：在 UI 区域内计算 I 的局部标准差（或平均绝对差）。

```cpp
// 第一遍：算平均
double sum = 0;
for (像素) sum += I;
avg = sum / count;

// 第二遍：算标准差
double sumSq = 0;
for (像素) sumSq += (I - avg) * (I - avg);
float stdI = sqrt(sumSq / count);
```

**如何用：** 加入匹配模型：
```
alpha_match = f(avg(I), stdI, ...)
scale_match = g(avg(I), stdI, ...)
```

数据验证预测：`stdI` 越高 → contrast masking 越强 → 半透明 UI 的可见性降低 → 同样的 alpha 产生更低的主观透明度 → 需要更大的 alpha 或 scale 才能匹配 SDR 参考。

### 3.3 频带分解（更精细，高阶）

将背景分解为低频（亮度/轮廓层）和高频（纹理/细节层），分别分析：

```
I_low = 高斯模糊(I, σ ≈ UI_元素尺寸)
I_high = I - I_low
```

- **低频 I_low**：决定整体亮度匹配 → 用 `avg(I_low)` 修正 bgMultiplierI
- **高频 I_high**：决定 masking 强度 → 用 `rms(I_high)` 修正 alpha

**实现方式：** CPU 端对 raw RGBA 图像做简单 box blur 或小核高斯，然后在两个分量上分别统计。不改 shader。

**预期收益：** 对游戏场景（sky → 低频主导，foliage → 高频主导）有区分能力。但计算成本较高（需要 CPU 卷积），是否值得取决于数据。

---

## 4. 数据驱动方法：从统计量到回归模型

> 前面所有方案都需要预设变量形式和系数。既然实验已经在采集主观匹配数据，**让数据自己回答"什么参数重要"**可能更有效。

### 4.1 特征工程

对每张背景图 × 每个 bgNit 值，提取一个特征向量：

```
F = [avg(I), avgI_weighted,          ← 一阶亮度
     P10(I), P50(I), P90(I),         ← 分布分位数
     std(I),                          ← 对比度
     avg(Ct), avg(Cp),               ← 色度均值
     avg(saturation),                ← 饱和度均值 (sqrt(Ct²+Cp²))
     avg(hk_adjusted_I)              ← H-K 补偿 I (#2.2)
     local_contrast = P90 - P10,     ← 简单对比度范围
     pix_per_mm_monitor,             ← 显示器物理因素
     screen_luminance_max]           ← 峰值亮度
```

这些特征的获取全部不改变现有 Vulkan 渲染管线，只需在 `computeLocalAvgI()` 中追加计算。

### 4.2 回归模型

当积累了 N 个样本（N ≈ 30–50 对不同背景 × bgNit 的匹配参数）后，拟合：

```
alpha_match = β₀ + β₁·avgI + β₂·stdI + β₃·avgCt + β₄·hkFlag ...
I_scale_match = γ₀ + γ₁·avgI + γ₂·stdI + ...
```

**模型选择（推荐顺序）：**
1. **多元线性回归** — 最简单、可解释、过拟合概率低。O(N) 样本即可
2. **Lasso/Ridge 正则化回归** — 自动做特征选择，稀疏出最重要的维度
3. **小型随机森林**（可选）— 当样本量够大（N > 200）且发现非线性关系时

**关键：** 如果线性回归发现 `avgI` 的系数显著、其他特征的系数不显著（置信区间跨零），那就验证了一阶近似的充分性。如果其他特征显著，就获得了更好的可推广模型。

### 4.3 何时开始

如果现在开始在 `computeLocalAvgI()` 中记录上述统计量（写到控制台或文件），对现有实验流程**零影响**。等数据够了自然可以做第 2 轮分析。

---

## 5. 参考：各方案对比总表

| 方案 | 实现成本 | 需改 Shader | CPU 额外开销 | 预期增益 | 推荐优先级 |
|:----|:--------|:----------|:-----------|:--------|:---------|
| **1.1 UI-alpha 加权平均** | 低 | 否 | ~10% 循环 | 高 | 1 |
| **1.2 I 分位数 (P10/P50/P90)** | 低 | 否 | 需排序 | 中高 | 2 |
| **1.3 辅助统计量记录** | 低 | 否 | 几乎无 | 为回归铺垫 | 配合 1.2 做 |
| **2.2 H-K 补偿 I** | 低 | 否 | 少量 sqrt | 中 | 3 |
| **3.2 局部标准差 std(I)** | 低 | 否 | 第二遍遍历 | 中 | 3 |
| **3.3 频带分解** | 中高 | 否 | CPU 卷积 | 中（场景相关） | 按需 |
| **4.2 回归建模** | 中 | 否 | 事后分析 | 最高 | 长期方向 |
| **2.3 改 M2 扩展 I'** | 中 | 是 | 无 | 低（偏离标准） | 不推荐 |

### 推荐实施路线

```
Phase 1（下一步）: 1.1 + 1.2 → 改动最小，收益最高
Phase 2（数据积累）: 1.3 作为记录项 → 边实验边收集
Phase 3（分析）: 3.2 + 4.2 → 数据够了做回归
Phase 4（需要时）: 2.2 + 3.3 → 针对特定场景做精细补偿
```
