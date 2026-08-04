#!/usr/bin/env python3
"""Compare RTP receiver FPS and throughput across experiment CSV files."""

import argparse
import csv
import sys
from pathlib import Path

from plot_style import (
    FIG_SIZE,
    TIME_PLOT_MAX,
    XLABEL_TIME,
    YLABEL_AVG_FPS,
    YLABEL_AVG_THROUGHPUT,
    YLABEL_AVG_THROUGHPUT_SHORT,
    YLABEL_FPS,
    add_legend_below,
    apply_axis_fonts,
    apply_time_axis,
    clip_xy_by_time,
    plot_average_marker,
    plot_category_bars,
    plot_context,
    plot_series_line,
    save_figure,
)

DEFAULT_SCENARIOS = [
    ("noIntf", "recv-rtp-stats-noIntf.csv", "No interference"),
    ("noQoS", "recv-rtp-stats-noQoS.csv", "No QoS"),
    ("qos", "recv-rtp-stats-qos.csv", "QoS enabled"),
]


def load_csv(path):
    rows = []
    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            rows.append(
                {
                    "time_s": float(row["time_s"]),
                    "fps": float(row["fps"]),
                    "throughput_kbps": float(row["throughput_kbps"]),
                    "frames": int(row["frames"]),
                    "rtp_packets": int(row["rtp_packets"]),
                    "bytes": int(row["bytes"]),
                }
            )
    return rows


def trim_warmup(rows, warmup_s):
    if warmup_s <= 0:
        return rows
    return [r for r in rows if r["time_s"] > warmup_s]


def trim_active(rows):
    return [r for r in rows if r["frames"] > 0 or r["rtp_packets"] > 0]


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


def print_summary(scenarios):
    print(
        f"{'Scenario':<18} {'Samples':>8} {'Average FPS':>12} "
        f"{'Average throughput (kbps)':>24} {'Minimum FPS':>12} {'Maximum FPS':>12} {'Frames':>8}"
    )
    print("-" * 90)
    for item in scenarios:
        s = item["summary"]
        print(
            f"{item['label']:<18} {s['samples']:>8} {s['avg_fps']:>10.2f} "
            f"{s['avg_thr']:>10.1f} kbps {s['min_fps']:>9.1f} {s['max_fps']:>9.1f} "
            f"{s['total_frames']:>8}"
        )


def plot_metric_comparison(scenarios, metric, output_path, show=False):
    """Plot time series + bar chart for one metric ('fps' or 'throughput_kbps')."""
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib not installed; skip plot (pip install matplotlib)", file=sys.stderr)
        return

    is_fps = metric == "fps"
    ylabel_ts = YLABEL_FPS if is_fps else YLABEL_AVG_THROUGHPUT
    ylabel_bar = YLABEL_AVG_FPS if is_fps else YLABEL_AVG_THROUGHPUT_SHORT
    value_fmt = "{:.1f}" if is_fps else "{:.0f}"
    avg_key = "avg_fps" if is_fps else "avg_thr"

    with plot_context():
        fig, (ax_ts, ax_bar) = plt.subplots(2, 1, figsize=(FIG_SIZE[0], FIG_SIZE[1] * 2))

        labels = []
        avg_values = []
        all_times = []
        avg_items = []

        for i, item in enumerate(scenarios):
            rows = item["rows"]
            label = item["label"]
            labels.append(label)

            times = [r["time_s"] for r in rows]
            values = [r[metric] for r in rows]
            times, values = clip_xy_by_time(times, values, TIME_PLOT_MAX)
            all_times.append(times)

            plot_series_line(ax_ts, times, values, i, label)

            s = item["summary"]
            avg_val = s[avg_key]
            avg_values.append(avg_val)
            avg_items.append((i, label, avg_val))
            plot_average_marker(ax_ts, avg_val, i, label, avg_val, value_fmt=value_fmt)

        ax_ts.set_xlabel(XLABEL_TIME)
        ax_ts.set_ylabel(ylabel_ts)

        plot_category_bars(ax_bar, labels, avg_values)
        ax_bar.set_ylabel(ylabel_bar)

        for ax in (ax_ts, ax_bar):
            apply_axis_fonts(ax)
        apply_time_axis(ax_ts, all_times)

        ncol = min(4, max(1, len(labels)))
        fig.subplots_adjust(hspace=0.38, bottom=0.34)
        add_legend_below(
            fig,
            ax_ts,
            ncol=ncol,
            avg_items=avg_items,
            anchor_ax=ax_bar,
            bottom_min=0.34,
        )
        save_figure(fig, output_path)
        print(f"Wrote {output_path}")

    if show:
        plt.show()
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(
        description="Compare RTP FPS and throughput across experiment CSV files."
    )
    parser.add_argument(
        "--dir",
        type=Path,
        default=Path(__file__).resolve().parent,
        help="directory containing CSV files (default: rtp-exp/)",
    )
    parser.add_argument(
        "-o",
        "--output-fps",
        default="compare-fps.png",
        help="output FPS comparison plot (default: compare-fps.png)",
    )
    parser.add_argument(
        "-t",
        "--output-throughput",
        default="compare-throughput.png",
        help="output throughput comparison plot (default: compare-throughput.png)",
    )
    parser.add_argument(
        "--warmup",
        type=float,
        default=3.0,
        help="ignore samples with time_s <= warmup seconds (default: 3)",
    )
    parser.add_argument(
        "--active-only",
        action="store_true",
        help="also drop 1-second windows with zero frames/packets",
    )
    parser.add_argument(
        "--no-qos-csv",
        default="recv-rtp-stats-noQoS.csv",
        help="CSV for no-QoS scenario",
    )
    parser.add_argument(
        "--qos-csv",
        default="recv-rtp-stats-qos.csv",
        help="CSV for QoS scenario",
    )
    parser.add_argument(
        "--no-intf-csv",
        default="recv-rtp-stats-noIntf.csv",
        help="CSV for no-interference scenario",
    )
    parser.add_argument("--show", action="store_true", help="show plot window")
    args = parser.parse_args()

    scenario_defs = [
        ("noIntf", args.no_intf_csv, "No interference"),
        ("noQoS", args.no_qos_csv, "No QoS"),
        ("qos", args.qos_csv, "QoS enabled"),
    ]

    scenarios = []
    for _key, filename, label in scenario_defs:
        path = args.dir / filename
        if not path.is_file():
            print(f"Missing CSV: {path}", file=sys.stderr)
            sys.exit(1)

        rows = load_csv(path)
        rows = trim_warmup(rows, args.warmup)
        if args.active_only:
            rows = trim_active(rows)

        scenarios.append(
            {
                "label": label,
                "path": path,
                "rows": rows,
                "summary": summarize(rows),
            }
        )

    print_summary(scenarios)
    print()

    fps_path = args.dir / args.output_fps
    thr_path = args.dir / args.output_throughput

    plot_metric_comparison(scenarios, "fps", fps_path, show=args.show)
    plot_metric_comparison(scenarios, "throughput_kbps", thr_path, show=False)


if __name__ == "__main__":
    main()
