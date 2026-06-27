# CAM16 公式定义（从 colour-science/colour 源码逐行提取）

> 来源：github.com/colour-science/colour (BSD-3-Clause)
> 文件：colour/appearance/cam16.py, colour/appearance/ciecam02.py, colour/appearance/hunt.py, colour/adaptation/datasets/cat.py
> 论文：Li et al. (2017), "Comprehensive color solutions: CAM16, CAT16, and CAM16-UCS", Color Research & Application, 42(6), 703-718. doi:10.1002/col.22131

## 0. CAM16 与 CIECAM02 的关系

CAM16 复用 CIECAM02 的全部公式，仅两处不同：
1. 用 **CAT16 矩阵**替代 CAT02 矩阵
2. **不做 HPE（Hunt-Pointer-Estevez）转换**——CIECAM02 在 CAT02 锐化 RGB 后转 HPE 再做非线性压缩；CAM16 直接在 CAT16 锐化 RGB 上做非线性压缩

源码证据：cam16.py 中 `MATRIX_16 = CAT_CAT16`，且不调用 `RGB_to_rgb` / `rgb_to_RGB`（HPE 转换），直接调用 `post_adaptation_non_linear_response_compression_forward`。

## 1. CAT16 矩阵

```
M16 = [[ 0.401288,  0.650173, -0.051461],
       [-0.250268,  1.204414,  0.045854],
       [-0.002079,  0.048952,  0.953127]]
```

逆矩阵 `M16_inv = inv(M16)`。

来源：colour/adaptation/datasets/cat.py, `CAT_CAT16`，引用 :cite:`Li2017`

## 2. 观看环境参数

| 参数 | 含义 | 来源 |
|---|---|---|
| XYZ_w | 适应白的三刺激值（D65 的 XYZ，scale=100） | 输入 |
| L_A | 适应场亮度 (cd/m²)，通常取白物亮度的 ~20% | 输入 |
| Y_b | 背景亮度因子，= 100 × L_b/L_w | 输入 |
| F | 最大适应度，Average=1.0 | surround |
| c | 指数非线性，Average=0.69 | surround |
| N_c | 色度感应因子，Average=1.0 | surround |

surround 默认值（Average）来源：ciecam02.py `VIEWING_CONDITIONS_CIECAM02["Average"] = InductionFactors_CIECAM02(1, 0.69, 1)`

## 3. 中间参数计算

来源：ciecam02.py `viewing_conditions_dependent_parameters(Y_b, Y_w, L_A)`

```
n = Y_b / Y_w
```

来源：ciecam02.py `viewing_conditions_dependent_parameters`

```
k = 1 / (5 * L_A + 1)
k4 = k^4
F_L = 0.2 * k4 * (5 * L_A) + 0.1 * (1 - k4)^2 * (5 * L_A)^(1/3)
```

来源：hunt.py `luminance_level_adaptation_factor(L_A)`

```
N_bb = N_cb = 0.725 * (1/n)^0.2
```

来源：ciecam02.py `chromatic_induction_factors(n)`

```
z = 1.48 + sqrt(n)
```

来源：ciecam02.py `base_exponential_non_linearity(n)`

## 4. 色度适应度 D

来源：ciecam02.py `degree_of_adaptation(F, L_A)`

```
D = F * (1 - (1/3.6) * exp((-L_A - 42) / 92))
```

如果 `discount_illuminant = true`，则 D = 1.0。

## 5. 正向变换（XYZ → J, C, h, Q, M, s）

### Step 0: 白点预处理

```
RGB_w = M16 @ XYZ_w                    # CAT16 锐化
D_RGB[i] = D * Y_w / RGB_w[i] + 1 - D  # 逐通道
RGB_wc = D_RGB * RGB_w                 # 适应后白点
RGB_aw = forward_nonlinear(RGB_wc, F_L) # 非线性压缩
A_w = (2*RGB_aw[0] + RGB_aw[1] + (1/20)*RGB_aw[2] - 0.305) * N_bb
```

来源：cam16.py `XYZ_to_CAM16` Step 0 + ciecam02.py `achromatic_response_forward`

### Step 1-3: 刺激预处理

```
RGB = M16 @ XYZ              # CAT16 锐化
RGB_c = D_RGB * RGB          # 色度适应
RGB_a = forward_nonlinear(RGB_c, F_L)  # 非线性压缩
```

来源：cam16.py `XYZ_to_CAM16` Steps 1-3

### 非线性压缩函数 forward_nonlinear(RGB, F_L)

来源：ciecam02.py `post_adaptation_non_linear_response_compression_forward(RGB, F_L)`

```
对每个通道 c:
  F_L_RGB = (F_L * |RGB[c]| / 100)^0.42
  RGB_a[c] = 400 * sign(RGB[c]) * F_L_RGB / (27.13 + F_L_RGB) + 0.1
```

### Step 4: 对立色维度

来源：ciecam02.py `opponent_colour_dimensions_forward(RGB_a)`

```
a = RGB_a[0] - 12*RGB_a[1]/11 + RGB_a[2]/11
b = (RGB_a[0] + RGB_a[1] - 2*RGB_a[2]) / 9
```

### Step 5: 色相角 h

来源：ciecam02.py `hue_angle(a, b)`

```
h = degrees(atan2(b, a)) mod 360
```

### Step 6: 离心率因子 e_t

来源：ciecam02.py `eccentricity_factor(h)`

```
e_t = (1/4) * (cos(2 + h * pi/180) + 3.8)
```

### Step 7: 明度 J

来源：ciecam02.py `lightness_correlate(A, A_w, c, z)` + `achromatic_response_forward`

```
A = (2*RGB_a[0] + RGB_a[1] + (1/20)*RGB_a[2] - 0.305) * N_bb
J = 100 * (A / A_w)^(c * z)
```

### Step 8: 亮度 Q

来源：ciecam02.py `brightness_correlate(c, J, A_w, F_L)`

```
Q = (4/c) * sqrt(J/100) * (A_w + 4) * F_L^0.25
```

### Step 9: 彩度 C

来源：ciecam02.py `chroma_correlate(J, n, N_c, N_cb, e_t, a, b, RGB_a)` + `temporary_magnitude_quantity_forward`

```
t = (50000/13 * N_c * N_cb) * e_t * sqrt(a^2 + b^2) / (RGB_a[0] + RGB_a[1] + 21*RGB_a[2]/20)
C = t^0.9 * (J/100)^0.5 * (1.64 - 0.29^n)^0.73
```

### Step 10: 色彩度 M

来源：ciecam02.py `colourfulness_correlate(C, F_L)`

```
M = C * F_L^0.25
```

### Step 11: 饱和度 s

来源：ciecam02.py `saturation_correlate(M, Q)`

```
s = 100 * sqrt(M / Q)
```

## 6. 逆向变换（J, C, h → XYZ）

### Step 0: 白点预处理（与正向相同）

```
RGB_w = M16 @ XYZ_w
D_RGB[i] = D * Y_w / RGB_w[i] + 1 - D
RGB_wc = D_RGB * RGB_w
RGB_aw = forward_nonlinear(RGB_wc, F_L)
A_w = (2*RGB_aw[0] + RGB_aw[1] + (1/20)*RGB_aw[2] - 0.305) * N_bb
```

来源：cam16.py `CAM16_to_XYZ` Step 0

### Step 1: 从 C 或 M 得到 C

```
如果 C 未定义但 M 已定义：
  C = M / F_L^0.25
```

来源：cam16.py `CAM16_to_XYZ` Step 1

### Step 2: 临时量 t

来源：ciecam02.py `temporary_magnitude_quantity_inverse(C, J, n)`

```
t = (C / (sqrt(J/100) * (1.64 - 0.29^n)^0.73))^(1/0.9)
```

### Step 3: e_t, A, P_1~P_3

来源：ciecam02.py `eccentricity_factor(h)`, `achromatic_response_inverse`, `P(N_c, N_cb, e_t, t, A, N_bb)`

```
e_t = (1/4) * (cos(2 + h * pi/180) + 3.8)
A = A_w * (J/100)^(1/(c*z))
P_1 = (50000/13 * N_c * N_cb * e_t) / t
P_2 = A / N_bb + 0.305
P_3 = 21/20
```

### Step 4: 对立色逆变换 a, b

来源：ciecam02.py `opponent_colour_dimensions_inverse(P_n, h)`

```
hr = radians(h)
sin_hr = sin(hr)
cos_hr = cos(hr)

n_ab = P_2 * (2 + P_3) * (460 / 1403)

if |sin_hr| >= |cos_hr|:
    P_4 = P_1 / sin_hr
    b = n_ab / (P_4 + (2 + P_3)*(220/1403)*cos_hr/sin_hr - 27/1403 + P_3*(6300/1403))
    a = b * cos_hr/sin_hr
else:
    P_5 = P_1 / cos_hr
    a = n_ab / (P_5 + (2 + P_3)*(220/1403) - ((27/1403) - P_3*(6300/1403)) * sin_hr/cos_hr)
    b = a * sin_hr/cos_hr
```

### Step 5: 逆矩阵得到 RGB_a

来源：ciecam02.py `matrix_post_adaptation_non_linear_response_compression(P_2, a, b)`

```
RGB_a = [[460, 451, 288],
         [460, -891, -261],
         [460, -220, -6300]] @ [P_2, a, b] / 1403
```

### Step 6: 逆非线性压缩

来源：ciecam02.py `post_adaptation_non_linear_response_compression_inverse(RGB_a, F_L)`

```
对每个通道 c:
  RGB_c[c] = sign(RGB_a[c] - 0.1) * 100/F_L * (27.13 * |RGB_a[c] - 0.1| / (400 - |RGB_a[c] - 0.1|))^(1/0.42)
```

### Step 7: 逆色度适应 + 逆矩阵

来源：cam16.py `CAM16_to_XYZ` Steps 6-7

```
RGB = RGB_c / D_RGB          # 逆色度适应
XYZ = M16_inv @ RGB          # 逆 CAT16
```

## 7. 关键桥接公式：Q↔J, s↔(M,Q), M↔C

从正向公式可得：

```
Q = (4/c) * sqrt(J/100) * (A_w + 4) * F_L^0.25
→ J = 100 * (Q * c / (4 * (A_w + 4) * F_L^0.25))^2

s = 100 * sqrt(M/Q)
→ M = (s/100)^2 * Q

M = C * F_L^0.25
→ C = M / F_L^0.25
```

因此从 (Q', h, s) 可以推导出 (J', C', h)：
```
J' = 100 * (Q' * c / (4 * (A_w + 4) * F_L^0.25))^2
M' = (s/100)^2 * Q'
C' = M' / F_L^0.25
```
然后用标准逆向 (J', C', h) → XYZ。

**这验证了用户思路的数学可行性**：给定 (Q', h, s) 和观看环境参数，可以解析地推导出 (J', C', h)，再用 CAM16 标准逆向得到 XYZ → RGB。

## 8. 数值验证示例

来源：cam16.py docstring 中的 doctest

```python
XYZ = [19.01, 20.00, 21.78]
XYZ_w = [95.05, 100.00, 108.88]
L_A = 318.31
Y_b = 20.0
surround = Average (F=1, c=0.69, N_c=1)

XYZ_to_CAM16 结果:
  J = 41.7312079...
  C = 0.1033557...
  h = 217.0679597...
  s = 2.3450150...
  Q = 195.3717089...
  M = 0.1074367...

CAM16_to_XYZ(J=41.7312079, C=0.1033557, h=217.0679597) → [19.01, 20.00, 21.78] ✓
```

## 9. 源码文件对照表

| 公式 | 源文件 | 函数名 |
|---|---|---|
| CAT16 矩阵 | colour/adaptation/datasets/cat.py | `CAT_CAT16` |
| F_L | colour/appearance/hunt.py | `luminance_level_adaptation_factor` |
| n, N_bb, N_cb, z | colour/appearance/ciecam02.py | `viewing_conditions_dependent_parameters` |
| D | colour/appearance/ciecam02.py | `degree_of_adaptation` |
| 正向非线性压缩 | colour/appearance/ciecam02.py | `post_adaptation_non_linear_response_compression_forward` |
| 逆向非线性压缩 | colour/appearance/ciecam02.py | `post_adaptation_non_linear_response_compression_inverse` |
| a, b (正向) | colour/appearance/ciecam02.py | `opponent_colour_dimensions_forward` |
| a, b (逆向) | colour/appearance/ciecam02.py | `opponent_colour_dimensions_inverse` |
| h | colour/appearance/ciecam02.py | `hue_angle` |
| e_t | colour/appearance/ciecam02.py | `eccentricity_factor` |
| A (正向) | colour/appearance/ciecam02.py | `achromatic_response_forward` |
| A (逆向) | colour/appearance/ciecam02.py | `achromatic_response_inverse` |
| J | colour/appearance/ciecam02.py | `lightness_correlate` |
| Q | colour/appearance/ciecam02.py | `brightness_correlate` |
| t, C (正向) | colour/appearance/ciecam02.py | `temporary_magnitude_quantity_forward`, `chroma_correlate` |
| t (逆向) | colour/appearance/ciecam02.py | `temporary_magnitude_quantity_inverse` |
| M | colour/appearance/ciecam02.py | `colourfulness_correlate` |
| s | colour/appearance/ciecam02.py | `saturation_correlate` |
| P_1, P_2, P_3 | colour/appearance/ciecam02.py | `P` |
| RGB_a (逆向) | colour/appearance/ciecam02.py | `matrix_post_adaptation_non_linear_response_compression` |
| 正向全流程 | colour/appearance/cam16.py | `XYZ_to_CAM16` |
| 逆向全流程 | colour/appearance/cam16.py | `CAM16_to_XYZ` |
