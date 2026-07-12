#!/usr/bin/env python3
"""
Analyze UI PNG assets: alpha histogram + Y (luminance) histogram.

Usage:
    python3 analyze_ui.py <UI_name>

Example:
    python3 analyze_ui.py 1_rgb_128_53

Reads:
    pic/cropped_images/<UI_name>.png        (RGB, sRGB 8-bit)
    pic/cropped_images/<UI_name with _rgb_ -> _alpha_>.png  (alpha, 8-bit grayscale)

Outputs (to UI_info/):
    <UI_name>_alpha_hist.png   - histogram of alpha values (alpha!=0 pixels only)
    <UI_name>_Y_hist.png       - histogram of Y (BT.709 luma) from RGB (alpha!=0 pixels only)

Y is computed in linear domain: sRGB decode -> linear -> Y = 0.2126R + 0.7152G + 0.0722B
Then converted to nit: Y_nit = Y_linear * 350 (PAPER_WHITE_NIT)
"""

import sys
import os
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def srgb_to_linear(c):
    c = c / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <UI_name>")
        print(f"Example: {sys.argv[0]} 1_rgb_128_53")
        sys.exit(1)

    ui_name = sys.argv[1]

    # Find RGB and alpha PNGs
    base_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pic', 'cropped_images')
    if not os.path.isdir(base_dir):
        base_dir = os.path.join(os.getcwd(), 'pic', 'cropped_images')

    rgb_path = os.path.join(base_dir, ui_name + '.png')
    alpha_name = ui_name.replace('_rgb_', '_alpha_')
    alpha_path = os.path.join(base_dir, alpha_name + '.png')

    if not os.path.exists(rgb_path):
        print(f"Error: {rgb_path} not found")
        sys.exit(1)
    if not os.path.exists(alpha_path):
        print(f"Error: {alpha_path} not found")
        sys.exit(1)

    # Load images
    rgb_img = Image.open(rgb_path).convert('RGB')
    alpha_img = Image.open(alpha_path).convert('L')

    rgb = np.array(rgb_img, dtype=np.float64)
    alpha = np.array(alpha_img, dtype=np.float64)

    # Mask: alpha != 0
    mask = alpha > 0
    alpha_vals = alpha[mask]
    total_pixels = alpha.size
    nonzero_pixels = mask.sum()

    print(f"UI: {ui_name}")
    print(f"  Size: {rgb_img.size[0]}x{rgb_img.size[1]} = {total_pixels} pixels")
    print(f"  Alpha!=0 pixels: {nonzero_pixels} ({100*nonzero_pixels/total_pixels:.1f}%)")
    print(f"  Alpha range: {alpha_vals.min():.0f} - {alpha_vals.max():.0f}")

    # Output directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    out_dir = os.path.join(script_dir, 'UI_info')
    if not os.path.isdir(out_dir):
        out_dir = os.path.join(os.getcwd(), 'UI_info')
    os.makedirs(out_dir, exist_ok=True)

    # === Plot 1: Alpha histogram ===
    fig, ax = plt.subplots(figsize=(10, 6))
    # Use 256 bins for 8-bit (0-255), but only show 1-255 (alpha=0 excluded)
    bins = np.arange(0.5, 256.5, 1)  # bin centers at 1, 2, ..., 255
    ax.hist(alpha_vals, bins=bins, color='steelblue', edgecolor='white', linewidth=0.3)
    ax.set_xlabel('Alpha value (0-255)', fontsize=12)
    ax.set_ylabel('Pixel count', fontsize=12)
    ax.set_title(f'{ui_name} | Alpha distribution (alpha!=0 pixels, n={nonzero_pixels})', fontsize=11)
    ax.set_xlim(0, 255)
    ax.grid(True, alpha=0.3, axis='y')
    # Add stats text
    stats_text = f'mean={alpha_vals.mean():.1f}\nmedian={np.median(alpha_vals):.0f}\nstd={alpha_vals.std():.1f}'
    ax.text(0.97, 0.95, stats_text, transform=ax.transAxes, fontsize=10,
            verticalalignment='top', horizontalalignment='right',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    fig.tight_layout()
    out1 = os.path.join(out_dir, f'{ui_name}_alpha_hist.png')
    fig.savefig(out1, dpi=150)
    plt.close(fig)
    print(f"Saved: {out1}")

    # === Plot 2: Y (luminance) histogram ===
    # Compute Y in linear domain for alpha!=0 pixels
    r_lin = srgb_to_linear(rgb[:, :, 0])
    g_lin = srgb_to_linear(rgb[:, :, 1])
    b_lin = srgb_to_linear(rgb[:, :, 2])
    y_lin = 0.2126 * r_lin + 0.7152 * g_lin + 0.0722 * b_lin
    y_nit = y_lin[mask] * 350.0  # convert to nit (PAPER_WHITE_NIT)

    print(f"  Y (nit) range: {y_nit.min():.1f} - {y_nit.max():.1f}")
    print(f"  Y (nit) mean={y_nit.mean():.1f}, median={np.median(y_nit):.1f}")

    fig, ax = plt.subplots(figsize=(10, 6))
    # Use 50 bins for continuous nit values
    n_bins = 50
    ax.hist(y_nit, bins=n_bins, color='darkorange', edgecolor='white', linewidth=0.3)
    ax.set_xlabel('Y (nit, linear luminance)', fontsize=12)
    ax.set_ylabel('Pixel count', fontsize=12)
    ax.set_title(f'{ui_name} | Y (luminance) distribution (alpha!=0 pixels, n={nonzero_pixels})', fontsize=11)
    ax.grid(True, alpha=0.3, axis='y')
    # Add stats text
    stats_text2 = f'mean={y_nit.mean():.1f} nit\nmedian={np.median(y_nit):.1f} nit\nstd={y_nit.std():.1f} nit\nmin={y_nit.min():.1f} nit\nmax={y_nit.max():.1f} nit'
    ax.text(0.97, 0.95, stats_text2, transform=ax.transAxes, fontsize=10,
            verticalalignment='top', horizontalalignment='right',
            bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))
    fig.tight_layout()
    out2 = os.path.join(out_dir, f'{ui_name}_Y_hist.png')
    fig.savefig(out2, dpi=150)
    plt.close(fig)
    print(f"Saved: {out2}")


if __name__ == '__main__':
    main()
