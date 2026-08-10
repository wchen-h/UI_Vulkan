#!/usr/bin/env python3
"""
拟合纯黑半透明 UI 测试数据 (t_black_colorbg.csv) — Eff.Alpha 通道

模型 (与 hdr_compensation_plan_v2.md 4.2 一致):
    eff(B, a) = b(a) - (b(a) - g(a)) * exp(-B / tau(a))
    g(a)   = gamma * a^delta
    b(a)   = 1 - beta * (1-a)^b_exp            (饱和形式, a->1 时 b->1)
    tau(a) = tau0 * (a/0.5)^p_tau
    6 参数: gamma, delta, beta, b_exp, tau0, p_tau

约定: a = SDR_UI_Alpha, eff = Eff.Alpha Foreground (圆形 alpha mask, alpha_ini≈1)
输出: tests/0721/pic/fit_black_curves.png (各 alpha 拟合曲线 + 原始散点)
用法: python3 fit_black.py
"""
import os, csv
import numpy as np
from scipy.optimize import minimize
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

BASE = '/home/tuchenxi/.zeroclaw/workspace/UI_Vulkan/tests'
DATA = os.path.join(BASE, '0721', 't_black_colorbg.csv')
OUT_DIR = os.path.join(BASE, '0721', 'pic')
os.makedirs(OUT_DIR, exist_ok=True)


def load():
    rows = []
    with open(DATA, encoding='utf-8-sig', newline='') as f:
        for r in csv.DictReader(f):
            try:
                a = float(r['SDR_UI_Alpha'].strip())
                B = int(r['HDR_BG_Nit'])
                ea = r.get('Eff.Alpha Foreground', '').strip()
            except (ValueError, KeyError):
                continue
            if not ea or B < 30 or a >= 1.0:
                continue
            rows.append((a, float(B), float(ea)))
    return rows


def model(params, B, a):
    gamma, delta, beta, b_exp, tau0, p_tau = params
    g = gamma * np.power(a, delta)
    b = 1.0 - beta * np.power(1.0 - a, b_exp)
    b = np.maximum(b, g + 1e-3)        # 保证 b > g
    tau = np.maximum(tau0 * np.power(a / 0.5, p_tau), 10.0)
    return b - (b - g) * np.exp(-B / tau)


def fit(rows):
    a_vals = sorted(set(round(r[0], 4) for r in rows))
    all_a, all_B, all_eff = [], [], []
    per_curve = {}
    for a in a_vals:
        pts = sorted([(r[1], r[2]) for r in rows if abs(r[0] - a) < 0.001])
        per_curve[a] = pts
        for B, eff in pts:
            all_a.append(a); all_B.append(B); all_eff.append(eff)
    all_a = np.array(all_a); all_B = np.array(all_B, dtype=float); all_eff = np.array(all_eff)

    # 每 a 等权
    w = np.ones_like(all_eff)
    for a in a_vals:
        m = np.abs(all_a - a) < 0.001
        w[m] = 1.0 / max(m.sum(), 1) * len(a_vals)

    def loss(params):
        p = np.array(params, dtype=float)
        p[:5] = np.abs(p[:5])        # gamma,delta,beta,b_exp,tau0 > 0; p_tau 自由 (可负)
        pred = model(p, all_B, all_a)
        return np.sum(w * (pred - all_eff) ** 2)

    best, best_l = None, np.inf
    for g0 in [0.5, 0.8, 1.2]:
        for d0 in [0.8, 1.2, 1.6]:
            for be0 in [0.3, 0.5, 0.7]:
                for bx0 in [1.3, 1.7, 2.1]:
                    for t0 in [120, 195, 280]:
                        for pt0 in [-1.2, -0.8, -0.4]:
                            x0 = [g0, d0, be0, bx0, t0, pt0]
                            res = minimize(loss, x0, method='Nelder-Mead',
                                           options={'maxiter': 5000, 'xatol': 1e-6})
                            if res.fun < best_l:
                                best_l, best = res.fun, res.x.copy()
    best = best.copy()
    best[:5] = np.abs(best[:5])      # 前5个取正, p_tau 保留符号
    pred = model(best, all_B, all_a)
    rmse = float(np.sqrt(np.mean((pred - all_eff) ** 2)))
    ss_res = np.sum((all_eff - pred) ** 2)
    ss_tot = np.sum((all_eff - all_eff.mean()) ** 2)
    r2 = 1 - ss_res / ss_tot
    return best, rmse, r2, a_vals, all_a, all_B, all_eff, per_curve


def plot(params, rmse, r2, a_vals, per_curve):
    gamma, delta, beta, b_exp, tau0, p_tau = params
    fig, ax = plt.subplots(figsize=(9, 6))
    cmap = plt.cm.viridis
    for i, a in enumerate(a_vals):
        col = cmap(i / max(len(a_vals) - 1, 1))
        Bs = np.array([p[0] for p in per_curve[a]], dtype=float)
        effs = np.array([p[1] for p in per_curve[a]])
        ax.scatter(Bs, effs, color=col, s=28, zorder=3, label=f'a={a}')
        Bf = np.linspace(30, 1000, 200)
        af = np.full_like(Bf, a)
        ax.plot(Bf, model(params, Bf, af), color=col, lw=1.5, alpha=0.85)
    ax.set_xlabel('HDR Background Brightness B (nit)')
    ax.set_ylabel('Eff.Alpha')
    ax.set_title(f'纯黑 (L*=0) 半透明 UI Eff.Alpha 拟合\n'
                 f'g={gamma:.3f}·a^{delta:.2f}, b=1−{beta:.3f}·(1−a)^{b_exp:.2f}, '
                 f'τ={tau0:.0f}·(a/0.5)^{p_tau:.2f}\nRMSE={rmse:.4f}, R²={r2:.4f}')
    ax.grid(True, alpha=0.3)
    ax.legend(title='初始 alpha', ncol=2, fontsize=8)
    out = os.path.join(OUT_DIR, 'fit_black_curves.png')
    fig.tight_layout()
    fig.savefig(out, dpi=130)
    print(f'saved: {out}')


def main():
    rows = load()
    print(f'数据点: {len(rows)}, alpha: {sorted(set(round(r[0],3) for r in rows))}')
    params, rmse, r2, a_vals, all_a, all_B, all_eff, per_curve = fit(rows)
    gamma, delta, beta, b_exp, tau0, p_tau = params
    print(f'\n拟合参数 (纯黑):')
    print(f'  g(a)   = {gamma:.4f} * a^{delta:.3f}     (plan v2: 0.869 * a^1.19)')
    print(f'  b(a)   = 1 - {beta:.4f} * (1-a)^{b_exp:.3f}  (plan v2: 1 - 0.454*(1-a)^1.66)')
    print(f'  τ(a)   = {tau0:.3f} * (a/0.5)^{p_tau:.3f}    (plan v2: 195 * (a/0.5)^-0.87)')
    print(f'  RMSE = {rmse:.4f}  (plan v2: 0.022)   R² = {r2:.4f}  (plan v2: 0.991)')
    plot(params, rmse, r2, a_vals, per_curve)


if __name__ == '__main__':
    main()
