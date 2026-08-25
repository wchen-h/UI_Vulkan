#!/usr/bin/env python3
"""
生成测试用空 CSV 表格（hdr_compensation_plan.md 6.2 节）

7 个 UI × 10 alpha × 9 B = 630 行
alpha=1.0 时 Eff.Alpha 列留空（仅记录 Y-Scale）
"""

import os
import csv

OUTPUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "0721")

UIS = [
    "t_neutral",
    "t_redorange",
    "t_redorange_mid",
    "t_green",
    "t_green_mid",
    "t_bluepurple",
    "t_bluepurple_mid",
]

ALPHAS = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]
B_VALUES = [0, 30, 60, 100, 150, 200, 300, 500, 1000]

HEADER = [
    "SDR_UI_Alpha", "HDR_BG_Nit",
    "Y-Scale Foreground", "Eff.Alpha Foreground",
    "匹配_ChromaScale", "备注",
]


def main():
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for ui in UIS:
        path = os.path.join(OUTPUT_DIR, f"{ui}_colorbg.csv")
        with open(path, "w", newline="", encoding="utf-8-sig") as f:
            writer = csv.writer(f)
            writer.writerow(HEADER)
            for alpha in ALPHAS:
                for b in B_VALUES:
                    writer.writerow([alpha, b, "", "", "", ""])
        print(f"  {path}  ({len(ALPHAS) * len(B_VALUES)} rows)")

    print(f"\nDone. {len(UIS)} files, {len(ALPHAS) * len(B_VALUES)} rows each, "
          f"{len(UIS) * len(ALPHAS) * len(B_VALUES)} total.")


if __name__ == "__main__":
    main()
