#!/usr/bin/env python3
"""
任务2: 调整后 UI 与 HDR 背景做 alpha blending (线性 nits BT.2020 域)
输入: HDR bin (原始背景) + UI alpha bin (fp16 eff) + UI rgb bin (A2B10G10R10 PQ, 任务1输出)
输出: 含 UI 的 HDR bin (<hdr名>_withUI.bin, A2B10G10R10 PQ BT.2020)

blend 流程与测试代码 hdr_ui.frag:154-158 一致:
    bgNit = pqDecode(hdr_bin), uiNit = pqDecode(ui_rgb_bin)
    mixed = uiNit * eff + bgNit * (1 - eff)     (线性 nits 域)
    out = linearToPQ(mixed) -> A2B10G10R10 (A=1)

UI 外像素 eff=0 -> mixed = bgNit (原 HDR 背景)。
路径参数从 config.json (task2 节) 读取。
用法:
    python3 blend_ui.py [--config config.json] [--hdr-dir X] [--ui-bin-dir Y] [--outdir Z]
"""
import argparse
import os
import numpy as np

from common import (load_config, resolve_path, CONFIG_PATH,
                    read_hdr_bin, read_uialpha_bin, linear_to_pq,
                    pack_a2b10g10r10, PQ_MAX_NIT)


def blend_one(hdr_path, uirgb_path, uialpha_path, out_path):
    bg_nit = read_hdr_bin(hdr_path)              # HxWx3 线性 nits BT.2020
    ui_nit = read_hdr_bin(uirgb_path)            # _uiRGB.bin 同格式 A2B10G10R10 PQ
    eff = read_uialpha_bin(uialpha_path)         # HxW eff[0,1]
    eff_e = eff[..., None]
    mixed = ui_nit * eff_e + bg_nit * (1.0 - eff_e)
    mixed = np.clip(mixed, 0.0, PQ_MAX_NIT)      # 限制 PQ 定义域 [0, 10000] nit
    pq = linear_to_pq(mixed)
    packed = pack_a2b10g10r10(pq[..., 0] * 1023.0, pq[..., 1] * 1023.0,
                              pq[..., 2] * 1023.0, A=1.0)
    packed.tofile(out_path)
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--config', default=CONFIG_PATH, help='config.json 路径')
    ap.add_argument('--hdr-dir', default=None, help='覆盖 config.task2.hdr_dir (原始 HDR bin)')
    ap.add_argument('--ui-bin-dir', default=None, help='覆盖 config.task2.ui_bin_dir (任务1输出)')
    ap.add_argument('--outdir', default=None, help='覆盖 config.task2.outdir')
    args = ap.parse_args()

    cfg = load_config(args.config)
    hdr_dir = args.hdr_dir or resolve_path(cfg['task2']['hdr_dir'])
    ui_bin_dir = args.ui_bin_dir or resolve_path(cfg['task2']['ui_bin_dir'])
    outdir = args.outdir or resolve_path(cfg['task2']['outdir'])
    for d, name in [(hdr_dir, 'task2.hdr_dir'), (ui_bin_dir, 'task2.ui_bin_dir'),
                    (outdir, 'task2.outdir')]:
        if not d:
            raise SystemExit(f"config.{name} 未设置")
    os.makedirs(outdir, exist_ok=True)

    # 原始 HDR bin: 排除任务1/任务2产生的后缀文件
    hdr_files = sorted([os.path.join(hdr_dir, fn) for fn in os.listdir(hdr_dir)
                        if fn.endswith('.bin')
                        and not fn.endswith(('_uiAlpha.bin', '_uiRGB.bin', '_withUI.bin'))])
    print(f"HDR bins: {len(hdr_files)}")
    n_ok = 0
    for h in hdr_files:
        name = os.path.splitext(os.path.basename(h))[0]
        uialpha = os.path.join(ui_bin_dir, name + '_uiAlpha.bin')
        uirgb = os.path.join(ui_bin_dir, name + '_uiRGB.bin')
        if not (os.path.exists(uialpha) and os.path.exists(uirgb)):
            print(f"  [skip] {name}: 未找到对应 UI bin (期望 {name}_uiAlpha.bin / _uiRGB.bin 于 {ui_bin_dir})")
            continue
        out = os.path.join(outdir, name + '_withUI.bin')
        blend_one(h, uirgb, uialpha, out)
        print(f"  {name} -> {out}")
        n_ok += 1
    print(f"Done. blended {n_ok}/{len(hdr_files)}")


if __name__ == '__main__':
    main()
