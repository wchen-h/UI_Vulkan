# SDR/HDR 主观匹配实验 — 测试设计与表格说明

> 配套数据表：`matching_experiment_data.csv`（由 `generate_test_grid.py` 生成）

---

## 一、测试目的

在 **SDR + HDR 双显示器**环境下，研究"达到与 SDR 主观视觉一致时，HDR 侧所需的亮度/透明度/chroma 参数关系"，并验证 **Hunt 效应**（HDR 高亮度使 colorfulness $M$ 放大 $\approx 5.6\%$，需降低 chroma $\approx 5.3\%$ 补偿，详见 `../HUNT_EFFECT_CAUSAL_CHAIN_ANALYSIS.md`）。

### 匹配任务

- **SDR 窗口（基准）**：固定 UI 素材 + 固定 Alpha，背景固定 350 nit，UI 亮度不可调 → 参照画面
- **HDR 窗口（被试调节）**：被试调节参数，使 HDR 窗口 UI 的主观感受与 SDR 一致
- **记录**：达成匹配时的 HDR 参数组合

---

## 二、测试方案（三层嵌套遍历）

| 层级 | 变量 | 取值 | 组数 | 角色 |
|------|------|------|------|------|
| Layer 1 | **SDR_UI_Alpha** | 0.1 ~ 1.0，步长 0.1 | **10** | 自变量（实验者设定） |
| Layer 2 | **HDR_BG_Nit** | 50 ~ 1000 nit，步长 50 | **20**¹ | 自变量（实验者设定） |
| Layer 3 | **UI 素材** | `pic/cropped_images/*_rgb_*.png` | **12**² | 遍历对象 |
| — | SDR_BG_Nit | 固定 350 nit | 1 | 常量 |

> ¹ BG=0 已剔除——黑背景下恒亮度 Lock 使 Eff.Alpha 对渲染完全失效（见 `ISSUES_SUMMARY.md §11`）。50~1000 步长 50 含两端共 20 点。
> ² 实际素材为 12 个（`pic/cropped_images` 下 `_rgb_` 文件数）。之前口述的 13 与之有出入。

**测试点总数** = 10 × 20 × 12 = **2400**（CSV 含表头共 2401 行）

### 单次试验流程（每个 UI 组合）

1. 固定 SDR_UI_Alpha（本组 block）和 HDR_BG_Nit（本组 sub-block）
2. 在 HDR 窗口选定当前 UI 素材
3. **调节 HDR 的 UI Nit** 至接近
4. 按 **Lock**（锁定 `uiLum×effAlpha + bgNit×(1-effAlpha)`，见 `src/hdr_app.cpp:190`）
5. **调节 UI Alpha** 使主观亮度与 SDR 一致
6. **微调 ChromaScale**（`src/hdr_app.cpp:197`）使色彩完全一致
7. **记录**：UI Nit、UI Alpha、ChromaScale

### 循环顺序

```
for SDR_UI_Alpha in [0.1 .. 1.0]:          # 最外层 block
    for HDR_BG_Nit in [50, 100 .. 1000]:     # 中层 sub-block
        for UI in 所有UI素材:               # 最内层逐个测
            调节 -> Lock -> 调Alpha -> 微调chroma -> 记录
```

---

## 三、表格设计（`matching_experiment_data.csv`）

每行 = 一次试验 = 一个数据点。UTF-8 with BOM（Excel/WPS 中文不乱码）。

| 列名 | 含义 | 填写方式 |
|------|------|----------|
| `SDR_BG_Nit(固定)` | SDR 背景亮度，全程 350 | 预填 |
| `SDR_UI_Alpha` | 当前 SDR Alpha block（自变量） | 预填 |
| `HDR_BG_Nit` | 当前 HDR 背景亮度（自变量） | 预填 |
| `UI素材` | 当前 UI 组合名 | 预填 |
| `匹配_UI_Nit` | 达成匹配时的 HDR UI Lum Nit（**因变量**） | 被试填 |
| `匹配_UI_Alpha` | 达成匹配时的 Eff. Alpha（**因变量**，Lock 后调节值） | 被试填 |
| `匹配_ChromaScale` | 达成匹配时的 Chroma Scale（**因变量**，Hunt 补偿） | 被试填 |
| `备注` | 现象观察（色偏、Hunt 效应、异常等） | 被试填 |

**设计要点**：
- 前 4 列为实验条件（预填，含固定常量 SDR_BG_Nit 保证数据自描述）
- 后 4 列为记录项，3 个因变量对应被试调节的 3 个物理量 + 备注
- 数据排序：SDR_UI_Alpha → HDR_BG_Nit → UI素材，与遍历顺序一致，便于按 block 填写

---

## 四、环境信息

见 `test_environment.csv`（显示器型号、GPU、驱动、Vulkan 版本、环境光等，测试前填一次）。

---

## 五、重新生成 / 调整网格

修改 `generate_test_grid.py` 顶部参数后重跑：

```bash
cd tests
python3 generate_test_grid.py
```

可调项：`SDR_BG_NIT_FIXED`、`SDR_ALPHAS`、`HDR_BG_NITS`、`ASSET_DIR`。

---

## 六、程序控件对照

| CSV 列 | 程序控件 | 源码位置 |
|---------|----------|----------|
| SDR_UI_Alpha | ImGui "Alpha" 滑条 (0.1~1.0) | `src/sdr_app.cpp:159` |
| HDR_BG_Nit | ImGui "BG Nit" 滑条 (0~1000) | `src/hdr_app.cpp` |
| 匹配_UI_Nit | ImGui "UI Lum Nit" 滑条 (0~1000) | `src/hdr_app.cpp` |
| 匹配_UI_Alpha | ImGui "Eff. Alpha" 滑条 (0~1.0)，Lock 后调节 | `src/hdr_app.cpp:196` |
| 匹配_ChromaScale | ImGui "Chroma Scale" (0~2.0) | `src/hdr_app.cpp:197` |
| Lock | "Lock"/"Unlock" 按钮 | `src/hdr_app.cpp:188` |
