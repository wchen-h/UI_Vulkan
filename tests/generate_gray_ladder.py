#!/usr/bin/env python3
"""
生成灰阶测试素材 + 测试网格 CSV（补充测试方案 v2 灰阶优先）

素材：pic/cropped_images/t_rgb_gray_NNN.png（纯灰）+ t_alpha_gray_NNN.png（圆形 alpha mask）
CSV ：tests/gray_ladder_grid.csv，SDR_UI_Nit × SDR_BG_Nit × HDR_BG_Nit = 7×3×7 = 147 行

用法：python3 generate_gray_ladder.py
"""
import os
import csv
import numpy as np
from PIL import Image

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_DIR = os.path.join(PROJECT_ROOT, "pic", "cropped_images")
OUT_CSV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gray_ladder_grid.csv")

PAPER_WHITE_NIT = 350
UI_NITS = [50, 100, 150, 200, 250, 300, 350]
SDR_BG_NITS = [50, 150, 350]
HDR_BG_NITS = [50, 100, 150, 200, 300, 500, 1000]
SIZE = 173  # 与现有 t_* 合成素材一致（圆形内切于 173×173）


def srgb_encode(x):
    x = max(0.0, min(1.0, x))
    if x <= 0.0031308:
        return 12.92 * x
    return 1.055 * (x ** (1.0 / 2.4)) - 0.055


def nit_to_byte(nit):
    return int(round(srgb_encode(nit / PAPER_WHITE_NIT) * 255))


def circle_alpha(diameter):
    """抗锯齿圆形 alpha mask：圆内 255，圆外 0，边缘线性渐变。"""
    size = diameter
    center = (size - 1) / 2.0
    radius = diameter / 2.0
    y, x = np.ogrid[:size, :size]
    dist = np.sqrt((x - center) ** 2 + (y - center) ** 2)
    alpha = np.clip(radius + 0.5 - dist, 0.0, 1.0)
    return (alpha * 255).astype(np.uint8)


def generate_assets():
    os.makedirs(IMAGE_DIR, exist_ok=True)
    alpha = circle_alpha(SIZE)
    print(f"灰阶素材（圆形 alpha mask，不透明占比 {np.mean(alpha > 128):.3f}）:")
    for nit in UI_NITS:
        byte = nit_to_byte(nit)
        rgb = Image.new("RGB", (SIZE, SIZE), (byte, byte, byte))
        alpha_img = Image.fromarray(alpha, mode="L")
        tag = f"{nit:03d}"
        rgb_path = os.path.join(IMAGE_DIR, f"t_rgb_gray_{tag}.png")
        alpha_path = os.path.join(IMAGE_DIR, f"t_alpha_gray_{tag}.png")
        rgb.save(rgb_path)
        alpha_img.save(alpha_path)
        print(f"  {nit:3d} nit -> sRGB {byte:3d}  ->  t_rgb_gray_{tag}.png / t_alpha_gray_{tag}.png")
    print(f"共 {len(UI_NITS)} 组素材 -> {IMAGE_DIR}")


def generate_csv():
    header = ["SDR_UI_Nit", "SDR_BG_Nit", "HDR_BG_Nit", "UI素材", "匹配_K", "备注"]
    total = len(UI_NITS) * len(SDR_BG_NITS) * len(HDR_BG_NITS)
    with open(OUT_CSV, "w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for ui in UI_NITS:
            for sbg in SDR_BG_NITS:
                for hbg in HDR_BG_NITS:
                    w.writerow([ui, sbg, hbg, f"t_gray_{ui:03d}", "", ""])
    print(f"\n测试网格: {len(UI_NITS)} × {len(SDR_BG_NITS)} × {len(HDR_BG_NITS)} = {total} 行 -> {OUT_CSV}")


def main():
    generate_assets()
    generate_csv()
    print("\nDone.")


if __name__ == "__main__":
    main()
