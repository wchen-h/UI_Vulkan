#!/usr/bin/env python3
"""
拟合 0721 测试数据 — 不透明度通道

模型: eff(B) = b - (b - g) * exp(-B / tau)
只拟合 B >= 30, alpha < 1.0 的数据

分步策略:
  第一步: 中性色基线 — 拟合 g(a), b(a), tau(a)
  第二步: 引入彩色数据 — 检验色彩度 c 对 (g, b, tau) 的影响

用法:
    python3 fit_0721.py              # 完整拟合 + 输出图
    python3 fit_0721.py --neutral    # 仅中性色

输出 (tests/0721/pic/):
    fit_neutral_curves.png   — 各 alpha 曲线拟合效果图
    fit_neutral_params.png   — g(a), b(a), tau(a) 参数图
    fit_color_compare.png    — 彩色 vs 中性色对比图
"""

import sys
import os
import csv
import numpy as np
from scipy.optimize import minimize, curve_fit
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(PROJECT_ROOT, "tests", "0721")
IMAGE_DIR = os.path.join(PROJECT_ROOT, "pic", "cropped_images")
OUTPUT_DIR = os.path.join(DATA_DIR, "pic")
FIT_DIR = os.path.join(OUTPUT_DIR, "fit")

PAPER_WHITE_NIT = 350.0

# 彩色 UI 的 CIELAB 色度信息
UI_INFO = {
    "t_neutral":         {"L": 50, "a": 0,   "b": 0,    "chroma": 0},
    "t_redorange":       {"L": 50, "a": 70,  "b": 45,   "chroma": 83.0},
    "t_redorange_mid":   {"L": 50, "a": 35,  "b": 22.5, "chroma": 41.5},
    "t_green":           {"L": 50, "a": -70, "b": 40,   "chroma": 80.6},
    "t_green_mid":       {"L": 50, "a": -35, "b": 20,   "chroma": 40.3},
    "t_bluepurple":      {"L": 50, "a": 30,  "b": -90,  "chroma": 94.9},
    "t_bluepurple_mid":  {"L": 50, "a": 15,  "b": -45,  "chroma": 47.4},
}


def compute_alpha_ini(ui_name):
    """从 alpha 图像计算前景平均 alpha"""
    parts = ui_name.split('_', 1)
    path = os.path.join(IMAGE_DIR, f"{parts[0]}_alpha_{parts[1]}.png")
    from PIL import Image
    img = Image.open(path)
    if img.mode != 'L':
        img = img.convert('L')
    arr = np.array(img, dtype=np.float64) / 255.0
    fg = arr[arr > 0.5]
    return float(np.mean(fg)) if len(fg) > 0 else 1.0


def load_csv_data(ui_name, alpha_ini):
    """加载 CSV, 返回 [(a, B, eff, yscale), ...]

    a = alpha_ini * SDR_UI_Alpha
    eff = alpha_ini * Eff.Alpha_Foreground
    只返回 B >= 30 且有 Eff.Alpha 数据的行
    """
    path = os.path.join(DATA_DIR, f"{ui_name}_colorbg.csv")
    with open(path, encoding='utf-8-sig', newline='') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    data = []
    for r in rows:
        bn = int(r['HDR_BG_Nit'])
        if bn < 30:
            continue
        sa = r['SDR_UI_Alpha'].strip()
        if sa == "1":
            continue
        # 彩色 UI 只统计 alpha >= 0.5
        if ui_name != "t_neutral" and float(sa) < 0.5:
            continue
        ea = r.get('Eff.Alpha Foreground', '').strip()
        ys = r.get('Y-Scale Foreground', '').strip()
        if not ea:
            continue
        a_val = alpha_ini * float(sa)
        eff_val = alpha_ini * float(ea)
        ys_val = float(ys) if ys else None
        data.append((a_val, bn, eff_val, ys_val))
    return data


def fit_neutral():
    """第一步: 中性色基线拟合"""
    ui = "t_neutral"
    alpha_ini = compute_alpha_ini(ui)
    print(f"[Neutral] alpha_ini = {alpha_ini:.6f}")

    raw = load_csv_data(ui, alpha_ini)
    if not raw:
        print("[Neutral] No data")
        return None

    # 按 a 分组
    a_vals = sorted(set(round(r[0], 4) for r in raw))
    all_B, all_a, all_eff = [], [], []
    per_curve = {}
    for a in a_vals:
        pts = [(r[1], r[2]) for r in raw if abs(r[0] - a) < 0.001]
        pts.sort()
        per_curve[a] = pts
        for B, eff in pts:
            all_B.append(B)
            all_a.append(a)
            all_eff.append(eff)
    all_B = np.array(all_B, dtype=np.float64)
    all_a = np.array(all_a, dtype=np.float64)
    all_eff = np.array(all_eff, dtype=np.float64)

    print(f"  {len(a_vals)} alpha levels, {len(all_eff)} data points")
    print(f"  a range: [{min(a_vals):.4f}, {max(a_vals):.4f}]")

    # --- 模型: eff(B; a) = b(a) - (b(a) - g(a)) * exp(-B / tau(a)) ---
    # 参数化:
    #   g(a) = gamma * a^delta
    #   b(a) = b0 + b1 * a  (线性, 保证 b > a)
    #   tau(a) = tau0 * (a / 0.5)^p_tau
    # 总参数: gamma, delta, b0, b1, tau0, p_tau

    def model(params, B, a):
        gamma, delta, b0, b1, tau0, p_tau = params
        g = gamma * a ** delta
        b = b0 + b1 * a
        b = np.maximum(b, a + 0.01)
        tau = np.maximum(tau0 * (a / 0.5) ** p_tau, 10.0)
        return b - (b - g) * np.exp(-B / tau)

    def loss(params):
        pred = model(params, all_B, all_a)
        resid = pred - all_eff
        # 权重: 每个 a 值等权
        w = np.ones_like(all_eff)
        for a in a_vals:
            mask = np.abs(all_a - a) < 0.001
            w[mask] = 1.0 / mask.sum() * len(a_vals)
        return np.sum(w * resid ** 2)

    best_loss = float('inf')
    best_params = None
    for b0_0 in [0.85, 0.90, 0.95]:
        for b1_0 in [0.05, 0.10, 0.15]:
            for g0 in [0.15, 0.25, 0.35]:
                for d0 in [0.5, 0.8, 1.2]:
                    for t0 in [80, 120, 160]:
                        for pt in [-0.4, -0.6, -0.8]:
                            p0 = [g0, d0, b0_0, b1_0, t0, pt]
                            bounds = [(0.01, 1), (0.1, 2),
                                      (0.5, 1.5), (-0.2, 0.5),
                                      (30, 400), (-2.0, 0)]
                            try:
                                res = minimize(loss, p0, method='L-BFGS-B', bounds=bounds,
                                               options={'maxiter': 20000, 'ftol': 1e-12})
                                if res.fun < best_loss:
                                    best_loss = res.fun
                                    best_params = res.x
                            except Exception:
                                pass

    if best_params is None:
        print("[Neutral] Fitting failed")
        return None

    gamma, delta, b0, b1, tau0, p_tau = best_params
    pred = model(best_params, all_B, all_a)
    rmse = np.sqrt(np.mean((pred - all_eff) ** 2))
    ss_res = np.sum((all_eff - pred) ** 2)
    ss_tot = np.sum((all_eff - np.mean(all_eff)) ** 2)
    r2 = 1 - ss_res / ss_tot

    print(f"\n  g(a) = {gamma:.4f} * a^{delta:.4f}")
    print(f"  b(a) = {b0:.4f} + {b1:.4f}*a")
    print(f"  tau(a) = {tau0:.1f} * (a/0.5)^{p_tau:.4f}")
    print(f"  RMSE = {rmse:.4f}, R² = {r2:.4f}")

    # --- 绘制拟合曲线 ---
    fig, axes = plt.subplots(1, 2, figsize=(16, 7))
    ax = axes[0]
    color_map = plt.cm.viridis(np.linspace(0.1, 0.9, len(a_vals)))
    for i, a in enumerate(a_vals):
        pts = per_curve[a]
        xs_obs = [p[0] for p in pts]
        ys_obs = [p[1] for p in pts]
        xs_fit = np.linspace(30, 1000, 200)
        ys_fit = model(best_params, xs_fit, a)
        ax.plot(xs_obs, ys_obs, 'o', color=color_map[i], markersize=5)
        ax.plot(xs_fit, ys_fit, '-', color=color_map[i], linewidth=1.5,
                label=f'a={a:.3f}')
    ax.set_xlabel('BG Nit')
    ax.set_ylabel('Effective Alpha')
    ax.set_title(f'Neutral: Data vs Fit (RMSE={rmse:.4f}, R²={r2:.4f})')
    ax.legend(fontsize=7, loc='lower right', ncol=2)
    ax.grid(True, alpha=0.3)

    # --- 参数图 ---
    ax = axes[1]
    a_arr = np.linspace(0.05, 1.0, 200)
    g_arr = gamma * a_arr ** delta
    b_arr = np.maximum(b0 + b1 * a_arr, a_arr + 0.01)
    tau_arr = np.maximum(tau0 * (a_arr / 0.5) ** p_tau, 10.0)

    ax.plot(a_arr, g_arr, 'b-', label=f'g(a) = {gamma:.3f}*a^{delta:.3f}')
    ax.plot(a_arr, b_arr, 'r-', label=f'b(a) = {b0:.3f}+{b1:.3f}*a')
    ax.plot(a_arr, a_arr, 'k--', alpha=0.3, label='a (identity)')
    ax.set_xlabel('a (initial opacity)')
    ax.set_ylabel('Parameter value')
    ax.set_title('Model Parameters')
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    ax2 = ax.twinx()
    ax2.plot(a_arr, tau_arr, 'g--', label=f'τ(a) = {tau0:.1f}*(a/0.5)^{p_tau:.3f}')
    ax2.set_ylabel('τ (nits)', color='green')
    ax2.legend(fontsize=8, loc='upper left')

    plt.tight_layout()
    out = os.path.join(FIT_DIR, "fit_neutral_curves.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"\n  Saved: {out}")

    return {
        "params": best_params,
        "alpha_ini": alpha_ini,
        "rmse": rmse,
        "r2": r2,
        "a_vals": a_vals,
        "per_curve": per_curve,
    }


def fit_color(neutral_result):
    """第二步: 引入彩色数据, 检验 c 的影响"""
    if neutral_result is None:
        print("[Color] No neutral baseline, skipping")
        return

    neutral_params = neutral_result["params"]
    alpha_ini = neutral_result["alpha_ini"]

    colored_uis = ["t_redorange", "t_redorange_mid",
                  "t_green", "t_green_mid",
                  "t_bluepurple", "t_bluepurple_mid"]

    fig, axes = plt.subplots(2, 3, figsize=(18, 10))
    axes = axes.flatten()

    for idx, ui in enumerate(colored_uis):
        ui_alpha_ini = compute_alpha_ini(ui)
        raw = load_csv_data(ui, ui_alpha_ini)
        if not raw:
            print(f"[Color] {ui}: no data")
            continue

        a_vals = sorted(set(round(r[0], 4) for r in raw))
        all_B, all_a, all_eff = [], [], []
        per_curve = {}
        for a in a_vals:
            pts = [(r[1], r[2]) for r in raw if abs(r[0] - a) < 0.001]
            pts.sort()
            per_curve[a] = pts
            for B, eff in pts:
                all_B.append(B)
                all_a.append(a)
                all_eff.append(eff)
        all_B = np.array(all_B)
        all_a = np.array(all_a)
        all_eff = np.array(all_eff)

        # 用中性色模型预测彩色数据
        def neutral_model(B, a):
            gamma, delta, b0, b1, tau0, p_tau = neutral_params
            g = gamma * a ** delta
            b = np.maximum(b0 + b1 * a, a + 0.01)
            tau = np.maximum(tau0 * (a / 0.5) ** p_tau, 10.0)
            return b - (b - g) * np.exp(-B / tau)

        neutral_pred = neutral_model(all_B, all_a)
        neutral_rmse = np.sqrt(np.mean((neutral_pred - all_eff) ** 2))

        # 逐曲线单独拟合 (3 自由参数 per curve: g, b, tau)
        ax = axes[idx]
        color_map = plt.cm.viridis(np.linspace(0.2, 0.8, len(a_vals)))
        for i, a in enumerate(a_vals):
            pts = per_curve[a]
            xs_obs = np.array([p[0] for p in pts])
            ys_obs = np.array([p[1] for p in pts])

            def single_model(B, g, b, tau):
                return b - (b - g) * np.exp(-B / tau)

            try:
                p0 = [a * 0.5, max(a + 0.1, 0.8), 100]
                popt, _ = curve_fit(single_model, xs_obs, ys_obs, p0=p0,
                                     bounds=([0, a, 10], [a, 1.5, 500]),
                                     maxfev=10000)
                g_fit, b_fit, tau_fit = popt
            except Exception:
                g_fit, b_fit, tau_fit = [np.nan] * 3

            xs_fit = np.linspace(30, 1000, 200)
            ys_fit = single_model(xs_fit, g_fit, b_fit, tau_fit)

            ax.plot(xs_obs, ys_obs, 'o', color=color_map[i], markersize=4)
            ax.plot(xs_fit, ys_fit, '-', color=color_map[i], linewidth=1.2,
                    label=f'a={a:.3f}')
            # 中性色预测 (虚线)
            ys_neutral = neutral_model(xs_fit, a)
            ax.plot(xs_fit, ys_neutral, ':', color=color_map[i], alpha=0.5, linewidth=1)

        chroma = UI_INFO[ui]["chroma"]
        ax.set_xlabel('BG Nit')
        ax.set_ylabel('Eff.Alpha')
        ax.set_title(f'{ui} (c={chroma:.1f})\nneutral_pred RMSE={neutral_rmse:.4f}')
        ax.legend(fontsize=6, loc='lower right', ncol=2)
        ax.grid(True, alpha=0.3)
        ax.set_xlim(left=20)

        print(f"[Color] {ui} (c={chroma:.1f}): {len(all_eff)} pts, "
              f"neutral_pred RMSE={neutral_rmse:.4f}")

    plt.tight_layout()
    out = os.path.join(FIT_DIR, "fit_color_compare.png")
    plt.savefig(out, dpi=150)
    plt.close()
    print(f"\n  Saved: {out}")


def main():
    os.makedirs(FIT_DIR, exist_ok=True)
    neutral_only = "--neutral" in sys.argv

    print("=" * 60)
    print("Step 1: Neutral baseline fitting")
    print("=" * 60)
    result = fit_neutral()

    if not neutral_only and result:
        print()
        print("=" * 60)
        print("Step 2: Color influence analysis")
        print("=" * 60)
        fit_color(result)

    print("\nDone.")


if __name__ == '__main__':
    main()
