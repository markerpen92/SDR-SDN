#!/usr/bin/env python3
"""Compare three receiver CSV snapshots (FPS & throughput)."""

import argparse
import sys
from pathlib import Path

_RTP_EXP = Path(__file__).resolve().parent.parent
if str(_RTP_EXP) not in sys.path:
    sys.path.insert(0, str(_RTP_EXP))

from plot_style import (  # noqa: E402
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

from csv_utils import (  # noqa: E402
    load_stats_csv,
    print_summary_table,
    summarize,
    trim_active,
    trim_warmup,
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

            times  = [r["time_s"] for r in rows]
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
    exp_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Compare RTP receiver FPS/throughput across three archived CSV files."
    )
    parser.add_argument(
        "--dir",
        type=Path,
        default=exp_dir,
        help="directory with archived CSV copies (default: Exp1/)",
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
    parser.add_argument("--warmup", type=float, default=3.0)
    parser.add_argument("--active-only", action="store_true")
    parser.add_argument("--no-intf-csv", default="recv-rtp-stats-noIntf.csv")
    parser.add_argument("--no-qos-csv", default="recv-rtp-stats-noQoS.csv")
    parser.add_argument("--qos-csv", default="recv-rtp-stats-qos.csv")
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    scenario_defs = [
        (args.no_intf_csv, "No interference"),
        (args.no_qos_csv, "No QoS"),
        (args.qos_csv, "QoS enabled"),
    ]

    scenarios = []
    summary_items = []
    for filename, label in scenario_defs:
        path = args.dir / filename
        if not path.is_file():
            print(f"Missing CSV: {path}", file=sys.stderr)
            sys.exit(1)
        rows = load_stats_csv(path)
        rows = trim_warmup(rows, args.warmup)
        if args.active_only:
            rows = trim_active(rows)
        summary = summarize(rows)
        scenarios.append({"label": label, "path": path, "rows": rows, "summary": summary})
        summary_items.append((label, summary))

    print_summary_table(summary_items)
    print()

    fps_path = args.dir / args.output_fps
    thr_path = args.dir / args.output_throughput

    plot_metric_comparison(scenarios, "fps", fps_path, show=args.show)
    plot_metric_comparison(scenarios, "throughput_kbps", thr_path, show=False)


if __name__ == "__main__":
    main()
