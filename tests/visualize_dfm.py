#!/usr/bin/env python3
"""
Visualize test data from dfm_*.csv files.

Usage:
    python3 visualize_dfm.py <table_name>

Example:
    python3 visualize_dfm.py dfm_1_1839_724

Produces two plots (all SDR_UI_Alpha curves on one figure):
    - <table_name>_UI_Nit.png    (UI_Nit vs BG_Nit, one line per SDR_UI_Alpha)
    - <table_name>_UI_Alpha.png  (UI_Alpha vs BG_Nit, one line per SDR_UI_Alpha)

X axis: HDR_BG_Nit
Y axis: 匹配_UI_Nit or 匹配_UI_Alpha
Each line = one SDR_UI_Alpha (0.2, 0.4, 0.6, 0.8, 1.0 ...)
"""

import sys
import os
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <table_name>")
        print(f"Example: {sys.argv[0]} dfm_1_1839_724")
        sys.exit(1)

    table_name = sys.argv[1]

    # Find CSV file
    csv_path = None
    for search_dir in ['tests', '.', os.path.dirname(os.path.abspath(__file__))]:
        candidate = os.path.join(search_dir, table_name + '.csv')
        if os.path.exists(candidate):
            csv_path = candidate
            break

    if not csv_path:
        print(f"Error: {table_name}.csv not found")
        sys.exit(1)

    # Read CSV
    with open(csv_path, encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        all_rows = list(reader)

    if not all_rows:
        print(f"Error: no data in {csv_path}")
        sys.exit(1)

    # Group by SDR_UI_Alpha, only keep rows with data
    from collections import defaultdict
    groups = defaultdict(list)  # alpha -> [(bg_nit, ui_nit, ui_alpha), ...]
    for r in all_rows:
        alpha = r['SDR_UI_Alpha']
        nit = r['匹配_UI_Nit'].strip() if r['匹配_UI_Nit'] else ''
        uia = r['匹配_UI_Alpha'].strip() if r['匹配_UI_Alpha'] else ''
        if not nit and not uia:
            continue  # skip empty rows
        bg = int(r['HDR_BG_Nit'])
        nit_val = float(nit) if nit else None
        uia_val = float(uia) if uia else None
        groups[alpha].append((bg, nit_val, uia_val))

    # Sort each group by BG_Nit
    for alpha in groups:
        groups[alpha].sort(key=lambda t: t[0])

    alphas = sorted(groups.keys(), key=float)
    if not alphas:
        print(f"Error: no filled data rows in {csv_path}")
        sys.exit(1)

    ui_name = all_rows[0].get('UI素材', 'unknown')
    out_dir = os.path.dirname(csv_path)

    # Color cycle for different alphas
    colors = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd',
              '#8c564b', '#e377c2', '#7f7f7f', '#bcbd22', '#17becf']

    # --- Plot 1: UI_Nit vs BG_Nit ---
    fig, ax = plt.subplots(figsize=(12, 7))
    for i, alpha in enumerate(alphas):
        data = groups[alpha]
        pts = [(bg, nit) for bg, nit, _ in data if nit is not None]
        if not pts:
            continue
        xs, ys = zip(*pts)
        color = colors[i % len(colors)]
        ax.plot(xs, ys, 'o-', color=color, markersize=5, linewidth=1.8,
                label=f'Alpha={alpha}')

    ax.set_xlabel('BG Nit', fontsize=12)
    ax.set_ylabel('UI Nit (matched)', fontsize=12)
    ax.set_title(f'{table_name} | UI={ui_name} | UI_Nit vs BG_Nit', fontsize=12)
    ax.legend(fontsize=9, loc='best')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    fig.tight_layout()
    out1 = os.path.join(out_dir, f'{table_name}_UI_Nit.png')
    fig.savefig(out1, dpi=150)
    plt.close(fig)
    print(f"Saved: {out1}")

    # --- Plot 2: UI_Alpha vs BG_Nit ---
    fig, ax = plt.subplots(figsize=(12, 7))
    for i, alpha in enumerate(alphas):
        data = groups[alpha]
        pts = [(bg, uia) for bg, _, uia in data if uia is not None]
        if not pts:
            continue
        xs, ys = zip(*pts)
        color = colors[i % len(colors)]
        ax.plot(xs, ys, 's-', color=color, markersize=5, linewidth=1.8,
                label=f'Alpha={alpha}')

    ax.set_xlabel('BG Nit', fontsize=12)
    ax.set_ylabel('UI Alpha (matched)', fontsize=12)
    ax.set_title(f'{table_name} | UI={ui_name} | UI_Alpha vs BG_Nit', fontsize=12)
    ax.legend(fontsize=9, loc='best')
    ax.grid(True, alpha=0.3)
    ax.set_xlim(left=-20)
    fig.tight_layout()
    out2 = os.path.join(out_dir, f'{table_name}_UI_Alpha.png')
    fig.savefig(out2, dpi=150)
    plt.close(fig)
    print(f"Saved: {out2}")

if __name__ == '__main__':
    main()
