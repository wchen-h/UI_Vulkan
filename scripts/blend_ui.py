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
    python3 blend_ui.py [--config config.json] [--hdr-dir X] [--ui-dir D] [--outdir Z]
"""
import argparse
import os
import numpy as np

import common
from common import (load_config, resolve_path, path_filled, natural_key, CONFIG_PATH,
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
    ap.add_argument('--hdr-dir', default=None, help='覆盖 config.task2.hdr_dir (原始 HDR bin 目录)')
    ap.add_argument('--ui-dir', default=None, help='覆盖 config.task2.ui_dir (任务1 输出目录, 默认 task1.outdir)')
    ap.add_argument('--outdir', default=None, help='覆盖 config.task2.outdir')
    args = ap.parse_args()

    cfg = load_config(args.config)
    common.set_dims(cfg['common']['height'], cfg['common']['width'])

    # 必填: 原始 HDR bin 目录
    if not path_filled(cfg['task2']['hdr_dir']) and not args.hdr_dir:
        raise SystemExit("config.task2.hdr_dir 未设置")
    hdr_dir = args.hdr_dir or resolve_path(cfg['task2']['hdr_dir'])
    if not os.path.isdir(hdr_dir):
        raise SystemExit(f"目录不存在: {hdr_dir} (config.task2.hdr_dir)")

    # 可选 UI bin 目录: 用户填了用用户的, 没填默认 task1.outdir
    ui_dir_cfg = cfg['task2'].get('ui_dir', '')
    if args.ui_dir:
        ui_dir = resolve_path(args.ui_dir)
    elif path_filled(ui_dir_cfg):
        ui_dir = resolve_path(ui_dir_cfg)
    elif path_filled(cfg['task1']['outdir']):
        ui_dir = resolve_path(cfg['task1']['outdir'])
    else:
        raise SystemExit("config.task2.ui_dir 未设置, 且默认值 config.task1.outdir 也未设置")
    if not os.path.isdir(ui_dir):
        raise SystemExit(f"UI bin 目录不存在: {ui_dir} (默认 task1.outdir; 若任务1 输出在别处, 请设 config.task2.ui_dir)")

    # 必填: 输出目录
    if not path_filled(cfg['task2']['outdir']) and not args.outdir:
        raise SystemExit("config.task2.outdir 未设置")
    outdir = args.outdir or resolve_path(cfg['task2']['outdir'])
    os.makedirs(outdir, exist_ok=True)

    # 旋转后的 HDR bin (_rotate 副本): 与任务1 输出 (_rotate_uiAlpha/_uiRGB) 按名配对
    hdr_files = sorted([os.path.join(hdr_dir, fn) for fn in os.listdir(hdr_dir)
                        if fn.endswith('_rotate.bin')], key=natural_key)
    if not hdr_files:
        raise SystemExit(f"{hdr_dir} 下未找到 *_rotate.bin; 请先运行 task0 旋转: python3 scripts/rotate_hdr.py")
    print(f"HDR bins: {len(hdr_files)}, UI dir={ui_dir}")
    n_ok = 0
    for h in hdr_files:
        name = os.path.splitext(os.path.basename(h))[0]
        uialpha = os.path.join(ui_dir, name + '_uiAlpha.bin')
        uirgb = os.path.join(ui_dir, name + '_uiRGB.bin')
        if not (os.path.isfile(uialpha) and os.path.isfile(uirgb)):
            print(f"  [skip] {name}: 未找到 {name}_uiAlpha.bin / _uiRGB.bin 于 {ui_dir}")
            continue
        out = os.path.join(outdir, name + '_withUI.bin')
        blend_one(h, uirgb, uialpha, out)
        print(f"  {name} -> {out}")
        n_ok += 1
    if n_ok == 0:
        raise SystemExit(f"未在 {ui_dir} 找到匹配的 UI bin (期望 <hdr名>_uiAlpha.bin / _uiRGB.bin)")
    print(f"Done. blended {n_ok}/{len(hdr_files)}")


if __name__ == '__main__':
    main()
