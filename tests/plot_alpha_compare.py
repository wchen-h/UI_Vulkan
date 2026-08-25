#!/usr/bin/env python3
"""
绘制多个 UI 的绝对有效不透明度对比图（同一张图）

将多个 CSV 文件的数据绘制在同一张图中，便于对比不同 UI 的亮度调节行为。

用法:
    python3 plot_alpha_compare.py                          # 默认对比 1_128_53 和 1_1839_724
    python3 plot_alpha_compare.py 1_128_53 1_1839_724      # 指定 UI

输入文件:
    pic/cropped_images_dfm/<prefix>_alpha_<rest>.png  — alpha 图像
    tests/dfm_<name>_colorbg.csv                           — 调节数据

输出:
    tests/compare_alpha_vs_bg_nit.png
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

DEFAULT_UIS = ["1_128_53", "1_1839_724"]

SDR_ALPHAS_TO_PLOT = ["1", "0.8", "0.6"]
SDR_ALPHA_LABELS = {"1": "1.0", "0.8": "0.8", "0.6": "0.6"}

# 每个 UI 一组色系：1_128_53 暖色，1_1839_724 冷色
COLORS = {
    "1_128_53":   {"1.0": "#d62728", "0.8": "#ff7f0e", "0.6": "#2ca02c"},
    "1_1839_724": {"1.0": "#1f77b4", "0.8": "#9467bd", "0.6": "#17becf"},
}
MARKER_FG = 'o'
MARKER_BG = 's'


def load_alpha_image(name):
    """加载 alpha PNG, 返回 float64 [0,1] 数组"""
    parts = name.split('_', 1)
    if len(parts) != 2:
        print(f"  [ERROR] Invalid name format: {name}")
        return None
    prefix, rest = parts
    path = os.path.join(ALPHA_DIR, f"{prefix}_alpha_{rest}.png")
    if not os.path.exists(path):
        print(f"  [ERROR] Alpha image not found: {path}")
        return None
    img = Image.open(path)
    if img.mode != 'L':
        img = img.convert('L')
    return np.array(img, dtype=np.float64) / 255.0


def compute_initial_alphas(alpha_arr):
    """计算前景和背景的初始不透明度 (算术平均)

    前景: alpha > 0.5 的像素
    背景: 0 < alpha <= 0.5 的像素
    """
    fg_pixels = alpha_arr[alpha_arr > 0.5]
    bg_pixels = alpha_arr[(alpha_arr > 0.0) & (alpha_arr <= 0.5)]

    if len(fg_pixels) == 0 or len(bg_pixels) == 0:
        print(f"  [WARN] Empty fg or bg pixels: fg={len(fg_pixels)}, bg={len(bg_pixels)}")
        return 0.0, 0.0

    return float(np.mean(fg_pixels)), float(np.mean(bg_pixels))


def load_csv_data(name, sdr_alphas):
    """加载 CSV, 返回 {sdr_alpha: [(bg_nit, eff_alpha_fg, eff_alpha_bg), ...]}"""
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


def plot_compare(names):
    """将多个 UI 的数据绘制在同一张图中"""
    ui_data = {}
    for name in names:
        print(f"\n[{name}]")
        alpha_arr = load_alpha_image(name)
        if alpha_arr is None:
            continue
        alpha_ini_fg, alpha_ini_bg = compute_initial_alphas(alpha_arr)
        print(f"  Alpha_ini_fg = {alpha_ini_fg:.6f}")
        print(f"  Alpha_ini_bg = {alpha_ini_bg:.6f}")
        csv_data = load_csv_data(name, SDR_ALPHAS_TO_PLOT)
        if csv_data is None:
            continue
        ui_data[name] = {
            "alpha_ini_fg": alpha_ini_fg,
            "alpha_ini_bg": alpha_ini_bg,
            "csv_data": csv_data,
        }

    if not ui_data:
        print("No data to plot.")
        return

    fig, ax = plt.subplots(figsize=(16, 9))

    for name, info in ui_data.items():
        alpha_ini_fg = info["alpha_ini_fg"]
        alpha_ini_bg = info["alpha_ini_bg"]
        csv_data = info["csv_data"]
        ui_colors = COLORS.get(name, {"1.0": "#333", "0.8": "#666", "0.6": "#999"})

        for sdr_alpha in SDR_ALPHAS_TO_PLOT:
            if sdr_alpha not in csv_data:
                continue
            data = csv_data[sdr_alpha]
            sdr_label = SDR_ALPHA_LABELS[sdr_alpha]
            sdr_val = float(sdr_label)
            color = ui_colors.get(sdr_label, '#333333')

            # 前景: Y = Alpha_ini_fg × Eff.Alpha_FG
            fg_pts = [(nit, alpha_ini_fg * fg_val) for nit, fg_val, _ in data if fg_val is not None]
            if fg_pts:
                xs, ys = zip(*fg_pts)
                result_fg = alpha_ini_fg * sdr_val
                ax.plot(xs, ys, MARKER_FG + '-', color=color, markersize=5, linewidth=1.8,
                        label=f'{alpha_ini_fg:.3f} * {sdr_label} = {result_fg:.3f} (FG, {name})')
                ax.annotate(f'{result_fg:.3f}', xy=(xs[0], ys[0]),
                            textcoords='offset points', xytext=(6, 4),
                            fontsize=7, color=color, fontweight='bold')

            # 背景曲线: Y = Alpha_ini_bg × Eff.Alpha_BG
            bg_pts = [(nit, alpha_ini_bg * bg_val) for nit, _, bg_val in data if bg_val is not None]
            if bg_pts:
                xs, ys = zip(*bg_pts)
                result_bg = alpha_ini_bg * sdr_val
                ax.plot(xs, ys, MARKER_BG + '--', color=color, markersize=4, linewidth=1.5,
                        label=f'{alpha_ini_bg:.3f} * {sdr_label} = {result_bg:.3f} (BG, {name})')
                ax.annotate(f'{result_bg:.3f}', xy=(xs[0], ys[0]),
                            textcoords='offset points', xytext=(6, -8),
                            fontsize=7, color=color, fontweight='bold')

    ax.set_xlabel('BG Nit', fontsize=13)
    ax.set_ylabel('Effective Alpha (Alpha_ini × Eff.Alpha)', fontsize=13)

    alpha_ini_fg_0 = next(iter(ui_data.values()))["alpha_ini_fg"]
    alpha_ini_bg_0 = next(iter(ui_data.values()))["alpha_ini_bg"]
    title_lines = []
    for name, info in ui_data.items():
        title_lines.append(
            f'{name}: FG={info["alpha_ini_fg"]:.3f}  BG={info["alpha_ini_bg"]:.3f}'
        )
    ax.set_title(
        f'Effective Alpha vs BG Nit (compare)\n' + '  |  '.join(title_lines),
        fontsize=12
    )
    ax.legend(fontsize=8, loc='lower right', ncol=2)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    ax.set_ylim(top=1.0)
    ax.axhline(0, color='gray', linewidth=0.5)

    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "compare_alpha_vs_bg_nit.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"\nSaved: {out}")


def main():
    names = sys.argv[1:] if len(sys.argv) > 1 else DEFAULT_UIS
    print(f"Compare effective alpha for: {names}")
    plot_compare(names)
    print("Done.")


if __name__ == '__main__':
    main()
