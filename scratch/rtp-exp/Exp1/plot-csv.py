#!/usr/bin/env python3
"""Plot FPS & throughput from a single archived CSV (+ print summary)."""

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
    parser = argparse.ArgumentParser(description="Plot one RTP stats CSV (FPS & throughput)")
    parser.add_argument(
        "csv",
        nargs="?",
        default="recv-rtp-stats.csv",
        help="CSV file name or path (default: recv-rtp-stats.csv in Exp1/)",
    )
    parser.add_argument("-o", "--output", default=None, help="output PNG (default: <csv>.png)")
    parser.add_argument("--label", default=None, help="legend / title label")
    parser.add_argument("--warmup", type=float, default=0.0)
    parser.add_argument("--active-only", action="store_true")
    parser.add_argument("--show", action="store_true")
    args = parser.parse_args()

    csv_path = Path(args.csv)
    if not csv_path.is_file():
        csv_path = exp_dir / args.csv
    if not csv_path.is_file():
        raise SystemExit(f"CSV not found: {args.csv}")

    rows = load_stats_csv(csv_path)
    rows = trim_warmup(rows, args.warmup)
    if args.active_only:
        rows = trim_active(rows)

    label = args.label or csv_path.stem
    summary = summarize(rows)
    print_summary_table([(label, summary)])
    print()

    output = Path(args.output) if args.output else csv_path.with_suffix(".png")
    plot_fps_thr_series(
        [
            {
                "label": label,
                "times": [r["time_s"] for r in rows],
                "fps": [r["fps"] for r in rows],
                "thr": [r["throughput_kbps"] for r in rows],
                "summary": summary,
            }
        ],
        output,
        title=f"RTP stats: {label}",
        show=args.show,
    )
    print(f"Wrote {output}")


if __name__ == "__main__":
    main()
