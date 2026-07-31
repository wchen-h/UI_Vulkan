#!/usr/bin/env python3
"""
任务3: 含 UI 的 HDR bin 序列 -> HDR10 视频 (make_video.py)
流程 (沿用 commands.txt 两步式):
    1. 生成 metadata.txt (HDR Vivid 动态元数据, 每行 = 帧号 + 固定十进制参数, 帧号从1递增)
    2. cat 所有 <名>_withUI.bin 拼成一个 raw 序列 -> 命令2转 yuv420p10le
    3. 命令1: yuv -> libx265 HDR10 MP4 (BT.2020 + PQ + master-display, -vmeta_url metadata.txt)

分辨率从 config.json 的 common.width/height 读取 (默认 2328×1080, 即 -s 2328x1080)。
路径参数从 config.json (task3 节) 读取。
用法:
    python3 make_video.py [--config config.json] [--blended-dir X] [--video-out Y] [--keep-temp]
"""
import argparse
import os
import shutil
import subprocess
import tempfile

import common
from common import load_config, resolve_path, CONFIG_PATH

# HDR Vivid 动态元数据 (固定十进制参数, 来自用户提供)
METADATA_PAYLOAD = ("1 1 1343 0 3948 1 1 2770 1 5717 24 897 0 10 1 1 1 6 6 1 1 0 "
                    "252 614 245 573 127 1 2186 333 419 103 2080 1 8335 24 630 0 10 1 1 "
                    "1 6 6 1 1 0 252 0 613 306 127 1 1921 562 437 114 0")

# libx265 HDR10 参数 (commands.txt 命令1, 分辨率由 config 决定)
X265_PARAMS = ("keyint=50:bframes=0:colorprim=bt2020:transfer=smpte2084:"
               "colormatrix=bt2020nc:master-display=G(13250,34500)B(7500,3000)"
               "R(34000,16000)WP(15635,16450)L(12100000,60):max-cll=0.0")


def generate_metadata(path, n_frames):
    """每行: 帧号(从1) + 空格 + 固定元数据; 共 n_frames 行
    若文件已存在则跳过 (复用已有 metadata)"""
    if os.path.exists(path):
        return path
    with open(path, 'w', encoding='utf-8') as f:
        for i in range(1, n_frames + 1):
            f.write(f"{i} {METADATA_PAYLOAD}\n")
    return path


def concat_bins(bins, out_path, frame_bytes):
    """把多个 bin 流式拼接成单个 raw 文件, 校验每帧字节数"""
    with open(out_path, 'wb') as out:
        for b in bins:
            sz = os.path.getsize(b)
            if sz != frame_bytes:
                raise SystemExit(f"帧字节数不符: {b} = {sz}, 期望 {frame_bytes} "
                                 f"(= {common.H_IMG}x{common.W_IMG}x4)")
            with open(b, 'rb') as inp:
                shutil.copyfileobj(inp, out, length=1024 * 1024)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--config', default=CONFIG_PATH, help='config.json 路径')
    ap.add_argument('--blended-dir', default=None, help='覆盖 config.task3.blended_dir (_withUI.bin 目录)')
    ap.add_argument('--video-out', default=None, help='覆盖 config.task3.video_out')
    ap.add_argument('--keep-temp', action='store_true', help='保留中间 raw/yuv 文件')
    args = ap.parse_args()

    cfg = load_config(args.config)
    common.set_dims(cfg['common']['height'], cfg['common']['width'])
    blended_dir = args.blended_dir or resolve_path(cfg['task3']['blended_dir'])
    video_out = args.video_out or resolve_path(cfg['task3']['video_out'])
    ffmpeg = resolve_path(cfg['task3']['encoder_script'])
    if not blended_dir:
        raise SystemExit("config.task3.blended_dir 未设置")
    if not video_out:
        raise SystemExit("config.task3.video_out 未设置")
    if not ffmpeg or not os.path.isfile(ffmpeg):
        raise SystemExit(f"ffmpeg_venc 未找到: '{ffmpeg}' (请在 config.task3.encoder_script 设置路径)")

    W, H = common.W_IMG, common.H_IMG   # 从 config 读取
    fps, bitrate = 50, "10M"
    frame_bytes = W * H * 4

    bins = sorted([os.path.join(blended_dir, fn) for fn in os.listdir(blended_dir)
                   if fn.endswith('_withUI.bin')])
    n = len(bins)
    if n == 0:
        raise SystemExit(f"{blended_dir} 下没有 *_withUI.bin")
    print(f"frames: {n}, resolution: {W}x{H}, fps={fps}, bitrate={bitrate}")

    out_dir = os.path.dirname(os.path.abspath(video_out))
    os.makedirs(out_dir, exist_ok=True)
    metadata_path = os.path.join(out_dir, 'metadata.txt')
    generate_metadata(metadata_path, n)
    print(f"metadata: {metadata_path} ({n} 行, 帧号 1..{n})")

    tmp = tempfile.mkdtemp(prefix='makevideo_')
    all_bin = os.path.join(tmp, 'all.bin')
    all_yuv = os.path.join(tmp, 'all.yuv')
    try:
        # 命令2: cat 的 raw bin -> yuv420p10le
        print("concatenating bins ->", all_bin)
        concat_bins(bins, all_bin, frame_bytes)
        cmd2 = [ffmpeg, "-y", "-f", "rawvideo", "-vcodec", "rawvideo",
                "-pix_fmt", "x2bgr10le", "-s", f"{W}x{H}", "-i", all_bin,
                "-pix_fmt", "yuv420p10le", all_yuv]
        print("cmd2:", " ".join(cmd2))
        subprocess.run(cmd2, check=True)

        # 命令1: yuv -> libx265 HDR10 MP4 (带 -vmeta_url metadata.txt)
        cmd1 = [ffmpeg, "-s", f"{W}x{H}", "-pix_fmt", "yuv420p10le", "-r", str(fps),
                "-i", all_yuv, "-fps_mode", "passthrough", "-b:v", bitrate,
                "-c:v", "libx265", "-vmeta_url", metadata_path,
                "-preset", "medium", "-x265-params", X265_PARAMS,
                "-an", "-y", "-tag:v", "hvc1", video_out]
        print("cmd1:", " ".join(cmd1))
        subprocess.run(cmd1, check=True)
    finally:
        if not args.keep_temp:
            shutil.rmtree(tmp, ignore_errors=True)

    print(f"Done. -> {video_out}")


if __name__ == '__main__':
    main()
