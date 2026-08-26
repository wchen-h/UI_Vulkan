#!/usr/bin/env python3
"""
T2: UI and background distinction via temporal differencing.

Input:  Directory of SDR bins (A2B10G10R10_UNORM, sRGB+BT.709, multiple frames)
Output: PNG image(s) with UI edges marked in red

Temporal differencing principle:
  - Opaque UI:   delta_M ~ 0  (static UI, color never changes)
  - Semi-trans UI: delta_M small (bg changes dampened by (1-alpha))
  - Background:  delta_M large  (scene changes directly)

Usage:
    python3 detect_ui.py --sdr-dir bin/hdr_img/SDR_ui_on_lkwg --output ui_edges.png
    python3 detect_ui.py --sdr-dir <dir> --theta-quiet 0.005 --theta-bg 0.05
"""
import argparse
import os
import numpy as np
from PIL import Image
from scipy.ndimage import binary_erosion, label as cc_label, find_objects


def auto_detect_resolution(path):
    """Auto-detect H, W from bin file size (4 bytes/pixel)."""
    sz = os.path.getsize(path)
    pixels = sz // 4
    for (h, w) in [(1080, 2328), (2160, 4660), (1080, 1920), (720, 1280)]:
        if pixels == h * w:
            return h, w
    # Fallback: assume 1080 rows
    h = 1080
    w = pixels // h
    return h, w


def read_sdr_bin(path, H, W):
    """Read A2B10G10R10 sRGB BT.709 bin -> sRGB [0,1] (H x W x 3).

    Layout: little-endian uint32, R[0:9] | G[10:19] | B[20:29] | A[30:31]
    10-bit value / 1023 = sRGB-encoded value (NOT linear, NOT PQ).
    """
    raw = np.fromfile(path, dtype='<u4').astype(np.uint32).reshape(H, W)
    R = (raw & 0x3FF).astype(np.float64)
    G = ((raw >> 10) & 0x3FF).astype(np.float64)
    B = ((raw >> 20) & 0x3FF).astype(np.float64)
    return np.stack([R, G, B], axis=-1) / 1023.0


def compute_temporal_diff(frames):
    """Compute average per-pixel temporal difference across consecutive frame pairs.

    frames: list of (H, W, 3) arrays in sRGB [0,1]
    Returns: avg_diff (H, W) float, average of max-channel |M_t - M_{t-1}|
    """
    if len(frames) < 2:
        raise ValueError("Need at least 2 frames for temporal differencing")
    diffs = []
    for i in range(1, len(frames)):
        d = np.abs(frames[i] - frames[i - 1]).max(axis=-1)
        diffs.append(d)
    return np.mean(diffs, axis=0)


def classify_pixels(avg_diff, theta_quiet=0.005, theta_bg=0.05):
    """Classify pixels by temporal difference into 3 classes.

    Returns: ui_mask (all UI), opaque_mask, semi_mask, bg_mask
    """
    opaque_mask = avg_diff < theta_quiet
    bg_mask = avg_diff >= theta_bg
    semi_mask = ~opaque_mask & ~bg_mask
    ui_mask = opaque_mask | semi_mask
    return ui_mask, opaque_mask, semi_mask, bg_mask


def find_ui_edges(ui_mask):
    """Find boundary pixels of UI regions (UI pixel with at least one non-UI neighbor).

    Uses morphological erosion: edge = mask AND NOT eroded(mask)
    """
    eroded = binary_erosion(ui_mask, iterations=1)
    edges = ui_mask & ~eroded
    return edges


def draw_edges_on_image(image_srgb, edges, edge_color=(255, 0, 0)):
    """Draw red edges on an sRGB [0,1] image, return uint8 RGB for display.

    image_srgb: (H, W, 3) float [0,1] sRGB
    edges: (H, W) bool
    edge_color: (R, G, B) uint8
    """
    img8 = (np.clip(image_srgb, 0, 1) * 255).astype(np.uint8)
    img8[edges] = edge_color
    return img8


def process_frame(sdr_bin_path, frames, H, W,
                 theta_quiet=0.005, theta_bg=0.05, output_path=None,
                 alpha_png=None):
    """Full pipeline: read frames, compute diff, classify, draw edges, save PNG.

    If alpha_png is provided, use it as UI mask instead of temporal differencing
    (for testing/validation when temporal data is insufficient).
    """
    if alpha_png:
        ui_mask = np.array(Image.open(alpha_png).convert('L'),
                          dtype=np.float64) / 255.0 > 0.0
        opaque_mask = np.array(Image.open(alpha_png).convert('L'),
                              dtype=np.float64) / 255.0 >= 0.5
        semi_mask = ui_mask & ~opaque_mask
        bg_mask = ~ui_mask
        avg_diff = None
        print("Using alpha.png as UI mask (temporal differencing bypassed)")
    else:
        avg_diff = compute_temporal_diff(frames)
        ui_mask, opaque_mask, semi_mask, bg_mask = classify_pixels(
            avg_diff, theta_quiet, theta_bg)

    edges = find_ui_edges(ui_mask)

    # Use the specified frame as the base image for display
    base = read_sdr_bin(sdr_bin_path, H, W)
    result = draw_edges_on_image(base, edges)

    # Stats
    total = H * W
    ui_pct = ui_mask.sum() / total * 100
    opaque_pct = opaque_mask.sum() / total * 100
    semi_pct = semi_mask.sum() / total * 100
    bg_pct = bg_mask.sum() / total * 100

    print(f"Resolution: {W}x{H}")
    if avg_diff is not None:
        print(f"Frames: {len(frames)}")
        print(f"  avg_diff: mean={avg_diff.mean():.6f}, median={np.median(avg_diff):.6f}, "
              f"95%={np.percentile(avg_diff, 95):.6f}")
        print(f"Thresholds: theta_quiet={theta_quiet}, theta_bg={theta_bg}")
    print(f"  Opaque UI:  {opaque_mask.sum():8d} px ({opaque_pct:.1f}%)")
    print(f"  Semi-trans: {semi_mask.sum():8d} px ({semi_pct:.1f}%)")
    print(f"  Background: {bg_mask.sum():8d} px ({bg_pct:.1f}%)")
    print(f"  UI total:   {ui_mask.sum():8d} px ({ui_pct:.1f}%)")
    print(f"  Edge pixels: {edges.sum():8d}")

    if output_path:
        Image.fromarray(result).save(output_path)
        print(f"Saved: {output_path}")

    return result, ui_mask, avg_diff


def main():
    ap = argparse.ArgumentParser(
        description="T2: Detect UI pixels via temporal differencing")
    ap.add_argument('--sdr-dir', required=True,
                    help='Directory containing SDR bins (multiple frames)')
    ap.add_argument('--output', default='ui_edges.png',
                    help='Output PNG path (default: ui_edges.png)')
    ap.add_argument('--frame', type=int, default=0,
                    help='Which frame to use as base image for display (default: 0)')
    ap.add_argument('--theta-quiet', type=float, default=0.005,
                    help='Delta below this = opaque UI (default: 0.005)')
    ap.add_argument('--theta-bg', type=float, default=0.05,
                    help='Delta above this = background (default: 0.05)')
    ap.add_argument('--height', type=int, default=None,
                    help='Image height (default: auto-detect)')
    ap.add_argument('--width', type=int, default=None,
                    help='Image width (default: auto-detect)')
    ap.add_argument('--alpha-png', default=None,
                    help='Use alpha.png as UI mask instead of temporal diff (fallback)')
    args = ap.parse_args()

    # Collect SDR bins
    bins = sorted(
        [os.path.join(args.sdr_dir, f) for f in os.listdir(args.sdr_dir)
         if f.endswith('.bin')],
        key=lambda p: [int(t) if t.isdigit() else t
                       for t in __import__('re').split(r'(\d+)', os.path.basename(p))])
    if len(bins) < 2 and not args.alpha_png:
        raise SystemExit(f"Need at least 2 SDR bins in {args.sdr_dir}, found {len(bins)}")

    # Resolution
    if args.height and args.width:
        H, W = args.height, args.width
    else:
        H, W = auto_detect_resolution(bins[0])

    print(f"Reading {len(bins)} frames from {args.sdr_dir}")
    frames = [read_sdr_bin(b, H, W) for b in bins] if len(bins) >= 2 else []

    # Use the specified frame as base for display
    base_path = bins[min(args.frame, len(bins) - 1)]

    process_frame(base_path, frames, H, W,
                  theta_quiet=args.theta_quiet, theta_bg=args.theta_bg,
                  output_path=args.output,
                  alpha_png=args.alpha_png)


if __name__ == '__main__':
    main()
