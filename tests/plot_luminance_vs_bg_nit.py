#!/usr/bin/env python3
"""
绘制 UI 亮度 vs 背景亮度曲线（对比两个 lkwg 数据集）

从 RGB 图像计算前景 UI 的平均初始亮度 (BT.709 + 350nit)，
乘以 CSV 中的 y-scale 倍数，绘制随背景亮度变化的曲线。

用法:
    python3 plot_luminance_vs_bg_nit.py

输入文件:
    pic/cropped_images/<rgb>.png     — UI RGB 图像
    pic/cropped_images/<alpha>.png   — UI alpha 图像
    tests/lkwg_2_874_160.csv          — 调节数据 (用 E 列 = 匹配_ChromaScale)
    tests/lkwg_1_1954_38.csv          — 调节数据 (用 C 列 = 匹配_UI_Nit)

输出:
    tests/compare_luminance_vs_bg_nit.png
"""

import os
import csv
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# ---- 配置 ----
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_DIR = os.path.join(PROJECT_ROOT, "pic", "cropped_images")
CSV_DIR = os.path.join(PROJECT_ROOT, "tests")
OUTPUT_DIR = CSV_DIR

PAPER_WHITE_NIT = 350.0

# 数据集配置: csv_name → (rgb_image, alpha_image, y-scale 列名, 显示名)
DATASETS = [
    {
        "csv": "lkwg_2_874_160.csv",
        "rgb": "2_rgb_874_160.png",
        "alpha": "2_alpha_874_160.png",
        "column": "匹配_ChromaScale",
        "label": "2_874_160",
        "color": "#d62728",
        "marker": 'o',
    },
    {
        "csv": "lkwg_1_1954_38.csv",
        "rgb": "1_rgb_1954_30.png",
        "alpha": "1_alpha_1954_30.png",
        "column": "匹配_UI_Nit",
        "label": "1_1954_38",
        "color": "#1f77b4",
        "marker": 's',
    },
]


def srgb_to_linear(c):
    """sRGB [0,1] -> linear [0,1]"""
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def compute_avg_luminance(rgb_path, alpha_path):
    """计算前景 UI 的平均初始亮度 (BT.709 luma × paperWhite)

    前景: alpha > 0.5 的像素
    """
    rgb_img = Image.open(rgb_path).convert('RGB')
    rgb_arr = np.array(rgb_img, dtype=np.float64) / 255.0

    alpha_img = Image.open(alpha_path)
    if alpha_img.mode != 'L':
        alpha_img = alpha_img.convert('L')
    alpha_arr = np.array(alpha_img, dtype=np.float64) / 255.0

    r_lin = srgb_to_linear(rgb_arr[:, :, 0])
    g_lin = srgb_to_linear(rgb_arr[:, :, 1])
    b_lin = srgb_to_linear(rgb_arr[:, :, 2])

    Y = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin
    Y_nit = Y * PAPER_WHITE_NIT

    fg_mask = alpha_arr > 0.5
    fg_pixels = Y_nit[fg_mask]

    if len(fg_pixels) == 0:
        print("  [WARN] No foreground pixels")
        return 0.0, 0

    return float(np.mean(fg_pixels)), int(len(fg_pixels))


def load_csv_y_scale(csv_path, column_name):
    """加载 CSV, 返回 SDR=1.0 的 [(bg_nit, y_scale), ...]"""
    with open(csv_path, encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    pts = []
    for r in rows:
        sa = r['SDR_UI_Alpha'].strip()
        if sa != '1':
            continue
        val_str = r.get(column_name, '').strip()
        if not val_str:
            continue
        bg_nit = int(r['HDR_BG_Nit'])
        y_scale = float(val_str)
        pts.append((bg_nit, y_scale))

    pts.sort(key=lambda t: t[0])
    return pts


def main():
    print("Luminance vs BG Nit plot")

    fig, ax = plt.subplots(figsize=(14, 8))

    for ds in DATASETS:
        csv_path = os.path.join(CSV_DIR, ds["csv"])
        rgb_path = os.path.join(IMAGE_DIR, ds["rgb"])
        alpha_path = os.path.join(IMAGE_DIR, ds["alpha"])

        print(f"\n[{ds['label']}]")
        print(f"  CSV: {csv_path}")
        print(f"  RGB: {rgb_path}")

        avg_lum, n_px = compute_avg_luminance(rgb_path, alpha_path)
        print(f"  Avg luminance (FG): {avg_lum:.2f} nit ({n_px} pixels)")

        pts = load_csv_y_scale(csv_path, ds["column"])
        print(f"  Data points: {len(pts)}")

        if not pts:
            print("  [SKIP] No data")
            continue

        xs = [p[0] for p in pts]
        ys = [p[1] * avg_lum for p in pts]
        result_0 = pts[0][1] * avg_lum

        ax.plot(xs, ys, ds["marker"] + '-', color=ds["color"], markersize=5, linewidth=1.8,
                label=f'{ds["label"]}: y-scale × {avg_lum:.1f}nit')
        ax.annotate(f'{result_0:.1f}', xy=(xs[0], ys[0]),
                    textcoords='offset points', xytext=(6, 4),
                    fontsize=7, color=ds["color"], fontweight='bold')

    ax.set_xlabel('BG Nit', fontsize=13)
    ax.set_ylabel('UI Luminance (y-scale × avg initial luminance)', fontsize=13)
    ax.set_title('UI Luminance vs BG Nit (compare)', fontsize=12)
    ax.legend(fontsize=9, loc='lower right')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    ax.axhline(0, color='gray', linewidth=0.5)

    plt.tight_layout()
    out = os.path.join(OUTPUT_DIR, "compare_luminance_vs_bg_nit.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"\nSaved: {out}")
    print("Done.")


if __name__ == '__main__':
    main()
