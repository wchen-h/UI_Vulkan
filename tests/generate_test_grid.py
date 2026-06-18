#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
生成 SDR/HDR 主观匹配实验的测试数据网格 CSV。

实验结构（三层嵌套）：
  Layer 1: SDR_UI_Alpha  0.1~1.0 步长 0.1        (10 组, SDR bg 固定 350 nit)
  Layer 2: HDR_BG_Nit    0~1000  步长 50 nit      (含两端共 21 个点)
  Layer 3: UI 素材        pic/cropped_images/*_rgb_*.png  (实际 12 个)

每行 = 一个测试点，被试调节达成主观一致后填写：
  匹配_UI_Nit, 匹配_UI_Alpha, 匹配_ChromaScale

输出: matching_experiment_data.csv (UTF-8 with BOM, Excel 中文兼容)
"""
import csv
import re
import sys
from pathlib import Path

# ---- 可调参数 ----
SDR_BG_NIT_FIXED = 350                       # SDR 窗口背景固定亮度 (nit)
SDR_ALPHAS = [round(0.1 * i, 1) for i in range(1, 11)]   # 0.1 .. 1.0
HDR_BG_NITS = list(range(0, 1001, 50))       # 0,50,...,1000 -> 21 点
# 若严格需要 20 组，改为: HDR_BG_NITS = list(range(0, 1000, 50))  # 0..950
#                或        HDR_BG_NITS = list(range(50, 1001, 50)) # 50..1000

ASSET_DIR = Path(__file__).resolve().parent.parent / "pic" / "cropped_images"
OUT_CSV   = Path(__file__).resolve().parent / "matching_experiment_data.csv"


def collect_ui_assets(asset_dir: Path):
    """收集所有 *_rgb_*.png，按 (id, x, y) 数字升序排序，返回去掉 .png 的名称。"""
    files = sorted(asset_dir.glob("*_rgb_*.png"))
    names = [f.stem for f in files]

    def key(n):
        m = re.match(r"(\d+)_rgb_(\d+)_(\d+)", n)
        return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (0, 0, 0)

    return sorted(names, key=key)


def main():
    if not ASSET_DIR.exists():
        print(f"[ERROR] 找不到素材目录: {ASSET_DIR}", file=sys.stderr)
        sys.exit(1)

    ui_assets = collect_ui_assets(ASSET_DIR)
    print(f"SDR Alpha 组数: {len(SDR_ALPHAS)}  {SDR_ALPHAS[0]}~{SDR_ALPHAS[-1]}")
    print(f"HDR BG Nit 点数: {len(HDR_BG_NITS)}  {HDR_BG_NITS[0]}~{HDR_BG_NITS[-1]} 步长 {HDR_BG_NITS[1]-HDR_BG_NITS[0]}")
    print(f"UI 素材数: {len(ui_assets)}")
    total = len(SDR_ALPHAS) * len(HDR_BG_NITS) * len(ui_assets)
    print(f"测试点总数: {len(SDR_ALPHAS)} x {len(HDR_BG_NITS)} x {len(ui_assets)} = {total}")
    print(f"SDR bg 固定: {SDR_BG_NIT_FIXED} nit")

    header = [
        "SDR_BG_Nit(固定)",
        "SDR_UI_Alpha",
        "HDR_BG_Nit",
        "UI素材",
        "匹配_UI_Nit",
        "匹配_UI_Alpha",
        "匹配_ChromaScale",
        "备注",
    ]

    # utf-8-sig => 写入 UTF-8 BOM, Excel/WPS 打开中文不乱码
    with OUT_CSV.open("w", encoding="utf-8-sig", newline="") as f:
        w = csv.writer(f)
        w.writerow(header)
        for alpha in SDR_ALPHAS:
            for bg in HDR_BG_NITS:
                for ui in ui_assets:
                    w.writerow([SDR_BG_NIT_FIXED, alpha, bg, ui, "", "", "", ""])

    print(f"\n已生成: {OUT_CSV}")
    print(f"总行数(含表头): {total + 1}")


if __name__ == "__main__":
    main()
