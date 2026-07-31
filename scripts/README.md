# scripts/ — HDR UI 补偿与视频编码流水线

三段式流水线: 对 UI 做亮度和不透明度的 HDR 补偿 → 与 HDR 背景做 alpha 混合 → 编码 HDR10 视频。

```
 原始 HDR bin 序列            原始 HDR bin 序列
        │                            │
        ▼                            ▼
┌─────────────────┐         ┌─────────────────┐
│  adjust_ui.py   │         │   blend_ui.py   │
│  (任务1 补偿)    │ ──────► │  (任务2 混合)     │
│                 │  UI bin │                 │
└─────────────────┘         └─────────────────┘
        ▲                            │
        │                            ▼
   UI png + alpha              ┌─────────────────┐
   (固定不变)                   │  make_video.py  │
                               │  (任务3 编码)    │
                               └─────────────────┘
                                        │
                                        ▼
                                   HDR10 MP4
```

所有脚本共享 `common.py` (色彩转换 / bin 读写 / config 读取), 路径参数统一从 `config.json` 读取, 可用命令行参数覆盖。字段说明见根目录 `config.json` (各占位值即注释)。

---

## 依赖

| 包       | 用途                          |
|----------|-------------------------------|
| numpy    | 数值计算                       |
| Pillow   | 读取 UI png                    |
| scipy    | 8 连通域识别 (bbox)            |

```bash
pip install numpy pillow scipy
```

任务3 编码需要定制的 `ffmpeg_venc` (带 `-vmeta_url` 参数), 在 `config.task3.encoder_script` 指定路径。

---

## 输入格式

### UI 素材 (固定, 所有帧共用)

| 文件        | 格式                                   | 说明                          |
|-------------|----------------------------------------|-------------------------------|
| `alpha.png` | 8-bit 灰度 (L), 1080×2328             | UI 不透明度, 0=透明 255=不透明 |
| `rgb.png`   | 8-bit sRGB BT.709 (RGB), 1080×2328     | UI 颜色 (预乘前), 黑色像素=无 UI |

两者由 `extract_ui_full.py` 从 `black.png` + `white.png` 提取:
- `alpha = 1 − (white − black).r / 255`
- `rgb = black / alpha` (alpha 裁剪下限 1e-5)

### HDR bin (逐帧, 原始背景, 不含 UI)

- 格式: A2B10G10R10 UNORM (Vulkan `VK_FORMAT_A2B10G10R10_UNORM_PACK32`)
- 色域: PQ ST.2084 + BT.2020, 1080×2328, 单帧无文件头
- 存储: 小端 uint32, 每像素 4 字节, 位布局 `R[0:9] | G[10:19] | B[20:29] | A[30:31]`, 10-bit 值 ÷1023 归一化
- 帧大小: 1080 × 2328 × 4 = 10,056,960 字节

---

## 三段任务

### 任务1: `adjust_ui.py` — UI 补偿调整

对每个 UI 像素, 根据其所在区域的 HDR 背景亮度 B 计算**有效不透明度 (Eff.Alpha)** 和**亮度缩放 (Y-Scale)**。

**流程:**
1. 在 `alpha.png` 上做 8 连通域识别 → UI 的 bbox; 删除 bbox 内 `alpha>0` 像素占比 < 50% 的碎片
2. 合并重叠 bbox; 每个 bbox 取 2 倍宽高的背景区域 (越界裁到图像边界), 算 BT.2020 luma 平均得 B, clamp [30, 1000] nit
3. 逐像素算 Eff.Alpha (中性/彩色分支), 线性 nits 域做 Y-Scale
4. 输出两个 bin:
   - `<hdr名>_uiAlpha.bin` — 单通道 fp16, 每像素 Eff.Alpha [0,1]
   - `<hdr名>_uiRGB.bin` — A2B10G10R10 PQ BT.2020 (A=1), 补偿后 UI 颜色

**补偿模型** (详见 `tests/hdr_compensation_plan/hdr_compensation_plan_v2.md` §4.2/4.3/5.1/5.2):

| 量           | 公式                                                    |
|--------------|---------------------------------------------------------|
| Eff.Alpha    | `eff = b(f,a) − (b(f,a)−g(f,a))·exp(−B/τ(f,a))`         |
| 彩色修正     | `eff += Δ_f(B)` (彩色像素, 且 `eff+Δ ≤ 1` 才加)         |
| Y-Scale      | `ys = 1 + fy·(ys1(B) − 1)`                              |
| f/fy 插值    | f=0 退化为无补偿, f=fy=1 为完整补偿 (config 可调)        |

```bash
python3 adjust_ui.py --config config.json [--hdr-dir X] [--outdir Y]
```

### 任务2: `blend_ui.py` — alpha 混合

在**线性 nits BT.2020 域**把补偿后 UI 混到 HDR 背景上 (与 `hdr_ui.frag:154-158` 一致):

```
bgNit = pqDecode(hdr_bin)
uiNit = pqDecode(ui_rgb_bin)          # 任务1 输出
mixed = uiNit · eff + bgNit · (1 − eff)   # eff 来自任务1 _uiAlpha.bin
out = linearToPQ(clip(mixed, 0, 10000)) → A2B10G10R10 (A=1)
```

UI 外 `eff=0` → `mixed = bgNit` (原背景不变)。输出 `<hdr名>_withUI.bin` (A2B10G10R10 PQ BT.2020)。

```bash
python3 blend_ui.py --config config.json [--hdr-dir X] [--ui-bin-dir Y] [--outdir Z]
```

> `task2.ui_bin_dir` 应指向任务1 的 `outdir`; `task2.hdr_dir` 与任务1 相同 (原始背景 bin)。

### 任务3: `make_video.py` — HDR10 视频编码

沿用 `commands.txt` 两步式:
1. 生成 `metadata.txt` (HDR Vivid 动态元数据, 每行=帧号从1 + 固定十进制 payload; **文件已存在则跳过**, 复用已有 metadata)
2. 拼接所有 `_withUI.bin` → raw → `ffmpeg_venc` 转 yuv420p10le → libx265 编码 HDR10 MP4

编码参数: BT.2020 + PQ, master-display/max-cll=0, fps=50, bitrate=10M, `-tag:v hvc1`。

```bash
python3 make_video.py --config config.json [--blended-dir X] [--video-out Y] [--keep-temp]
```

> `task3.blended_dir` 应指向任务2 的 `outdir`; `task3.encoder_script` 指向 `ffmpeg_venc` 可执行文件。

---

## 命令行参数

所有脚本都用 `--config` 指定 config.json 路径 (默认项目根目录的 `config.json`), 其余参数覆盖 config 中对应字段 (命令行优先, 留空则用 config 值)。字段说明见根目录 `config.json` (各值即注释)。

### `adjust_ui.py`

| 参数           | 默认        | 说明                                  |
|----------------|-------------|---------------------------------------|
| `--config`     | config.json | config.json 路径                      |
| `--hdr-dir`    | (config)    | 覆盖 `task1.hdr_dir`, 原始 HDR bin 目录 |
| `--outdir`     | (config)    | 覆盖 `task1.outdir`, UI bin 输出目录   |

### `blend_ui.py`

| 参数           | 默认        | 说明                                  |
|----------------|-------------|---------------------------------------|
| `--config`     | config.json | config.json 路径                      |
| `--hdr-dir`    | (config)    | 覆盖 `task2.hdr_dir`, 原始 HDR bin 目录 |
| `--ui-bin-dir` | (config)    | 覆盖 `task2.ui_bin_dir`, 任务1 输出目录 |
| `--outdir`     | (config)    | 覆盖 `task2.outdir`, 混合 bin 输出目录  |

### `make_video.py`

| 参数              | 默认        | 说明                                      |
|-------------------|-------------|-------------------------------------------|
| `--config`        | config.json | config.json 路径                          |
| `--blended-dir`   | (config)    | 覆盖 `task3.blended_dir`, `_withUI.bin` 目录 |
| `--video-out`     | (config)    | 覆盖 `task3.video_out`, 输出 MP4 路径     |
| `--keep-temp`     | (flag)      | 保留中间 raw/yuv 临时文件 (默认删除)       |

任务1/2 的脚本自动按后缀排除 (`_uiAlpha.bin` / `_uiRGB.bin` / `_withUI.bin`) 来识别原始 HDR bin, 因此原始 bin 与输出可放同一目录 (但建议分开)。

---

## 端到端用法

```bash
cd <项目根>

# 1. UI 补偿调整 (每帧 → _uiAlpha.bin + _uiRGB.bin)
python3 scripts/adjust_ui.py

# 2. 与 HDR 背景混合 (每帧 → _withUI.bin)
python3 scripts/blend_ui.py

# 3. 编码 HDR10 视频 (→ output.mp4 + metadata.txt)
python3 scripts/make_video.py
```

也可临时覆盖某段参数, 例如只重跑任务1 的输出目录:

```bash
python3 scripts/adjust_ui.py --outdir /tmp/task1_out
```

---

## 文件清单

| 文件              | 说明                                    |
|-------------------|-----------------------------------------|
| `common.py`       | 共享: 色彩转换 / bin 读写 / config 常量  |
| `adjust_ui.py`    | 任务1: UI 补偿调整                       |
| `blend_ui.py`     | 任务2: alpha 混合                        |
| `make_video.py`   | 任务3: HDR10 编码                        |
| `commands.txt`    | ffmpeg_venc 两步编码参考命令 (用户提供)   |

## 相关参考

- 根目录 `config.json` — 配置文件 (字段以占位值注释)
- `tests/hdr_compensation_plan/hdr_compensation_plan_v2.md` — 补偿模型完整推导 (v2 最新)
- `shaders/hdr_ui.frag` — 测试 shader (流程参照 :113-158)
- `workspace/python/extract_ui_full.py` — UI png 提取脚本
- `pic/ui/{alpha,rgb,black,white}.png` — UI 素材
