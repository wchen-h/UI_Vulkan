# 混合画面 UI 补偿方案框架

> 文档版本：v3.0 (框架)
> 编写时间：2026-08-24
> 状态：待审查
> 性质：实验指导框架，非最终规范。自顶向下，实验→反馈→拆解→细化→调整→回归。

---

## 0. 背景与约束

### 0.1 问题

现有 pipeline 假定能获得分离的 UI RGB + alpha + HDR 背景。实际工程中某些游戏只能提供：
- **混合画面** (bg + UI 已在 sRGB 或 linear 域 alpha blending 的 SDR 帧)
- **UI bounding box** (UI 在画面中的位置)
- **混合域标志** (游戏在 sRGB 域还是 linear 域混合，已知参数)

无法获得：分离的 UI RGB、UI alpha、不含 UI 的背景帧。

### 0.2 约束

| 约束 | 说明 |
|------|------|
| 无背景参考 | 不能获得不含 UI 的背景帧 |
| 无 UI 颜色 | 不能获得分离的 UI RGB |
| 无 UI alpha | 不能获得分离的 UI alpha |
| 混合域已知 | sRGB 或 linear，作为输入参数 |
| 调节域固定 | 补偿始终在 linear nits 域进行 |
| 调节目标 | 由混合域决定：sRGB 域混合的游戏匹配 sRGB blend 外观；linear 域混合的游戏匹配 linear blend 外观 |

### 0.3 可用信息源

| 信息 | 来源 | 可靠性 |
|------|------|--------|
| 混合像素值 M | SDR bin 直接读取 | 精确 |
| UI 区域位置 (bbox) | 游戏 UI 布局系统 | 精确 |
| bbox 外背景 | SDR bin (bbox 外 = 纯背景) | 精确 |
| 混合域 | 已知参数 | 精确 |
| 时域多帧 | 游戏连续输出 | 精确 (每帧 M 精确) |
| 不透明 UI 像素 | 时域求差 (ΔM≈0) | 可靠 (需多帧) |
| 半透明 UI 像素 | 时域求差 (ΔM 小且非零) | 可靠 (需多帧) |
| 背景像素 | 时域求差 (ΔM 大) | 可靠 (需多帧) |

---

## 1. 总体架构

### 1.1 两层方案

```
                         混合 SDR bin + bbox + 混合域标志
                                    │
                         ┌──────────┴──────────┐
                         │  时域求差: 像素分类    │  (共享前置)
                         │  (不透明/半透/背景)   │
                         └──────────┬──────────┘
                           ┌────────┴────────┐
                           │                 │
                    基础方案              进阶方案
                    直接调节              分离调节
                  ┌─────────┐      ┌──────────────────┐
                  │ 不区分   │      │ 时域回归求解      │
                  │ 不透明/  │      │ alpha + UI 颜色   │
                  │ 半透明   │      │ → eff + Y-Scale   │
                  │ 统一调   │      │ → 线性域重构      │
                  │ 混合像素 │      └──────────────────┘
                  │ 亮度    │
                  └─────────┘
```

### 1.2 方案关系与时域过渡

- **基础方案**是进阶方案的子集：共享时域求差分类，但不做 alpha/UI 颜色估计，统一调节亮度
- **进阶方案**在基础方案之上叠加：对半透明 UI 做时域回归，分离出 alpha 和 UI 颜色后按现有曲线补偿
- 两方案**不是非此即彼**，而是**时域过渡**关系：游戏启动初期无历史帧，只能用基础方案；积累足够帧后进阶方案收敛，逐步过渡到进阶方案
- 过渡期间两方案共存，按区域和时域权重混合输出，避免用户感知到 UI 突变

详见 §1.4。

### 1.3 共享前置：时域求差像素分类

每帧与上一帧做差，按 ΔM 分类：

```
ΔM(t) = |M_t - M_{t-1}|    (per pixel, max channel)

ΔM < θ_quiet    → 不透明 UI  (UI 颜色不变, ΔM≈0)
θ_quiet ≤ ΔM < θ_bg  → 半透明 UI  (背景变化被 (1-α) 衰减)
ΔM ≥ θ_bg       → 背景        (背景直接变化)
```

**[可优化-1]** θ_quiet, θ_bg 阈值设定：手动 vs Otsu 自适应 vs 基于局部统计

**[可优化-2]** 多帧差分策略：仅与上一帧差 vs 滑动窗口内方差 vs 与滑动均值差

> 此分类每帧执行，用于：
> 1. 基础方案：确定 UI 区域范围（不透明 + 半透明）
> 2. 进阶方案：区分不透明 UI（直接取颜色）和半透明 UI（需时域回归）
> 3. UI 变化检测：某区域分类突变 = UI 内容变化

### 1.4 基础→进阶时域过渡

#### 1.4.1 问题

游戏刚启动时没有历史帧，时域回归无数据可用，只能用基础方案（Y-boost）统一调节。经过一段时间（例如 10 秒）后，半透明 UI 的 alpha 和颜色回归收敛，进阶方案可生效。如果直接切换，用户会感知到 UI 亮度/不透明度的突变。因此需要在基础方案和进阶方案之间做**时域平滑过渡**。

#### 1.4.2 三阶段状态机

```
游戏启动
    │
    ▼
┌──────────────────────────┐
│ Stage 0: 纯基础           │  所有 UI 像素: Y-boost
│ (0 ~ T_converge)         │  累积时域回归数据
│                          │  每帧检查各区域是否收敛
└──────────┬───────────────┘
           │ 首个区域收敛 (t = t_c1)
           ▼
┌──────────────────────────┐
│ Stage 1: 混合过渡         │  已收敛区域: w(t) 混合 base + advanced
│ (t_c1 ~ t_last+T_blend) │  未收敛区域: 纯 base
│                          │  继续累积未收敛区域数据
│                          │  对已过渡区域: 纯 advanced + 缓存
└──────────┬───────────────┘
           │ 所有区域收敛且过渡完成
           ▼
┌──────────────────────────┐
│ Stage 2: 纯进阶           │  所有 UI 像素: advanced (缓存 α/ui)
│ (稳态)                   │  不做 bg 估计和回归 (定期校验除外)
└──────────┬───────────────┘
           │ 某区域 UI 变化检测
           ▼
    该区域回 Stage 0/1 (重新采集+过渡)
    其他区域保持 Stage 2
```

**关键设计：每个区域独立推进状态**。不同 UI 区域背后的背景变化不同，收敛时间不同。大 UI 元素覆盖变化背景的收敛快；小 UI 元素在静态背景上的可能很久不收敛。

#### 1.4.3 区域级过渡混合

对某区域 R，在收敛时刻 t_c(R) 后的 T_blend 帧内，混合基础和进阶输出：

```
HDR_out_R(t) = w_R(t) × HDR_advanced_R(t) + (1 - w_R(t)) × HDR_base_R(t)
```

其中 w_R(t) 为过渡权重，从 0（纯基础）平滑过渡到 1（纯进阶）：

```
w_R(t) = smoothstep(0, 1, (t - t_c(R)) / T_blend)

smoothstep(x) = 3x² - 2x³    (x clamp [0,1])
```

**smoothstep 的性质**：
- 起点和终点处导数为 0 → 无视觉突变
- 比线性 ramp 更平滑
- T_blend 控制过渡时长（如 60 帧 = 2 秒 @30fps）

**[可优化-8]** T_blend 设定：太短→突变可感知；太长→进阶方案生效太慢。需主观测试确定合理范围。

**[可优化-9]** 过渡权重函数：smoothstep vs cosine ramp vs 线性。验证标准：过渡期间用户是否感知突变。

#### 1.4.4 区域定义

"区域" = 时域分类中连续的半透明 UI 像素块。同一区域内的像素：
- 共享同一组 (α, ui)（时域回归时联合求解）
- 共享同一收敛时刻 t_c
- 共享同一过渡权重 w(t)

**[可优化-10]** 区域划分粒度：连通域级别 vs 固定网格 vs 自适应分块。影响回归数据量和过渡平滑度。

#### 1.4.5 不收敛区域处理

某些区域背景几乎不变（bg_t ≈ 常数），时域回归无法定斜率，永远不收敛。处理策略：

| 策略 | 做法 | 优点 | 缺点 |
|------|------|------|------|
| 保持纯基础 | 该区域始终用 Y-boost | 无风险 | 半透明 UI 补偿不足 |
| 强制收敛 | 数据不足时用假设值 (如 α=0.5) 初始化 | 有进阶补偿 | 假设值可能不准 |
| 空间借用 | 从邻近已收敛区域借用 α/ui | 利用空间相关性 | 邻近区域颜色可能不同 |

**[可优化-11]** 不收敛区域策略选择。验证标准：该区域 Y 差 vs ground truth。

#### 1.4.6 UI 变化时的过渡回退

当某区域检测到 UI 变化（ΔM 突变超过阈值），该区域需要重新采集数据。过渡策略：

```
检测到 UI 变化:
  1. 清除该区域的缓存 (α, ui)
  2. 该区域回退到 Stage 0 (纯基础)
  3. 重新累积时域数据
  4. 收敛后重新过渡到 Stage 2
```

**回退过渡**：回退时也要平滑过渡，避免从进阶直接跳回基础造成突变：

```
if UI_change_detected(region R):
    w_R ← 0  (重置过渡权重)
    HDR_out_R = w_R × advanced + (1-w_R) × base
    # w_R = 0 时即为纯 base, 自然过渡
    # 然后重新采集, 收敛后 w_R 再从 0 → 1
```

**[可优化-12]** 回退速度：快速回退（直接 w=0）vs 缓慢回退（w 从当前值 ramp 到 0）。验证标准：回退期间是否感知突变。

---

## 2. 基础方案：直接调节

### 2.1 方法

不区分不透明/半透明 UI，对 UI 区域（时域分类为不透明+半透明的像素）统一施加亮度调节。

**调节手段：Y-boost**

```
对 UI 区域每个像素:
  1. M_nits → PQ encode → 10-bit → YCbCr
  2. Y × Y-Scale(B, fy)           (Y-Scale 基于 B, 同现有 adjust_ui.py)
  3. YCbCr → RGB → PQ → 逆系统色调映射 (同现有 adjust_ui.py 步骤6)
  4. → PQ encode → A2B10G10R10

对非 UI 区域:
  M_nits → PQ encode (标准 SDR→HDR, 不施加 Y-Scale / 逆映射)
```

**效果**：
- 不透明 UI：Y-Scale + 逆映射 = 正确补偿（混合值 = UI 值，直接调 = 调 UI）
- 半透明 UI：部分补偿（混合值含 bg 贡献，调节同时影响 bg 和 UI）
- 背景：不施加调节（UI 区域外的纯背景标准转换）

**限制**：
- 无 eff 补偿（不调整不透明度，只调亮度）
- 半透明 UI 的背景部分被错误施加 Y-Scale 和逆映射
- sRGB 域混合游戏：M_nits ≠ linear blend，调节有偏差

### 2.2 实验方案

| 项目 | 内容 |
|------|------|
| 输入 | SDR 混合 bin (lkwg, 5帧) + bbox (从 alpha.png 提取) |
| 混合域 | sRGB (lkwg 已知) |
| 方法 | Y-boost on UI 区域 |
| 输出 | HDR bin (5帧) |
| Ground truth | 现有 pipeline (adjust_ui + blend_ui) 输出 |
| 编码 | make_video.py → HDR10 MP4 |

**评估**：
- bbox 内逐像素 Y 差 (nits) vs ground truth
- bbox 外逐像素 Y 差 (应 ≈ 0，验证不污染背景)
- 不透明 UI 区域 Y 差 (应小，验证不透明 UI 补偿正确)
- 半透明 UI 区域 Y 差 (评估半透明补偿不足程度)
- 主观视觉并排比较

### 2.3 预期结果

- 不透明 UI 区域：Y 差小（< 5 nit），Y-boost 对不透明 UI 等价于现有 pipeline
- 半透明 UI 区域：Y 差中等（10-30 nit），Y-boost 无法精确补偿
- bbox 外：Y 差 ≈ 0
- 主观：不透明 UI 一致性较好，半透明 UI 可能偏亮或偏暗

### 2.4 基础方案的色彩度补充 [待评估]

Y-boost 只调 Y 通道（亮度），Cb/Cr 不变。是否需要对 UI 区域补充色彩度调节？

**[可优化-3]** 色彩度调节：是否需要 + 如何设计 + 增益/劣化判定

---

## 3. 进阶方案：时域回归分离调节

### 3.1 方法概述

在基础方案的时域分类基础上，对半透明 UI 像素利用多帧时域信息，通过线性回归同时求解 alpha 和 UI 颜色，再按现有补偿曲线（eff + Y-Scale + 逆映射）处理。

```
时域分类 → 不透明 UI: M = ui (直接取颜色)
         → 半透明 UI: 时域回归 → (alpha, ui) → eff + Y-Scale → 重构
         → 背景: 标准 SDR→HDR
```

### 3.2 时域回归求解 alpha 和 UI 颜色

#### 3.2.1 数学推导

每帧每通道的混合方程（以 linear 域为例）：

```
M_t(c) = ui(c) · α + bg_t(c) · (1 - α)     c ∈ {R, G, B}, t = 1..N
```

这是关于 bg_t(c) 的线性方程：

```
M_t(c) = [α · ui(c)] + (1 - α) · bg_t(c)
          ↑截距 A(c)    ↑斜率 B
```

三通道独立回归，斜率 B 三通道应一致（alpha 是标量）：

```
α = 1 - mean(B_R, B_G, B_B)
ui(c) = A(c) / α
```

> sRGB 域混合游戏：在 sRGB 域做回归，M_srgb 对 sRGB(bg_t) 回归，截距 = α·sRGB(ui)，解出 sRGB(ui) 后转 linear。

#### 3.2.2 bg 估计

回归需要 bg_t(c)：bbox 内背景值，被 UI 遮挡，需估计。

**方法：border_extrapolate**（与现有方案一致）
- bbox 四边外侧取 K 行/列纯背景像素
- 行插值 (上→下) + 列插值 (左→右) 双线性融合

**[可优化-4]** bg 估计方法选择：
- border_extrapolate（当前默认）
- uniform（四边均值填充）
- inpaint（OpenCV，精度高但重）
- 时域外推（用背景区域时域变化模式外推到 UI 区域）
- 纯时域（不用空间外推，仅依赖回归中的 bg_t 变化）

**验证标准**：对比估计 bg 与 ground truth (HDR bin 转 SDR)，评估 MAE 和相对误差。

#### 3.2.3 回归数据采集

```
for each frame t:
    ΔM(t) = |M_t - M_{t-1}|  (时域求差, 共享前置)
    classify pixel: opaque / semi / bg

    if pixel is semi-transparent:
        bg_t = estimate_bg(frame_t)          [可优化-4]
        Δbg = |bg_t - bg_{t-1}|
        if Δbg > θ_bg_select:               [可优化-5]
            add (M_t, bg_t) to regression dataset
        recompute regression
        check convergence                   [可优化-6]
        if converged: cache (α, ui), stop bg estimation
```

**[可优化-5]** 选择性帧筛选策略：
- 当前方案：Δbg > θ_bg_select 才入选
- 待对比：全帧入选 vs 选择性入选，评估回归精度和计算效率
- 验证标准：回归 R²、α 估计 MAE、计算时间

**[可优化-6]** 回归质量判定与收敛标准：
- R² (三通道平均) > 0.9
- 斜率一致性 max(B) - min(B) < 0.03
- α 估计最近 K 帧变化 < 0.02
- ui 估计最近 K 帧变化 < 0.03
- 待验证：这些阈值是否合理，是否需要动态调整

### 3.3 补偿与重构

获得 (α, ui) 后，按现有补偿曲线处理：

```
1. eff = eff_neutral / eff_black / eff_neutral + Δ(B)  (复用 adjust_ui.py)
2. HDR_ui = ui → sRGB→linear→×350→BT.2020→PQ→Y-Scale→逆映射 (复用 adjust_ui.py 步骤5+6)
3. HDR_bg = bg_est → sRGB→linear→×350→BT.2020  (估计背景的 linear nits)
4. HDR_out = HDR_ui × eff + HDR_bg × (1 - eff)  (线性 nits 域)
```

**[可优化-7]** sRGB 域混合的 alpha 映射：
- linear 域混合游戏：α 直接用
- sRGB 域混合游戏：需 sRGB→linear alpha 映射（现有映射表仅黑色 UI）
- 待验证：非黑色 UI 的映射关系，是否可从回归结果推导

### 3.4 实验方案

| 项目 | 内容 |
|------|------|
| 输入 | SDR 混合 bin (lkwg, 5帧) + bbox + 混合域标志 |
| 方法 | 时域回归 (5帧) → (α, ui) → eff + Y-Scale → 重构 |
| 输出 | HDR bin (5帧) |
| Ground truth | 现有 pipeline 输出 + 真实 alpha.png + 真实 rgb.png |
| 编码 | make_video.py → HDR10 MP4 |

**评估**：
- alpha 估计精度：MAE vs alpha.png ground truth
- UI 颜色估计精度：MAE vs rgb.png ground truth
- 端到端 Y 差 vs ground truth pipeline
- vs 基础方案对比（半透明区域 Y 差是否显著改善）

> **注意**：lkwg 仅 5 帧，时域回归数据量可能不足。需评估 5 帧是否够回归稳定。如不足，考虑合成更多帧或扩展数据采集。

---

## 4. 优化点清单

| 编号 | 优化点 | 所属 | 当前状态 | 验证标准 |
|------|--------|---------|---------|---------|
| [可优化-1] | 时域分类阈值 θ_quiet, θ_bg | 共享 | 手动设定 | 分类准确率 vs ground truth alpha |
| [可优化-2] | 多帧差分策略 | 共享 | 仅相邻帧差 | 分类准确率, UI 变化检测灵敏度 |
| [可优化-3] | 色彩度调节 (Cb/Cr) | 基础 | 未实现 | UI 色彩保真度 vs 无色彩度调节 |
| [可优化-4] | bg 估计方法 | 进阶 | border_extrapolate | bg MAE vs ground truth, 回归 R² |
| [可优化-5] | 选择性帧筛选 | 进阶 | Δbg > 阈值 | 回归精度 (α MAE), 计算时间 |
| [可优化-6] | 回归质量判定与收敛 | 进阶 | 4 项指标 + 阈值 | 收敛帧数, 假收敛率, α MAE |
| [可优化-7] | sRGB→linear alpha 映射 (非黑色) | 进阶 | 仅黑色映射表 | alpha 映射 MAE, 端到端 Y 差 |
| [可优化-8] | T_blend 过渡时长 | 过渡 | 未设定 | 过渡期间用户是否感知突变 |
| [可优化-9] | 过渡权重函数 | 过渡 | smoothstep | 过渡平滑度, 主观评估 |
| [可优化-10] | 区域划分粒度 | 过渡 | 连通域 | 回归数据量, 过渡平滑度 |
| [可优化-11] | 不收敛区域策略 | 过渡 | 未实现 | 该区域 Y 差 vs ground truth |
| [可优化-12] | 回退速度 (UI 变化时) | 过渡 | 快速 (w=0) | 回退期间是否感知突变 |
| [可优化-13] | 多像素联合回归 | 进阶 | 未实现 | α MAE, 回归 R², 计算时间 |
| [可优化-14] | UI 变化检测策略 | 进阶 | ΔM 突变检测 | 检测率, 误检率, 恢复时间 |
| [可优化-15] | 定期校验策略 | 进阶 | 每 K 帧验算 | 漏检率, 校验开销 |
| [可优化-16] | 缓存生命周期管理 | 进阶 | 收敛后缓存 | 缓存命中率, 计算节省比 |
| [可优化-17] | 滑动窗口大小 | 进阶 | 30-60 帧 | 回归稳定性 vs UI 变化响应速度 |

---

## 5. 验证标准

### 5.1 alpha 估计精度

| 指标 | 计算 | 合格标准 |
|------|------|---------|
| MAE | `mean(|α_est - α_gt|)` over 半透明 UI 像素 | < 0.05 |
| 分段 MAE | α_gt ∈ [0.1,0.2), [0.2,0.3) ... 各段 | < 0.08 |
| 直方图 | α_est vs α_gt 分布 | 峰值对齐 |
| 散点图 | α_est (y) vs α_gt (x) | y ≈ x |

### 5.2 UI 颜色估计精度

| 指标 | 计算 | 合格标准 |
|------|------|---------|
| sRGB MAE | `mean(|û_srgb - ui_gt_srgb|)` per channel | < 0.03 |
| 色差 ΔE | CIELAB ΔE between û and ui_gt | < 3 |

### 5.3 端到端 HDR 输出质量

| 指标 | 计算 | 合格标准 |
|------|------|---------|
| 全图 Y 差 | `mean(|Y_out - Y_gt|)` | < 15 nit |
| 全图 Y 相对差 | `mean(|Y_out - Y_gt| / max(Y_gt, 1))` | < 10% |
| bbox 内 Y 差 | 仅 UI 区域 | < 25 nit |
| bbox 外 Y 差 | 仅非 UI 区域 | < 5 nit |
| 不透明 UI Y 差 | 仅不透明像素 | < 5 nit (两方案都应好) |
| 半透明 UI Y 差 | 仅半透明像素 | 基础 < 30 nit, 进阶 < 15 nit |

### 5.4 方案对比

| 对比 | 目的 |
|------|------|
| 基础 vs 进阶 (半透明区域) | 验证进阶方案在半透明 UI 的增益 |
| 基础 vs 现有 pipeline (不透明区域) | 验证基础方案对不透明 UI 的等价性 |
| 进阶 vs 现有 pipeline (全区域) | 验证进阶方案整体精度 |

### 5.5 计算效率

| 指标 | 说明 |
|------|------|
| 每帧处理时间 | 采集期 vs 缓存期分别统计 |
| 内存占用 | 回归数据 + bg 估计缓存 |
| 缓存命中率 | 缓存帧数 / 总帧数 |
| bg 估计调用次数 | 采集期 + 校验期 |

### 5.6 主观视觉评估

- 并排播放：现有 pipeline HDR 视频 vs 基础方案 HDR 视频 vs 进阶方案 HDR 视频
- 重点观察：不透明 UI 亮度一致性、半透明 UI 透明度一致性、UI 边缘自然度、背景不受污染

---

## 6. 实验进度规划

### Phase 1: 基础方案实现与验证

| 步骤 | 内容 | 产出 |
|------|------|------|
| 1a | common.py 新增函数 (read_sdr_bin, linear_to_srgb 等) | 代码 |
| 1b | 实现时域求差分类 | 分类结果图, 分类准确率 |
| 1c | 实现 Y-boost (基础方案) | HDR bin 输出 |
| 1d | 评估: vs 现有 pipeline, 分区域统计 | 评估报告 |
| 1e | 主观视觉评估 | 并排视频 |

**Phase 1 产出**：基础方案可用, 不透明 UI 验证通过, 半透明 UI 不足程度量化

### Phase 2: 进阶方案实现

| 步骤 | 内容 | 产出 |
|------|------|------|
| 2a | 实现 bg 估计 (border_extrapolate) | bg 估计图, bg MAE |
| 2b | 实现时域回归 (alpha + ui 求解) | α/ui 估计, MAE |
| 2c | 实现补偿与重构 (eff + Y-Scale + 重构) | HDR bin 输出 |
| 2d | 评估: vs 现有 pipeline + vs 基础方案 | 评估报告 |
| 2e | 主观视觉评估 | 并排视频 |

**Phase 2 产出**：进阶方案可用, alpha/ui 估计精度达标, 半透明 UI 改善量化

**Phase 2 风险**：lkwg 仅 5 帧, 时域回归可能数据不足。需评估或扩展数据。

### Phase 3: 逐项优化

按优先级逐个验证 [可优化-1] 到 [可优化-17]：

| 优先级 | 优化点 | 理由 |
|--------|--------|------|
| P0 | [可优化-4] bg 估计方法 | 回归精度的最大瓶颈 |
| P0 | [可优化-6] 回归质量判定 | 防止假收敛 |
| P1 | [可优化-5] 选择性帧筛选 | 计算效率 |
| P1 | [可优化-13] 多像素联合回归 | 数据量提升 |
| P1 | [可优化-1] 分类阈值 | 分类准确率 |
| P1 | [可优化-8] T_blend 过渡时长 | 用户体验关键 |
| P2 | [可优化-7] sRGB alpha 映射 | sRGB 域游戏精度 |
| P2 | [可优化-14] UI 变化检测 | 动态 UI 适配 |
| P2 | [可优化-17] 滑动窗口大小 | 回归稳定性 |
| P2 | [可优化-9] 过渡权重函数 | 过渡平滑度 |
| P3 | [可优化-2] 多帧差分策略 | 分类鲁棒性 |
| P3 | [可优化-3] 色彩度调节 | 色彩保真 |
| P3 | [可优化-10] 区域划分粒度 | 回归数据量 |
| P3 | [可优化-11] 不收敛区域策略 | 边界情况 |
| P3 | [可优化-12] 回退速度 | UI 变化体验 |
| P3 | [可优化-15] 定期校验 | 缓存可靠性 |
| P3 | [可优化-16] 缓存生命周期 | 计算效率 |

**每个优化点的验证流程**：
1. 实现优化
2. 在相同测试数据上运行
3. 对比优化前后的 §5 验证标准
4. 判定增益/劣化/无显著差异
5. 如果增益：保留; 如果劣化：回退; 如果无差异：记录, 降低优先级

### Phase 4: 回归验证

在所有保留的优化项组合后，做全面回归验证：
- 全部 §5 验证标准
- 如果有新测试数据（新游戏/新 UI 类型），扩展测试
- 与 Phase 1 基础方案和 Phase 2 初始进阶方案对比，确认净增益

---

## 7. 测试数据

### 7.1 现有数据

| 数据 | 路径 | 用途 |
|------|------|------|
| SDR 混合 bin (5帧) | `bin/hdr_img/SDR_ui_on_lkwg/` | 混合画面输入 |
| HDR 背景 bin (5帧) | `bin/hdr_img/HDR_ui_off_lkwg/` | ground truth 背景 |
| UI alpha.png | `pic/ui/alpha.png` | ground truth alpha + bbox |
| UI rgb.png | `pic/ui/rgb.png` | ground truth UI 颜色 |

### 7.2 数据缺口

| 缺口 | 影响 | 解决方案 |
|------|------|---------|
| 帧数不足 (仅5帧) | 时域回归可能不稳定 | 合成更多帧 / 扩展采集 / 用现有 150 帧数据 |
| 仅 sRGB 域混合 | linear 域混合未测试 | 合成 linear-blended SDR (从 HDR bg + UI rgb/alpha 合成) |
| UI 类型单一 | 通用性未验证 | 后续扩展其他游戏数据 |
| 无动态 UI | UI 变化检测未验证 | 合成 UI 变化序列 (如切换头像) |

### 7.3 linear-blended SDR 合成方法

```python
# 从 HDR bg bin + UI rgb/alpha 合成 linear-blended SDR:
# 1. HDR bg → PQ decode → linear nits BT.2020
# 2. BT.2020 → BT.709 → ÷350 → linear [0,1] (bg_linear)
# 3. UI rgb → srgb_to_linear → ui_linear [0,1]
# 4. UI alpha → alpha [0,1]
# 5. linear blend: M_linear = ui_linear * alpha + bg_linear * (1 - alpha)
# 6. linear → sRGB encode → M_srgb [0,1]
# 7. ×1023 → 10-bit → pack A2B10G10R10 → bin
```

---

## 8. 代码结构

### 8.1 新增文件

| 文件 | 用途 |
|------|------|
| `scripts/common.py` (修改) | 新增 read_sdr_bin, linear_to_srgb, hdr_nits_to_srgb, forward_lut, apply_system_tonemap |
| `scripts/adjust_mixed.py` (新增) | 混合画面补偿主脚本 (基础+进阶双模式) |
| `tests/eval_mixed.py` (新增) | 评估脚本 |
| `tests/gen_linear_blended_sdr.py` (新增) | 合成 linear-blended 测试数据 |
| `tests/gen_route1_diagrams.py` (已有) | 图表生成 |

### 8.2 不修改的文件

| 文件 | 原因 |
|------|------|
| `adjust_ui.py` | 复用其函数, 不改 |
| `blend_ui.py` | 混合场景不需要 |
| `rotate_hdr.py` | SDR bin 如需翻转复用逻辑 |
| `make_video.py` | 直接读 adjust_mixed 输出 |

### 8.3 复用函数

| 函数 | 来源 | 用途 |
|------|------|------|
| find_ui_bboxes / merge_overlapping_bboxes | adjust_ui | bbox 提取 |
| eff_neutral / eff_black / delta_color / yscale | adjust_ui | eff / Y-Scale |
| calculate_inv_ratio / inverse_lut / systemTMOCurve | adjust_ui | 逆色调映射 |
| srgb_to_linear / linear_to_pq / pq_decode | common | 色彩转换 |
| BT709_TO_BT2020 / rgb_to_ycbcr2020 / ycbcr_to_rgb2020 | common | 色域转换 |
| read_hdr_bin / pack_a2b10g10r10 | common | bin 读写 |
| load_config / resolve_path / natural_key | common | config |

### 8.4 config.json 新增

```json
{
  "task_mixed": {
    "sdr_dir": "<SDR mixed bin dir; CLI --sdr-dir>",
    "bbox_png": "<optional, bbox mask for testing>",
    "outdir": "<output HDR bin dir>",
    "blend_domain": "srgb",
    "route": "direct",
    "f": 1.0,
    "fy": 1.0,
    "bg_method": "border_extrapolate",
    "theta_quiet": 0.005,
    "theta_bg": 0.05,
    "regression_window": 30,
    "convergence_r2": 0.9,
    "convergence_slope_consistency": 0.03,
    "convergence_alpha_stability": 0.02,
    "convergence_ui_stability": 0.03,
    "bg_select_threshold": 0.02,
    "verify_interval": 30,
    "verify_drift_threshold": 0.02
  }
}
```

---

## 9. 子任务跟踪

### 9.1 任务总览

| 子任务 | 名称 | 依赖 | 关联优化点 | 测试脚本 | 状态 |
|--------|------|------|-----------|---------|------|
| T1 | 半透明UI直接调节曲线测试 | 无 | [可优化-3] | test_direct_adjust.py | 待开始 |
| T2 | UI和背景区分算法 | 无 | [可优化-1][可优化-2] | test_temporal_classify.py | 待开始 |
| T3 | 选择性帧筛选和bg估计 | T2 | [可优化-4][可优化-5] | test_bg_estimate.py | 待开始 |
| T4 | 半透明UI的alpha和颜色回归 | T2+T3 | [可优化-6][可优化-7][可优化-13][可优化-17] | test_regression.py | 待开始 |
| T5 | 离线端到端验证 | T1+T2+T3+T4 | — | adjust_mixed.py + eval_mixed.py | 待开始 |

依赖关系图：

```
T1 (直接调节曲线) ─────────────────────────────┐
                                               │
T2 (时域分类) ─────────┬──────────────────────┤
                       │                      │
T3 (bg估计+帧筛选) ────┤                      ├──> T5 (离线验证)
                       │                      │
T4 (alpha回归) ─────────┘                      │
                                               │
                    工程优化点 [8,9,10,11,12,15,16] → T5 通过后
```

共用数据：lkwg 序列（SDR 混合 bin 5帧 + HDR 背景 bin 5帧 + alpha.png + rgb.png）。如 5 帧不足，扩展至 150 帧数据或合成更多帧。

---

### 9.2 T1: 半透明UI直接调节曲线测试

**目标**：用 Vulkan 双窗口测试工具，对 UI 和背景混合后的结果施加 Y-Scale 亮度调节，采集匹配数据，拟合非分离方案下的 Y-Scale 曲线。同时判断是否需要补充色度 (Cb/Cr) 调节。

**已实现内容**（`mixed_ui_test` 分支，commit e75f07b）：

| 文件 | 改动 |
|------|------|
| `shaders/hdr_ui.frag` | 顺序从"先调 UI 再混合"改为"先混合再调混合结果"；alpha 为 plain `texAlpha*sliderAlpha`；BG Nit 不影响 UI 区（raw bg）；支持 linear/sRGB 域混合切换；Y-Scale 统一；CbCr-Scale 保留 |
| `src/hdr_app.h` | `bgYScale_` → `blendingMode_` (0=linear, 1=sRGB) |
| `src/hdr_app.cpp` | 去掉 BG Y-Scale 滑条；加 Linear/sRGB Blend radio button；alpha 滑条范围 [0,1.0]；Y-Scale 统一为单个滑条 |
| `shaders/sdr_ui.frag` | 不改（父分支已有 blending toggle，作为参考） |
| `src/sdr_app.cpp/h` | 不改（父分支已有 radio button） |

**shader 流程对比**：

```
之前 (chromaScale_YUV_GameBG, UI分离):
  UI -> PQ -> YCbCr -> Y*Y-Scale -> RGB -> PQ decode -> adjusted UI nits
  -> mix: mixed = adjustedUI * effAlpha + bg * (1-effAlpha)
  -> PQ encode -> output

现在 (mixed_ui_test, UI不分离):
  UI + bg -> mix FIRST: mixed = uiNit * alpha + bgNit_raw * (1-alpha)
  -> PQ encode -> YCbCr -> Y * Y-Scale -> RGB -> PQ output
  (sRGB blend: 在 BT.709 域做 sRGB encode -> blend -> decode, 再转 BT.2020)
```

**测试方法**：

1. SDR 窗口选 Linear Blend 或 sRGB Blend（参考画面）
2. HDR 窗口选相同的 Blend 模式
3. 设 BG Nit 到目标 B 值（仅影响非 UI 背景区域）
4. 设 FG/BG Alpha 滑条到测试 alpha 值（如 0.1, 0.2, ..., 0.9）
5. 调 Y-Scale 滑条直到 HDR 窗口与 SDR 窗口主观一致
6. 记录 (alpha, B, Y-Scale) 数据点
7. 遍历所有 (alpha, B) 组合后拟合 Y-Scale = f(B, alpha) 曲线
8. 可选：调 CbCr-Scale 观察色彩偏差，决定是否需要色度调节

**测试矩阵**：

| 维度 | 取值 | 数量 |
|------|------|------|
| alpha | 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9 | 9 |
| B (nit) | 0, 30, 60, 100, 150, 200, 300, 500, 1000 | 9 |
| blend mode | linear, sRGB | 2 |
| **配对数** | | 162 |

**步骤**：

| # | 步骤 | 状态 |
|---|------|------|
| T1-1 | shader + app 代码实现（mixed_ui_test 分支） | [x] |
| T1-2 | 编译验证（build.sh，shader 编译通过） | [ ] |
| T1-3 | 采集 linear blend 模式数据（9 alpha × 9 B = 81 对） | [ ] |
| T1-4 | 采集 sRGB blend 模式数据（81 对） | [ ] |
| T1-5 | 拟合 Y-Scale = f(B, alpha) 曲线（linear blend） | [ ] |
| T1-6 | 拟合 Y-Scale = f(B, alpha) 曲线（sRGB blend） | [ ] |
| T1-7 | 对比两条曲线，评估混合域对 Y-Scale 的影响 | [ ] |
| T1-8 | 测试 CbCr-Scale：固定 Y-Scale，调 CbCr-Scale 观察色彩偏差 | [ ] |
| T1-9 | 色度决策：是否需要 CbCr 调节 + 偏差量化 | [ ] |
| T1-10 | 确定最终直接调节参数（Y-Scale 曲线 + 色度决策） | [ ] |

**产出**：非分离方案下 Y-Scale = f(B, alpha) 曲线（linear + sRGB 各一条）+ 是否需要色度调节

**验证标准**：

| 指标 | 合格标准 |
|------|---------|
| Y-Scale 曲线拟合 R² | > 0.95 |
| linear vs sRGB 曲线差异 | 量化（若差异 < 5% 可合并为一条） |
| 色度决策 | 有明确结论（需要/不需要 + CbCr 偏差量） |
| 主观验证 | 操作员确认 HDR 匹配 SDR 的 Y-Scale 数据一致性 |

---

### 9.3 T2: UI和背景区分算法

**目标**：验证时域求差能否准确区分不透明 UI / 半透明 UI / 背景三类像素。

**核心问题**：给定多帧 SDR 混合画面，通过 ΔM = |M_t - M_{t-1}| 分类。不透明 UI 的 ΔM≈0（颜色不变），背景的 ΔM 大（场景变化），半透明 UI 的 ΔM 居中（被 (1-α) 衰减）。

**输入**：
- 多帧 SDR 混合 bin (lkwg 5帧, 或扩展至 150 帧)
- alpha.png (ground truth: alpha>0.9 → 不透明, 0<alpha≤0.9 → 半透明, alpha=0 → 背景)

**步骤**：

| # | 步骤 | 状态 |
|---|------|------|
| T2-1 | 编写 test_temporal_classify.py：读多帧 SDR bin → 计算逐像素 ΔM(t) | [ ] |
| T2-2 | 用初始阈值 (θ_quiet=0.005, θ_bg=0.05) 做三分类 | [ ] |
| T2-3 | 生成分类图：不透明=红, 半透明=橙, 背景=绿 (可视化) | [ ] |
| T2-4 | 与 ground truth (alpha.png) 对比，计算 per-class precision / recall / F1 | [ ] |
| T2-5 | 测试不同阈值策略：手动调参 vs Otsu 自适应 vs 基于局部统计 | [ ] |
| T2-6 | 测试不同差分策略：相邻帧差 vs 滑动窗口方差 vs 与滑动均值差 | [ ] |
| T2-7 | 分析：哪些区域分类错误？错误原因是什么（背景恰好不变/不透明 UI 恰好变化）？ | [ ] |
| T2-8 | 确定推荐阈值策略 + 差分策略 + 参数 | [ ] |

**产出**：时域分类准确率报告 + 推荐参数

**验证标准**：

| 指标 | 合格标准 |
|------|---------|
| 不透明 UI F1 | > 0.90 |
| 半透明 UI F1 | > 0.85 |
| 背景 F1 | > 0.90 |
| 分类图视觉 | 三类区域清晰可辨 |

**风险**：lkwg 仅 5 帧，相邻帧差可能不够稳定。如 F1 不达标，需扩展至 150 帧或合成更多帧。

---

### 9.4 T3: 选择性帧筛选和bg估计

**目标**：优化 bg 估计精度，为 T4 的 alpha 回归提供可靠的 bg_t 数据。

**核心问题**：alpha 回归的精度直接取决于 bg_t 估计的精度。bg_t 由 border_extrapolate 从 bbox 外推得到，需要验证其准确性，并优化帧筛选策略（哪些帧的 bg 变化大，对回归贡献大）。

**依赖**：T2（需知道哪些像素是半透明 UI，才能在其 bbox 内做 bg 估计）

**输入**：
- SDR 混合 bin (多帧)
- HDR 背景 bin (多帧, ground truth)
- T2 的分类结果（半透明 UI 区域）

**步骤**：

| # | 步骤 | 状态 |
|---|------|------|
| T3-1 | 编写 test_bg_estimate.py：实现 border_extrapolate bg 估计 | [ ] |
| T3-2 | 将估计 bg 与 ground truth 对比：HDR bg bin → BT.2020→BT.709 → ÷350 → sRGB encode → bg_srgb_gt | [ ] |
| T3-3 | 计算 bg 估计 MAE 和相对误差 (per region, per frame) | [ ] |
| T3-4 | 实现 uniform 方法（四边均值），对比 border_extrapolate | [ ] |
| T3-5 | 实现选择性帧筛选：计算每帧 Δbg = |bg_t - bg_{t-1}|，统计哪些帧 Δbg 大 | [ ] |
| T3-6 | 评估：大 Δbg 帧是否确实对回归贡献更大（用 T4 初版回归验证） | [ ] |
| T3-7 | 测试 inpaint 方法（如 border_extrapolate 精度不足） | [ ] |
| T3-8 | 确定：推荐 bg 估计方法 + 帧筛选策略 + 参数 | [ ] |

**产出**：bg 估计方法 + 精度报告 + 帧筛选参数

**验证标准**：

| 指标 | 合格标准 |
|------|---------|
| bg sRGB MAE | < 0.03 (border_extrapolate) |
| bg 相对误差 | < 10% |
| 帧筛选 | 大 Δbg 帧的回归贡献显著大于小 Δbg 帧 |

**关键判断**：如果 border_extrapolate 的 MAE > 0.05，bg 估计成为瓶颈，需优先解决 [可优化-4]（可能需要 inpaint 或其他方法）。

---

### 9.5 T4: 半透明UI的alpha和颜色回归

**目标**：用时域多帧线性回归同时求解半透明 UI 的 alpha 和 UI 颜色（RGB 三通道）。

**核心问题**：M_t(c) = α·ui(c) + (1-α)·bg_t(c) 是关于 bg_t 的线性方程。多帧回归：斜率→α，截距→α·ui(c)→ui(c)。需验证回归精度和收敛条件。

**依赖**：T2（分类，确定半透明像素）+ T3（bg 估计，提供 bg_t）

**输入**：
- SDR 混合 bin (多帧)
- T3 的 bg 估计结果（每帧 bg_t）
- T2 的分类结果（半透明 UI 像素）
- alpha.png + rgb.png (ground truth)

**步骤**：

| # | 步骤 | 状态 |
|---|------|------|
| T4-1 | 编写 test_regression.py：实现三通道独立线性回归 M_t(c) = A(c) + B·bg_t(c) | [ ] |
| T4-2 | 求解：α = 1 - mean(B_R, B_G, B_B)，ui(c) = A(c) / α | [ ] |
| T4-3 | 与 ground truth 对比：alpha MAE, ui sRGB MAE, 色差 ΔE | [ ] |
| T4-4 | 生成散点图：α_est vs α_gt, ui_est vs ui_gt | [ ] |
| T4-5 | 实现回归质量指标：R², 斜率一致性, α 稳定性, ui 稳定性 | [ ] |
| T4-6 | 测试不同帧数 (5, 10, 20, 30) 对回归精度的影响 | [ ] |
| T4-7 | 实现多像素联合回归（同一半透明区域内所有像素合并回归） | [ ] |
| T4-8 | 测试 sRGB 域混合的回归（在 sRGB 域做回归 vs linear 域回归，对比精度） | [ ] |
| T4-9 | 确定：收敛标准 + 推荐帧数 + 是否需要 sRGB 域专用回归 | [ ] |

**产出**：回归方法 + 精度报告 + 收敛标准

**验证标准**：

| 指标 | 合格标准 |
|------|---------|
| alpha MAE | < 0.05 |
| alpha 分段 MAE | 各段 < 0.08 |
| ui sRGB MAE | < 0.03 (per channel) |
| ui 色差 ΔE | < 3 |
| R² | > 0.9 |
| 斜率一致性 | max(B) - min(B) < 0.03 |

**关键判断**：
- 如果 alpha MAE > 0.1 → 回归不可靠，检查 bg 估计精度 (T3) 或帧数是否足够
- 如果 ui MAE 大但 alpha MAE 小 → bg 估计有系统性偏差
- 如果 R² < 0.8 → bg 跨帧变化不足，需扩展数据或放宽窗口

---

### 9.6 T5: 离线端到端验证

**目标**：将 T1-T4 的结果集成到 adjust_mixed.py，在完整序列上做端到端验证。

**依赖**：T1 + T2 + T3 + T4 全部完成

**步骤**：

| # | 步骤 | 状态 |
|---|------|------|
| T5-1 | 编写 adjust_mixed.py：集成时域分类(T2) + bg估计(T3) + 回归(T4) + Y-boost/direct(T1) + 重构 | [ ] |
| T5-2 | 在 lkwg 序列上运行，输出 HDR bin 序列 | [ ] |
| T5-3 | 编写 eval_mixed.py：端到端评估（逐像素 Y 差, 分区域统计, vs 现有 pipeline） | [ ] |
| T5-4 | 生成对比视频：现有 pipeline HDR vs 基础方案 HDR vs 进阶方案 HDR | [ ] |
| T5-5 | 问题定位：如某区域 Y 差大，逐像素追踪到 T2(分类错) / T3(bg估计错) / T4(回归错) / T1(曲线错) | [ ] |
| T5-6 | 迭代：修正子任务 → 重跑 → 验证，直到端到端 Y 差达标 | [ ] |
| T5-7 | 测试 linear 域混合数据（合成的 linear-blended SDR） | [ ] |
| T5-8 | 最终报告：基础方案 vs 进阶方案 vs 现有 pipeline，全量指标对比 | [ ] |

**验证标准**：

| 指标 | 基础方案 | 进阶方案 |
|------|---------|---------|
| 全图 Y 差 | < 15 nit | < 15 nit |
| bbox 内 Y 差 | < 30 nit | < 25 nit |
| 半透明 UI Y 差 | < 30 nit | < 15 nit |
| 不透明 UI Y 差 | < 5 nit | < 5 nit |
| bbox 外 Y 差 | < 5 nit | < 5 nit |

**问题定位流程**：

```
端到端 Y 差大
    │
    ├─ bbox 外 Y 差大 → T1 (Y-boost 污染了背景) 或实现 bug
    │
    ├─ 不透明 UI Y 差大 → T1 (Y-boost 曲线不对) 或逆映射参数错
    │
    ├─ 半透明 UI Y 差大 (基础方案) → 预期, 进阶方案应改善
    │
    └─ 半透明 UI Y 差大 (进阶方案) → 逐级排查:
         ├─ 分类是否正确? → T2 (时域分类)
         ├─ bg 估计是否准确? → T3 (bg 估计)
         ├─ alpha/ui 回归是否准确? → T4 (回归)
         ├─ eff 计算是否正确? → 检查 eff 公式输入
         └─ 重构混合是否正确? → 检查线性域混合实现
```

---

### 9.7 工程优化（推迟）

以下优化点在离线验证 (T5) 通过后，算法合入实际游戏工程时再做：

| 编号 | 优化点 | 前置条件 |
|------|--------|---------|
| [可优化-8] | T_blend 过渡时长 | T5 通过, 有实时运行环境 |
| [可优化-9] | 过渡权重函数 | T5 通过, 有实时运行环境 |
| [可优化-10] | 区域划分粒度 | T5 通过, 性能测试 |
| [可优化-11] | 不收敛区域策略 | T5 通过, 有不收敛的测试场景 |
| [可优化-12] | 回退速度 | T5 通过, 有 UI 变化的测试场景 |
| [可优化-14] | UI 变化检测策略 | T5 通过, 有 UI 变化的测试场景 |
| [可优化-15] | 定期校验 | T5 通过, 长时间运行测试 |
| [可优化-16] | 缓存生命周期 | T5 通过, 性能优化 |

---

### 9.8 每日进展记录

| 日期 | 子任务 | 今日完成 | 明日计划 | 问题/阻塞 |
|------|--------|---------|---------|----------|
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |
| | | | | |
