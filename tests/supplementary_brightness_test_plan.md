# 补充测试方案 v2：SDR 灰阶 × SDR 背景 × HDR 背景（灰阶优先）

> 修订自 v1。变更：① 灰阶去掉 90 nit 锚点；② 新增 **SDR 背景亮度** 维度；③ 色度锚点暂缓到阶段 2。
> 本版只做灰阶，尽快拟合 Y-Scale 调节曲线看规律。

---

## 1. 目标与约束

| 项 | 值 |
|----|----|
| 目标 | 拟合 **Y-Scale 感知调节曲线 = f(SDR UI 亮度, SDR 背景亮度, HDR 背景亮度)** |
| SDR UI 亮度轴 | 灰阶梯 50~350 nit，步长 50（7 点） |
| SDR 背景亮度轴 | 50 / 150 / 350（3 点） |
| HDR 背景亮度轴 | 50 / 100 / 150 / 200 / 300 / 500 / 1000（7 点） |
| SDR paperwhite | **350 nit**（保持现状，不改常量） |
| 参照 | SDR 窗口显示"灰阶 UI 叠在 SDR 背景"上，HDR 窗口匹配主观亮度 |
| 匹配量 | 灰阶 UI 无色度，只调 **Y-Scale**（alpha 固定 1） |

---

## 2. 为什么要扫 SDR 背景

实际游戏中 SDR golden 是"**任意亮度的 UI 叠在任意亮度的背景**"上，同时对比（simultaneous contrast）使 UI 的
感知亮度取决于周围背景。v1 只把 SDR 背景固定在代码硬编码的 18% 灰（63 nit，`sdr_app.cpp:325`），
无法刻画"UI/背景相对亮度"对感知的影响。因此本版把 SDR 背景亮度提升为一等变量。

---

## 3. 灰阶梯（SDR UI 亮度，R=G=B）

| 目标亮度 (nit) | sRGB byte (R=G=B) @paperwhite 350 |
|:--------------:|:---------------------------------:|
| 50 | 106 |
| 100 | 146 |
| 150 | 175 |
| 200 | 199 |
| 250 | 220 |
| 300 | 238 |
| 350 | 255 |

素材命名：`t_rgb_gray_050.png … t_rgb_gray_350.png`（对应 `t_alpha_gray_*.png`，alpha 全程 1）。

---

## 4. 测试矩阵（灰阶，alpha=1）

| 维度 | 取值 | 组数 |
|------|------|------|
| SDR_UI_Nit | 50~350 步长 50 | 7 |
| SDR_BG_Nit | 50 / 150 / 350 | 3 |
| HDR_BG_Nit | 50 / 100 / 150 / 200 / 300 / 500 / 1000 | 7 |

- 全因子：**7 × 3 × 7 = 147 点**。
- 每点预计 15 秒，纯匹配 ≈ 37 分钟，含切点/记录 ≈ 1 小时。

> 注意：BG = 0 已剔除（黑背景下恒亮度 Lock 使 Eff.Alpha/Y-Scale 对渲染失效，见 `ISSUES_SUMMARY.md` §11），
> 故 SDR_BG 与 HDR_BG 均从 50 起。

---

## 5. 代码 / 资产改动量

| 改动 | 位置 | 量级 |
|------|------|------|
| 灰阶梯 PNG 素材 | 新增 `t_rgb_gray_*.png` / `t_alpha_gray_*.png` | **Python 生成**，零 C++（`loadAssets()` 自动成对加载） |
| 测试 CSV 加 SDR_BG_Nit 列 | `tests/generate_test_grid.py` | Python ~10 行 |
| **SDR 背景亮度滑条** | `sdr_app.h` + `sdr_app.cpp` | **3 处小改**（见下） |

SDR 背景亮度滑条 3 处改动：

1. `sdr_app.h`：加成员 `int sdrBgNit_ = 63;`（默认 63 = 现有 18% 灰，行为不变）
2. `sdr_app.cpp` ImGui 段（FG/BG Alpha 附近）：加 `ImGui::DragInt("BG Nit", &sdrBgNit_, 1.0f, 0, 350);`
3. `sdr_app.cpp:325`：`(BG_GRAY * PAPER_WHITE_NIT)` → `(float)sdrBgNit_`

**结论：代码改动很小。** 核心渲染管线零改动；paperwhite 保持 350，无需改常量。

---

## 6. 阶段 2（暂缓）：色度锚点

待灰阶曲线拟合有结论后再补：纯饱和 R/G/B/C/M/Y 锚点（各色相的亮度上限）+ 半饱和中间色度。
