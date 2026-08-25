#!/usr/bin/env python3
"""
绘制 0721 测试数据曲线图

用法:
    python3 plot_0721.py                        # 绘制所有 UI
    python3 plot_0721.py t_neutral              # 绘制单个 UI
    python3 plot_0721.py t_neutral t_redorange  # 绘制多个 UI

输出 (tests/0721/pic/):
    <ui_name>_eff.png       — Eff.Alpha vs BG Nit (各 alpha 档)
    <ui_name>_yscale.png    — Y-Scale vs BG Nit (各 alpha 档)
    compare_eff.png         — 全部 UI 对比 (alpha=0.5)
    compare_yscale.png      — 全部 UI Y-Scale 对比 (alpha=0.5)
"""

import sys
import os
import csv
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(PROJECT_ROOT, "tests", "0721")
IMAGE_DIR = os.path.join(PROJECT_ROOT, "pic", "cropped_images")
OUTPUT_DIR = os.path.join(DATA_DIR, "pic")
PER_UI_DIR = os.path.join(OUTPUT_DIR, "per_ui")
COMPARE_DIR = os.path.join(OUTPUT_DIR, "compare")

ALL_UIS = [
    "t_neutral", "t_redorange", "t_redorange_mid",
    "t_green", "t_green_mid", "t_bluepurple", "t_bluepurple_mid",
]

UI_COLORS = {
    "t_neutral":       "#808080",
    "t_redorange":     "#d62728",
    "t_redorange_mid": "#e07a6a",
    "t_green":         "#2ca02c",
    "t_green_mid":     "#5fb85f",
    "t_bluepurple":   "#1f77b4",
    "t_bluepurple_mid": "#5b94c4",
}


def compute_alpha_ini(ui_name):
    """从 alpha 图像计算前景平均 alpha"""
    parts = ui_name.split('_', 1)
    prefix, rest = parts
    path = os.path.join(IMAGE_DIR, f"{prefix}_alpha_{rest}.png")
    img = Image.open(path)
    if img.mode != 'L':
        img = img.convert('L')
    arr = np.array(img, dtype=np.float64) / 255.0
    fg = arr[arr > 0.5]
    return float(np.mean(fg)) if len(fg) > 0 else 1.0


def load_csv(ui_name):
    """加载 CSV, 返回 {sdr_alpha: [(bg_nit, yscale, eff_alpha), ...]}"""
    path = os.path.join(DATA_DIR, f"{ui_name}_colorbg.csv")
    with open(path, encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    groups = {}
    for r in rows:
        bn = int(r['HDR_BG_Nit'])
        if bn == 0:
            continue
        sa = r['SDR_UI_Alpha'].strip()
        # 彩色 UI 只统计 alpha >= 0.5
        if ui_name != "t_neutral" and float(sa) < 0.5:
            continue
        ys = r.get('Y-Scale Foreground', '').strip()
        ea = r.get('Eff.Alpha Foreground', '').strip()
        ys_val = float(ys) if ys else None
        ea_val = float(ea) if ea else None
        if ys_val is None and ea_val is None:
            continue
        groups.setdefault(sa, []).append((bn, ys_val, ea_val))

    for sa in groups:
        groups[sa].sort(key=lambda t: t[0])
    return groups


def plot_single_ui(ui_name, alpha_ini):
    """绘制单个 UI 的 Eff.Alpha 和 Y-Scale 曲线"""
    data = load_csv(ui_name)
    if not data:
        print(f"  [SKIP] No data for {ui_name}")
        return

    alphas = sorted(data.keys(), key=float)
    color_map = plt.cm.viridis(np.linspace(0.1, 0.9, len(alphas)))

    # --- Eff.Alpha plot ---
    fig, ax = plt.subplots(figsize=(14, 8))
    for i, sa in enumerate(alphas):
        pts = [(bn, alpha_ini * ea) for bn, _, ea in data[sa] if ea is not None]
        if not pts:
            continue
        xs, ys = zip(*pts)
        ax.plot(xs, ys, 'o-', color=color_map[i], markersize=4, linewidth=1.5,
                label=f'alpha={sa}')
        # 起始点标注 (左侧)
        ax.annotate(f'{ys[0]:.3f}', xy=(xs[0], ys[0]),
                    textcoords='offset points', xytext=(-30, 5),
                    fontsize=6, color=color_map[i],
                    ha='right', va='bottom')
        # 终点标注 (右侧)
        ax.annotate(f'{ys[-1]:.3f}', xy=(xs[-1], ys[-1]),
                    textcoords='offset points', xytext=(30, -5),
                    fontsize=6, color=color_map[i],
                    ha='left', va='top')

    ax.set_xlabel('BG Nit', fontsize=12)
    ax.set_ylabel('Effective Alpha (alpha_ini × Eff.Alpha)', fontsize=12)
    ax.set_title(f'{ui_name} | alpha_ini={alpha_ini:.4f} | Eff.Alpha vs BG Nit', fontsize=11)
    ax.legend(fontsize=8, loc='lower right', ncol=2)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    ax.axhline(0, color='gray', linewidth=0.5)
    plt.tight_layout()
    out = os.path.join(PER_UI_DIR, f"{ui_name}_eff.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  {out}")

    # --- Y-Scale plot ---
    fig, ax = plt.subplots(figsize=(12, 7))
    for i, sa in enumerate(alphas):
        pts = [(bn, ys) for bn, ys, _ in data[sa] if ys is not None]
        if not pts:
            continue
        xs, ys = zip(*pts)
        ax.plot(xs, ys, 's-', color=color_map[i], markersize=4, linewidth=1.5,
                label=f'alpha={sa}')

    ax.set_xlabel('BG Nit', fontsize=12)
    ax.set_ylabel('Y-Scale', fontsize=12)
    ax.set_title(f'{ui_name} | alpha_ini={alpha_ini:.4f} | Y-Scale vs BG Nit', fontsize=11)
    ax.legend(fontsize=8, loc='lower right', ncol=2)
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    ax.axhline(1.0, color='gray', linewidth=0.5, linestyle='--')
    plt.tight_layout()
    out = os.path.join(PER_UI_DIR, f"{ui_name}_yscale.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"  {out}")


def plot_compare(ui_names, alpha_ini_map):
    """绘制全部 UI 在 alpha=0.5 时的对比图"""
    compare_alpha = "0.5"

    # --- Eff.Alpha compare ---
    fig, ax = plt.subplots(figsize=(14, 8))
    has_data = False
    for ui in ui_names:
        data = load_csv(ui)
        if compare_alpha not in data:
            continue
        alpha_ini = alpha_ini_map[ui]
        pts = [(bn, alpha_ini * ea) for bn, _, ea in data[compare_alpha] if ea is not None]
        if not pts:
            continue
        xs, ys = zip(*pts)
        color = UI_COLORS.get(ui, '#333')
        ax.plot(xs, ys, 'o-', color=color, markersize=5, linewidth=1.8,
                label=f'{ui} (a={alpha_ini*0.5:.3f})')
        has_data = True

    if has_data:
        ax.set_xlabel('BG Nit', fontsize=12)
        ax.set_ylabel('Effective Alpha', fontsize=12)
        ax.set_title(f'Eff.Alpha Comparison (SDR_Alpha={compare_alpha})', fontsize=11)
        ax.legend(fontsize=8, loc='lower right')
        ax.grid(True, alpha=0.3)
        ax.set_xlim(left=-20)
        ax.axhline(0, color='gray', linewidth=0.5)
        plt.tight_layout()
        out = os.path.join(COMPARE_DIR, "compare_eff.png")
        plt.savefig(out, dpi=150)
        plt.close()
        print(f"  {out}")
    else:
        plt.close()

    # --- Y-Scale compare ---
    fig, ax = plt.subplots(figsize=(14, 8))
    has_data = False
    for ui in ui_names:
        data = load_csv(ui)
        if compare_alpha not in data:
            continue
        pts = [(bn, ys) for bn, ys, _ in data[compare_alpha] if ys is not None]
        if not pts:
            continue
        xs, ys = zip(*pts)
        color = UI_COLORS.get(ui, '#333')
        ax.plot(xs, ys, 's-', color=color, markersize=5, linewidth=1.8,
                label=ui)
        has_data = True

    if has_data:
        ax.set_xlabel('BG Nit', fontsize=12)
        ax.set_ylabel('Y-Scale', fontsize=12)
        ax.set_title(f'Y-Scale Comparison (SDR_Alpha={compare_alpha})', fontsize=11)
        ax.legend(fontsize=8, loc='lower right')
        ax.grid(True, alpha=0.3)
        ax.set_xlim(left=-20)
        ax.axhline(1.0, color='gray', linewidth=0.5, linestyle='--')
        plt.tight_layout()
        out = os.path.join(COMPARE_DIR, "compare_yscale.png")
        plt.savefig(out, dpi=150)
        plt.close()
        print(f"  {out}")
    else:
        plt.close()


def main():
    os.makedirs(PER_UI_DIR, exist_ok=True)
    os.makedirs(COMPARE_DIR, exist_ok=True)
    ui_names = sys.argv[1:] if len(sys.argv) > 1 else ALL_UIS
    print(f"Plotting: {ui_names}")

    alpha_ini_map = {}
    for ui in ui_names:
        print(f"\n[{ui}]")
        alpha_ini = compute_alpha_ini(ui)
        alpha_ini_map[ui] = alpha_ini
        print(f"  alpha_ini = {alpha_ini:.6f}")
        plot_single_ui(ui, alpha_ini)

    if len(ui_names) > 1:
        print(f"\n[Compare]")
        plot_compare(ui_names, alpha_ini_map)

    print("\nDone.")


if __name__ == '__main__':
    main()
