#!/usr/bin/env python3
"""Legacy: node0 vs node9 only. Prefer plot-exp.py for 4-node comparison."""

import argparse
from pathlib import Path

from csv_utils import (
    load_stats_csv,
    plot_fps_thr_series,
    print_summary_table,
    summarize,
    trim_active,
    trim_warmup,
)


def main():
    exp_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(
        description="Plot node0 and node9 RTP stats from archived CSV copies"
    )
    parser.add_argument("--node0", default="node0-rtp-stats.csv")
    parser.add_argument("--node9", default="node9-rtp-stats.csv")
    parser.add_argument(
        "--dir",
        type=Path,
        default=exp_dir,
        help="directory with archived CSV copies (default: Exp1/)",
    )
    parser.add_argument("-o", "--output", default="rtp-fps-node0-node9.png")
    parser.add_argument("--warmup", type=float, default=0.0)
    parser.add_argument("--active-only", action="store_true")
    parser.add_argument(
        "--align-node0",
        type=float,
        default=None,
        help="subtract offset from node0 time_s before plotting",
    )
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    def load_named(name, label):
        path = args.dir / name
        if not path.is_file():
            raise SystemExit(f"Missing CSV: {path}")
        rows = load_stats_csv(path)
        rows = trim_warmup(rows, args.warmup)
        if args.active_only:
            rows = trim_active(rows)
        return path, label, rows, summarize(rows)

    _p0, label0, rows0, sum0 = load_named(args.node0, "node0 ingress")
    _p9, label9, rows9, sum9 = load_named(args.node9, "node9 egress")

    print_summary_table([(label0, sum0), (label9, sum9)])
    print()

    t0 = [r["time_s"] for r in rows0]
    if args.align_node0 is not None:
        t0 = [t - args.align_node0 for t in t0]

    plot_fps_thr_series(
        [
            {
                "label": label0,
                "times": t0,
                "fps": [r["fps"] for r in rows0],
                "thr": [r["throughput_kbps"] for r in rows0],
                "summary": sum0,
            },
            {
                "label": label9,
                "times": [r["time_s"] for r in rows9],
                "fps": [r["fps"] for r in rows9],
                "thr": [r["throughput_kbps"] for r in rows9],
                "summary": sum9,
            },
        ],
        args.dir / args.output,
        title="RTP: node0 ingress vs node9 egress",
        show=args.show,
    )
    print(f"Wrote {args.dir / args.output}")


if __name__ == "__main__":
    main()
