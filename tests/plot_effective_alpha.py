#!/usr/bin/env python3
"""
绘制 UI 绝对有效不透明度 vs 背景亮度曲线

从 alpha 图像计算前景/背景 UI 的初始不透明度 (Alpha_ini)，
乘以 CSV 中的 Eff.Alpha 调节倍数，得到绝对有效不透明度，
绘制随背景亮度变化的曲线。

用法:
    python3 plot_effective_alpha.py                          # 默认 UI=1_128_53
    python3 plot_effective_alpha.py 1_1839_724               # 指定 UI 名称
    python3 plot_effective_alpha.py 1_128_53 1_1839_724      # 多个 UI

输入文件:
    pic/cropped_images_dfm/<name>_alpha.png    — alpha 图像
    tests/<name>_colorbg.csv                   — 调节数据

输出:
    tests/<name>_effective_alpha.png          — 曲线图
"""

import sys
import os
import csv
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# ---- 配置 ----
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ALPHA_DIR = os.path.join(PROJECT_ROOT, "pic", "cropped_images_dfm")
CSV_DIR = os.path.join(PROJECT_ROOT, "tests")
OUTPUT_DIR = CSV_DIR

# 只绘制这三组 SDR alpha
SDR_ALPHAS_TO_PLOT = ["1", "0.8", "0.6"]
SDR_ALPHA_LABELS = {"1": "1.0", "0.8": "0.8", "0.6": "0.6"}

# 前景/背景颜色 (每个 SDR alpha 一组色)
COLORS_FG = {"1.0": "#d62728", "0.8": "#ff7f0e", "0.6": "#bcbd22"}
COLORS_BG = {"1.0": "#1f77b4", "0.8": "#2ca02c", "0.6": "#9467bd"}

MARKER_FG = 'o'
MARKER_BG = 's'


def load_alpha_image(name):
    """加载 alpha PNG, 返回 float32 [0,1] 数组

    name 格式: "1_128_53" → 文件名 "1_alpha_128_53.png"
    """
    parts = name.split('_', 1)
    if len(parts) != 2:
        print(f"  [ERROR] Invalid name format: {name}")
        return None
    prefix, rest = parts
    filename = f"{prefix}_alpha_{rest}.png"
    path = os.path.join(ALPHA_DIR, filename)
    if not os.path.exists(path):
        print(f"  [ERROR] Alpha image not found: {path}")
        return None
    img = Image.open(path)
    if img.mode != 'L':
        img = img.convert('L')
    return np.array(img, dtype=np.float64) / 255.0


def compute_initial_alphas(alpha_arr):
    """计算前景和背景的初始不透明度 (加权平均)

    前景: alpha > 0.5 的像素, 权重 = 像素数 / 总非零像素数
    背景: 0 < alpha <= 0.5 的像素, 同上
    """
    fg_mask = alpha_arr > 0.5
    bg_mask = (alpha_arr > 0.0) & (alpha_arr <= 0.5)

    fg_pixels = alpha_arr[fg_mask]
    bg_pixels = alpha_arr[bg_mask]
    total_nz = len(fg_pixels) + len(bg_pixels)

    if len(fg_pixels) == 0 or len(bg_pixels) == 0 or total_nz == 0:
        print(f"  [WARN] Empty fg or bg pixels: fg={len(fg_pixels)}, bg={len(bg_pixels)}")
        return 0.0, 0.0, 0.0, 0.0

    alpha_ini_fg = np.mean(fg_pixels)
    alpha_ini_bg = np.mean(bg_pixels)
    w_fg = len(fg_pixels) / total_nz
    w_bg = len(bg_pixels) / total_nz

    return alpha_ini_fg, alpha_ini_bg, w_fg, w_bg


def load_csv_data(name, sdr_alphas):
    """加载 CSV, 返回 {sdr_alpha: [(bg_nit, eff_alpha_fg, eff_alpha_bg), ...]}

    name 格式: "1_128_53" → 文件名 "dfm_1_128_53_colorbg.csv"
    """
    path = os.path.join(CSV_DIR, f"dfm_{name}_colorbg.csv")
    if not os.path.exists(path):
        print(f"  [ERROR] CSV not found: {path}")
        return None

    with open(path, encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    groups = {}
    for r in rows:
        sa = r['SDR_UI_Alpha'].strip()
        if sa not in sdr_alphas:
            continue
        fg_a = r.get('Eff.Alpha Foreground', '').strip()
        bg_a = r.get('Eff.Alpha Background', '').strip()
        if not fg_a and not bg_a:
            continue
        bg_nit = int(r['HDR_BG_Nit'])
        fg_val = float(fg_a) if fg_a else None
        bg_val = float(bg_a) if bg_a else None
        groups.setdefault(sa, []).append((bg_nit, fg_val, bg_val))

    for sa in groups:
        groups[sa].sort(key=lambda t: t[0])

    return groups


def plot_effective_alpha(name, alpha_arr):
    """绘制绝对有效不透明度 vs 背景亮度"""
    alpha_ini_fg, alpha_ini_bg, w_fg, w_bg = compute_initial_alphas(alpha_arr)
    print(f"  Alpha_ini_fg = {alpha_ini_fg:.6f} (weight={w_fg:.4f})")
    print(f"  Alpha_ini_bg = {alpha_ini_bg:.6f} (weight={w_bg:.4f})")

    csv_data = load_csv_data(name, SDR_ALPHAS_TO_PLOT)
    if csv_data is None or not csv_data:
        print(f"  [SKIP] No CSV data for {name}")
        return

    fig, ax = plt.subplots(figsize=(14, 8))

    for sdr_alpha in SDR_ALPHAS_TO_PLOT:
        if sdr_alpha not in csv_data:
            print(f"  [SKIP] No data for SDR_Alpha={sdr_alpha}")
            continue

        data = csv_data[sdr_alpha]
        sdr_label = SDR_ALPHA_LABELS[sdr_alpha]

        # 前景曲线: Y = Alpha_ini_fg × Eff.Alpha_FG
        fg_pts = [(bg, alpha_ini_fg * fg) for bg, fg, _ in data if fg is not None]
        if fg_pts:
            xs, ys = zip(*fg_pts)
            sdr_ref_fg = alpha_ini_fg * float(sdr_alpha)
            color = COLORS_FG.get(sdr_label, '#333333')
            ax.plot(xs, ys, MARKER_FG + '-', color=color, markersize=5, linewidth=1.8,
                    label=f'{alpha_ini_fg:.3f} * {sdr_label} = {sdr_ref_fg:.3f} (FG)')

        # 背景曲线: Y = Alpha_ini_bg × Eff.Alpha_BG
        bg_pts = [(bg, alpha_ini_bg * bg) for bg, _, bg in data if bg is not None]
        if bg_pts:
            xs, ys = zip(*bg_pts)
            sdr_ref_bg = alpha_ini_bg * float(sdr_alpha)
            color = COLORS_BG.get(sdr_label, '#666666')
            ax.plot(xs, ys, MARKER_BG + '--', color=color, markersize=4, linewidth=1.5,
                    label=f'{alpha_ini_bg:.3f} * {sdr_label} = {sdr_ref_bg:.3f} (BG)')

    ax.set_xlabel('BG Nit', fontsize=13)
    ax.set_ylabel('Effective Alpha (Alpha_ini × Eff.Alpha)', fontsize=13)
    ax.set_title(
        f'{name} | Effective Alpha vs BG Nit\n'
        f'Alpha_ini: FG={alpha_ini_fg:.3f} (w={w_fg:.2f})  BG={alpha_ini_bg:.3f} (w={w_bg:.2f})',
        fontsize=12
    )
    ax.legend(fontsize=9, loc='best', ncol=2)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    ax.axhline(0, color='gray', linewidth=0.5)

    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, f"dfm_{name}_effective_alpha.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  Saved: {out}")


def main():
    names = sys.argv[1:] if len(sys.argv) > 1 else ["1_128_53"]
    print(f"Effective alpha plot for: {names}")

    for name in names:
        print(f"\n[{name}]")
        alpha_arr = load_alpha_image(name)
        if alpha_arr is None:
            continue
        print(f"  Alpha image: {alpha_arr.shape[1]}x{alpha_arr.shape[0]}")
        plot_effective_alpha(name, alpha_arr)

    print("\nDone.")


if __name__ == '__main__':
    main()
