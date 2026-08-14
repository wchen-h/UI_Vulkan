#!/usr/bin/env python3
"""
任务3: bin 序列 -> HDR10/SDR 视频 (make_video.py)
流程 (沿用 commands.txt 两步式):
    1. (仅 HDR) 生成 metadata.txt (HDR Vivid 动态元数据, 行数==帧数则复用, 否则重新生成)
    2. cat 所有最终帧 -> raw -> yuv420p10le
    3. yuv -> libx265 MP4: HDR (BT.2020+PQ+master-display+-vmeta_url) 或 SDR (BT.709+sRGB)

bin 格式 (HDR/SDR 相同): A2B10G10R10 UNORM 小端 uint32; HDR 为 PQ+BT.2020, SDR 为 sRGB+BT.709。
分辨率从 config.json 的 common.width/height 读取 (默认 2328×1080)。
用法:
    python3 make_video.py [--mode hdr|sdr] [--config config.json]
                           [--blended-dir X] [--sdr-dir S] [--video-out Y] [--sdr-video-out S]
                           [--fps N] [--keep-temp]
"""
import argparse
import os
import shutil
import subprocess
import tempfile

import common
from common import load_config, resolve_path, path_filled, natural_key, CONFIG_PATH

# HDR Vivid 动态元数据 (固定十进制参数, 来自用户提供)
METADATA_PAYLOAD = ("1 1 1343 0 3948 1 1 2770 1 5717 24 897 0 10 1 1 1 6 6 1 1 0 "
                    "252 614 245 573 127 1 2186 333 419 103 2080 1 8335 24 630 0 10 1 1 "
                    "1 6 6 1 1 0 252 0 613 306 127 1 1921 562 437 114 0")

# libx265 HDR10 参数 (commands.txt 命令1, 分辨率由 config 决定)
X265_PARAMS = ("keyint=50:bframes=0:colorprim=bt2020:transfer=smpte2084:"
               "colormatrix=bt2020nc:master-display=G(13250,34500)B(7500,3000)"
               "R(34000,16000)WP(15635,16450)L(12100000,60):max-cll=0.0")

# libx265 SDR 参数 (BT.709 色域 + sRGB 传输函数; 无 master-display/max-cll/vmeta)
X265_SDR_PARAMS = "keyint=50:bframes=0:colorprim=bt709:transfer=iec61966-2-1:colormatrix=bt709"


def generate_metadata(path, n_frames):
    """每行: 帧号(从1) + 空格 + 固定元数据; 共 n_frames 行
    若文件已存在且行数 == n_frames 则复用 (省去重写); 否则 (含帧数变化) 重新生成"""
    if os.path.exists(path):
        with open(path, encoding='utf-8') as f:
            existing = sum(1 for _ in f)
        if existing == n_frames:
            print(f"metadata: 复用 {path} (行数 {existing})")
            return path
    with open(path, 'w', encoding='utf-8') as f:
        for i in range(1, n_frames + 1):
            f.write(f"{i} {METADATA_PAYLOAD}\n")
    print(f"metadata: 重新生成 {path} ({n_frames} 行)")
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
    ap.add_argument('--mode', choices=['hdr', 'sdr'], default=None,
                    help='合成模式: hdr (HDR10 BT.2020+PQ) / sdr (BT.709+sRGB); 不指定时用 config.task3.mode')
    ap.add_argument('--blended-dir', default=None, help='HDR 模式: 覆盖混合 bin 目录 (默认 config.task2.outdir)')
    ap.add_argument('--sdr-dir', default=None, help='SDR 模式: SDR bin 目录 (默认 config.task3.sdr_dir)')
    ap.add_argument('--video-out', default=None, help='覆盖 config.task3.video_out (HDR 输出)')
    ap.add_argument('--sdr-video-out', default=None, help='SDR 视频输出路径 (mode=sdr 时; 或 config.task3.sdr_video_out)')
    ap.add_argument('--fps', default=None, type=int, help='MP4 帧率 (默认 config.task3.fps 或 30)')
    ap.add_argument('--keep-temp', action='store_true', help='保留中间 raw/yuv 文件')
    args = ap.parse_args()

    cfg = load_config(args.config)
    common.set_dims(cfg['common']['height'], cfg['common']['width'])

    # 模式: CLI > config.task3.mode > 默认 hdr
    mode = args.mode or cfg.get('task3', {}).get('mode', 'hdr')
    is_sdr = (mode == 'sdr')
    print(f"mode: {'SDR (BT.709+sRGB)' if is_sdr else 'HDR (BT.2020+PQ)'}")

    # 输入 bin 目录
    if is_sdr:
        # SDR 模式: 从 sdr_dir 读 SDR bin
        sdr_dir_cfg = cfg.get('task3', {}).get('sdr_dir', '')
        if args.sdr_dir:
            bin_dir = resolve_path(args.sdr_dir)
        elif path_filled(sdr_dir_cfg):
            bin_dir = resolve_path(sdr_dir_cfg)
        else:
            raise SystemExit("SDR 模式需指定 bin 目录: --sdr-dir 或 config.task3.sdr_dir")
    else:
        # HDR 模式: 从 blended_dir 读 (默认 task2.outdir)
        if args.blended_dir:
            bin_dir = resolve_path(args.blended_dir)
        elif path_filled(cfg['task2']['outdir']):
            bin_dir = resolve_path(cfg['task2']['outdir'])
        else:
            raise SystemExit("HDR 模式需指定 bin 目录: --blended-dir 或 config.task2.outdir")
    if not os.path.isdir(bin_dir):
        raise SystemExit(f"bin 目录不存在: {bin_dir}")

    # 输出路径
    if is_sdr:
        sdr_video_out_cfg = cfg.get('task3', {}).get('sdr_video_out', '')
        out_video = args.sdr_video_out or (resolve_path(sdr_video_out_cfg) if path_filled(sdr_video_out_cfg) else None)
        if not out_video:
            raise SystemExit("SDR 模式需指定输出路径: --sdr-video-out 或 config.task3.sdr_video_out")
    else:
        if not path_filled(cfg['task3']['video_out']) and not args.video_out:
            raise SystemExit("config.task3.video_out 未设置")
        out_video = args.video_out or resolve_path(cfg['task3']['video_out'])

    # 必填: ffmpeg_venc
    if not path_filled(cfg['task3']['encoder_script']):
        raise SystemExit("config.task3.encoder_script 未设置")
    ffmpeg = resolve_path(cfg['task3']['encoder_script'])
    if not os.path.isfile(ffmpeg):
        raise SystemExit(f"ffmpeg_venc 未找到: {ffmpeg} (config.task3.encoder_script)")

    W, H = common.W_IMG, common.H_IMG
    fps = args.fps or int(cfg.get('task3', {}).get('fps', 30))
    bitrate = "10M"
    frame_bytes = W * H * 4

    # 收集最终帧
    final = {}
    for fn in os.listdir(bin_dir):
        if not fn.endswith('.bin'):
            continue
        if fn.endswith('_withUI.bin'):
            stem = fn[:-len('_withUI.bin')]
            final[stem] = os.path.join(bin_dir, fn)
        elif fn.endswith('_rotate.bin'):
            stem = fn[:-len('.bin')]
            final.setdefault(stem, os.path.join(bin_dir, fn))
        else:
            stem = fn[:-len('.bin')]
            final.setdefault(stem, os.path.join(bin_dir, fn))
    bins = sorted(final.values(), key=natural_key)
    n = len(bins)
    if n == 0:
        raise SystemExit(f"{bin_dir} 下没有 .bin 文件")
    print(f"frames: {n}, resolution: {W}x{H}, fps={fps}, bitrate={bitrate}")

    out_dir = os.path.dirname(os.path.abspath(out_video))
    os.makedirs(out_dir, exist_ok=True)

    # metadata 仅 HDR 模式需要 (HDR Vivid 动态元数据); SDR 跳过
    if is_sdr:
        metadata_path = None
    else:
        metadata_path = os.path.join(out_dir, 'metadata.txt')
        generate_metadata(metadata_path, n)

    tmp = tempfile.mkdtemp(prefix='makevideo_')
    all_bin = os.path.join(tmp, 'all.bin')
    all_yuv = os.path.join(tmp, 'all.yuv')
    try:
        print("concatenating bins ->", all_bin)
        concat_bins(bins, all_bin, frame_bytes)
        # 命令2: cat 的 raw bin -> yuv420p10le (full range, 显式矩阵, 不让 ffmpeg 做 full->limited 缩放)
        matrix = "bt709" if is_sdr else "bt2020nc"
        cmd2 = [ffmpeg, "-y", "-f", "rawvideo", "-vcodec", "rawvideo",
                "-pix_fmt", "x2bgr10le", "-color_range", "pc",
                "-s", f"{W}x{H}", "-i", all_bin,
                "-vf", f"scale=in_range=pc:in_color_matrix={matrix}:"
                       f"out_range=pc:out_color_matrix={matrix}",
                "-pix_fmt", "yuv420p10le", "-color_range", "pc", all_yuv]
        print("cmd2:", " ".join(cmd2))
        subprocess.run(cmd2, check=True)

        # 命令1: yuv -> libx265 MP4
        #   HDR: BT.2020+PQ+master-display + -vmeta_url metadata.txt
        #   SDR: BT.709+sRGB, 无 master-display/vmeta
        #   full range 透传 (range=full), 避免播放器按 limited 展开->整体偏亮
        x265 = (X265_SDR_PARAMS if is_sdr else X265_PARAMS) + ":range=full"
        cs = (["-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "iec61966-2-1"]
              if is_sdr else
              ["-colorspace", "bt2020nc", "-color_primaries", "bt2020", "-color_trc", "smpte2084"])
        cmd1 = [ffmpeg, "-s", f"{W}x{H}", "-pix_fmt", "yuv420p10le", "-color_range", "pc",
                "-r", str(fps), "-i", all_yuv, "-fps_mode", "passthrough", "-b:v", bitrate,
                "-c:v", "libx265", "-preset", "medium",
                "-x265-params", x265,
                "-an", "-y", "-tag:v", "hvc1", "-color_range", "pc"] + cs
        if not is_sdr:
            cmd1 += ["-vmeta_url", metadata_path]
        cmd1.append(out_video)
        print("cmd1:", " ".join(cmd1))
        subprocess.run(cmd1, check=True)
    finally:
        if not args.keep_temp:
            shutil.rmtree(tmp, ignore_errors=True)

    print(f"Done ({'SDR' if is_sdr else 'HDR'}). -> {out_video}")


if __name__ == '__main__':
    main()
