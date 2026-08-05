#!/usr/bin/env python3
"""
任务1: UI 亮度和不透明度 HDR 补偿调整
输入: UI alpha png(8-bit灰度) + UI rgb png(8-bit sRGB BT.709) + HDR bin(A2B10G10R10 PQ BT.2020)
输出: 每个 HDR bin -> <hdr名>_uiAlpha.bin(单通道fp16) + <hdr名>_uiRGB.bin(A2B10G10R10, A=1)

补偿规律来自 hdr_compensation_plan_v2.md 4.2/4.3 节; f/fy 调节见 5.1/5.2。
路径参数从 config.json (task1 节) 读取。
用法:
    python3 adjust_ui.py [--config config.json] [--hdr-dir X] [--outdir Y]
"""
import argparse
import os
import numpy as np
from PIL import Image
from scipy.ndimage import label as cc_label, find_objects

import common
from common import (PAPER_WHITE_NIT, BT709_TO_BT2020,
                    srgb_to_linear, srgb_to_chroma, linear_to_pq,
                    rgb_to_ycbcr2020, ycbcr_to_rgb2020,
                    read_hdr_bin, pack_a2b10g10r10,
                    load_config, resolve_path, path_filled, natural_key, CONFIG_PATH)


# ===== 补偿公式 (hdr_compensation_plan_v2.md 4.2/4.3 基础 + 5.1/5.2 f/fy 插值) =====

def g1(a):     # f=1 时的 g(a)
    return 0.913 * np.power(a, 1.894)

def b1(a):     # f=1 时的 b(a)
    return 1.0 - 0.690 * np.power(1.0 - a, 1.769)

def tau1(a):   # f=1 时的 τ(a)
    return 104.0 * np.power(a / 0.5, -0.567)

def g_a(a, f):      # 5.1: g(f,a) = a + (g1(a)-a)·f
    return a + (g1(a) - a) * f

def b_a(a, f):      # 5.1: b(f,a) = a + (b1(a)-a)·f
    return a + (b1(a) - a) * f

def tau_a(a, f):    # 5.1: τ(f,a) = τ0 + (τ1(a)-τ0)·f, τ0=500 (f=0 时 exp≈1, eff≈a)
    return 500.0 + (tau1(a) - 500.0) * f

def eff_neutral(B, a, f):
    with np.errstate(divide='ignore', invalid='ignore'):
        b, g, t = b_a(a, f), g_a(a, f), tau_a(a, f)
        return b - (b - g) * np.exp(-B / t)

def delta_color(B, f):   # 5.1: Δ_f(B) = f·Δ1(B)
    return f * (-0.085 * np.exp(-B / 295.0) + 0.012)

def yscale(B, fy):      # 5.2: ys(B,fy) = 1 + fy·(ys1(B)-1)
    sig = 1.0 / (1.0 + np.exp(-(B - 100.0) / 20.0))
    ys1 = 1.0 + 0.0837 * sig * np.log(np.maximum(1.0, B / 70.0))
    return 1.0 + fy * (ys1 - 1.0)


# ===== bbox 识别 + B 计算 =====

def find_ui_bboxes(alpha):
    mask = alpha > 0.0
    structure = np.ones((3, 3), dtype=int)  # 8 连通
    labeled, n = cc_label(mask, structure=structure)
    objs = find_objects(labeled)
    bboxes = []
    for i, (ys, xs) in enumerate(objs, start=1):
        if ys is None:
            continue
        bboxes.append((i, xs.start, ys.start, xs.stop - 1, ys.stop - 1))  # lab_id,x0,y0,x1,y1
    return bboxes, labeled


def merge_overlapping_bboxes(bboxes):
    """合并重叠的 bbox (并集矩形, 大的扩展覆盖小的, 消除重叠)。
    返回: merged=[(x0,y0,x1,y1),...], lab_map={lab_id: merged_idx}"""
    boxes = [[lab_id, x0, y0, x1, y1, [lab_id]]
             for (lab_id, x0, y0, x1, y1) in bboxes]

    def _ov(a, b):  # 真重叠 (不含仅相切)
        return a[1] < b[3] and b[1] < a[3] and a[2] < b[4] and b[2] < a[4]

    changed = True
    while changed:
        changed = False
        for i in range(len(boxes)):
            if boxes[i] is None:
                continue
            for j in range(i + 1, len(boxes)):
                if boxes[j] is None:
                    continue
                if _ov(boxes[i], boxes[j]):
                    a, b = boxes[i], boxes[j]
                    boxes[i] = [-1, min(a[1], b[1]), min(a[2], b[2]),
                                max(a[3], b[3]), max(a[4], b[4]), a[5] + b[5]]
                    boxes[j] = None
                    changed = True
        boxes = [b for b in boxes if b is not None]
    merged = [(b[1], b[2], b[3], b[4]) for b in boxes]
    lab_map = {lab_id: idx for idx, b in enumerate(boxes) for lab_id in b[5]}
    return merged, lab_map


def compute_bg_brightness(x0, y0, x1, y1, hdr_nits):
    cx = (x0 + x1) / 2.0
    cy = (y0 + y1) / 2.0
    hw = (x1 - x0 + 1)   # bbox 宽
    hh = (y1 - y0 + 1)   # bbox 高
    left = int(round(cx - hw))
    right = int(round(cx + hw))
    top = int(round(cy - hh))
    bottom = int(round(cy + hh))
    left, right = max(left, 0), min(right, common.W_IMG)
    top, bottom = max(top, 0), min(bottom, common.H_IMG)
    region = hdr_nits[top:bottom, left:right, :]
    Y = 0.2627 * region[..., 0] + 0.6780 * region[..., 1] + 0.0593 * region[..., 2]
    B = float(np.mean(Y))
    return min(max(B, 30.0), 1000.0)


# ===== 主处理 =====

def process(ui_alpha_png, ui_rgb_png, hdr_bin_path, outdir, f=1.0, fy=1.0):
    # ---- 0. 读取输入 ----
    #   UI alpha: 8-bit 灰度 -> [0,1]
    #   UI rgb:   8-bit sRGB BT.709 -> [0,1]
    #   HDR bin:  A2B10G10R10 PQ BT.2020 -> 线性 nits BT.2020 (该帧纯背景, 不含 UI)
    name = os.path.splitext(os.path.basename(hdr_bin_path))[0]
    alpha = np.array(Image.open(ui_alpha_png).convert('L'), dtype=np.float64) / 255.0
    rgb = np.array(Image.open(ui_rgb_png).convert('RGB'), dtype=np.float64) / 255.0
    hdr_nits = read_hdr_bin(hdr_bin_path)  # HxWx3 线性nits BT.2020 (纯背景)

    # ---- 步骤1: 在 alpha 上识别 UI bbox (alpha>0 = UI 覆盖区域) ----
    #   alpha>0 的 8 连通域 = 候选 UI; 记录最小外接矩形 (x0,y0)-(x1,y1)
    bboxes, labeled = find_ui_bboxes(alpha)

    # ---- 步骤1.5: 用 alpha 占比筛选, 删除 bbox 内 alpha>0 占比 < 50% 的 (碎片/噪点) ----
    bboxes = [(lab_id, x0, y0, x1, y1) for (lab_id, x0, y0, x1, y1) in bboxes
              if (alpha[y0:y1 + 1, x0:x1 + 1] > 0).sum() >= 0.5 * ((x1 - x0 + 1) * (y1 - y0 + 1))]

    # ---- 步骤2: 合并重叠 bbox (大的扩展覆盖小的, 消除重叠) ----
    merged_boxes, lab_to_merged = merge_overlapping_bboxes(bboxes)

    # ---- 步骤3: 计算每个 UI 背景区域的平均亮度 B (BT.2020 luma), clamp 到 [30,1000] ----
    #   B = 区域内 HDR 线性 nits 的 BT.2020 luma (Y=0.2627R+0.6780G+0.0593B) 取平均
    #   每个 UI 用其所属合并 bbox 的背景区域 (消除重叠后)
    B_lut = np.zeros(int(labeled.max()) + 1, dtype=np.float64)
    for (lab_id, x0, y0, x1, y1) in bboxes:
        mx0, my0, mx1, my1 = merged_boxes[lab_to_merged[lab_id]]
        B_lut[lab_id] = compute_bg_brightness(mx0, my0, mx1, my1, hdr_nits)
    B_map = np.clip(B_lut[labeled], 30.0, 1000.0)   # 每像素 B = 其所属 UI 的 B (UI 外为 0, 但 UI 外 alpha=0 不参与计算)

    # 中性色/彩色判定: 逐像素 CIELAB C* (BT.709/D65), C*>5 视为彩色
    chroma = srgb_to_chroma(rgb)
    is_color = chroma > 5.0

    # ---- 步骤4: 逐像素调整 UI alpha -> Eff.Alpha (§4.2 模型 + §5.1 f 调节) ----
    #   输入: a=该像素初始 alpha, B=该像素所属 UI 的背景亮度, f=沉浸度滑块
    #   中性色: eff = b(f,a) - (b(f,a)-g(f,a)) * exp(-B/τ(f,a))
    #   彩色:   eff = eff_neutral + Δ_f(B); 但若 eff+Δ > 1 则不加 Δ (避免超 1)
    #   初始 alpha=0 的像素直接输出 eff=0
    a = alpha
    eff = eff_neutral(B_map, a, f)
    eff = np.where(a > 0, eff, 0.0)  # 初始 alpha=0 直接输出 0
    d = delta_color(B_map, f)
    eff_with_delta = eff + d
    use_delta = is_color & (eff_with_delta <= 1.0)   # 彩色且不超 1 才加 Δ
    eff = np.where(use_delta, eff_with_delta, eff)
    eff = np.clip(eff, 0.0, 1.0)   # 下界 0: 低 a 彩色像素加 Δ 可能算出负

    # 输出 _uiAlpha.bin: 单通道 fp16, 每像素 Eff.Alpha [0,1]
    alpha_out = eff.astype(np.float16)
    alpha_path = os.path.join(outdir, name + '_uiAlpha.bin')
    alpha_out.tofile(alpha_path)

    # ---- 步骤5: 逐像素调整 UI 亮度 (Y-Scale), 输出 PQ+BT.2020 ----
    #   流程与测试代码 hdr_ui.frag:113-149 一致:
    #     sRGB -> linear -> ×paperwhite -> BT.709 ->[BT.709→BT.2020 矩阵]-> 线性 nits BT.2020
    #     -> PQ encode -> ×1023 (10-bit) -> BT.2020 YCbCr -> Y×ys (Cb/Cr 不变)
    #     -> clamp -> YCbCr->RGB -> /1023 -> PQ code -> A2B10G10R10 (A=1)
    #   ys 由 B 决定 (§4.3, 与颜色/a 无关); 初始 rgb 全 0 的黑色像素直接输出 0
    black = (rgb.sum(axis=-1) == 0)
    lin709 = srgb_to_linear(rgb) * PAPER_WHITE_NIT          # sRGB->linear, ×350 -> 线性nits BT.709
    lin2020 = lin709 @ BT709_TO_BT2020.T                     # BT.709->BT.2020 矩阵 -> 线性nits BT.2020
    pq = linear_to_pq(lin2020)                              # 线性nits -> PQ code [0,1]
    rgb10 = pq * 1023.0                                     # -> 10-bit [0,1023] (PQ 域)
    Y, Cb, Cr = rgb_to_ycbcr2020(rgb10[..., 0], rgb10[..., 1], rgb10[..., 2])  # RGB->YCbCr (PQ 域, 10-bit full range)
    ys = yscale(B_map, fy)                                  # Y-Scale (§4.3, 仅依赖 B)
    Y = np.clip(Y * ys, 0.0, 1023.0)                        # 只缩放 Y, Cb/Cr 保持
    Cb = np.clip(Cb, 0.0, 1023.0)
    Cr = np.clip(Cr, 0.0, 1023.0)
    R_adj, G_adj, B_adj = ycbcr_to_rgb2020(Y, Cb, Cr)       # YCbCr->RGB (PQ 域)
    R_adj = np.clip(R_adj, 0.0, 1023.0)
    G_adj = np.clip(G_adj, 0.0, 1023.0)
    B_adj = np.clip(B_adj, 0.0, 1023.0)
    pq_out = np.stack([R_adj, G_adj, B_adj], axis=-1) / 1023.0   # /1023 -> PQ code [0,1]
    pq_out[black] = 0.0                                     # 黑色像素 (初始 rgb 全 0) 直接输出 0
    rgb_packed = pack_a2b10g10r10(pq_out[..., 0] * 1023.0,
                                  pq_out[..., 1] * 1023.0,
                                  pq_out[..., 2] * 1023.0, A=1.0)   # -> A2B10G10R10 (A=1.0)
    rgb_path = os.path.join(outdir, name + '_uiRGB.bin')
    rgb_packed.tofile(rgb_path)

    print(f"  {name}: {len(bboxes)} UIs, B=[{B_map[alpha>0].min():.1f},{B_map[alpha>0].max():.1f}] "
          f"eff=[{eff[alpha>0].min():.3f},{eff[alpha>0].max():.3f}] ys=[{ys[alpha>0].min():.3f},{ys[alpha>0].max():.3f}]")
    print(f"    -> {alpha_path}\n    -> {rgb_path}")
    return alpha_path, rgb_path


def main():
    # ---- 命令行参数 (config 路径 + 覆盖项) ----
    ap = argparse.ArgumentParser()
    ap.add_argument('--config', default=CONFIG_PATH, help='config.json 路径')
    ap.add_argument('--hdr-dir', default=None, help='覆盖 config.task1.hdr_dir')
    ap.add_argument('--outdir', default=None, help='覆盖 config.task1.outdir')
    args = ap.parse_args()

    # ---- 读 config: UI png 固定不变, HDR bin 逐帧变化 ----
    cfg = load_config(args.config)
    common.set_dims(cfg['common']['height'], cfg['common']['width'])

    # 必填输入: UI png (固定), 检查已填 + 文件存在
    for p, name in [(cfg['common']['ui_alpha_png'], 'common.ui_alpha_png'),
                     (cfg['common']['ui_rgb_png'], 'common.ui_rgb_png')]:
        if not path_filled(p):
            raise SystemExit(f"config.{name} 未设置")
    ui_alpha = resolve_path(cfg['common']['ui_alpha_png'])
    ui_rgb = resolve_path(cfg['common']['ui_rgb_png'])
    for p, name in [(ui_alpha, 'common.ui_alpha_png'), (ui_rgb, 'common.ui_rgb_png')]:
        if not os.path.isfile(p):
            raise SystemExit(f"文件不存在: {p} (config.{name})")

    # 必填: 原始 HDR bin 目录, 检查已填 + 目录存在
    if not path_filled(cfg['task1']['hdr_dir']) and not args.hdr_dir:
        raise SystemExit("config.task1.hdr_dir 未设置")
    hdr_dir = args.hdr_dir or resolve_path(cfg['task1']['hdr_dir'])
    if not os.path.isdir(hdr_dir):
        raise SystemExit(f"目录不存在: {hdr_dir} (config.task1.hdr_dir)")

    # 必填: 输出目录
    if not path_filled(cfg['task1']['outdir']) and not args.outdir:
        raise SystemExit("config.task1.outdir 未设置")
    outdir = args.outdir or resolve_path(cfg['task1']['outdir'])
    os.makedirs(outdir, exist_ok=True)

    f = float(cfg['common'].get('f', 1.0))                   # 沉浸度滑块 (Eff.Alpha 强度, §5.1)
    fy = float(cfg['common'].get('fy', 1.0))                 # 亮度保真度滑块 (Y-Scale 强度, §5.2)

    # ---- 遍历旋转后的 HDR bin (_rotate 副本), 逐帧处理 ----
    #   原始 HDR bin 上下颠倒, 需先经 task0 (rotate_hdr.py) 旋转 180° 生成 <名>_rotate.bin
    #   每帧 -> 对应输出 <名>_rotate_uiAlpha.bin + <名>_rotate_uiRGB.bin
    hdr_files = sorted([os.path.join(hdr_dir, fn) for fn in os.listdir(hdr_dir)
                        if fn.endswith('_rotate.bin')], key=natural_key)
    if not hdr_files:
        raise SystemExit(f"{hdr_dir} 下未找到 *_rotate.bin; 原始 HDR 画面上下颠倒, "
                         f"请先运行 task0 旋转: python3 scripts/rotate_hdr.py")
    print(f"UI: alpha={ui_alpha} rgb={ui_rgb} f={f} fy={fy}")
    print(f"HDR bins: {len(hdr_files)}")
    for h in hdr_files:
        process(ui_alpha, ui_rgb, h, outdir, f, fy)
    print("Done.")


if __name__ == '__main__':
    main()
