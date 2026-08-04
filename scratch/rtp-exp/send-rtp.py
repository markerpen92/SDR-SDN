#!/usr/bin/env python3
"""H.264 RTP sender (gst) with live FPS/throughput stats and plot on exit.

Equivalent to:
  gst-launch-1.0 filesrc location=naruto.h264 ! h264parse ! \\
    rtph264pay config-interval=1 pt=96 ! \\
    udpsink host=192.168.4.111 port=5004 sync=true
"""

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
HISTORY_LEN = 120
SAMPLE_INTERVAL = 1.0

_lock = threading.Lock()
_total_packets = 0
_total_bytes = 0
_total_frames = 0
_total_idr = 0

_last_packets = 0
_last_bytes = 0
_last_frames = 0
_last_idr = 0

_samples = []
_start_time = None
_running = True
MAX_PEAK_ALL_TIME = 0


def draw_ascii_graph(fps_deque, thr_deque):
    global MAX_PEAK_ALL_TIME

    current_fps = fps_deque[-1] if fps_deque else 0
    current_thr = thr_deque[-1] if thr_deque else 0
    peak = max(current_fps, current_thr * 0.05)
    if peak > MAX_PEAK_ALL_TIME:
        MAX_PEAK_ALL_TIME = peak

    display_max = max(peak, 10)
    sys.stdout.write(f"\033[{GRAPH_HEIGHT + 4}F")
    print(
        f"\033[KSend: FPS=\033[1;32m{current_fps:.1f}\033[0m | "
        f"Thr=\033[1;34m{current_thr:.0f} kbps\033[0m | samples={len(_samples)}"
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

        for val in fps_deque:
            if val >= threshold:
                line += "█" if val >= threshold + (display_max / GRAPH_HEIGHT) * 0.5 else "▄"
            else:
                line += " "

        print(f"\033[K{line}")

    print(f"\033[K     └" + ("─" * len(fps_deque)))
    print(f"\033[K      (History: {len(fps_deque)}s) \r")
    sys.stdout.flush()


def statistics_loop():
    global _last_packets, _last_bytes, _last_frames, _last_idr, _running

    fps_history = deque([0] * HISTORY_LEN, maxlen=HISTORY_LEN)
    thr_history = deque([0] * HISTORY_LEN, maxlen=HISTORY_LEN)
    print("\n" * (GRAPH_HEIGHT + 5))

    while _running:
        time.sleep(SAMPLE_INTERVAL)

        with _lock:
            pkt = _total_packets
            byt = _total_bytes
            frm = _total_frames
            idr = _total_idr

        dp = pkt - _last_packets
        db = byt - _last_bytes
        df = frm - _last_frames
        di = idr - _last_idr
        _last_packets = pkt
        _last_bytes = byt
        _last_frames = frm
        _last_idr = idr

        fps = df / SAMPLE_INTERVAL
        throughput_kbps = (db * 8.0) / SAMPLE_INTERVAL / 1000.0
        t = time.time() - _start_time if _start_time else 0.0

        _samples.append(
            {
                "time_s": t,
                "fps": fps,
                "throughput_kbps": throughput_kbps,
                "frames": df,
                "idr_frames": di,
                "rtp_packets": dp,
                "bytes": db,
            }
        )

        fps_history.append(fps)
        thr_history.append(throughput_kbps)
        draw_ascii_graph(fps_history, thr_history)


def on_frame_probe(_pad, info):
    global _total_frames, _total_idr
    buffer = info.get_buffer()
    if buffer is None:
        return Gst.PadProbeReturn.OK

    is_idr = False
    ok, map_info = buffer.map(Gst.MapFlags.READ)
    if ok and map_info.size > 0:
        nal_type = map_info.data[0] & 0x1F
        if nal_type == 5:
            is_idr = True
        buffer.unmap(map_info)

    with _lock:
        _total_frames += 1
        if is_idr:
            _total_idr += 1
    return Gst.PadProbeReturn.OK


def _count_rtp_buffer(buffer):
    global _total_packets, _total_bytes
    # Match gateway RtpFpsStats: Observe() uses UDP-packet GetSize() (8-byte UDP
    # header + RTP), not RTP buffer size alone.
    size = buffer.get_size() + 8
    with _lock:
        _total_packets += 1
        _total_bytes += size


def on_pay_probe(_pad, info):
    # rtph264pay often pushes GstBufferList (one list per frame); BUFFER-only
    # probes miss most RTP packets and under-report throughput.
    if info.type & Gst.PadProbeType.BUFFER_LIST:
        buf_list = info.get_buffer_list()
        for i in range(buf_list.length()):
            buffer = buf_list.get(i)
            if buffer is not None:
                _count_rtp_buffer(buffer)
    else:
        buffer = info.get_buffer()
        if buffer is not None:
            _count_rtp_buffer(buffer)
    return Gst.PadProbeReturn.OK


def on_bus_message(_bus, message, loop):
    t = message.type
    if t == Gst.MessageType.EOS:
        print("\nGot EOS — stream finished.")
        loop.quit()
    elif t == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        print(f"\nGStreamer error: {err}", file=sys.stderr)
        if debug:
            print(debug, file=sys.stderr)
        loop.quit()


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
                "idr_frames",
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
    ax_fps.axhline(avg_fps, color="#2ca02c", linestyle="--", linewidth=1.2, alpha=0.85)
    ax_thr.axhline(avg_thr, color="#1f77b4", linestyle="--", linewidth=1.2, alpha=0.85)

    ax_fps.set_xlabel("Time (s)")
    ax_fps.set_ylabel("FPS", color="#2ca02c")
    ax_thr.set_ylabel("Throughput (kbps)", color="#1f77b4")
    ax_fps.tick_params(axis="y", labelcolor="#2ca02c")
    ax_thr.tick_params(axis="y", labelcolor="#1f77b4")
    ax_fps.grid(True, alpha=0.3)
    ax_fps.set_title(title)

    fig.legend(
        [line_fps, line_thr],
        [f"FPS (avg {avg_fps:.1f})", f"Throughput (avg {avg_thr:.0f} kbps)"],
        loc="upper right",
    )
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"Wrote {output_path} ({len(times)} points)")

    if show:
        plt.show()
    plt.close(fig)


def main():
    global _start_time, _running

    parser = argparse.ArgumentParser(
        description="Send H.264 RTP with FPS/throughput stats (replaces gst-launch)"
    )
    parser.add_argument(
        "file",
        nargs="?",
        default="naruto.h264",
        help="Annex-B H.264 file (default: naruto.h264)",
    )
    parser.add_argument(
        "--host",
        default="192.168.4.111",
        help="destination IP (default: 192.168.4.111)",
    )
    parser.add_argument("--port", type=int, default=5004, help="UDP port")
    parser.add_argument("--pt", type=int, default=96, help="RTP payload type")
    parser.add_argument(
        "--config-interval",
        type=int,
        default=1,
        help="rtph264pay config-interval (default: 1)",
    )
    parser.add_argument(
        "--sync",
        action="store_true",
        default=True,
        help="udpsink sync=true (default: on, real-time pacing)",
    )
    parser.add_argument(
        "--no-sync",
        action="store_true",
        help="udpsink sync=false (burst send)",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="send-rtp-stats.png",
        help="output plot (default: send-rtp-stats.png)",
    )
    parser.add_argument(
        "--csv",
        default="send-rtp-stats.csv",
        help="CSV output (default: send-rtp-stats.csv)",
    )
    parser.add_argument("--no-csv", action="store_true", help="do not write CSV")
    parser.add_argument("--show", action="store_true", help="show plot on exit")
    args = parser.parse_args()

    sync = "true" if (args.sync and not args.no_sync) else "false"

    pipeline = Gst.parse_launch(
        f"filesrc location={args.file} ! h264parse name=parse ! "
        f"rtph264pay name=pay config-interval={args.config_interval} pt={args.pt} ! "
        f"udpsink host={args.host} port={args.port} sync={sync}"
    )

    parse = pipeline.get_by_name("parse")
    parse.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, on_frame_probe)

    pay = pipeline.get_by_name("pay")
    pay.get_static_pad("src").add_probe(
        Gst.PadProbeType.BUFFER | Gst.PadProbeType.BUFFER_LIST,
        on_pay_probe,
    )

    stat_thread = threading.Thread(target=statistics_loop, daemon=True)
    stat_thread.start()

    loop = GLib.MainLoop()
    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", on_bus_message, loop)

    print(
        f"Sending {args.file} -> {args.host}:{args.port} "
        f"(pt={args.pt}, sync={sync})"
    )
    _start_time = time.time()
    pipeline.set_state(Gst.State.PLAYING)

    try:
        loop.run()
    except KeyboardInterrupt:
        print("\nInterrupted.")
    finally:
        _running = False
        stat_thread.join(timeout=SAMPLE_INTERVAL * 1.5)
        pipeline.set_state(Gst.State.NULL)
        bus.remove_signal_watch()
        if not args.no_csv:
            write_csv(args.csv)
        plot_stats(
            args.output,
            title="Sender RTP: FPS & throughput",
            show=args.show,
        )


if __name__ == "__main__":
    main()
