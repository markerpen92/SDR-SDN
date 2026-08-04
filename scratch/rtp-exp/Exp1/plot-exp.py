#!/usr/bin/env python3
"""Compare sender / node0 / node9 / receiver on two plots (FPS and throughput).

Default CSV names (copy archived experiment files into Exp1/):
  send-rtp-stats.csv    -> sender
  node0-rtp-stats.csv   -> display label "1" (ingress gateway, sim node 0)
  node9-rtp-stats.csv   -> display label "9" (egress gateway, sim node 9)
  recv-rtp-stats.csv    -> receiver
"""

import argparse
import sys
from pathlib import Path

_RTP_EXP = Path(__file__).resolve().parent.parent
if str(_RTP_EXP) not in sys.path:
    sys.path.insert(0, str(_RTP_EXP))

from plot_style import (  # noqa: E402
    FIG_SIZE,
    TIME_PLOT_MAX,
    YLABEL_FPS,
    YLABEL_THROUGHPUT,
    add_legend_below,
    apply_axis_fonts,
    apply_sample_axis,
    apply_time_axis,
    clip_rows_to_time,
    plot_average_marker,
    plot_context,
    plot_series_line,
    save_figure,
)

from csv_utils import (
    assign_sample_index,
    find_rtp_lag,
    load_stats_csv,
    print_sender_gateway_alignment,
    print_summary_table,
    summarize,
    trim_active,
    trim_warmup,
)

DEFAULT_NODES = [
    ("send-rtp-stats.csv", "sender"),
    ("node0-rtp-stats.csv", "ingress gateway"),
    ("node9-rtp-stats.csv", "egress gateway"),
    ("recv-rtp-stats.csv", "receiver"),
]


def plot_metric_comparison(
    series, metric, ylabel, output_path, title=None, show=False, x_key="sample_idx"
):
    """Plot one metric for multiple nodes with legend below the axes."""
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
                xlabel = "second (s)"
            else:
                xs = [r["sample_idx"] for r in rows]
                xlabel = "Time (s)"
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
        fig.subplots_adjust(bottom=0.22)
        add_legend_below(fig, ax, ncol=ncol, avg_items=avg_items)
        save_figure(fig, output_path)

    if show:
        plt.show()
    plt.close(fig)


def print_node_pair_lag(rows_a, label_a, rows_b, label_b):
    """Report index lag between two nodes (same rtp_packets row alignment)."""
    lag = find_rtp_lag(rows_a, rows_b)
    if lag > 0:
        desc = f"{label_b} trails {label_a} by {lag} sample(s)"
    elif lag < 0:
        desc = f"{label_a} trails {label_b} by {-lag} sample(s)"
    else:
        desc = "no index offset detected"
    print(f"=== {label_a} vs {label_b} (rtp_packets index lag={lag}) ===")
    print(f"  {desc}")
    print(
        "  Plots default to 1 s sample index on X (labeled Time (s)); "
        "use --wall-time for each node's local time_s."
    )
    print()


def load_node(path, label, warmup_s, active_only):
    rows = load_stats_csv(path)
    rows = trim_warmup(rows, warmup_s)
    if active_only:
        rows = trim_active(rows)
    rows = assign_sample_index(rows)
    return {
        "label": label,
        "path": path,
        "rows": rows,
        "summary": summarize(rows),
    }


def main():
    exp_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Plot 4-node RTP comparison: FPS and throughput (2 images)."
    )
    parser.add_argument(
        "--dir",
        type=Path,
        default=exp_dir,
        help="directory with archived CSV copies (default: Exp1/)",
    )
    parser.add_argument("--sender-csv", default="send-rtp-stats.csv")
    parser.add_argument("--node0-csv", default="node0-rtp-stats.csv")
    parser.add_argument("--node9-csv", default="node9-rtp-stats.csv")
    parser.add_argument("--receiver-csv", default="recv-rtp-stats.csv")
    parser.add_argument("--sender-label", default="sender")
    parser.add_argument("--node0-label", default="ingress gateway")
    parser.add_argument("--node9-label", default="egress gateway")
    parser.add_argument("--receiver-label", default="receiver")
    parser.add_argument("-o", "--output-fps", default="compare-fps.png")
    parser.add_argument("-t", "--output-throughput", default="compare-throughput.png")
    parser.add_argument("--warmup", type=float, default=0.0)
    parser.add_argument("--active-only", action="store_true")
    parser.add_argument(
        "--wall-time",
        action="store_true",
        help="x-axis = each node's local time_s (default: 1 s sample index as Time (s))",
    )
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    node_defs = [
        (args.sender_csv, args.sender_label),
        (args.node0_csv, args.node0_label),
        (args.node9_csv, args.node9_label),
        (args.receiver_csv, args.receiver_label),
    ]

    series = []
    summary_items = []
    for filename, label in node_defs:
        path = args.dir / filename
        if not path.is_file():
            print(f"Missing CSV: {path}", file=sys.stderr)
            print(
                "Copy experiment CSVs into Exp1/:\n"
                "  send-rtp-stats.csv, node0-rtp-stats.csv,\n"
                "  node9-rtp-stats.csv, recv-rtp-stats.csv",
                file=sys.stderr,
            )
            sys.exit(1)
        item = load_node(path, label, args.warmup, args.active_only)
        series.append(item)
        summary_items.append((label, item["summary"]))

    print("=== Four-node summary ===")
    print_summary_table(summary_items)
    print()

    sender_item = next((s for s in series if s["label"] == args.sender_label), None)
    node0_item = next((s for s in series if s["label"] == args.node0_label), None)
    if sender_item and node0_item:
        print_sender_gateway_alignment(
            sender_item["rows"],
            node0_item["rows"],
            gateway_label=args.node0_label,
        )

    node9_item = next((s for s in series if s["label"] == args.node9_label), None)
    receiver_item = next((s for s in series if s["label"] == args.receiver_label), None)
    if node9_item and receiver_item:
        print_node_pair_lag(
            node9_item["rows"],
            args.node9_label,
            receiver_item["rows"],
            args.receiver_label,
        )

    x_key = "time_s" if args.wall_time else "sample_idx"
    fps_path = args.dir / args.output_fps
    thr_path = args.dir / args.output_throughput

    plot_metric_comparison(
        series,
        metric="fps",
        ylabel=YLABEL_FPS,
        output_path=fps_path,
        title="RTP FPS: sender / node 1 / node 9 / receiver",
        show=args.show,
        x_key=x_key,
    )
    print(f"Wrote {fps_path}")

    plot_metric_comparison(
        series,
        metric="throughput_kbps",
        ylabel=YLABEL_THROUGHPUT,
        output_path=thr_path,
        title="RTP throughput: sender / node 1 / node 9 / receiver",
        show=False,
        x_key=x_key,
    )
    print(f"Wrote {thr_path}")


if __name__ == "__main__":
    main()
