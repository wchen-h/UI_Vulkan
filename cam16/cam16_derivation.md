# CAM16 UI 亮度提亮与色彩一致性——数学推导

> 前置文档：cam16/cam16_formulas.md（全部公式来源）
> 本文档基于 cam16_formulas.md 的公式进行推导，不引入任何未经源码验证的公式或概念。

## 1. 思路检验

### 1.1 目标

给定一个 UI 像素（在 PQ 域 BT.2020 RGB），在固定背景亮度下：
1. 用 CAM16 正向变换算出初始感知属性 (Q, h, s)
2. 用 ImGui 滑动条设定 Q 的倍率 k，得到目标 Q' = Q * k
3. 保持 h' = h, s' = s 不变
4. 从 (Q', h', s') 反解出新的 PQ 域 RGB

### 1.2 正向映射的确定性

**结论：确定。**

给定 RGB_PQ + 观看环境参数（XYZ_w, L_A, Y_b, surround），正向链是：
```
RGB_PQ → PQ_decode → nit (BT.2020) → XYZ → CAM16正向 → (J, C, h, Q, M, s)
```
每一步是确定性函数，无歧义。

### 1.3 逆向的可解性

**结论：可解，且为解析解。**

CAM16 标准逆向接受 (J, C, h) → XYZ。我们需要从 (Q', h', s') 推出 (J', C')。

从 cam16_formulas.md §7 的桥接公式：
```
Q = (4/c) * sqrt(J/100) * (A_w + 4) * F_L^0.25

反推 J:
  sqrt(J/100) = Q * c / (4 * (A_w + 4) * F_L^0.25)
  J = 100 * (Q * c / (4 * (A_w + 4) * F_L^0.25))^2
```

```
s = 100 * sqrt(M / Q)

反推 M:
  sqrt(M/Q) = s / 100
  M/Q = (s/100)^2
  M = (s/100)^2 * Q
```

```
M = C * F_L^0.25

反推 C:
  C = M / F_L^0.25
```

因此：
```
J' = 100 * (Q' * c / (4 * (A_w + 4) * F_L^0.25))^2
M' = (s / 100)^2 * Q'
C' = M' / F_L^0.25
```

所有变量（c, A_w, F_L）在固定观看环境下是常数。这是一个解析公式，不需要数值求解。

然后用标准逆向 (J', C', h') → XYZ' → nit' → PQ_encode → RGB_PQ'。

### 1.4 解的物理可行性

**结论：有条件可行——解出的 RGB 可能超出 [0, 1023] 范围（色域外）。**

CAM16 逆向会算出 XYZ'，再转成 nit'，再 PQ 编码得到 RGB_PQ'。如果 Q' 太大（k 太大），RGB_PQ' 可能超过 1023（PQ 最大值，= 10000 nit）或低于 0。

处理策略：**clamp 到 [0, 1023]**，并在 ImGui 中显示"色域外像素数"（即被 clamp 的像素数），让测试者知道何时开始失真。

### 1.5 思路检验总结

| 检验点 | 结论 |
|---|---|
| 正向确定性 | ✅ 确定 |
| (Q', s') → (J', C') 桥接 | ✅ 有解析公式 |
| (J', C', h) → XYZ 标准逆向 | ✅ CAM16 已定义 |
| XYZ → RGB_PQ | ✅ 线性矩阵 + PQ 编码 |
| 物理可行性 | ⚠️ 可能超出 [0,1023]，需 clamp + 计数 |

---

## 2. 完整公式链推导

### 2.1 观看环境参数确定

在 UI_Vulkan 的 HDR 管线中，观看环境是固定的：

| 参数 | 值 | 来源 |
|---|---|---|
| XYZ_w | D65 白点的 XYZ（scale=100） | 标准：XYZ_w = [95.04, 100.0, 108.88] |
| L_A | 适应场亮度 | = bgNit / 5（经验值：取白物亮度的 ~20%，白物亮度 ≈ BG Nit；来源：工作日志 06-12 的先例） |
| Y_b | 背景亮度因子 | = 100 × bgNit / 350（背景 nit 占 paper white 的比例 × 100） |
| surround | Average (F=1, c=0.69, N_c=1) | 默认值，来源：ciecam02.py VIEWING_CONDITIONS |

**注意**：L_A 和 Y_b 随 BG Nit 变化，当 BG Nit 改变时需要重新计算中间参数（n, F_L, N_bb, N_cb, z, D, A_w）。

### 2.2 正向链（RGB_PQ → Q, h, s）

输入：RGB_PQ（PQ 域 BT.2020，值域 [0, 1023]，10-bit full range）

#### Step F1: PQ 解码 → nit (BT.2020 线性)

```
对每个通道 c:
  y = RGB_PQ[c] / 1023.0                          # 归一化到 [0,1]
  vp = y^(32/2523)                                 # PQ 逆 OETF
  num = max(vp - 3424/4096, 0)
  den = (2413/128) - (2392/128) * vp
  nit_c = 10000 * (num / den)^(16384/2610)         # 绝对亮度 (cd/m²)
```

来源：ST.2084 PQ EOTF（与 shader 中 linearToPQ 互逆）

#### Step F2: BT.2020 nit → CIE XYZ

BT.2020 RGB → XYZ 的矩阵（标准色彩学，BT.2020 色度坐标 + D65 白点推导）。

从 luma 系数 0.2627/0.6780/0.0593 可知 Y = 0.2627*R + 0.6780*G + 0.0593*B。

完整的 BT.2020→XYZ 矩阵（从 BT.2020 原色色度 + D65 白点推导）：
```
XYZ = M_BT2020_to_XYZ @ [R_nit, G_nit, B_nit]
```

**注意**：这个矩阵需要从 BT.2020 原色色度坐标和 D65 白点推导。在 colour-science 库中可通过 `colour.models.RGB_COLOURSPACE_BT2020.matrix_RGB_to_XYZ` 获取。在本推导中先用符号 M_2020_XYZ 表示，实现时从标准库获取精确值。

（实际上，BT.2020→XYZ 矩阵是公开标准值，可在 ITU-R BT.2020 文档中查到。）

#### Step F3: CAM16 正向（XYZ → Q, h, s）

按 cam16_formulas.md §5 的步骤：

```
# 中间参数（在固定观看环境下只算一次）
n = Y_b / Y_w
k = 1 / (5 * L_A + 1)
k4 = k^4
F_L = 0.2 * k4 * (5 * L_A) + 0.1 * (1 - k4)^2 * (5 * L_A)^(1/3)
N_bb = N_cb = 0.725 * (1/n)^0.2
z = 1.48 + sqrt(n)
D = F * (1 - (1/3.6) * exp((-L_A - 42) / 92))     # F=1 for Average

# 白点预处理
RGB_w = M16 @ XYZ_w
D_RGB[i] = D * Y_w / RGB_w[i] + 1 - D
RGB_wc = D_RGB * RGB_w
RGB_aw = forward_nonlinear(RGB_wc, F_L)
A_w = (2*RGB_aw[0] + RGB_aw[1] + (1/20)*RGB_aw[2] - 0.305) * N_bb

# 刺激处理
RGB = M16 @ XYZ
RGB_c = D_RGB * RGB
RGB_a = forward_nonlinear(RGB_c, F_L)

# 感知属性
a_opp = RGB_a[0] - 12*RGB_a[1]/11 + RGB_a[2]/11
b_opp = (RGB_a[0] + RGB_a[1] - 2*RGB_a[2]) / 9
h = degrees(atan2(b_opp, a_opp)) mod 360

e_t = (1/4) * (cos(2 + h * pi/180) + 3.8)
A = (2*RGB_a[0] + RGB_a[1] + (1/20)*RGB_a[2] - 0.305) * N_bb
J = 100 * (A / A_w)^(c * z)
Q = (4/c) * sqrt(J/100) * (A_w + 4) * F_L^0.25

t = (50000/13 * N_c * N_cb) * e_t * sqrt(a_opp^2 + b_opp^2) / (RGB_a[0] + RGB_a[1] + 21*RGB_a[2]/20)
C = t^0.9 * (J/100)^0.5 * (1.64 - 0.29^n)^0.73
M = C * F_L^0.25
s = 100 * sqrt(M / Q)
```

输出：(Q, h, s)

### 2.3 调节

```
Q' = Q * k_scale     # k_scale 由 ImGui 滑动条控制，起始 1.0
h' = h               # 不变
s' = s               # 不变
```

### 2.4 桥接（Q', s' → J', C'）

```
J' = 100 * (Q' * c / (4 * (A_w + 4) * F_L^0.25))^2
M' = (s / 100)^2 * Q'
C' = M' / F_L^0.25
```

### 2.5 逆向链（J', C', h' → RGB_PQ'）

按 cam16_formulas.md §6 的步骤：

```
# Step 1: t
t = (C' / (sqrt(J'/100) * (1.64 - 0.29^n)^0.73))^(1/0.9)

# Step 2: e_t, A, P_1~P_3
e_t = (1/4) * (cos(2 + h * pi/180) + 3.8)
A = A_w * (J'/100)^(1/(c*z))
P_1 = (50000/13 * N_c * N_cb * e_t) / t
P_2 = A / N_bb + 0.305
P_3 = 21/20

# Step 3: a, b (逆对立色)
hr = radians(h)
sin_hr = sin(hr)
cos_hr = cos(hr)
n_ab = P_2 * (2 + P_3) * (460 / 1403)

if |sin_hr| >= |cos_hr|:
    P_4 = P_1 / sin_hr
    b = n_ab / (P_4 + (2+P_3)*(220/1403)*cos_hr/sin_hr - 27/1403 + P_3*(6300/1403))
    a = b * cos_hr/sin_hr
else:
    P_5 = P_1 / cos_hr
    a = n_ab / (P_5 + (2+P_3)*(220/1403) - ((27/1403) - P_3*(6300/1403)) * sin_hr/cos_hr)
    b = a * sin_hr/cos_hr

# Step 4: RGB_a
RGB_a = [[460, 451, 288], [460, -891, -261], [460, -220, -6300]] @ [P_2, a, b] / 1403

# Step 5: 逆非线性压缩
对每个通道 c:
  RGB_c[c] = sign(RGB_a[c] - 0.1) * 100/F_L * (27.13 * |RGB_a[c] - 0.1| / (400 - |RGB_a[c] - 0.1|))^(1/0.42)

# Step 6: 逆色度适应 + 逆 CAT16
RGB = RGB_c / D_RGB
XYZ' = M16_inv @ RGB
```

#### Step I1: XYZ' → BT.2020 nit

```
[R_nit', G_nit', B_nit'] = M_XYZ_to_BT2020 @ XYZ'
```

（M_XYZ_to_BT2020 是 M_BT2020_to_XYZ 的逆矩阵）

#### Step I2: PQ 编码 → RGB_PQ'

```
对每个通道 c:
  y = R_nit'[c] / 10000.0
  yPow = y^(2610/16384)
  num = 3424/4096 + (2413/128) * yPow
  den = 1 + (2392/128) * yPow
  pq = (num / den)^(2523/32)
  RGB_PQ'[c] = pq * 1023.0
```

#### Step I3: Clamp

```
RGB_PQ'[c] = clamp(RGB_PQ'[c], 0, 1023)
```

如果 clamp 发生，标记为"色域外像素"。

### 2.6 完整流程图

```
RGB_PQ (输入)
  → PQ decode → nit (BT.2020)
  → XYZ
  → CAM16 正向 → (Q, h, s)
  → 调节: Q'=Q*k, h'=h, s'=s
  → 桥接: (Q', s') → (J', C')
  → CAM16 逆向: (J', C', h') → XYZ'
  → BT.2020 nit'
  → PQ encode → RGB_PQ' (输出)
  → clamp [0, 1023]
```

---

## 3. 需要实现的功能模块

| 模块 | 输入 | 输出 | 公式来源 |
|---|---|---|---|
| PQ_decode | RGB_PQ [0,1023] | nit [0,10000] | §2.2 Step F1 |
| BT2020_to_XYZ | R,G,B nit | X,Y,Z | 标准矩阵 |
| CAM16_forward | XYZ + 观看环境 | (J,C,h,Q,M,s) | §2.2 Step F3 |
| QhS_to_JCh | (Q',h,s) + 常数 | (J',C',h) | §2.4 桥接 |
| CAM16_inverse | (J',C',h) + 观看环境 | XYZ' | §2.5 |
| XYZ_to_BT2020 | X,Y,Z | R,G,B nit | 标准逆矩阵 |
| PQ_encode | nit [0,10000] | RGB_PQ [0,1023] | §2.5 Step I2 |

## 4. 诚实标注

1. **BT.2020↔XYZ 矩阵**：本文用符号 M_BT2020_to_XYZ 和 M_XYZ_to_BT2020 表示。精确值需从 ITU-R BT.2020 标准或 colour-science 库获取。这不是推导的难点（标准矩阵），但实现时需取精确值。

2. **L_A 的取值**：L_A = bgNit / 5 是经验值（取白物亮度的 ~20%）。这在工作日志 06-12 中有先例。是否精确需要实测验证，但作为首轮实现的合理起点。

3. **Y_b 的取值**：Y_b = 100 × bgNit / 350。这里 350 是 PAPER_WHITE_NIT（paper white）。当 bgNit = 350 时 Y_b = 100（与白点相同），当 bgNit = 0 时 Y_b = 0。这在物理上合理（背景越亮，Y_b 越大）。

4. **色域外处理**：clamp 是最简方案。更优方案是 gamut mapping（压缩到色域内），但首轮实现用 clamp 即可。

5. **桥接公式的正确性**：从 Q 反推 J、从 s 反推 M 再推 C，这些是公式的代数逆运算，不涉及近似。唯一的风险是除以零（Q=0 或 F_L=0），需在实现中加保护。
