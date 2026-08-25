#!/usr/bin/env python3
"""
生成测试用圆形 UI 纹理（PNG）

生成内容:
  1. neutral_alpha.png  — 抗锯齿圆形 alpha mask (alpha=1.0 内, 0.0 外)
  2. neutral_rgb.png    — 中性色 (195, 195, 195) RGB
  3. redorange_rgb.png  — CIELAB(50, +70, +45) 红
  4. green_rgb.png      — CIELAB(50, -70, +40) 绿
  5. bluepurple_rgb.png — CIELAB(50, +30, -90) 蓝紫

圆形大小参考 1_alpha_1839_724.png 的非零区域包围盒。
彩色 UI 复用 neutral_alpha.png 作为 alpha mask。

用法:
    python3 generate_test_ui.py
"""

import os
import numpy as np
from PIL import Image

# ---- 配置 ----
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_DIR = os.path.join(PROJECT_ROOT, "pic", "cropped_images")
REFERENCE_ALPHA = os.path.join(IMAGE_DIR, "1_alpha_1839_724.png")

# 中性灰 (sRGB 8-bit)
NEUTRAL_RGB = (195, 195, 195)

# CIELAB 颜色 (来自 hdr_compensation_plan.md 4.2.1 + 4.2.2 节)
# 最不中性色 + 中点 (与中性色 L=50,a=0,b=0 的连线中点)
COLORS = [
    {"name": "redorange",      "L": 50, "a":  70, "b":  45},
    {"name": "redorange_mid",  "L": 50, "a":  35, "b":  22.5},
    {"name": "green",          "L": 50, "a": -70, "b":  40},
    {"name": "green_mid",      "L": 50, "a": -35, "b":  20},
    {"name": "bluepurple",     "L": 50, "a":  30, "b": -90},
    {"name": "bluepurple_mid", "L": 50, "a":  15, "b": -45},
]

# D65 参考白点 (归一化)
Xn, Yn, Zn = 0.95047, 1.0, 1.08883


def lab_to_srgb(L, a, b):
    """CIELAB (L, a, b) -> sRGB (R, G, B) in [0, 255]"""

    # CIELAB -> XYZ
    fy = (L + 16) / 116.0
    fx = fy + a / 500.0
    fz = fy - b / 200.0

    def f_inv(t):
        delta = 6.0 / 29.0
        if t > delta:
            return t ** 3
        else:
            return 3 * delta ** 2 * t

    X = Xn * f_inv(fx)
    Y = Yn * f_inv(fy)
    Z = Zn * f_inv(fz)

    # XYZ -> linear RGB (sRGB primaries, D65)
    R =  3.2406 * X - 1.5372 * Y - 0.4986 * Z
    G = -0.9689 * X + 1.8758 * Y + 0.0415 * Z
    B =  0.0557 * X - 0.2040 * Y + 1.0570 * Z

    # linear RGB -> sRGB
    def linear_to_srgb(c):
        c = max(0.0, min(1.0, c))
        if c <= 0.0031308:
            return 12.92 * c
        else:
            return 1.055 * c ** (1.0 / 2.4) - 0.055

    r = int(round(linear_to_srgb(R) * 255))
    g = int(round(linear_to_srgb(G) * 255))
    bl = int(round(linear_to_srgb(B) * 255))

    return (r, g, bl)


def find_circle_diameter(alpha_path):
    """从 alpha 图像的非零区域确定圆形直径"""
    img = Image.open(alpha_path)
    if img.mode != 'L':
        img = img.convert('L')
    arr = np.array(img)

    rows = np.any(arr > 0, axis=1)
    cols = np.any(arr > 0, axis=0)
    rmin, rmax = np.where(rows)[0][[0, -1]]
    cmin, cmax = np.where(cols)[0][[0, -1]]

    width = cmax - cmin + 1
    height = rmax - rmin + 1
    diameter = min(width, height)

    print(f"  Reference image: {img.size[0]}x{img.size[1]}")
    print(f"  Non-zero bbox: {width}x{height} at ({cmin},{rmin})-({cmax},{rmax})")
    print(f"  Circle diameter: {diameter} px")

    return diameter


def create_circle_alpha(diameter):
    """创建抗锯齿圆形 alpha mask

    圆内 alpha=255, 圆外 alpha=0, 边缘线性渐变
    """
    size = diameter
    center = (size - 1) / 2.0
    radius = diameter / 2.0

    y, x = np.ogrid[:size, :size]
    dist = np.sqrt((x - center) ** 2 + (y - center) ** 2)

    # 抗锯齿: 在 [radius-0.5, radius+0.5] 范围内线性渐变
    alpha = np.clip(radius + 0.5 - dist, 0.0, 1.0)
    alpha_8bit = (alpha * 255).astype(np.uint8)

    return alpha_8bit


def create_solid_rgb(color, size):
    """创建纯色 RGB 图像"""
    return Image.new('RGB', (size, size), color)


def main():
    print("Generating test UI textures...")
    print()

    # 确定圆形大小
    print("[Reference]")
    diameter = find_circle_diameter(REFERENCE_ALPHA)
    print()

    # 文件名前缀: t = test
    PREFIX = "t"

    # 生成 alpha PNG (每组 UI 各一份，内容相同)
    print("[Alpha]")
    alpha_arr = create_circle_alpha(diameter)
    alpha_img = Image.fromarray(alpha_arr, mode='L')

    alpha_names = ["neutral"] + [c["name"] for c in COLORS]
    for an in alpha_names:
        alpha_path = os.path.join(IMAGE_DIR, f"{PREFIX}_alpha_{an}.png")
        alpha_img.save(alpha_path)
    print(f"  Size: {diameter}x{diameter}")
    nz = alpha_arr[alpha_arr > 0]
    fg = alpha_arr[alpha_arr > 128]
    print(f"  Non-zero pixels: {len(nz)} / {diameter**2}")
    print(f"  FG pixels (alpha>128): {len(fg)}, mean alpha: {fg.mean()/255:.4f}")
    print(f"  Saved: {PREFIX}_alpha_{{{','.join(alpha_names)}}}.png (4 copies)")
    print()

    # 生成中性色 RGB PNG
    print("[Neutral RGB]")
    neutral_img = create_solid_rgb(NEUTRAL_RGB, diameter)
    neutral_path = os.path.join(IMAGE_DIR, f"{PREFIX}_rgb_neutral.png")
    neutral_img.save(neutral_path)
    print(f"  Color: sRGB{NEUTRAL_RGB}")
    print(f"  Saved: {neutral_path}")
    print()

    # 生成彩色 RGB PNG
    print("[Colored RGB]")
    for c in COLORS:
        rgb = lab_to_srgb(c["L"], c["a"], c["b"])
        img = create_solid_rgb(rgb, diameter)
        path = os.path.join(IMAGE_DIR, f"{PREFIX}_rgb_{c['name']}.png")
        img.save(path)
        print(f"  {c['name']}: CIELAB({c['L']}, {c['a']:+}, {c['b']:+}) -> sRGB{rgb}")
        print(f"    Saved: {path}")
    print()

    print("Done.")
    print()
    print("Output files (prefix=t, code key = t_<name>.png):")
    for an in alpha_names:
        print(f"  {PREFIX}_alpha_{an}.png + {PREFIX}_rgb_{an}.png  -> key=\"{PREFIX}_{an}.png\"")


if __name__ == '__main__':
    main()
