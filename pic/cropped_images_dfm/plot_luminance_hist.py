#!/usr/bin/env python3
"""
UI 亮度分布直方图统计 (BT.709 + sRGB, paperWhite=350nit)

用法:
    python3 plot_luminance_hist.py                          # 默认统计 1_rgb_128_53, 1_rgb_1839_724
    python3 plot_luminance_hist.py 2_rgb_874_160 4_rgb_1531_166  # 统计指定文件(不带扩展名)

输出:
    statistics/<name>_luminance_hist.png
"""

import sys
import os
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

# ---- 配置 ----
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
STATS_DIR = os.path.join(SCRIPT_DIR, "statistics")
os.makedirs(STATS_DIR, exist_ok=True)

PAPER_WHITE_NIT = 350.0
DEFAULT_UIS = ["1_rgb_128_53", "1_rgb_1839_724"]


def srgb_to_linear(c):
    """sRGB [0,1] -> linear [0,1]"""
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def load_rgb_luminance(path):
    """加载 RGB PNG, 返回 (width, height, luminance_nit_array)
    
    亮度计算: sRGB -> linear -> BT.709 luma -> ×paperWhite
    Y = 0.2126*R_lin + 0.7152*G_lin + 0.0722*B_lin
    Y_nit = Y * paperWhite
    """
    img = Image.open(path).convert('RGB')
    arr = np.array(img, dtype=np.float64) / 255.0  # [0,1] sRGB

    r_lin = srgb_to_linear(arr[:, :, 0])
    g_lin = srgb_to_linear(arr[:, :, 1])
    b_lin = srgb_to_linear(arr[:, :, 2])

    Y = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin
    Y_nit = Y * PAPER_WHITE_NIT
    return img.size[0], img.size[1], Y_nit


def plot_luminance_hist(name, lum_nit):
    """绘制亮度直方图并保存"""
    flat = lum_nit.flatten()

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # 左图: 亮度分布 (nit), 50 bins
    ax = axes[0]
    max_nit = max(flat.max(), 1.0)
    ax.hist(flat, bins=50, range=(0, max_nit), color='steelblue', edgecolor='black', linewidth=0.3)
    ax.set_xlabel('Luminance (nit)')
    ax.set_ylabel('Pixel Count')
    ax.set_title(f'{name} — Luminance Distribution (BT.709, sRGB)')
    ax.axvline(63, color='red', linestyle='--', linewidth=1, label='18% gray @ 350nit = 63nit')
    ax.legend()

    # 右图: 累积分布函数 (CDF)
    ax = axes[1]
    sorted_lum = np.sort(flat)
    cdf = np.arange(1, len(sorted_lum) + 1) / len(sorted_lum)
    ax.plot(sorted_lum, cdf, color='steelblue', linewidth=1.5)
    ax.set_xlabel('Luminance (nit)')
    ax.set_ylabel('Cumulative Fraction')
    ax.set_title(f'{name} — CDF')
    ax.grid(True, alpha=0.3)
    ax.axvline(63, color='red', linestyle='--', linewidth=1, label='63nit')
    ax.legend()

    # 统计信息
    mean_nit = flat.mean()
    median_nit = np.median(flat)
    p10 = np.percentile(flat, 10)
    p90 = np.percentile(flat, 90)
    fig.suptitle(
        f'{name}  |  {lum_nit.shape[1]}x{lum_nit.shape[0]}  |  '
        f'Mean: {mean_nit:.1f}nit  Median: {median_nit:.1f}nit  '
        f'P10: {p10:.1f}nit  P90: {p90:.1f}nit  |  '
        f'BT.709+sRGB, PaperWhite={PAPER_WHITE_NIT:.0f}nit',
        fontsize=10
    )

    plt.tight_layout()
    out = os.path.join(STATS_DIR, f'{name}_luminance_hist.png')
    plt.savefig(out, dpi=150)
    plt.close()
    print(f'  Saved: {out}')


def main():
    uis = sys.argv[1:] if len(sys.argv) > 1 else DEFAULT_UIS
    print(f'Luminance histogram for: {uis}')
    for name in uis:
        path = os.path.join(SCRIPT_DIR, f'{name}.png')
        if not os.path.exists(path):
            print(f'  [SKIP] Not found: {path}')
            continue
        w, h, lum = load_rgb_luminance(path)
        print(f'  {name}: {w}x{h}, mean={lum.mean():.1f}nit, max={lum.max():.1f}nit')
        plot_luminance_hist(name, lum)
    print('Done.')


if __name__ == '__main__':
    main()
