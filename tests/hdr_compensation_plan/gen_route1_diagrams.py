#!/usr/bin/env python3
"""Generate Route 1 separation-based adjustment diagrams (English, ASCII-only)."""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import FancyBboxPatch, Rectangle, Patch

plt.rcParams['font.sans-serif'] = ['DejaVu Sans']
plt.rcParams['font.family'] = 'sans-serif'
plt.rcParams['axes.unicode_minus'] = False

OUT = '/home/tuchenxi/.zeroclaw/workspace/UI_Vulkan/tests/hdr_compensation_plan/pic_route1'


# ============================================================
# Fig 1: Route 1 pipeline overview
# ============================================================
def fig_pipeline():
    fig, ax = plt.subplots(figsize=(14, 9))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 10)
    ax.axis('off')
    ax.set_aspect('equal')

    def box(x, y, w, h, text, color='#4A90D9', textcolor='white', fontsize=9):
        rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.15",
                              facecolor=color, edgecolor='#2C5F8A', linewidth=1.5)
        ax.add_patch(rect)
        ax.text(x + w / 2, y + h / 2, text, ha='center', va='center',
                fontsize=fontsize, color=textcolor, weight='bold')

    def arrow(x1, y1, x2, y2):
        ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle='->', color='#333', lw=1.8))

    box(0.3, 8.2, 2.5, 1.2, 'SDR mixed bin\n(sRGB+BT.709)', '#E8A838')
    box(0.3, 6.5, 2.5, 1.2, 'UI bbox\n(x0,y0,x1,y1)', '#E8A838')
    box(0.3, 4.8, 2.5, 1.0, 'blend domain\nsrgb / linear', '#E8A838')

    box(3.5, 6.8, 2.5, 1.5, 'Step 0: SDR->linear\nStep 0b: B calc', '#6BAF6B')

    arrow(2.8, 8.8, 3.5, 8.0)
    arrow(2.8, 7.1, 3.5, 7.4)
    arrow(2.8, 5.3, 3.5, 7.0)

    box(6.8, 8.5, 2.8, 1.0, 'Step 1: bg estimate\n(border_extrapolate)', '#4A90D9')
    box(6.8, 7.0, 2.8, 1.0, 'Step 2: pixel classify\n(opaque/semi/bg)', '#4A90D9')
    box(6.8, 5.5, 2.8, 1.0, 'Step 3: UI color\n(from opaque px)', '#4A90D9')
    box(6.8, 4.0, 2.8, 1.0, 'Step 4: alpha estimate\n(semi-trans px)', '#4A90D9')
    box(6.8, 2.5, 2.8, 1.0, 'Step 5: eff calc\n(eff_neutral / eff_black)', '#4A90D9')
    box(6.8, 1.0, 2.8, 1.0, 'Step 6: HDR reconstruct\n(eff*UI + (1-eff)*bg)', '#C0504D')

    arrow(6.0, 7.55, 6.8, 8.8)
    arrow(8.2, 8.5, 8.2, 8.0)
    arrow(8.2, 7.0, 8.2, 6.5)
    arrow(8.2, 5.5, 8.2, 5.0)
    arrow(8.2, 4.0, 8.2, 3.5)
    arrow(8.2, 2.5, 8.2, 2.0)

    box(3.5, 1.0, 2.5, 1.0, 'HDR bin\n(PQ+BT.2020)', '#804080')
    arrow(6.8, 1.5, 6.0, 1.5)

    ax.text(5.5, 9.2, 'shared', fontsize=8, color='#6BAF6B', style='italic')
    ax.text(10.0, 5.0, 'Route 1', fontsize=10, color='#4A90D9', weight='bold',
            rotation=90, va='center')

    ax.set_title('Route 1 Separation Adjustment - Pipeline Overview', fontsize=13, weight='bold', pad=15)
    plt.tight_layout()
    plt.savefig(f'{OUT}_pipeline.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_pipeline.png")


# ============================================================
# Fig 2: Background estimation (border_extrapolate)
# ============================================================
def fig_bg_estimate():
    fig, ax = plt.subplots(figsize=(10, 8))
    ax.set_xlim(-1, 11)
    ax.set_ylim(-1, 9)
    ax.set_aspect('equal')
    ax.axis('off')

    bx0, by0, bx1, by1 = 3, 2, 8, 7
    rect = Rectangle((bx0, by0), bx1 - bx0, by1 - by0, linewidth=2,
                     edgecolor='#C0504D', facecolor='#FFF0E0', alpha=0.5)
    ax.add_patch(rect)
    ax.text((bx0 + bx1) / 2, (by0 + by1) / 2 + 0.3, 'bbox interior\n(bg needs estimation)',
            ha='center', va='center', fontsize=10, color='#C0504D', weight='bold')

    K = 1.2
    for (rx, ry, rw, rh, label, rot) in [
        (bx0, by1 + 0.05, bx1 - bx0, K, 'top border\n(pure bg)', 0),
        (bx0, by0 - K - 0.05, bx1 - bx0, K, 'bottom border\n(pure bg)', 0),
        (bx0 - K - 0.05, by0, K, by1 - by0, 'left border\n(pure bg)', 90),
        (bx1 + 0.05, by0, K, by1 - by0, 'right border\n(pure bg)', 90),
    ]:
        r = Rectangle((rx, ry), rw, rh, linewidth=1,
                      edgecolor='#4A90D9', facecolor='#B0D0F0', alpha=0.6)
        ax.add_patch(r)
        ax.text(rx + rw / 2, ry + rh / 2, label, ha='center', va='center',
                fontsize=8, color='#2C5F8A', rotation=rot)

    for x in [bx0 + 1, bx0 + 3, bx1 - 1]:
        ax.annotate('', xy=(x, by0 + 0.5), xytext=(x, by1 - 0.5),
                    arrowprops=dict(arrowstyle='<->', color='#6BAF6B', lw=1.5))
    ax.text(bx0 - 0.5, (by0 + by1) / 2, 'row interp\ntop->bot', ha='center', va='center',
            fontsize=8, color='#6BAF6B', rotation=90)

    for y in [by0 + 1, by0 + 3, by1 - 1]:
        ax.annotate('', xy=(bx0 + 0.5, y), xytext=(bx1 - 0.5, y),
                    arrowprops=dict(arrowstyle='<->', color='#E8A838', lw=1.5))
    ax.text((bx0 + bx1) / 2, by0 - 0.7, 'col interp left->right', ha='center', va='center',
            fontsize=8, color='#E8A838')

    ax.text((bx0 + bx1) / 2, by0 - 1.8,
            'bg_est(x,y) = (row_interp(x,y) + col_interp(x,y)) / 2',
            ha='center', fontsize=10, color='#333',
            bbox=dict(boxstyle='round,pad=0.4', facecolor='#F5F5F5', edgecolor='#999'))

    ax.set_title('Step 1: Background Estimation - border_extrapolate', fontsize=13, weight='bold', pad=15)
    plt.tight_layout()
    plt.savefig(f'{OUT}_bg_estimate.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_bg_estimate.png")


# ============================================================
# Fig 3: Pixel classification
# ============================================================
def fig_classification():
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))

    np.random.seed(42)
    bg_vals = np.random.normal(0.01, 0.015, 500)
    semi_vals = np.random.normal(0.08, 0.04, 300)
    opaque_vals = np.random.normal(0.3, 0.08, 200)

    ax1.hist([bg_vals, semi_vals, opaque_vals], bins=40, stacked=True,
             color=['#6BAF6B', '#E8A838', '#C0504D'], alpha=0.8,
             label=['bg (a~0)', 'semi-trans UI (0<a<1)', 'opaque UI (a~1)'])

    theta_low = 0.03
    theta_high = 0.15
    ax1.axvline(theta_low, color='#333', ls='--', lw=1.5)
    ax1.axvline(theta_high, color='#333', ls='--', lw=1.5)
    ax1.text(theta_low, ax1.get_ylim()[1] * 0.9, 'theta_low\n=0.03',
             ha='center', fontsize=9)
    ax1.text(theta_high, ax1.get_ylim()[1] * 0.9, 'theta_high\n=0.15',
             ha='center', fontsize=9)

    ax1.set_xlabel('d = |M_srgb - bg_est_srgb| (max channel)', fontsize=10)
    ax1.set_ylabel('pixel count', fontsize=10)
    ax1.set_title('Step 2: Pixel Classification - Difference Histogram', fontsize=11, weight='bold')
    ax1.legend(fontsize=9)

    ax2.set_xlim(0, 10)
    ax2.set_ylim(0, 8)
    ax2.set_aspect('equal')
    ax2.axis('off')

    np.random.seed(123)
    img = np.ones((80, 100, 3)) * 0.7
    img[20:30, 30:70] = 0.0
    img[35:45, 25:75] = 0.0
    for i in range(80):
        for j in range(100):
            if 18 <= i <= 32 and 28 <= j <= 72:
                d = max(abs(i - 25), abs(j - 50))
                if d > 5:
                    alpha = 0.5 * (1 - (d - 5) / 5)
                    img[i, j] = 0.7 * (1 - alpha)

    bg_color = np.array([0.42, 0.69, 0.42])
    semi_color = np.array([0.91, 0.66, 0.22])
    opaque_color = np.array([0.75, 0.31, 0.30])

    display = np.zeros((80, 100, 3))
    for i in range(80):
        for j in range(100):
            d = abs(img[i, j] - 0.7).max()
            if d < 0.03:
                display[i, j] = bg_color
            elif d > 0.15:
                display[i, j] = opaque_color
            else:
                display[i, j] = semi_color

    ax2.imshow(display, extent=[1, 9, 1, 7], origin='lower')

    legend_items = [Patch(facecolor=bg_color, label='bg (d<=theta_low)'),
                    Patch(facecolor=semi_color, label='semi-trans (theta_low<d<=theta_high)'),
                    Patch(facecolor=opaque_color, label='opaque (d>theta_high)')]
    ax2.legend(handles=legend_items, loc='upper center', bbox_to_anchor=(0.5, -0.02),
               fontsize=8, ncol=3)
    ax2.set_title('Step 2: Pixel Classification Result in bbox', fontsize=11, weight='bold')

    plt.tight_layout()
    plt.savefig(f'{OUT}_classification.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_classification.png")


# ============================================================
# Fig 4: Alpha estimation geometry
# ============================================================
def fig_alpha_geometry():
    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    # --- Left: linear domain blending ---
    ax = axes[0]
    ax.set_xlim(-0.5, 5)
    ax.set_ylim(-2.5, 3)
    ax.set_aspect('equal')
    ax.axis('off')

    y_axis = 0
    ax.annotate('', xy=(4.5, y_axis), xytext=(-0.3, y_axis),
                arrowprops=dict(arrowstyle='->', color='#333', lw=2))

    bg_x = 0.5
    ui_x = 4.0
    M_x = 2.5

    ax.plot([bg_x, ui_x], [y_axis, y_axis], 'k-', lw=3)
    ax.plot([bg_x], [y_axis], 'o', color='#4A90D9', markersize=12, zorder=5)
    ax.plot([ui_x], [y_axis], 'o', color='#C0504D', markersize=12, zorder=5)
    ax.plot([M_x], [y_axis], 's', color='#E8A838', markersize=12, zorder=5)

    ax.text(bg_x, y_axis - 0.5, 'bg', ha='center', va='top', fontsize=10, color='#2C5F8A')
    ax.text(ui_x, y_axis - 0.5, 'ui', ha='center', va='top', fontsize=10, color='#C0504D')
    ax.text(M_x, y_axis + 0.4, 'M\n(mixed)', ha='center', va='bottom', fontsize=10, color='#E8A838')

    ax.annotate('', xy=(M_x, y_axis + 1.2), xytext=(bg_x, y_axis + 1.2),
                arrowprops=dict(arrowstyle='<->', color='#6BAF6B', lw=1.5))
    ax.text((bg_x + M_x) / 2, y_axis + 1.4, 'M - bg', ha='center', fontsize=9, color='#6BAF6B')

    ax.annotate('', xy=(ui_x, y_axis + 1.2), xytext=(bg_x, y_axis + 1.2),
                arrowprops=dict(arrowstyle='<->', color='#999', lw=1, ls='--'))
    ax.text((bg_x + ui_x) / 2, y_axis + 1.55, 'ui - bg', ha='center', fontsize=9, color='#999')

    ax.text(2.0, y_axis + 2.3, r'$\alpha = \frac{M - bg}{ui - bg}$',
            ha='center', fontsize=14, color='#333',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='#F5F5F5'))

    ax.text(2.0, y_axis - 1.8, 'linear domain blending', ha='center', fontsize=11, weight='bold')

    # --- Right: sRGB domain blending ---
    ax = axes[1]
    ax.set_xlim(-0.5, 5)
    ax.set_ylim(-2.5, 3)
    ax.set_aspect('equal')
    ax.axis('off')

    ax.annotate('', xy=(4.5, y_axis), xytext=(-0.3, y_axis),
                arrowprops=dict(arrowstyle='->', color='#333', lw=2))

    srgb_bg = 0.8
    srgb_ui = 4.0
    srgb_M = 2.9

    ax.plot([srgb_bg, srgb_ui], [y_axis, y_axis], 'k-', lw=3)
    ax.plot([srgb_bg], [y_axis], 'o', color='#4A90D9', markersize=12, zorder=5)
    ax.plot([srgb_ui], [y_axis], 'o', color='#C0504D', markersize=12, zorder=5)
    ax.plot([srgb_M], [y_axis], 's', color='#E8A838', markersize=12, zorder=5)

    ax.text(srgb_bg, y_axis - 0.5, 'sRGB(bg)', ha='center', va='top', fontsize=9, color='#2C5F8A')
    ax.text(srgb_ui, y_axis - 0.5, 'sRGB(ui)', ha='center', va='top', fontsize=9, color='#C0504D')
    ax.text(srgb_M, y_axis + 0.4, 'M_srgb\n(mixed)', ha='center', va='bottom', fontsize=9, color='#E8A838')

    ax.annotate('', xy=(srgb_M, y_axis + 1.2), xytext=(srgb_bg, y_axis + 1.2),
                arrowprops=dict(arrowstyle='<->', color='#6BAF6B', lw=1.5))
    ax.text((srgb_bg + srgb_M) / 2, y_axis + 1.4, 'M - sRGB(bg)', ha='center', fontsize=9, color='#6BAF6B')

    ax.annotate('', xy=(srgb_ui, y_axis + 1.2), xytext=(srgb_bg, y_axis + 1.2),
                arrowprops=dict(arrowstyle='<->', color='#999', lw=1, ls='--'))
    ax.text((srgb_bg + srgb_ui) / 2, y_axis + 1.55, 'sRGB(ui) - sRGB(bg)', ha='center', fontsize=9, color='#999')

    ax.text(2.0, y_axis + 2.3, r'$\alpha_{srgb} = \frac{M_{srgb} - sRGB(bg)}{sRGB(ui) - sRGB(bg)}$',
            ha='center', fontsize=14, color='#333',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='#F5F5F5'))

    ax.text(2.0, y_axis - 1.8, 'sRGB domain blending', ha='center', fontsize=11, weight='bold')

    fig.text(0.5, 0.01,
             'Same alpha: sRGB blend M is closer to ui (more opaque)\n'
             '=> sRGB domain needs sRGB->linear alpha mapping to compensate',
             ha='center', fontsize=10, color='#666', style='italic')

    plt.suptitle('Step 4: Alpha Estimation Geometry - Position of M between bg and ui determines alpha',
                 fontsize=13, weight='bold')
    plt.tight_layout(rect=[0, 0.05, 1, 0.93])
    plt.savefig(f'{OUT}_alpha_geometry.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_alpha_geometry.png")


# ============================================================
# Fig 5: sRGB vs linear domain blending comparison
# ============================================================
def fig_blend_domain():
    fig, axes = plt.subplots(2, 1, figsize=(12, 7), gridspec_kw={'height_ratios': [3, 2]})

    ax = axes[0]
    alpha = np.linspace(0, 1, 100)

    bg_lin = 0.1
    ui_lin = 0.9

    M_linear = ui_lin * alpha + bg_lin * (1 - alpha)

    def lin2srgb(v):
        v = np.clip(v, 0, 1)
        return np.where(v <= 0.0031308, v * 12.92, 1.055 * (v ** (1 / 2.4)) - 0.055)

    def srgb2lin(v):
        v = np.clip(v, 0, 1)
        return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)

    bg_srgb = lin2srgb(bg_lin)
    ui_srgb = lin2srgb(ui_lin)
    M_srgb_blend = ui_srgb * alpha + bg_srgb * (1 - alpha)
    M_from_srgb_to_lin = srgb2lin(M_srgb_blend)

    ax.plot(alpha, M_linear, 'b-', lw=2.5, label='linear blend: M = ui*a + bg*(1-a)')
    ax.plot(alpha, M_from_srgb_to_lin, 'r--', lw=2.5,
            label='sRGB blend->linear: sRGBToLinear(sRGB(ui)*a + sRGB(bg)*(1-a))')

    ax.fill_between(alpha, M_linear, M_from_srgb_to_lin, alpha=0.15, color='orange')

    ax.set_xlabel('alpha (a)', fontsize=11)
    ax.set_ylabel('mixed value (linear [0,1])', fontsize=11)
    ax.set_title('sRGB vs linear domain blending: same alpha gives different mixed value',
                 fontsize=12, weight='bold')
    ax.legend(fontsize=9, loc='upper left')
    ax.set_xlim(0, 1)
    ax.set_ylim(0, 1)
    ax.grid(True, alpha=0.3)

    idx = 50
    ax.annotate('sRGB blend more opaque\n(M closer to ui)',
                xy=(alpha[idx], M_from_srgb_to_lin[idx]),
                xytext=(alpha[idx] + 0.15, M_from_srgb_to_lin[idx] + 0.08),
                fontsize=9, color='#C0504D',
                arrowprops=dict(arrowstyle='->', color='#C0504D'))

    ax2 = axes[1]
    diff = M_from_srgb_to_lin - M_linear
    ax2.plot(alpha, diff, 'r-', lw=2)
    ax2.fill_between(alpha, 0, diff, alpha=0.15, color='red')
    ax2.set_xlabel('alpha (a)', fontsize=11)
    ax2.set_ylabel('delta M', fontsize=11)
    ax2.set_title('Deviation after sRGB blend -> linear conversion (delta > 0 = more opaque)',
                  fontsize=11, weight='bold')
    ax2.axhline(0, color='#333', lw=0.8)
    ax2.grid(True, alpha=0.3)

    plt.tight_layout()
    plt.savefig(f'{OUT}_blend_domain.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_blend_domain.png")


# ============================================================
# Fig 6: HDR reconstruction
# ============================================================
def fig_hdr_reconstruct():
    fig, ax = plt.subplots(figsize=(12, 7))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 7)
    ax.set_aspect('equal')
    ax.axis('off')

    def box(x, y, w, h, text, color='#4A90D9', textcolor='white', fontsize=9):
        rect = FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.12",
                              facecolor=color, edgecolor='#2C5F8A', linewidth=1.5)
        ax.add_patch(rect)
        ax.text(x + w / 2, y + h / 2, text, ha='center', va='center',
                fontsize=fontsize, color=textcolor, weight='bold')

    def arrow(x1, y1, x2, y2, color='#333'):
        ax.annotate('', xy=(x2, y2), xytext=(x1, y1),
                    arrowprops=dict(arrowstyle='->', color=color, lw=1.8))

    box(0.3, 5.5, 2.8, 1.0, 'Opaque UI\na=1, eff=1', '#C0504D')
    box(0.3, 3.0, 2.8, 1.0, 'Semi-trans UI\n0<a<1', '#E8A838')
    box(0.3, 0.5, 2.8, 1.0, 'Background\na=0, eff=0', '#6BAF6B')

    box(4.0, 5.5, 2.5, 1.0, 'HDR_ui\n(Y-Scale +\ninv. tonemap)', '#804080')
    box(4.0, 3.0, 2.5, 1.0, 'HDR_ui\n(Y-Scale +\ninv. tonemap)', '#804080')
    box(4.0, 0.5, 2.5, 1.0, 'HDR_bg\n(est. bg)', '#4A90D9')

    arrow(3.1, 6.0, 4.0, 6.0)
    arrow(3.1, 3.5, 4.0, 3.5)
    arrow(3.1, 1.0, 4.0, 1.0)

    box(7.2, 5.5, 2.5, 1.0, 'HDR_out\n= HDR_ui * 1\n+ bg * 0\n= HDR_ui', '#333')
    box(7.2, 2.7, 2.5, 1.5, 'HDR_out\n= HDR_ui * eff\n+ HDR_bg * (1-eff)\n(linear nits)', '#333')
    box(7.2, 0.5, 2.5, 1.0, 'HDR_out\n= HDR_ui * 0\n+ bg * 1\n= HDR_bg', '#333')

    arrow(6.5, 6.0, 7.2, 6.0)
    arrow(6.5, 3.5, 7.2, 3.7)

    box(4.0, 1.5, 2.5, 0.8, 'HDR_bg\n(same)', '#4A90D9', fontsize=8)
    arrow(5.25, 2.3, 5.25, 3.0, color='#4A90D9')

    ax.text(5.0, -0.3,
            'Formula:  HDR_out = HDR_ui_compensated * eff + HDR_bg_estimated * (1 - eff)    (linear nits BT.2020)',
            ha='center', fontsize=10, color='#333',
            bbox=dict(boxstyle='round,pad=0.3', facecolor='#FFFFE0', edgecolor='#999'))

    ax.set_title('Step 6: HDR Reconstruction - Three Pixel Classes', fontsize=13, weight='bold', pad=15)
    plt.tight_layout()
    plt.savefig(f'{OUT}_hdr_reconstruct.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_hdr_reconstruct.png")


# ============================================================
# Fig 7: Black UI alpha estimation specialization
# ============================================================
def fig_black_ui_alpha():
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.set_xlim(-0.5, 5)
    ax.set_ylim(-2.5, 3)
    ax.set_aspect('equal')
    ax.axis('off')

    y = 0
    ax.annotate('', xy=(4.5, y), xytext=(-0.3, y),
                arrowprops=dict(arrowstyle='->', color='#333', lw=2))

    bg_x = 0.8
    M_x = 0.35

    ax.plot([0, bg_x], [y, y], 'k-', lw=3)
    ax.plot([bg_x], [y], 'o', color='#4A90D9', markersize=12, zorder=5)
    ax.plot([M_x], [y], 's', color='#E8A838', markersize=12, zorder=5)
    ax.plot([0], [y], 'o', color='#333', markersize=8, zorder=5)

    ax.text(0, y - 0.5, '0\n(ui=0)', ha='center', va='top', fontsize=10, color='#333')
    ax.text(bg_x, y - 0.5, 'bg', ha='center', va='top', fontsize=10, color='#2C5F8A')
    ax.text(M_x, y + 0.4, 'M\n(mixed)', ha='center', va='bottom', fontsize=10, color='#E8A838')

    ax.annotate('', xy=(M_x, y + 1.2), xytext=(0, y + 1.2),
                arrowprops=dict(arrowstyle='<->', color='#6BAF6B', lw=1.5))
    ax.text(M_x / 2, y + 1.4, 'M = bg*(1-a)', ha='center', fontsize=9, color='#6BAF6B')

    ax.annotate('', xy=(bg_x, y + 1.8), xytext=(0, y + 1.8),
                arrowprops=dict(arrowstyle='<->', color='#999', lw=1, ls='--'))
    ax.text(bg_x / 2, y + 2.0, 'bg', ha='center', fontsize=9, color='#999')

    ax.text(2.5, y + 1.0,
            r'$\alpha = 1 - \frac{M}{bg}$' + '\n(black UI: no UI color needed)',
            ha='center', fontsize=13, color='#333',
            bbox=dict(boxstyle='round,pad=0.4', facecolor='#F5F5F5'))

    ax.text(2.5, y - 2.0,
            'Black UI (ui=0): M = bg*(1-a), only need bg to solve alpha\n'
            'Skip Step 3 (UI color), Step 4 simplifies to division',
            ha='center', fontsize=10, color='#666', style='italic')

    ax.set_title('Black UI Alpha Estimation Specialization', fontsize=13, weight='bold', pad=15)
    plt.tight_layout()
    plt.savefig(f'{OUT}_black_ui_alpha.png', dpi=150, bbox_inches='tight')
    plt.close()
    print(f"saved {OUT}_black_ui_alpha.png")


if __name__ == '__main__':
    import os
    d = '/'.join(OUT.split('/')[:-1])
    if d:
        os.makedirs(d, exist_ok=True)
    fig_pipeline()
    fig_bg_estimate()
    fig_classification()
    fig_alpha_geometry()
    fig_blend_domain()
    fig_hdr_reconstruct()
    fig_black_ui_alpha()
    print("All diagrams generated.")
