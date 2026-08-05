#!/usr/bin/env python3
"""
任务0: 垂直翻转原始 HDR bin (保存的原始画面上下颠倒)
输入: HDR bin (A2B10G10R10 PQ BT.2020, 上下颠倒) 于 hdr_dir
输出: <hdr名>_rotate.bin (翻转后副本, 存于同目录 hdr_dir)

垂直翻转 = 翻转行序 (raw[::-1, :]); 仅上下颠倒需此 (非 180° 旋转, 否则会引入左右镜像)。
无损操作 (仅重排像素, 不经 PQ 解码/编码)。
路径参数从 config.json (task0 节) 读取; task0.hdr_dir 默认 task1.hdr_dir。
用法:
    python3 rotate_hdr.py [--config config.json] [--hdr-dir X]
"""
import argparse
import os
import numpy as np

import common
from common import load_config, resolve_path, path_filled, natural_key, CONFIG_PATH


def rotate_one(path, out_path):
    """读 HDR bin (A2B10G10R10 uint32), 垂直翻转 (上下颠倒修正), 写出"""
    raw = np.fromfile(path, dtype='<u4').reshape(common.H_IMG, common.W_IMG)
    rotated = np.ascontiguousarray(raw[::-1, :])   # 垂直翻转: 仅翻转行序
    rotated.tofile(out_path)
    return out_path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--config', default=CONFIG_PATH, help='config.json 路径')
    ap.add_argument('--hdr-dir', default=None, help='覆盖 hdr_dir (默认 config.task1.hdr_dir)')
    args = ap.parse_args()

    cfg = load_config(args.config)
    common.set_dims(cfg['common']['height'], cfg['common']['width'])

    # hdr_dir: task0 未填则默认 task1.hdr_dir
    task0_cfg = cfg.get('task0', {})
    hdr_dir_cfg = task0_cfg.get('hdr_dir', '')
    if args.hdr_dir:
        hdr_dir = resolve_path(args.hdr_dir)
    elif path_filled(hdr_dir_cfg):
        hdr_dir = resolve_path(hdr_dir_cfg)
    elif path_filled(cfg['task1']['hdr_dir']):
        hdr_dir = resolve_path(cfg['task1']['hdr_dir'])
    else:
        raise SystemExit("config.task0.hdr_dir 未设置 (默认 task1.hdr_dir 也未设置)")
    if not os.path.isdir(hdr_dir):
        raise SystemExit(f"目录不存在: {hdr_dir}")

    # 原始 HDR bin: 排除已旋转副本 (_rotate) 和任务1/2/3 产生的后缀文件
    files = sorted([os.path.join(hdr_dir, fn) for fn in os.listdir(hdr_dir)
                    if fn.endswith('.bin')
                    and not fn.endswith(('_rotate.bin', '_uiAlpha.bin',
                                         '_uiRGB.bin', '_withUI.bin'))], key=natural_key)
    print(f"HDR bins: {len(files)}, hdr_dir={hdr_dir}")
    n = 0
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0]
        out = os.path.join(hdr_dir, name + '_rotate.bin')
        if os.path.exists(out):
            print(f"  [skip] {name}: 已存在 {name}_rotate.bin")
            continue
        rotate_one(f, out)
        print(f"  {name} -> {out}")
        n += 1
    print(f"Done. rotated {n}/{len(files)}")


if __name__ == '__main__':
    main()
