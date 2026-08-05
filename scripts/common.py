#!/usr/bin/env python3
"""共享: 色彩转换、HDR bin 读写、config 读取 (adjust_ui / blend_ui 共用)"""
import os
import re
import json
import numpy as np

PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.path.join(PROJECT_ROOT, "config.json")

PAPER_WHITE_NIT = 350.0
H_IMG, W_IMG = 1080, 2328

# BT.709 -> BT.2020 (行主序, new = M @ old), 标准 BT.2407 线性光域矩阵
BT709_TO_BT2020 = np.array([
    [0.627404, 0.329283, 0.043313],
    [0.069097, 0.919540, 0.011362],
    [0.016391, 0.088013, 0.895595]], dtype=np.float64)

# sRGB(BT.709) -> XYZ D65 (行主序)
SRGB2XYZ = np.array([
    [0.4124, 0.3576, 0.1805],
    [0.2126, 0.7152, 0.0722],
    [0.0193, 0.1192, 0.9505]], dtype=np.float64)
XYZ_W = np.array([0.95047, 1.0, 1.08883], dtype=np.float64)  # D65

# PQ ST.2084 常数
M1 = 2610.0 / 16384.0
M2 = 2523.0 / 32.0
C1 = 3424.0 / 4096.0
C2 = 2413.0 / 128.0
C3 = 2392.0 / 128.0

PQ_MAX_NIT = 10000.0


# ===== config =====

def load_config(path=CONFIG_PATH):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


def resolve_path(p):
    return p if os.path.isabs(p) else os.path.join(PROJECT_ROOT, p)


def set_dims(h, w):
    """设置图像分辨率 (各脚本 main 从 config 读取后调用);
    read_hdr_bin / read_uialpha_bin 等按此 reshape"""
    global H_IMG, W_IMG
    H_IMG, W_IMG = int(h), int(w)


def path_filled(p):
    """路径是否已填写 (非空且非 <...> 占位符); 用于 config 必填校验"""
    return bool(p) and not str(p).startswith('<')


def natural_key(p):
    """自然排序键: 按文件名中的数字段数值排序 (frame_2 < frame_10, 而非字典序)"""
    return [int(t) if t.isdigit() else t
            for t in re.split(r'(\d+)', os.path.basename(str(p)))]


# ===== 色彩转换 =====

def srgb_to_linear(v):
    v = np.clip(v, 0.0, 1.0)
    return np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)


def linear_to_pq(linear_nits):
    """ST.2084 OETF: 线性 nits -> PQ code [0,1]; 输入 clamp 到 PQ 定义域 [0,10000]"""
    y = np.clip(linear_nits, 0.0, PQ_MAX_NIT) / PQ_MAX_NIT
    yp = y ** M1
    num = C1 + C2 * yp
    den = 1.0 + C3 * yp
    return (num / den) ** M2


def pq_decode(pq):
    """ST.2084 EOTF: PQ code [0,1] -> 线性 nits"""
    pq = np.clip(pq, 0.0, 1.0)
    vp = pq ** (1.0 / M2)
    num = np.clip(vp - C1, 0.0, None)
    den = np.clip(C2 - C3 * vp, 1e-4, None)
    return PQ_MAX_NIT * (num / den) ** (1.0 / M1)


def rgb_to_ycbcr2020(r, g, b):
    """BT.2020 YCbCr, 10-bit full range, 作用在 PQ code 上"""
    Y = 0.2627 * r + 0.6780 * g + 0.0593 * b
    Cb = (-0.1396 * r - 0.3604 * g + 0.5000 * b) + 512.0
    Cr = (0.5000 * r - 0.4598 * g - 0.0402 * b) + 512.0
    return Y, Cb, Cr


def ycbcr_to_rgb2020(Y, Cb, Cr):
    Cb = Cb - 512.0
    Cr = Cr - 512.0
    R = Y + 1.4746 * Cr
    B = Y + 1.8814 * Cb
    G = (Y - 0.2627 * R - 0.0593 * B) / 0.6780
    return R, G, B


def srgb_to_chroma(rgb):
    """sRGB [0,1] -> CIELAB C* (chroma), 用 BT.709/D65"""
    lin = srgb_to_linear(rgb)
    xyz = lin @ SRGB2XYZ.T
    xyz_n = xyz / XYZ_W
    eps = (6.0 / 29.0) ** 3
    f = np.where(xyz_n > eps, np.cbrt(xyz_n),
                 (1.0 / 3.0) * (29.0 / 6.0) ** 2 * xyz_n + 4.0 / 29.0)
    fx, fy, fz = f[..., 0], f[..., 1], f[..., 2]
    a_star = 500.0 * (fx - fy)
    b_star = 200.0 * (fy - fz)
    return np.sqrt(a_star ** 2 + b_star ** 2)


# ===== HDR bin 读写 (A2B10G10R10 UNORM, Vulkan PACK32) =====
# bit: R[0:9] | G[10:19] | B[20:29] | A[30:31], 小端 uint32, 值 /1023 归一化

def read_hdr_bin(path):
    """A2B10G10R10 PQ bin -> HxWx3 线性 nits BT.2020"""
    raw = np.fromfile(path, dtype='<u4').astype(np.uint32).reshape(H_IMG, W_IMG)
    R = (raw & 0x3FF).astype(np.float64)
    G = ((raw >> 10) & 0x3FF).astype(np.float64)
    B = ((raw >> 20) & 0x3FF).astype(np.float64)
    pq = np.stack([R, G, B], axis=-1) / 1023.0
    return pq_decode(pq)


def read_uialpha_bin(path):
    """单通道 fp16 eff 值 -> HxW float"""
    arr = np.fromfile(path, dtype=np.float16).astype(np.float64)
    return arr.reshape(H_IMG, W_IMG)


def pack_a2b10g10r10(R, G, B, A=1.0):
    """线性 RGB/PQ code(任意) -> A2B10G10R10 uint32 小端; A2 位放 A(默认1.0)"""
    ri = np.clip(np.rint(R), 0, 1023).astype(np.uint32)
    gi = np.clip(np.rint(G), 0, 1023).astype(np.uint32)
    bi = np.clip(np.rint(B), 0, 1023).astype(np.uint32)
    ai = np.clip(np.rint(A * 3.0), 0, 3).astype(np.uint32)
    return (ri | (gi << 10) | (bi << 20) | (ai << 30)).astype('<u4')
