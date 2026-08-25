#!/usr/bin/env python3
"""
UI Alpha 分布直方图统计

用法:
    python3 plot_alpha_hist.py                          # 默认统计 1_alpha_128_53, 1_alpha_1839_724
    python3 plot_alpha_hist.py 2_alpha_874_160 4_alpha_1531_166  # 统计指定文件(不带扩展名)

输出:
    statistics/<name>_alpha_hist.png
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
DEFAULT_UIS = ["1_alpha_128_53", "1_alpha_1839_724"]


def load_alpha(path):
    """加载 alpha PNG, 返回 (width, height, alpha_array[float 0-1])"""
    img = Image.open(path)
    if img.mode != 'L':
        img = img.convert('L')
    arr = np.array(img, dtype=np.float32) / 255.0
    return img.size[0], img.size[1], arr


def plot_alpha_hist(name, alpha):
    """绘制 alpha 直方图并保存"""
    flat = alpha.flatten()
    nonzero = flat[flat > 0]

    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # 左图: 全范围 [0, 1], 50 bins
    ax = axes[0]
    ax.hist(flat, bins=50, range=(0, 1), color='steelblue', edgecolor='black', linewidth=0.3)
    ax.set_xlabel('Alpha')
    ax.set_ylabel('Pixel Count')
    ax.set_title(f'{name} — Alpha Distribution (all pixels)')
    ax.axvline(0.5, color='red', linestyle='--', linewidth=1, label='fg/bg threshold (0.5)')
    ax.legend()

    # 右图: 分前景(>0.5) / 背景(0,0.5] / 透明(=0)
    ax = axes[1]
    bins_fg = np.linspace(0.5, 1.0, 30)
    bins_bg = np.linspace(0.001, 0.5, 30)
    ax.hist(nonzero[nonzero > 0.5], bins=bins_fg, color='orange', alpha=0.7, label=f'Foreground (>0.5): {np.sum(nonzero > 0.5)} px')
    ax.hist(nonzero[(nonzero > 0) & (nonzero <= 0.5)], bins=bins_bg, color='skyblue', alpha=0.7, label=f'Background (0,0.5]: {np.sum((nonzero > 0) & (nonzero <= 0.5))} px')
    ax.set_xlabel('Alpha')
    ax.set_ylabel('Pixel Count')
    ax.set_title(f'{name} — Foreground vs Background')
    ax.legend()

    # 统计信息
    total = flat.size
    zero_pct = np.sum(flat == 0) / total * 100
    fg_pct = np.sum(flat > 0.5) / total * 100
    bg_pct = np.sum((flat > 0) & (flat <= 0.5)) / total * 100
    fig.suptitle(
        f'{name}  |  {alpha.shape[1]}x{alpha.shape[0]}  |  '
        f'Transparent: {zero_pct:.1f}%  FG: {fg_pct:.1f}%  BG: {bg_pct:.1f}%  |  '
        f'PaperWhite={PAPER_WHITE_NIT:.0f}nit',
        fontsize=10
    )

    plt.tight_layout()
    out = os.path.join(STATS_DIR, f'{name}_alpha_hist.png')
    plt.savefig(out, dpi=150)
    plt.close()
    print(f'  Saved: {out}')


def main():
    uis = sys.argv[1:] if len(sys.argv) > 1 else DEFAULT_UIS
    print(f'Alpha histogram for: {uis}')
    for name in uis:
        path = os.path.join(SCRIPT_DIR, f'{name}.png')
        if not os.path.exists(path):
            print(f'  [SKIP] Not found: {path}')
            continue
        w, h, alpha = load_alpha(path)
        print(f'  {name}: {w}x{h}')
        plot_alpha_hist(name, alpha)
    print('Done.')


if __name__ == '__main__':
    main()
