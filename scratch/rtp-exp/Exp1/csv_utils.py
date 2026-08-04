"""Shared CSV load / summary / plot helpers for RTP experiment analysis."""

import csv
import sys
from pathlib import Path

_RTP_EXP = Path(__file__).resolve().parent.parent
if str(_RTP_EXP) not in sys.path:
    sys.path.insert(0, str(_RTP_EXP))

from plot_style import (  # noqa: E402
    FIG_SIZE,
    TIME_PLOT_MAX,
    XLABEL_SAMPLE,
    XLABEL_TIME,
    YLABEL_FPS,
    YLABEL_THROUGHPUT,
    add_legend_below,
    apply_axis_fonts,
    apply_sample_axis,
    apply_time_axis,
    clip_rows_to_time,
    clip_xy_by_time,
    plot_average_marker,
    plot_context,
    plot_series_line,
    save_figure,
)


def load_stats_csv(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        fields = reader.fieldnames or []
        has_thr = "throughput_kbps" in fields
        has_bytes = "bytes" in fields
        for row in reader:
            fps = float(row["fps"])
            if has_thr:
                thr = float(row["throughput_kbps"])
            elif has_bytes:
                thr = float(row["bytes"]) * 8.0 / 1000.0
            else:
                thr = 0.0
            rows.append(
                {
                    "time_s": float(row["time_s"]),
                    "fps": fps,
                    "throughput_kbps": thr,
                    "frames": int(row.get("frames", 0) or 0),
                    "rtp_packets": int(row.get("rtp_packets", 0) or 0),
                    "bytes": int(row.get("bytes", 0) or 0),
                    "idr_frames": int(row.get("idr_frames", 0) or 0),
                }
            )
    return rows


def trim_warmup(rows, warmup_s):
    if warmup_s <= 0:
        return rows
    return [r for r in rows if r["time_s"] > warmup_s]


def trim_active(rows):
    return [r for r in rows if r["frames"] > 0 or r["rtp_packets"] > 0]


def assign_sample_index(rows):
    """Number each 1 s CSV row from 1 (fair cross-node compare; not wall/sim time_s)."""
    for i, row in enumerate(rows, start=1):
        row["sample_idx"] = i
    return rows


def find_rtp_lag(rows_a, rows_b, max_lag=15):
    """Return lag where rows_a[i] best matches rows_b[i] on rtp_packets (i + lag)."""
    va = [r["rtp_packets"] for r in rows_a]
    vb = [r["rtp_packets"] for r in rows_b]
    best_lag, best_score = 0, float("-inf")
    for lag in range(-max_lag, max_lag + 1):
        score = 0.0
        for i in range(len(vb)):
            j = i + lag
            if 0 <= j < len(va):
                score -= abs(va[j] - vb[i])
        if score > best_score:
            best_score, best_lag = score, lag
    return best_lag


def align_pair(rows_a, rows_b, lag):
    """Pair rows from A and B using index offset lag (A index = B index + lag)."""
    pairs = []
    for i in range(len(rows_b)):
        j = i + lag
        if 0 <= j < len(rows_a):
            pairs.append((rows_a[j], rows_b[i]))
    return pairs


def print_sender_gateway_alignment(sender_rows, gateway_rows, gateway_label="node0"):
    lag = find_rtp_lag(sender_rows, gateway_rows)
    pairs = align_pair(sender_rows, gateway_rows, lag)
    if not pairs:
        print(f"No aligned samples for sender vs {gateway_label}.")
        return

    thr_diff = []
    bytes_diff = []
    pkt_diff = []
    matched_pkts = []
    for a, b in pairs:
        thr_diff.append(float(a["throughput_kbps"]) - float(b["throughput_kbps"]))
        bytes_diff.append(int(a["bytes"]) - int(b["bytes"]))
        ra, rb = int(a["rtp_packets"]), int(b["rtp_packets"])
        pkt_diff.append(ra - rb)
        if ra == rb and ra > 0:
            matched_pkts.append((int(a["bytes"]) / ra, int(b["bytes"]) / rb))

    n = len(pairs)
    avg_thr_a = sum(float(a["throughput_kbps"]) for a, _ in pairs) / n
    avg_thr_b = sum(float(b["throughput_kbps"]) for _, b in pairs) / n
    total_a = sum(int(a["bytes"]) for a, _ in pairs)
    total_b = sum(int(b["bytes"]) for _, b in pairs)

    print(f"=== Sender vs {gateway_label} (index-aligned, lag={lag}) ===")
    print(
        f"  paired samples: {n} | average throughput: sender {avg_thr_a:.1f} kbps, "
        f"{gateway_label} {avg_thr_b:.1f} kbps, "
        f"delta {avg_thr_a - avg_thr_b:+.1f} kbps"
    )
    print(f"  total bytes sender {total_a} | {gateway_label} {total_b} | delta {total_a - total_b:+d}")
    if matched_pkts:
        avg_a = sum(x[0] for x in matched_pkts) / len(matched_pkts)
        avg_b = sum(x[1] for x in matched_pkts) / len(matched_pkts)
        print(
            f"  same rtp_packets rows: {len(matched_pkts)} | "
            f"bytes/pkt sender {avg_a:.1f} | {gateway_label} {avg_b:.1f}"
        )
    print(
        "  (sender should be >= gateway; small gaps are timing/rounding. "
        "Do not compare raw time_s overlays.)"
    )
    print()


def summarize(rows):
    if not rows:
        return {
            "avg_fps": 0.0,
            "avg_thr": 0.0,
            "min_fps": 0.0,
            "max_fps": 0.0,
            "total_frames": 0,
            "total_bytes": 0,
            "samples": 0,
        }

    fps = [r["fps"] for r in rows]
    thr = [r["throughput_kbps"] for r in rows]
    return {
        "avg_fps": sum(fps) / len(fps),
        "avg_thr": sum(thr) / len(thr),
        "min_fps": min(fps),
        "max_fps": max(fps),
        "total_frames": sum(r["frames"] for r in rows),
        "total_bytes": sum(r["bytes"] for r in rows),
        "samples": len(rows),
    }


def print_summary_table(items):
    print(
        f"{'Label':<22} {'Samples':>8} {'Average FPS':>12} "
        f"{'Avg throughput (kbps)':>22} {'Total bytes':>12} {'Frames':>8}"
    )
    print("-" * 90)
    for label, summary in items:
        s = summary
        print(
            f"{label:<22} {s['samples']:>8} {s['avg_fps']:>10.2f} "
            f"{s['avg_thr']:>10.1f} kbps {s['total_bytes']:>12} "
            f"{s['total_frames']:>8}"
        )


def plot_metric_comparison(
    series,
    metric,
    ylabel,
    output_path,
    title,
    show=False,
    x_key="time_s",
):
    """Plot one metric for multiple nodes. metric: 'fps' or 'throughput_kbps'."""
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("matplotlib is required: pip install matplotlib") from exc

    use_time = x_key == "time_s"
    value_fmt = "{:.1f}" if metric == "fps" else "{:.0f}"

    with plot_context():
        fig, ax = plt.subplots(figsize=FIG_SIZE)

        all_xs = []
        avg_items = []
        for i, item in enumerate(series):
            rows = item["rows"]
            if use_time:
                rows = clip_rows_to_time(rows, TIME_PLOT_MAX)
            values = [r[metric] for r in rows]
            if use_time:
                xs = [r["time_s"] for r in rows]
                xlabel = XLABEL_TIME
            else:
                xs = [r["sample_idx"] for r in rows]
                xlabel = XLABEL_SAMPLE
            all_xs.append(xs)

            plot_series_line(ax, xs, values, i, item["label"])
            avg = item["summary"][f"avg_{'fps' if metric == 'fps' else 'thr'}"]
            avg_items.append((i, item["label"], avg))

        ax.set_xlabel(xlabel)
        ax.set_ylabel(ylabel)
        apply_axis_fonts(ax, integer_x=not use_time)
        if use_time:
            apply_time_axis(ax, all_xs)
        elif all_xs:
            apply_sample_axis(ax, all_xs[0])

        for i, label, avg in avg_items:
            plot_average_marker(ax, avg, i, label, avg, value_fmt=value_fmt)

        ncol = min(4, max(1, len(series)))
        add_legend_below(fig, ax, ncol=ncol, avg_items=avg_items)
        save_figure(fig, output_path)

    if show:
        plt.show()
    plt.close(fig)


def plot_fps_thr_series(
    series,
    output_path,
    title,
    show=False,
):
    """series: list of dict(label=, times=, fps=, thr=, summary=)."""
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise SystemExit("matplotlib is required: pip install matplotlib") from exc

    with plot_context():
        fig, (ax_fps, ax_thr) = plt.subplots(2, 1, figsize=FIG_SIZE, sharex=True)

        all_times = []
        avg_fps_items = []
        avg_thr_items = []
        for i, item in enumerate(series):
            times, fps = clip_xy_by_time(item["times"], item["fps"], TIME_PLOT_MAX)
            times, thr = clip_xy_by_time(times, item["thr"], TIME_PLOT_MAX)
            all_times.append(times)
            plot_series_line(ax_fps, times, fps, i, item["label"])
            plot_series_line(ax_thr, times, thr, i, item["label"])
            if "summary" in item:
                s = item["summary"]
                avg_fps_items.append((i, item["label"], s["avg_fps"]))
                avg_thr_items.append((i, item["label"], s["avg_thr"]))

        ax_fps.set_ylabel(YLABEL_FPS)
        ax_thr.set_xlabel(XLABEL_TIME)
        ax_thr.set_ylabel(YLABEL_THROUGHPUT)
        apply_axis_fonts(ax_fps)
        apply_axis_fonts(ax_thr)
        apply_time_axis(ax_fps, all_times)
        apply_time_axis(ax_thr, all_times)

        for i, label, avg in avg_fps_items:
            plot_average_marker(ax_fps, avg, i, label, avg, value_fmt="{:.1f}")
        for i, label, avg in avg_thr_items:
            plot_average_marker(ax_thr, avg, i, label, avg, value_fmt="{:.0f}")

        ncol = min(4, max(1, len(series)))
        add_legend_below(
            fig, ax_fps, ncol=ncol, avg_items=avg_fps_items, anchor_ax=ax_thr
        )
        save_figure(fig, output_path)

    if show:
        plt.show()
    plt.close(fig)
