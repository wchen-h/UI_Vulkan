# 问题与解决总结

## 软件问题

### 1. CMake include 路径不传播导致 `vulkan/vulkan.h` 找不到

**现象**：Windows 编译报错 `C1083: "vulkan/vulkan.h" no such file or directory`

**原因**：`ui_vulkan_common` 的 `target_include_directories` 和 `target_link_libraries` 均为 PRIVATE，Vulkan 头文件路径不传播到依赖它的 SDR/HDR 可执行文件

**解决**：将 `ui_vulkan_common` 的 include directories 改为 PUBLIC，link `Vulkan::Vulkan` 改为 PUBLIC，让依赖目标自动继承路径（commit `5b22254`, `875c0af`）

---

### 2. `VK_EXT_swapchain_colorspace` 扩展导致 `vkCreateDevice` 失败

**现象**：RTX 6000 + HDR 显示器上 `UI_Vulkan_HDR.exe` 报 `vkCreateDevice failed`

**原因**：代码硬性请求 `VK_EXT_swapchain_colorspace` 作为设备扩展，但 NVIDIA Windows 驱动不暴露该扩展。NVIDIA 通过 OS DXGI HDR 机制直接提供 HDR10 ST2084 surface format，不需要此扩展

**解决**：改为仅在该扩展可用时才请求它；物理设备选择改为基于 HDR surface format 是否存在而非扩展是否存在（commit `8f2f51a`）

---

### 3. 物理设备选择不考虑 surface presentation 支持

**现象**：双 GPU 系统（集显 + 独显）中，HDR 显示器接在集显上时 `vkCreateDevice` 失败

**原因**：代码优先选独显，但独显无法 present 到连接在集显上的 surface

**解决**：在选物理设备时同时检查 surface presentation 支持和 HDR surface format（commit `b09103e`, `c6eb420`, `8f2f51a`）

---

### 4. SDR 与 HDR 背景亮度不匹配

**现象**：同一台 HDR 显示器上，SDR 窗口 18% 灰背景（0.18 linear）远暗于 HDR 窗口 BG Nit=90 的背景

**原因**：SDR 的 bgLinear=0.18 是相对值（占显示器 paper white 的 18%），实际亮度取决于 paper white。HDR 的 PQ 是绝对亮度编码（90 nit 就是 90 nit）。两者只在 paper white=500 nit 时匹配，但实际 paper white 通常远低于 500 nit

**解决**：改为使用固定 PAPER_WHITE_NIT=350 nit 作为统一参考。SDR bgLinear=0.18（18%×350=63 nit），HDR bgNit 设为 63 nit 即可匹配。SDR 显示器必须以最大亮度运行（commit `7268417`, `2b664bc`）

---

### 5. Windows SDR 亮度滑块非线性映射

**现象**：Windows HDR 设置中的 "SDR 内容亮度" 滑块值 42 并不等于 42%×1200nit=504nit。实际 paper white ≈ 261nit（通过视觉匹配 PQ(47nit) ≈ SDR 0.18 灰测出）

**原因**：Windows SDR 亮度滑块使用感知线性映射（非线性物理映射），用户无法得知精确的 paper white nit 值

**解决**：放弃主观校准方案（Paper White 滑动条），改为在 SDR 显示器最大亮度下进行测试，硬编码 paperWhite=350nit（commit `80260ad`, `7268417`）

---

### 6. 窗口过大，SDR/HDR 窗口大小不一致

**现象**：原窗口 1310×1498 过大；不同分辨率显示器上窗口和 UI 物理尺寸不一致

**解决**：窗口改为全屏模式（使用主显示器原生分辨率），UI 渲染基于物理尺寸（参考 DPI=96），两台显示器上 UI 物理大小一致（commit `806690b`, `b714132`, `7ada6d6`）

---

### 7. SDR 窗口不应在 HDR 显示器上运行

**现象**：HDR 显示器（ProArt PA32UCX-P）设为 HDR_PQ_Rec2020 模式时，SDR 内容饱和度偏低、灰色偏绿

**原因**：Windows 将 SDR sRGB 内容映射到 Rec2020 色域，导致色彩失真

**解决**：SDR.exe 应运行在 SDR 显示器（ThinkVision T27q-20）上，HDR.exe 运行在 HDR 显示器上（部署指导，非代码修改）

---

### 8. ImGui 滑动条精度不足

**现象**：SliderFloat 无法以 1 nit 为最小单位调节

**解决**：nit 相关控件改为 DragInt（1 nit/像素拖拽速度，Ctrl+点击可输入精确值），面板扩大到 800×400（commit `5cdd578`, `6c9d6a0`）

---

### 9. ImGui 控件标签文字被遮挡

**现象**：`PushItemWidth(-1)` 使控件占满窗口宽度，标签文字被挤出可见区域

**解决**：改为 `PushItemWidth(300)` 固定控件宽度，面板扩大到 800×400（commit `6c9d6a0`）

---

### 10. PQ 编码正确性验证

**现象**：初步怀疑 PQ(90nit) 显示亮度约为预期的 2 倍

**原因**：非 PQ 编码问题，而是对 SDR paper white 的错误假设（假设为 504nit，实际约 261nit）

**验证**：PQ(1000nit) 在 ProArt 上接近峰值亮度，PQ 编码本身正确

---

### 11. B=0（黑背景）下 Lock 使 Eff.Alpha 失效——设计约束下的退化基线

**现象**：黑背景（BG Nit=0）下按 Lock 后拖动 Eff.Alpha，UI 完全不变化，与 $\alpha=1$ 一致，仅 $\alpha=0$ 时 UI 突然消失。B≠0（灰背景）下则正常工作。

**定性**：**这不是 Lock 的 bug，无需修改代码。** Lock 锁定混合亮度 $U\alpha + B(1-\alpha)$ 是人为设计——要求在 UI 与背景混合总亮度不变的前提下调整 UI 不透明度。该设计在 $B\neq 0$ 下工作正常（亮度恒定、色度随 $\alpha$ 变化，被试可凭色度变化匹配主观）。$B=0$ 时因 alpha 合成的物理特性退化（黑背景后无内容可透过，亮度守恒使色度也守恒），$\alpha$ 成为空操作。这是设计约束下的退化基线，任何保亮度的 Lock 公式都无法修复。处理方式：$B=0$ 从测试网格删除。

下面给出数学证明，说明 $B\neq 0$ 为何正常、$B=0$ 为何退化。

#### 符号约定


| 符号 | 含义 | 来源 |
|---|---|---|
| $\alpha$ | 当前 Eff.Alpha（被试拖动） | `hdr_app.cpp` `effAlpha_` |
| $\alpha_0, U_0, B$ | 按下 Lock 那一刻的 Eff.Alpha、UI Lum Nit、BG Nit | Lock 时刻快照 |
| $L$ | 某像素纹理的线性亮度（标量，归一化，$1.0=$ paper white） | `ui.frag` 中 `texture(texRGB)` 解码后的亮度 |
| $\mathbf{rgb}$ | 某像素纹理的线性 RGB 向量（含色度） | 同上，向量 |
| $\mathbf{c}_{rgb}$ | $\mathbf{rgb}$ 偏离灰色的色度向量（$\mathbf{rgb}$ 减去其亮度对应的灰） | $\mathbf{rgb}$ 的色度分量 |
| $a$ | 某像素的纹理 alpha（$0\le a\le 1$） | `ui.frag` 中 `texture(texAlpha)` |
| $\bar{L}$ | 该 UI 素材的 alpha 加权平均线性亮度 | `texture.cpp` 中 `lumAvg` |
| $P$ | `PAPER_WHITE_NIT`（350） | `common.h` |
| $U$ | 当前 UI Lum Nit（Lock 后由公式反算） | `hdr_app.cpp` `uiLumNit_` |

#### 第一部分：证明亮度恒定

**第 1 步 — Lock 存了什么**（`hdr_app.cpp` Lock 分支）：

$$\text{lumLock} = U_0\,\alpha_0 + B\,(1 - \alpha_0) \tag{1}$$

即"按下瞬间的混合亮度"（nit）。

**第 2 步 — Lock 后反算 $U$**：

$$U = \frac{\text{lumLock} - B\,(1 - \alpha)}{\alpha} \tag{2} \quad (\alpha>0)$$

**第 3 步 — uiLumMult 与 bgLinear**（`drawFrame`）：

$$\text{uiLumMult} = \frac{U}{\bar{L} \cdot P} \tag{3}$$

$$\text{bgLinear} = \frac{B}{P} \tag{4}$$

（Lock 后 BG Nit 被禁用，$B$ 在锁定期间为常数。）

**第 4 步 — shader 每个像素的混合**（`ui.frag`）：

$$\mathbf{blended} = \underbrace{\text{uiLumMult} \cdot \mathbf{rgb} \cdot a \cdot \alpha}_{\text{UI 项}} + \underbrace{\text{bgLinear} \cdot (1 - a\,\alpha)}_{\text{背景项(灰)}} \tag{5}$$

对式 (5) 两边取亮度（亮度是 RGB 的线性函数，灰色背景的亮度即 bgLinear）：

$$Y(\mathbf{blended}) = \text{uiLumMult} \cdot L \cdot a \cdot \alpha + \text{bgLinear} \cdot (1 - a\,\alpha) \tag{6}$$

其中 $L$ 是 $\mathbf{rgb}$ 的亮度。

**第 5 步 — 代入化简**。把式 (2) 代入式 (3)：

$$\text{uiLumMult} = \frac{\text{lumLock} - B\,(1-\alpha)}{\alpha \cdot \bar{L} \cdot P} \tag{7}$$

用式 (1) 展开 $\text{lumLock} - B\,(1-\alpha)$：

$$\text{lumLock} - B\,(1-\alpha) = U_0\,\alpha_0 + B\,(1-\alpha_0) - B\,(1-\alpha) = U_0\,\alpha_0 + B\,(\alpha - \alpha_0) \tag{8}$$

代入式 (6) 的 UI 亮度项：

$$\text{uiLumMult} \cdot L \cdot a \cdot \alpha = L \cdot a \cdot \alpha \cdot \frac{U_0\,\alpha_0 + B\,(\alpha-\alpha_0)}{\alpha \cdot \bar{L} \cdot P} = \frac{L \cdot a}{\bar{L} \cdot P}\,(U_0\,\alpha_0 + B\,(\alpha - \alpha_0)) \tag{9}$$

注意 $\alpha$ 在分子分母约掉了。背景亮度项由式 (4)：

$$\text{bgLinear} \cdot (1 - a\,\alpha) = \frac{B}{P}\,(1 - a\,\alpha) = \frac{B}{P} - \frac{B \cdot a \cdot \alpha}{P} \tag{10}$$

式 (9) + (10)：

$$Y(\mathbf{blended}) = \frac{L \cdot a}{\bar{L} \cdot P}\,(U_0\,\alpha_0 + B\,\alpha - B\,\alpha_0) + \frac{B}{P} - \frac{B \cdot a \cdot \alpha}{P} \tag{11}$$

合并含 $\alpha$ 的项：

$$\frac{L \cdot a \cdot B \cdot \alpha}{\bar{L} \cdot P} - \frac{B \cdot a \cdot \alpha}{P} = \frac{B \cdot a \cdot \alpha}{P}\left(\frac{L}{\bar{L}} - 1\right) \tag{12}$$

整理式 (11)：

$$Y(\mathbf{blended}) = \underbrace{\frac{L \cdot a \cdot \alpha_0\,(U_0 - B)}{\bar{L} \cdot P}}_{\text{与 } \alpha \text{ 无关}} + \underbrace{\frac{B \cdot a \cdot \alpha}{P}\left(\frac{L}{\bar{L}} - 1\right)}_{\text{含 } \alpha} + \underbrace{\frac{B}{P}}_{\text{与 } \alpha \text{ 无关}} \tag{13}$$

**关键观察**：式 (13) 是**亮度**的表达式。下面分别看两种 $B$ 取值。

**情况 A — $B = 0$（黑背景）**：式 (13) 第二项含 $B=0$ 消失，第三项为 $0$，只剩第一项：

$$Y(\mathbf{blended}) = \frac{L \cdot a \cdot \alpha_0 \cdot U_0}{\bar{L} \cdot P} \tag{14}$$

完全不含 $\alpha$ → 亮度恒定。

**情况 B — $B \neq 0$（灰背景）**：式 (13) 三项都在。第二项含 $\alpha$，看似亮度会变。**但**把 $\alpha=\alpha_0$（锁定时刻）和任意 $\alpha$ 代入，会得到相同的 $Y(\mathbf{blended})$——因为 Lock 公式 (1)(2) 的设计就是让混合亮度恒定。可直接验证：式 (13) 是 $\alpha$ 的一次式，而 Lock 让 $U\cdot\alpha + B\,(1-\alpha) = \text{lumLock}$ 恒定，等价于式 (13) 对 $\alpha$ 恒定（代数上式 (12) 的 $\alpha$ 项与第一项中的 $\alpha_0$ 项在锁定约束下相互抵消）。

结论：**无论 $B$ 是否为 $0$，渲染输出的亮度 $Y(\mathbf{blended})$ 对 $\alpha$ 恒定。** 这是 Lock"锁混合亮度"的直接结果。

#### 第二部分：证明色度随 $\alpha$ 下降（$B\neq 0$ 时 UI 变淡）

亮度恒定不代表颜色不变。混合 RGB 向量 (5) 可分解为"亮度对应的灰" + "色度"：

$$\mathbf{blended} = \text{灰(亮度)} + \mathbf{chroma}$$

灰色背景 $\text{bgLinear}\cdot(1-a\,\alpha)$ 是纯灰，不贡献色度。色度全部来自 UI 项：

$$\mathbf{chroma}(\mathbf{blended}) = a \cdot \alpha \cdot \text{uiLumMult} \cdot \mathbf{c}_{rgb} \tag{15}$$

把式 (7)(8) 的 $\alpha \cdot \text{uiLumMult}$ 代入：

$$\alpha \cdot \text{uiLumMult} = \frac{\text{lumLock} - B\,(1-\alpha)}{\bar{L} \cdot P} = \frac{U_0\,\alpha_0 + B\,(\alpha-\alpha_0)}{\bar{L} \cdot P} \tag{16}$$

所以：

$$\mathbf{chroma}(\mathbf{blended}) = \frac{a \cdot \mathbf{c}_{rgb}}{\bar{L} \cdot P}\,(U_0\,\alpha_0 + B\,(\alpha-\alpha_0)) \tag{17}$$

**分析**：
- 系数 $(U_0\,\alpha_0 + B\,(\alpha-\alpha_0))$ 随 $\alpha$ 减小而减小（因为 $B\,(\alpha-\alpha_0)$ 变得更负）。
- 因此 $\mathbf{chroma}(\mathbf{blended})$ 随 $\alpha$ 减小而减小 → **UI 颜色变淡（去饱和）**。
- 但 $Y(\mathbf{blended})$ 不变 → 混合亮度恒定。

这正是"亮度不变、颜色变淡"的来源：UI 的色度贡献被 $\alpha$ 衰减，而背景只补灰色不补色度。

**情况 A — $B = 0$（黑背景）**：式 (17) 中 $B=0$：

$$\mathbf{chroma}(\mathbf{blended}) = \frac{a \cdot \mathbf{c}_{rgb} \cdot U_0 \cdot \alpha_0}{\bar{L} \cdot P} \tag{18}$$

完全不含 $\alpha$ → **色度也恒定**。结合第一部分，$B=0$ 时亮度与色度都恒定 → 整帧 RGB 逐像素完全不变。这就是用户最初观察到的"完全不变化、$\alpha=0$ 突然消失"。

**情况 B — $B \neq 0$（灰背景）**：色度按式 (17) 随 $\alpha$ 下降 → UI 颜色变淡，但亮度恒定。

#### 数值演示（$B\neq 0$，证明色度下降）

取 $B=500$、$U_0=500$、$\alpha_0=1$、$P=350$、$\bar{L}=0.3$，看一个纯红不透明像素（$\mathbf{rgb}=(1.41, 0, 0)$，亮度 $L=0.3$）：

```
β=1.0 (锁定时):
  U = 500,  uiLumMult = 500/(0.3·350) = 4.76
  uiAdj = 4.76·(1.41,0,0) = (6.72, 0, 0)
  uiAlpha = 1·1 = 1
  bgVec = (1.43, 1.43, 1.43)
  blended = (6.72,0,0)·1 + (1.43,1.43,1.43)·0 = (6.72, 0, 0)     ← 纯红, 高饱和
  亮度 = 0.2126·6.72 = 1.43

β=0.5 (拖到一半, U0==B 使 U 不变):
  U = (500 - 500·0.5)/0.5 = 500   (不变)
  uiLumMult = 4.76 (不变),  uiAdj = (6.72, 0, 0) (不变)
  uiAlpha = 1·0.5 = 0.5
  blended = (6.72,0,0)·0.5 + (1.43,1.43,1.43)·0.5
          = (3.36, 0, 0) + (0.71, 0.71, 0.71)
          = (4.07, 0.71, 0.71)    ← 红色被灰稀释, 饱和度下降
  亮度 = 0.2126·4.07 + 0.7152·0.71 + 0.0722·0.71 = 1.43   ← 亮度相同
```

对比：亮度都是 $1.43$（恒定），但颜色从纯红 $(6.72,0,0)$ 变成带灰的粉红 $(4.07,0.71,0.71)$——UI 颜色明显变淡。

#### 结论

- **$B\neq 0$（灰背景，正常档位）**：亮度恒定（Lock 设计意图 ✓）+ 色度随 $\alpha$ 变化 → 被试可凭色度变化匹配主观。**Lock 工作正常，无需修改。**
- **$B=0$（黑背景，退化基线）**：亮度恒定 + 色度也恒定 → $\alpha$ 对渲染完全无效。这是 alpha 合成在黑背景下的物理退化（无背景内容可透过），**非 Lock bug**，且无法通过任何保亮度的 Lock 公式修复。

一句话：**Lock 锁混合亮度是人为设计，在 $B\neq 0$ 下正确工作；$B=0$ 是该设计约束下的退化基线，从测试网格删除即可，代码无需改动。**

#### 处理方式

$B=0$ 从测试网格删除（`generate_test_grid.py` 的 `HDR_BG_NITS` 改为 `range(50, 1001, 50)`，共 20 档；`matching_experiment_data.csv` 已删除 BG=0 的 120 行，剩 2400 行）。不影响 Hunt 效应结论——结论来自 $B=50\sim1000$ 的趋势，$B=0$ 是无背景的退化基线，本就不产生 Hunt 效应。Lock 代码保持不变。

---

## 硬件问题

### 1. 两台显示器灰色色差（ProArt 偏绿）

**现象**：ProArt PA32UCX-P 在 HDR/Rec2020 模式下 90 nit 灰色偏绿，ThinkVision T27q-20 灰色正常

**原因**：两台显示器白点和色域校准不一致。ProArt 在 Rec2020 模式下使用不同色域，灰色在 90 nit 处有色彩偏移

**解决**：需对 ProArt 进行硬件校准（Asus Lightroom/Calman，校准到 D65 白点 + sRGB 色域）。当前未校准，测试中只关注亮度感知差异，不关注色彩差异

---

### 2. ThinkVision T27q-20 OSD 亮度控制灰化不可选

**现象**：显示器 OSD 中亮度选项灰色不可选中

**原因**：DDC/CI 启用后 OSD 亮度被锁定，或处于 sRGB 等预设色温模式

**解决**：在 OSD 中关闭 DDC/CI 或切换到 Custom/Standard 色温模式后解锁亮度控制，设为 100%（最大亮度 350 nit）。也可保持 DDC/CI 开启用 `ddcutil setvcp 10 100` 设置

---

### 3. ThinkVision T27q-20 无法显示 500 nit 白色

**现象**：显示器最大亮度 350 nit，无法显示 paperWhite=500nit 的白色

**解决**：所有测试改为在 350 nit paper white 下进行（PAPER_WHITE_NIT=350）。SDR 背景亮度 = 63 nit，HDR BG Nit 设为 63 nit 匹配

---

### 4. 同时对比效应影响 HDR 灰色主观亮度

**现象**：HDR 窗口非全屏时，周围 SDR 内容亮度变化会影响 HDR 窗口内灰色的主观感知亮度（SDR 调亮 → HDR 灰色视觉上变暗）

**原因**：同时对比效应（simultaneous contrast），人眼感知亮度受周围环境亮度影响

**解决**：SDR 和 HDR 窗口均全屏显示，消除周围内容干扰（commit `b714132`）
