#!/usr/bin/env python3
"""Generate Hunt Effect causal chain derivation slides (2 slides, .pptx)"""

import io
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
from pptx.enum.shapes import MSO_SHAPE

DARK_BLUE = RGBColor(0x1B, 0x2A, 0x4A)
DARK_RED = RGBColor(0x8B, 0x00, 0x00)
MEDIUM_GRAY = RGBColor(0x44, 0x44, 0x44)
LIGHT_GRAY = RGBColor(0x99, 0x99, 0x99)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)
ACCENT_BLUE = RGBColor(0x2E, 0x5E, 0x8E)
ACCENT_BAR = RGBColor(0xCC, 0x33, 0x33)

SLIDE_W = Inches(13.333)
SLIDE_H = Inches(7.5)
TOTAL = 2


def render_formula(latex_str, fontsize=15, dpi=200, figw=11, figh=0.8):
    fig, ax = plt.subplots(figsize=(figw, figh))
    ax.axis('off')
    ax.text(0.5, 0.5, f'${latex_str}$', fontsize=fontsize,
            ha='center', va='center',
            transform=ax.transAxes,
            color='#1B2A4A')
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=dpi, bbox_inches='tight',
                transparent=True, pad_inches=0.08)
    plt.close(fig)
    buf.seek(0)
    return buf


def add_text_box(slide, left, top, width, height, text, font_size=14,
                 bold=False, color=MEDIUM_GRAY, alignment=PP_ALIGN.LEFT,
                 font_name='Calibri'):
    txBox = slide.shapes.add_textbox(left, top, width, height)
    tf = txBox.text_frame
    tf.word_wrap = True
    p = tf.paragraphs[0]
    p.text = text
    p.font.size = Pt(font_size)
    p.font.bold = bold
    p.font.color.rgb = color
    p.font.name = font_name
    p.alignment = alignment
    p.space_after = Pt(0)
    p.line_spacing = Pt(font_size * 1.3)
    return txBox


def add_rich_text(slide, left, top, width, height, runs_list, font_size=14):
    txBox = slide.shapes.add_textbox(left, top, width, height)
    tf = txBox.text_frame
    tf.word_wrap = True
    for para_idx, runs in enumerate(runs_list):
        if para_idx == 0:
            p = tf.paragraphs[0]
        else:
            p = tf.add_paragraph()
        p.space_after = Pt(3)
        p.line_spacing = Pt(font_size * 1.5)
        for text, bold, color, size_override in runs:
            run = p.add_run()
            run.text = text
            run.font.size = Pt(size_override if size_override else font_size)
            run.font.bold = bold
            run.font.color.rgb = color
            run.font.name = 'Calibri'
    return txBox


def add_divider(slide, left, top, width):
    shape = slide.shapes.add_shape(
        MSO_SHAPE.RECTANGLE, left, top, width, Pt(1))
    shape.fill.solid()
    shape.fill.fore_color.rgb = RGBColor(0xE0, 0xE0, 0xE0)
    shape.line.fill.background()


def add_accent_bar(slide, left, top, height):
    shape = slide.shapes.add_shape(
        MSO_SHAPE.RECTANGLE, left, top, Pt(4), height)
    shape.fill.solid()
    shape.fill.fore_color.rgb = ACCENT_BAR
    shape.line.fill.background()


def add_title_bar(slide, title, subtitle, page):
    s = slide.shapes.add_shape(
        MSO_SHAPE.RECTANGLE, Inches(0), Inches(0), SLIDE_W, Inches(1.0))
    s.fill.solid()
    s.fill.fore_color.rgb = DARK_BLUE
    s.line.fill.background()
    add_text_box(slide, Inches(0.6), Inches(0.12), Inches(10), Inches(0.55),
                 title, font_size=28, bold=True, color=WHITE)
    add_text_box(slide, Inches(0.6), Inches(0.6), Inches(10), Inches(0.35),
                 subtitle, font_size=13, color=RGBColor(0xAA, 0xBB, 0xCC))
    add_text_box(slide, Inches(12.3), Inches(7.05), Inches(0.8), Inches(0.35),
                 f'{page}/{TOTAL}', font_size=11, color=LIGHT_GRAY,
                 alignment=PP_ALIGN.RIGHT)


prs = Presentation()
prs.slide_width = SLIDE_W
prs.slide_height = SLIDE_H
blank_layout = prs.slide_layouts[6]

x = Inches(0.9)
fw = Inches(11.5)

# ============================================================
# Slide 1: ① 适应亮度  ② 锥体响应压缩  ③ 对立色信号
# ============================================================
slide = prs.slides.add_slide(blank_layout)
add_title_bar(slide, 'Hunt Effect 因果推导链（1/2）',
              'L_W → F_L → 锥体响应压缩 → 对立色信号  |  CIECAM02 (CIE 159:2004)', 1)

y = Inches(1.3)
add_accent_bar(slide, Inches(0.65), y, Inches(1.0))
add_text_box(slide, x, y, fw, Inches(0.3),
             '① 适应亮度与亮度级因子', font_size=18, bold=True, color=DARK_BLUE)
y += Inches(0.38)
buf = render_formula(
    r'L_W = \mathrm{scene\;white\;luminance\;(cd/m^2)},\quad '
    r'L_A \approx \frac{L_W}{5},\quad '
    r'F_L \approx L_A^{1/3}',
    fontsize=15, figw=12, figh=0.7)
slide.shapes.add_picture(buf, x, y, fw)
y += Inches(0.55)
add_rich_text(slide, x, y, fw, Inches(0.3), [
    [('→  ', True, DARK_RED, 15),
     ('更亮的背景亮度  →  更大的 L_A  →  更大的 F_L', False, MEDIUM_GRAY, 15)],
], font_size=15)
y += Inches(0.45)
add_divider(slide, x, y, fw)
y += Inches(0.25)

add_accent_bar(slide, Inches(0.65), y, Inches(1.3))
add_text_box(slide, x, y, fw, Inches(0.3),
             '② 非线性响应压缩 R_a\'', font_size=18, bold=True, color=DARK_BLUE)
y += Inches(0.38)
buf = render_formula(
    r'R_a^\prime = \frac{400 \cdot (F_L \cdot R / 100)^{0.42}}'
    r'{(F_L \cdot R / 100)^{0.42} + 27.13} + 0.1',
    fontsize=15, figw=10, figh=0.8)
slide.shapes.add_picture(buf, x, y, Inches(9.5))
y += Inches(0.6)
add_rich_text(slide, x, y, fw, Inches(0.35), [
    [('→  ', True, DARK_RED, 15),
     ('更大的 F_L  →  压缩函数输入增大（f 单调递增）→  每个压缩后锥体响应都增大', False, MEDIUM_GRAY, 15)],
], font_size=15)
y += Inches(0.5)
add_divider(slide, x, y, fw)
y += Inches(0.25)

add_accent_bar(slide, Inches(0.65), y, Inches(1.4))
add_text_box(slide, x, y, fw, Inches(0.3),
             '③ 对立色信号 a, b', font_size=18, bold=True, color=DARK_BLUE)
y += Inches(0.38)
buf = render_formula(
    r'a = R_a^\prime - 12 \cdot \frac{G_a^\prime - B_a^\prime}{11},\quad '
    r'b = \frac{R_a^\prime + G_a^\prime - 2 \cdot B_a^\prime}{9}',
    fontsize=15, figw=11, figh=0.7)
slide.shapes.add_picture(buf, x, y, Inches(10))
y += Inches(0.6)
add_rich_text(slide, x, y, fw, Inches(0.7), [
    [('→  ', True, DARK_RED, 15),
     ('a, b 是 R_a\', G_a\', B_a\' 的线性组合', False, MEDIUM_GRAY, 15)],
    [('→  ', True, DARK_RED, 15),
     ('F_L 增大使每个锥体响应增大，但非线性压缩下各通道缩放比不同（', False, MEDIUM_GRAY, 15),
     ('r_R ≠ r_G ≠ r_B', True, ACCENT_BLUE, 15),
     ('）', False, MEDIUM_GRAY, 15)],
    [('→  ', True, DARK_RED, 15),
     ('|a|, |b| 通常增大（非严格必然），但与无色彩信号的', False, MEDIUM_GRAY, 15),
     ('比值近似不变', True, ACCENT_BLUE, 15)],
], font_size=15)

# ============================================================
# Slide 2: ④ Chroma C  ⑤ Colorfulness M  总结
# ============================================================
slide = prs.slides.add_slide(blank_layout)
add_title_bar(slide, 'Hunt Effect 因果推导链（2/2）',
              'Chroma C → Colorfulness M → 补偿方案  |  CIECAM02 (CIE 159:2004)', 2)

y = Inches(1.3)
add_accent_bar(slide, Inches(0.65), y, Inches(2.0))
add_text_box(slide, x, y, fw, Inches(0.3),
             '④ Chroma C  ≈  常数', font_size=18, bold=True, color=ACCENT_BLUE)
y += Inches(0.4)
buf = render_formula(
    r't = \frac{\frac{50000}{13} \cdot N_c \cdot N_{cb} \cdot e_t \cdot \sqrt{a^2 + b^2}}'
    r'{R_a^\prime + G_a^\prime + \frac{21}{20} B_a^\prime}'
    r',\quad C = t^{0.9} \cdot \sqrt{\frac{J}{100}} \cdot (1.64 - 0.29^n)^{0.73}',
    fontsize=14, figw=14, figh=0.9)
slide.shapes.add_picture(buf, Inches(0.5), y, Inches(12.5))
y += Inches(0.85)
add_rich_text(slide, x, y, fw, Inches(0.9), [
    [('t 是比值：', True, DARK_BLUE, 15),
     ('分子 √(a²+b²)（对立色信号幅度）/ 分母 [R_a\'+G_a\'+(21/20)B_a\']（锥体信号总量）', False, MEDIUM_GRAY, 15)],
    [('→  ', True, DARK_RED, 15),
     ('分子和分母', False, MEDIUM_GRAY, 15),
     ('均随 F_L 近似等比例缩放', True, DARK_RED, 15),
     ('  →  比值 t ≈ 常数', False, MEDIUM_GRAY, 15)],
    [('→  ', True, DARK_RED, 15),
     ('C ≈ 常数', True, ACCENT_BLUE, 15),
     ('  —  C 是"相对彩度"（相对于参考白亮度），设计为亮度不变', False, MEDIUM_GRAY, 15)],
], font_size=15)
y += Inches(1.15)
add_divider(slide, x, y, fw)
y += Inches(0.25)

add_accent_bar(slide, Inches(0.65), y, Inches(1.4))
add_text_box(slide, x, y, fw, Inches(0.3),
             '⑤ Colorfulness M  ←  Hunt Effect 体现于此', font_size=18, bold=True, color=DARK_RED)
y += Inches(0.4)
buf = render_formula(
    r'M = C \cdot F_L^{0.25},\quad '
    r'\frac{M_{\mathrm{HDR}}}{M_{\mathrm{SDR}}} = \left(\frac{0.86}{0.69}\right)^{0.25} \approx 1.056',
    fontsize=16, figw=12, figh=0.7)
slide.shapes.add_picture(buf, x, y, fw)
y += Inches(0.6)
add_rich_text(slide, x, y, fw, Inches(0.5), [
    [('→  ', True, DARK_RED, 15),
     ('C ≈ 不变，但 ', False, MEDIUM_GRAY, 15),
     ('F_L 直接以 F_L^0.25 放大 M', True, DARK_RED, 15),
     ('  →  HDR 下 M 高约 5.6%（Hunt Effect）', False, MEDIUM_GRAY, 15)],
], font_size=15)
y += Inches(0.7)
add_divider(slide, x, y, fw)
y += Inches(0.25)

add_rich_text(slide, x, y, fw, Inches(0.7), [
    [('结论：', True, DARK_RED, 17),
     ('提高 HDR 显示亮度通过 F_L^0.25 直接放大 M，可通过', False, DARK_BLUE, 17),
     ('降低 C 约 5.3%', True, DARK_RED, 17),
     ('（C_HDR ≈ C_SDR / 1.056）保持 M 不变', False, DARK_BLUE, 17)],
], font_size=17)

out = '/home/tuchenxi/.zeroclaw/workspace/UI_Vulkan/hunt_effect_derivation.pptx'
prs.save(out)
print(f'Saved: {out}')
print(f'Slides: {len(prs.slides)}')
