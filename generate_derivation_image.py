#!/usr/bin/env python3
"""Generate Hunt Effect derivation chain image."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
import matplotlib.font_manager as fm

plt.rcParams['font.family'] = 'Noto Sans CJK SC'
plt.rcParams['mathtext.fontset'] = 'custom'
plt.rcParams['mathtext.rm'] = 'Noto Sans CJK SC'
plt.rcParams['mathtext.it'] = 'Noto Sans CJK SC:italic'
plt.rcParams['mathtext.bf'] = 'Noto Sans CJK SC:bold'
plt.rcParams['mathtext.sf'] = 'Noto Sans CJK SC'
plt.rcParams['mathtext.tt'] = 'Noto Sans CJK SC'
plt.rcParams['mathtext.cal'] = 'Noto Sans CJK SC'

fm._load_fontmanager(try_read_cache=False)

zh_font = fm.FontProperties(family='Noto Sans CJK SC')
zh_font_bold = fm.FontProperties(family='Noto Sans CJK SC', weight='bold')
zh_font_italic = fm.FontProperties(family='Noto Sans CJK SC', style='italic')

fig, ax = plt.subplots(figsize=(14, 18))
ax.set_xlim(0, 14)
ax.set_ylim(0, 18)
ax.axis('off')
fig.patch.set_facecolor('#FAFAF8')

DARK_BLUE = '#1B2A4A'
DARK_RED = '#8B0000'
ACCENT_BLUE = '#2E5E8E'
MEDIUM_GRAY = '#444444'
LIGHT_BG = '#F0F2F5'
ARROW_COLOR = '#888888'

def add_formula_box(ax, x, y, w, h, title, formulas, note=None, title_color=DARK_BLUE, bg_color=LIGHT_BG):
    box = mpatches.FancyBboxPatch((x, y), w, h, boxstyle="round,pad=0.15",
                                   facecolor=bg_color, edgecolor='#CCCCCC', linewidth=1)
    ax.add_patch(box)
    ax.text(x + 0.3, y + h - 0.35, title, fontsize=13, fontweight='bold',
            color=title_color, va='top')
    fy = y + h - 0.85
    for f in formulas:
        ax.text(x + 0.5, fy, f, fontsize=14, color=DARK_BLUE, va='top')
        fy -= 0.5
    if note:
        ax.text(x + 0.3, y + 0.25, note, fontsize=10, color='#888888',
                va='bottom')

def add_arrow(ax, x, y1, y2):
    ax.annotate('', xy=(x, y2), xytext=(x, y1),
                arrowprops=dict(arrowstyle='->', color=ARROW_COLOR, lw=2))

def add_step_label(ax, x, y, text, color=DARK_RED):
    ax.text(x, y, text, fontsize=12, fontweight='bold', color=color,
            va='center')

# Title
ax.text(7, 17.5, 'Hunt Effect 推导链：亮度如何影响色彩感知',
        fontsize=20, fontweight='bold', color=DARK_BLUE,
        ha='center', va='center')
ax.text(7, 17.05, 'CIECAM02 色彩外观模型视角  |  来源：CIE 159:2004, Fairchild Ch.16',
        fontsize=11, color='#888888', ha='center', va='center')

# Step 1: F_L and L_A
add_formula_box(ax, 0.5, 15.0, 13, 1.7,
    '① 适应场亮度与亮度水平适应因子',
    [
        r'$L_A \approx L_W / 5$     ($L_W$: 适应场中参考白的绝对亮度, cd/m²)',
        r'$F_L \approx \frac{1}{5} k^4 (5L_A) + \frac{1}{10}(1-k^4)^2 (5L_A)^{1/3}$',
        r'在明视觉条件下近似:  $F_L \propto L_A^{1/3}$',
    ],
    note='k = 1/(5L_A + 1)')

add_arrow(ax, 7, 15.0, 14.6)
add_step_label(ax, 7.4, 14.8, '更亮背景 → 更大 L_A → 更大 F_L')

# Step 2: Compression function
add_formula_box(ax, 0.5, 12.5, 13, 1.8,
    '② F_L 增大 → 压缩函数输入增大',
    [
        r'$R_a^\prime = \frac{400 \cdot (F_L \cdot R^\prime / 100)^{0.42}}{27.13 + (F_L \cdot R^\prime / 100)^{0.42}} + 0.1$',
        r'$G_a^\prime, B_a^\prime$ 同理（对三个锥体响应分别压缩）',
        r'$F_L$ 出现在分子输入端: $F_L \uparrow \;\Rightarrow\; (F_L \cdot R^\prime/100) \uparrow$',
    ],
    note='sigmoid-like 压缩函数，模拟锥体响应的非线性饱和')

add_arrow(ax, 7, 12.5, 12.1)
add_step_label(ax, 7.4, 12.3, '输入范围增大 → 输出差异增大')

# Step 3: Opponent color
add_formula_box(ax, 0.5, 9.8, 13, 1.9,
    '③ 压缩后锥体差异 → opponent color 信号',
    [
        r'$a = R_a^\prime - \frac{12}{11} G_a^\prime + \frac{1}{11} B_a^\prime$     (红-绿对抗)',
        r'$b = \frac{1}{9}(R_a^\prime + G_a^\prime - 2 B_a^\prime)$     (黄-蓝对抗)',
        r'锥体响应差异增大 $\Rightarrow$ $a, b$ 的幅度 $\sqrt{a^2+b^2}$ 增大',
    ],
    note='对 D65 灰色: R\'_bg = G\'_bg = B\'_bg → a_bg = b_bg = 0（灰色不贡献色差）')

add_arrow(ax, 7, 9.8, 9.4)
add_step_label(ax, 7.4, 9.6, '√(a²+b²) 增大 → t 增大')

# Step 4: Chroma C
add_formula_box(ax, 0.5, 6.8, 13, 2.3,
    '④ CIECAM02 chroma C',
    [
        r'$t = \frac{\frac{50000}{13} \cdot N_c \cdot N_{cb} \cdot e_t \cdot \sqrt{a^2+b^2}}{R_a^\prime + G_a^\prime + \frac{21}{20}B_a^\prime}$',
        r'$C = t^{0.9} \cdot \sqrt{\frac{J}{100}} \cdot (1.64 - 0.29^n)^{0.73}$',
        r'$\sqrt{a^2+b^2} \uparrow \;\Rightarrow\; t \uparrow \;\Rightarrow\; C \uparrow$',
        r'注: 分母 $[R_a^\prime+G_a^\prime+\frac{21}{20}B_a^\prime]$ 也随 $F_L$ 增大，部分抵消分子增长',
    ],
    note='C 为相对彩度（相对于参考白），n = Y_b/Y_w 为背景相对亮度')

add_arrow(ax, 7, 6.8, 6.4)
add_step_label(ax, 7.4, 6.6, 'C 增大 → M 增大')

# Step 5: Colorfulness M
add_formula_box(ax, 0.5, 3.8, 13, 2.3,
    '⑤ CIECAM02 colorfulness M（鲜艳度）',
    [
        r'$M = C \cdot F_L^{0.25}$',
        r'Hunt Effect 的双重增强:',
        r'  路径 A:  $F_L \uparrow \;\Rightarrow\; a,b \uparrow \;\Rightarrow\; C \uparrow \;\Rightarrow\; M \uparrow$',
        r'  路径 B:  $F_L^{0.25}$ 直接放大 $M$（即使 $C$ 不变，$M$ 仍增大）',
    ],
    note='Hunt Effect (Hunt, 1952): 同一颜色在更高亮度下感知为更鲜艳',
    title_color=DARK_RED, bg_color='#FFF5F5')

# Final conclusion box
box = mpatches.FancyBboxPatch((0.5, 1.5), 13, 1.8, boxstyle="round,pad=0.15",
                               facecolor=DARK_BLUE, edgecolor=DARK_BLUE, linewidth=2)
ax.add_patch(box)
ax.text(7, 2.9, '结论',
        fontsize=14, fontweight='bold', color='white',
        ha='center', va='center')
ax.text(7, 2.3, r'$\text{更亮背景} \;\Rightarrow\; F_L \uparrow \;\Rightarrow\; \text{colorfulness } M \uparrow$'
        r'   （同一 UI 素材在 HDR 下主观更鲜艳）',
        fontsize=14, color='white', ha='center', va='center')
ax.text(7, 1.75, r'本项目: $M_{HDR}/M_{SDR} = (F_{L,HDR}/F_{L,SDR})^{0.25} = (0.86/0.69)^{0.25} \approx 1.056$'
        r'  →  HDR 侧鲜艳度高约 5.6%，需 chromaScale ≈ 0.94 补偿',
        fontsize=12, color='#AABBCC', ha='center', va='center')

plt.tight_layout(pad=0.5)
plt.savefig('/home/tuchenxi/.zeroclaw/workspace/UI_Vulkan/hunt_effect_derivation.png',
            dpi=200, bbox_inches='tight', facecolor=fig.get_facecolor())
plt.close()
print('Image saved: hunt_effect_derivation.png')
