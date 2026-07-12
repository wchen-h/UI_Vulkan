#!/usr/bin/env python3
"""
Visualize test data for opaque UI (lkwg_*.csv).

Usage:
    python3 visualize_lkwg.py <table_name>

Example:
    python3 visualize_lkwg.py lkwg_2_874_160

Produces in tests/result_img/:
    - <table_name>_target_nit.png    (target Y nit = avg_Y * Y-Scale vs BG_Nit)

Since opaque UI has alpha=1.0 for most pixels, only Y-Scale curve is plotted.
Split by alpha<=0.5 and alpha>0.5 groups.
"""

import sys
import os
import csv
import numpy as np
from PIL import Image
from collections import defaultdict
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def compute_ui_stats(ui_name, pic_dir, pic_dir_lkwg):
    """Compute avg Y (nit) split by alpha<=0.5 and alpha>0.5."""
    # Try cropped_images first, then cropped_images_lkwg
    rgb_path = os.path.join(pic_dir, ui_name + '.png')
    if not os.path.exists(rgb_path):
        rgb_path = os.path.join(pic_dir_lkwg, ui_name + '.png')
    alpha_name = ui_name.replace('_rgb_', '_alpha_')
    alpha_path = os.path.join(pic_dir, alpha_name + '.png')
    if not os.path.exists(alpha_path):
        alpha_path = os.path.join(pic_dir_lkwg, alpha_name + '.png')

    rgb = np.array(Image.open(rgb_path).convert('RGB'), dtype=np.float64)
    alpha = np.array(Image.open(alpha_path).convert('L'), dtype=np.float64) / 255.0

    r_lin = srgb_to_linear(rgb[:, :, 0])
    g_lin = srgb_to_linear(rgb[:, :, 1])
    b_lin = srgb_to_linear(rgb[:, :, 2])
    y_lin = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin
    y_nit = y_lin * 350.0

    # Low group: 0 < alpha <= 0.5
    mask_low = (alpha > 0) & (alpha <= 0.5)
    a_low = alpha[mask_low]
    n_low = len(a_low)
    avg_y_low = float((y_nit[mask_low] * a_low).sum() / n_low) if n_low > 0 else 0.0

    # High group: alpha > 0.5
    mask_high = alpha > 0.5
    a_high = alpha[mask_high]
    n_high = len(a_high)
    avg_y_high = float((y_nit[mask_high] * a_high).sum() / n_high) if n_high > 0 else 0.0

    return avg_y_low, avg_y_high


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <table_name>")
        print(f"Example: {sys.argv[0]} lkwg_2_874_160")
        sys.exit(1)

    table_name = sys.argv[1]

    # Find CSV
    csv_path = None
    script_dir = os.path.dirname(os.path.abspath(__file__))
    for search_dir in ['tests', '.', script_dir]:
        candidate = os.path.join(search_dir, table_name + '.csv')
        if os.path.exists(candidate):
            csv_path = candidate
            break

    if not csv_path:
        print(f"Error: {table_name}.csv not found")
        sys.exit(1)

    with open(csv_path, encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        all_rows = list(reader)

    if not all_rows:
        print(f"Error: no data in {csv_path}")
        sys.exit(1)

    # Derive UI name from filename: lkwg_2_874_160 -> 2_rgb_874_160
    name_part = table_name.replace('lkwg_', '', 1)
    first_us = name_part.find('_')
    ui_name = name_part[:first_us] + '_rgb_' + name_part[first_us+1:]

    # Compute UI stats
    # Try both pic/cropped_images and pic/cropped_images_lkwg for lkwg UIs
    pic_dir = os.path.join(os.path.dirname(script_dir), 'pic', 'cropped_images')
    pic_dir_lkwg = os.path.join(os.path.dirname(script_dir), 'pic', 'cropped_images_lkwg')
    if not os.path.isdir(pic_dir):
        pic_dir = os.path.join(os.getcwd(), 'pic', 'cropped_images')

    avg_y_low, avg_y_high = compute_ui_stats(ui_name, pic_dir, pic_dir_lkwg)
    print(f"UI: {ui_name}")
    print(f"  alpha<=0.5: avg_Y={avg_y_low:.1f} nit")
    print(f"  alpha>0.5:  avg_Y={avg_y_high:.1f} nit")

    # Group by SDR_UI_Alpha, only keep rows with data
    groups = defaultdict(list)
    for r in all_rows:
        alpha = r['SDR_UI_Alpha']
        nit = r['匹配_UI_Nit'].strip() if r['匹配_UI_Nit'] else ''
        if not nit:
            continue
        bg = int(r['HDR_BG_Nit'])
        try:
            y_scale = float(nit)
        except ValueError:
            continue
        groups[alpha].append((bg, y_scale))

    for alpha in groups:
        groups[alpha].sort(key=lambda t: t[0])

    alphas = sorted(groups.keys(), key=float)
    if not alphas:
        print(f"Error: no filled data rows in {csv_path}")
        sys.exit(1)

    # Output directory
    out_dir = os.path.join(script_dir, 'tests', 'result_img')
    if not os.path.isdir(out_dir):
        out_dir = os.path.join(os.getcwd(), 'tests', 'result_img')
    os.makedirs(out_dir, exist_ok=True)

    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd']

    # === Plot: Target Y nit = avg_Y * Y-Scale vs BG_Nit ===
    fig, ax = plt.subplots(figsize=(12, 7))
    for i, alpha in enumerate(alphas):
        data = groups[alpha]
        pts = [(bg, ys) for bg, ys in data if ys is not None]
        if not pts:
            continue
        xs, ys_scales = zip(*pts)
        target_nits_low = [avg_y_low * s for s in ys_scales]
        target_nits_high = [avg_y_high * s for s in ys_scales]
        color = colors[i % len(colors)]
        ax.plot(xs, target_nits_low, 'o-', color=color, markersize=5, linewidth=1.8,
                label=f'SDR_Alpha={alpha} (alpha<=0.5)')
        ax.plot(xs, target_nits_high, '^--', color=color, markersize=4, linewidth=1.2,
                alpha=0.6, label=f'SDR_Alpha={alpha} (alpha>0.5)')

    ax.axhline(y=avg_y_low, color='red', linestyle='--', linewidth=1.2, alpha=0.7,
               label=f'Init avg Y = {avg_y_low:.1f} nit (alpha<=0.5)')
    ax.axhline(y=avg_y_high, color='darkred', linestyle=':', linewidth=1.2, alpha=0.7,
               label=f'Init avg Y = {avg_y_high:.1f} nit (alpha>0.5)')

    ax.set_xlabel('BG Nit', fontsize=12)
    ax.set_ylabel('Target Y (nit) = avg_Y x Y-Scale', fontsize=12)
    ax.set_title(f'{table_name} | UI={ui_name} | Target Y vs BG_Nit\n'
                 f'alpha<=0.5: avg_Y={avg_y_low:.1f} nit | alpha>0.5: avg_Y={avg_y_high:.1f} nit',
                 fontsize=11)
    ax.legend(fontsize=7, loc='best', ncol=2)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    fig.tight_layout()
    out1 = os.path.join(out_dir, f'{table_name}_target_nit.png')
    fig.savefig(out1, dpi=150)
    plt.close(fig)
    print(f"Saved: {out1}")


if __name__ == '__main__':
    main()
