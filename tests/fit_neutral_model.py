#!/usr/bin/env python3
"""
Final model — two-group b with τ varying by α.
b>α guaranteed by definition: b = max(b_group, α) where b_group > max_α_in_group.
For α_init > b_FG, b → α (no-op for near-opaque UI).
"""
import numpy as np, csv, os
from scipy.optimize import minimize

base = '/home/tuchenxi/.zeroclaw/workspace/UI_Vulkan/tests'
with open(os.path.join(base, 'dfm_1_1839_724_colorbg.csv'), encoding='utf-8-sig') as f:
    rows = list(csv.DictReader(f))

all_data = {}
for r in rows:
    sa = float(r['SDR_UI_Alpha'].strip())
    bn = int(r['HDR_BG_Nit'])
    for src, a0, key in [("FG", 0.7054, "Eff.Alpha Foreground"),
                          ("BG", 0.2285, "Eff.Alpha Background")]:
        val = r.get(key, '').strip()
        if val:
            eff = float(val) * a0
            a_init = round(a0 * sa, 4)
            all_data.setdefault(a_init, {"B":[],"eff":[],"src":src})
            all_data[a_init]["B"].append(bn)
            all_data[a_init]["eff"].append(eff)

for ai in all_data:
    idx = np.argsort(all_data[ai]["B"])
    all_data[ai]["B"] = np.array(all_data[ai]["B"])[idx]
    all_data[ai]["eff"] = np.array(all_data[ai]["eff"])[idx]

ai_vals = sorted(all_data.keys())
all_B, all_ai, all_eff, src_arr = [], [], [], []
for ai in ai_vals:
    d = all_data[ai]
    for i in range(len(d["B"])):
        all_B.append(d["B"][i]); all_ai.append(ai)
        all_eff.append(d["eff"][i]); src_arr.append(d["src"])
all_B = np.array(all_B); all_ai = np.array(all_ai)
all_eff = np.array(all_eff); src_arr = np.array(src_arr)

weights = np.ones_like(all_eff)
for ai in ai_vals:
    mask = np.abs(all_ai - ai) < 0.001
    weights[mask] = 1.0 / mask.sum() * len(ai_vals)

# which points belong to which group's b
# FG_pts have b = b_FG, BG_pts have b = b_BG
# If α_init exceeds group asymptote, b = α (naturally satisfied)

def model_loss(params):
    b_FG, b_BG, gamma, delta, tau0, p_tau = params

    b = np.where(src_arr == 'FG', b_FG, b_BG)
    # Guarantee b > α: if α > b_group for a point, set b = α
    # (this would only happen for α outside the data range)
    b = np.maximum(b, all_ai)

    g = gamma * all_ai ** delta
    tau = tau0 * (all_ai / 0.5) ** p_tau
    pred = b - (b - g) * np.exp(-all_B / tau)
    resid = pred - all_eff

    pen = 0
    pen += 1000 * np.sum(np.maximum(-(b - g), 0)**2)   # b > g
    pen += 100 * np.sum(np.maximum(10 - tau, 0)**2)     # τ > 10

    return np.sum(weights * resid**2) + pen

best_loss = float('inf'); best_params = None
for b_fg0 in [0.80, 0.82, 0.84, 0.86]:
    for b_bg0 in [0.33, 0.34, 0.35, 0.36]:
        for t0 in [80, 95, 110, 130]:
            for pt in [-0.4, -0.6, -0.8, -1.0]:
                p0 = [b_fg0, b_bg0, 0.3, 0.7, t0, pt]
                bounds = [(0.71, 0.95), (0.23, 0.80), (0.01, 1), (0.1, 2),
                          (30, 300), (-2.0, 0)]
                res = minimize(model_loss, p0, method='L-BFGS-B', bounds=bounds,
                              options={'maxiter': 20000, 'ftol': 1e-12})
                if res.fun < best_loss:
                    best_loss = res.fun; best_params = res.x

b_FG, b_BG, gamma, delta, tau0, p_tau = best_params

# Final evaluation
b = np.where(src_arr == 'FG', b_FG, b_BG)
b = np.maximum(b, all_ai)
g = gamma * all_ai ** delta
tau = tau0 * (all_ai / 0.5) ** p_tau
pred = b - (b - g) * np.exp(-all_B / tau)
rmse = np.sqrt(np.mean((pred - all_eff)**2))
r2 = 1 - np.sum((all_eff - pred)**2) / np.sum((all_eff - np.mean(all_eff))**2)

# Print model
print("=" * 70)
print("FINAL MODEL")
print("=" * 70)
print(f"""
b(α) = b_group where b_group > α for all α in group's domain.
       For α > b_group, b(α) = α.

       FG group (α₀ = 0.7054):  b_FG = {b_FG:.4f}
       BG group (α₀ = 0.2285):  b_BG = {b_BG:.4f}

g(α) = γ × α^δ
       γ = {gamma:.4f}
       δ = {delta:.4f}

τ(α) = τ₀ × (α / 0.5)^p_τ
       τ₀  = {tau0:.1f}
       p_τ = {p_tau:.4f}

RMSE = {rmse:.4f}
R²   = {r2:.4f}
""")

# Per-curve
print(f"{'α':>8s}  {'src':>4s}  {'b':>8s}  {'g(0)':>8s}  {'τ':>8s}  {'B90%':>8s}  {'RMSE':>8s}")
for ai in ai_vals:
    mask = np.abs(all_ai - ai) < 0.001
    obs = all_eff[mask]; B_sub = all_B[mask]; s = all_data[ai]["src"]
    bv = b_FG if s == "FG" else b_BG
    bv = max(bv, ai)
    gv = gamma * ai ** delta
    tv = tau0 * (ai / 0.5) ** p_tau
    pred_sub = bv - (bv - gv) * np.exp(-B_sub / tv)
    r = np.sqrt(np.mean((pred_sub - obs)**2))
    print(f"  {ai:.4f}  {s:>4s}  {bv:.4f}  {gv:.4f}  {tv:.1f}  {tv*np.log(10):.0f}  {r:.4f}")

# Validation of b>α
print(f"\nb(α) > α validation (extrapolated):")
for a_test in [0.05, 0.14, 0.23, 0.4, 0.5, 0.6, 0.7, 0.8, 0.85, 0.9, 0.95]:
    # Map to nearest group
    bv = b_FG  # assume high-alpha case
    if a_test < 0.35:
        bv = b_BG
    bv = max(bv, a_test)
    gap = bv - a_test
    print(f"  α={a_test:.3f}: b={bv:.4f} (gap={gap:.4f})  {'✅' if gap > 0 else '❌'}")

# Monotonicity
print(f"\nMonotonicity:")
for B_t in [0, 30, 120, 500, 1000]:
    vals = []
    for ai in ai_vals:
        s = all_data[ai]["src"]
        bv = b_FG if s == "FG" else b_BG
        bv = max(bv, ai)
        gv = gamma * ai ** delta
        tv = tau0 * (ai / 0.5) ** p_tau
        vals.append(bv - (bv - gv) * np.exp(-B_t / tv))
    ok = all(vals[i] <= vals[i+1] for i in range(len(vals)-1))
    print(f"  B={B_t:>5d}: {'✅' if ok else '❌'}  " + "  ".join(f"{v:.4f}" for v in vals))

# Prediction table for all 6 curves
print(f"\nPrediction table:")
for B_t in [0, 15, 30, 60, 90, 120, 150, 200, 300, 500, 800, 1000]:
    vals = []
    for ai in ai_vals:
        s = all_data[ai]["src"]
        bv = b_FG if s == "FG" else b_BG
        bv = max(bv, ai)
        gv = gamma * ai ** delta
        tv = tau0 * (ai / 0.5) ** p_tau
        vals.append(bv - (bv - gv) * np.exp(-B_t / tv))
    row = "  ".join(f"{v:.4f}" for v in vals)
    print(f"  B={B_t:>4d}: {row}")

# Extrapolation for higher α
print(f"\nExtrapolation for α > 0.71 (using FG asymptote with clamp):")
for a_ext in [0.75, 0.80, 0.85, 0.90]:
    bv = max(b_FG, a_ext)
    gv = gamma * a_ext ** delta
    tv = tau0 * (a_ext / 0.5) ** p_tau
    print(f"  α={a_ext:.2f}: b={bv:.4f}  g(0)={gv:.4f}  τ={tv:.1f}  B90={tv*np.log(10):.0f}")
    eff_ext = [bv - (bv - gv)*np.exp(-B_t/tv) for B_t in [30, 120, 500, 1000]]
    print(f"    eff(30)={eff_ext[0]:.4f}  eff(120)={eff_ext[1]:.4f}  eff(500)={eff_ext[2]:.4f}  eff(1000)={eff_ext[3]:.4f}")

PYEOF