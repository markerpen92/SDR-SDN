#!/usr/bin/env python3
"""RTP/H.264 receiver with live ASCII stats and FPS/throughput plot on exit."""

import argparse
import csv
import sys
import threading
import time
from collections import deque

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GObject", "2.0")
from gi.repository import Gst, GLib

Gst.init(None)

GRAPH_HEIGHT = 10
HISTORY_LEN = 40
SAMPLE_INTERVAL = 1.0

# Counters (updated by probes)
_lock = threading.Lock()
_total_packets = 0
_total_bytes = 0
_total_frames = 0

# Per-interval snapshot for statistics thread
_last_packets = 0
_last_bytes = 0
_last_frames = 0

# Time series for final plot
_samples = []
_start_time = None
MAX_PEAK_ALL_TIME = 0


def draw_ascii_graph(data_deque):
    global MAX_PEAK_ALL_TIME

    if len(data_deque) > 0:
        current_window_max = max(data_deque)
    else:
        current_window_max = 0

    if current_window_max > MAX_PEAK_ALL_TIME:
        MAX_PEAK_ALL_TIME = current_window_max

    display_max = max(current_window_max, 10)
    sys.stdout.write(f"\033[{GRAPH_HEIGHT + 4}F")

    current_val = data_deque[-1] if len(data_deque) > 0 else 0
    print(
        f"\033[KStats: Current: \033[1;32m{current_val} FPS\033[0m | "
        f"Peak(All-time): {MAX_PEAK_ALL_TIME} | Scale: 0-{display_max}"
    )
    print(f"\033[K{'-' * 60}")

    for row in range(GRAPH_HEIGHT, 0, -1):
        line = ""
        threshold = (row / GRAPH_HEIGHT) * display_max
        if row == GRAPH_HEIGHT:
            line += f"{int(display_max):>4} ┤"
        elif row == GRAPH_HEIGHT // 2:
            line += f"{int(display_max / 2):>4} ┤"
        else:
            line += "     │"

        for val in data_deque:
            if val >= threshold:
                if val >= threshold + (display_max / GRAPH_HEIGHT) * 0.5:
                    line += "█"
                else:
                    line += "▄"
            else:
                line += " "

        print(f"\033[K{line}")

    print(f"\033[K     └" + ("─" * len(data_deque)))
    print(f"\033[K      (History: {len(data_deque)}s) \r")
    sys.stdout.flush()


def statistics_loop():
    global _last_packets, _last_bytes, _last_frames, _start_time

    fps_history = deque([0] * HISTORY_LEN, maxlen=HISTORY_LEN)
    print("\n" * (GRAPH_HEIGHT + 5))

    while True:
        time.sleep(SAMPLE_INTERVAL)

        with _lock:
            pkt = _total_packets
            byt = _total_bytes
            frm = _total_frames

        dp = pkt - _last_packets
        db = byt - _last_bytes
        df = frm - _last_frames
        _last_packets = pkt
        _last_bytes = byt
        _last_frames = frm

        fps = df / SAMPLE_INTERVAL
        throughput_kbps = (db * 8.0) / SAMPLE_INTERVAL / 1000.0
        t = time.time() - _start_time if _start_time else 0.0

        _samples.append(
            {
                "time_s": t,
                "fps": fps,
                "throughput_kbps": throughput_kbps,
                "frames": df,
                "rtp_packets": dp,
                "bytes": db,
            }
        )

        fps_history.append(fps)
        draw_ascii_graph(fps_history)


def on_rtp_probe(_pad, info):
    global _total_packets, _total_bytes
    buffer = info.get_buffer()
    if buffer is None:
        return Gst.PadProbeReturn.OK
    size = buffer.get_size()
    with _lock:
        _total_packets += 1
        _total_bytes += size
    return Gst.PadProbeReturn.OK


def on_frame_probe(_pad, info):
    global _total_frames
    buffer = info.get_buffer()
    if buffer is None:
        return Gst.PadProbeReturn.OK
    with _lock:
        _total_frames += 1
    return Gst.PadProbeReturn.OK


def write_csv(path):
    if not _samples:
        return
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=[
                "time_s",
                "fps",
                "throughput_kbps",
                "frames",
                "rtp_packets",
                "bytes",
            ],
        )
        writer.writeheader()
        writer.writerows(_samples)
    print(f"Wrote {path} ({len(_samples)} rows)")


def plot_stats(output_path, title, show=False):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed; skip plot (pip install matplotlib)", file=sys.stderr)
        return

    if not _samples:
        print("No samples collected; skip plot", file=sys.stderr)
        return

    times = [s["time_s"] for s in _samples]
    fps = [s["fps"] for s in _samples]
    thr = [s["throughput_kbps"] for s in _samples]

    avg_fps = sum(fps) / len(fps)
    avg_thr = sum(thr) / len(thr)

    fig, ax_fps = plt.subplots(figsize=(10, 5))
    ax_thr = ax_fps.twinx()

    line_fps, = ax_fps.plot(times, fps, color="#2ca02c", linewidth=1.5, label="FPS")
    line_thr, = ax_thr.plot(times, thr, color="#1f77b4", linewidth=1.5, label="Throughput")
    line_avg_fps = ax_fps.axhline(
        avg_fps,
        color="#2ca02c",
        linestyle="--",
        linewidth=1.2,
        alpha=0.85,
        label=f"Avg FPS ({avg_fps:.1f})",
    )
    line_avg_thr = ax_thr.axhline(
        avg_thr,
        color="#1f77b4",
        linestyle="--",
        linewidth=1.2,
        alpha=0.85,
        label=f"Avg throughput ({avg_thr:.1f} kbps)",
    )

    ax_fps.set_xlabel("Time (s)")
    ax_fps.set_ylabel("FPS", color="#2ca02c")
    ax_thr.set_ylabel("Throughput (kbps)", color="#1f77b4")
    ax_fps.tick_params(axis="y", labelcolor="#2ca02c")
    ax_thr.tick_params(axis="y", labelcolor="#1f77b4")
    ax_fps.grid(True, alpha=0.3)
    ax_fps.set_title(title)

    fig.legend(
        [line_fps, line_thr, line_avg_fps, line_avg_thr],
        [
            "FPS",
            "Throughput (kbps)",
            f"Avg FPS ({avg_fps:.1f})",
            f"Avg throughput ({avg_thr:.1f} kbps)",
        ],
        loc="upper right",
    )
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"Wrote {output_path} ({len(times)} points)")

    if show:
        plt.show()
    plt.close(fig)


def main():
    global _start_time

    parser = argparse.ArgumentParser(description="RTP/H.264 receiver with stats plot")
    parser.add_argument("--port", type=int, default=5004, help="UDP listen port")
    parser.add_argument(
        "-o",
        "--output",
        default="recv-rtp-stats.png",
        help="output plot image (default: recv-rtp-stats.png)",
    )
    parser.add_argument(
        "--csv",
        default="recv-rtp-stats.csv",
        help="CSV output path (default: recv-rtp-stats.csv)",
    )
    parser.add_argument("--no-csv", action="store_true", help="do not write CSV")
    parser.add_argument("--show", action="store_true", help="show plot window on exit")
    args = parser.parse_args()

    pipeline = Gst.parse_launch(
        f"udpsrc name=src port={args.port} caps=application/x-rtp,encoding-name=H264 ! "
        "rtpjitterbuffer latency=0 ! rtph264depay ! h264parse name=parse ! "
        "decodebin ! videoconvert ! autovideosink sync=false"
    )

    src = pipeline.get_by_name("src")
    src.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_rtp_probe)

    parse = pipeline.get_by_name("parse")
    parse.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_frame_probe)

    stat_thread = threading.Thread(target=statistics_loop, daemon=True)
    stat_thread.start()

    print("GStreamer started. Waiting for data on port", args.port)
    _start_time = time.time()
    pipeline.set_state(Gst.State.PLAYING)

    loop = GLib.MainLoop()
    try:
        loop.run()
    except KeyboardInterrupt:
        pass
    finally:
        pipeline.set_state(Gst.State.NULL)
        if not args.no_csv:
            write_csv(args.csv)
        plot_stats(
            args.output,
            title="Receiver RTP: FPS & throughput",
            show=args.show,
        )


if __name__ == "__main__":
    main()

