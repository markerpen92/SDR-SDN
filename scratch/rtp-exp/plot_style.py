"""FastReRoute-style plot formatting (see plot-format.md)."""

import logging
from contextlib import contextmanager
from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator

logger = logging.getLogger(__name__)

FIG_SIZE = (8, 5)
SAVE_DPI = 300
TIME_PLOT_MAX = 100

# User-visible axis / table labels (see plot-format.md).
# Repeated terms: spell out on first use in a figure or table header, then abbreviate.
XLABEL_TIME = "Time (s)"
XLABEL_SAMPLE = "Sample index"
YLABEL_FPS = "FPS"
YLABEL_THROUGHPUT = "Throughput (kbps)"
YLABEL_AVG_FPS = "Average FPS"
YLABEL_AVG_THROUGHPUT = "Average throughput (kbps)"
YLABEL_AVG_THROUGHPUT_SHORT = "Avg throughput (kbps)"

COLOR_SET = ["b", "y", "r", "m", "c", "k", "g"]
MARKER_DICT = ["-o", "-X", "-p", "-h"]
HATCH_SET = ["xxx", "o", "///", "---", ".", "|||", "\\\\\\", "*", "+", "O"]

# Distinct dash patterns for average markers (B&W print friendly).
AVG_LINE_STYLES = [
    (0, (6, 3)),
    (0, (3, 1, 1, 1)),
    (0, (1, 1)),
    (0, (5, 2, 1, 2)),
    (0, (4, 1, 1, 1)),
    (0, (2, 2)),
    (0, (8, 2, 2, 2)),
]

LEGEND_KW = {
    "frameon": True,
    "fontsize": 14,
    "title_fontsize": 14,
    "columnspacing": 1,
}


def _parse_marker(marker_spec):
    """'-o' -> ('-', 'o')."""
    if len(marker_spec) >= 2 and marker_spec[0] in "-":
        return marker_spec[0], marker_spec[1:]
    return "-", marker_spec


def _line_style(idx, n_points):
    if n_points >= 300:
        return "-", 15
    return ("--" if idx % 2 == 0 else "-"), None


def _configure_fonts():
    """Register IEEE-style serif fonts and set a Linux-friendly fallback stack."""
    from matplotlib import font_manager as fm

    for path in (
        "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/times.ttf",
        "/usr/share/fonts/opentype/urw-base35/NimbusRoman-Regular.otf",
    ):
        if Path(path).is_file():
            try:
                fm.fontManager.addfont(path)
            except (OSError, ValueError):
                pass

    plt.rcParams["font.serif"] = [
        "Times New Roman",
        "Nimbus Roman",
        "Liberation Serif",
        "DejaVu Serif",
    ]
    plt.rcParams["font.family"] = "serif"


@contextmanager
def plot_context():
    """Apply science + ieee style when SciencePlots is available."""
    try:
        import scienceplots  # noqa: F401

        style_ctx = plt.style.context(["science", "ieee"])
    except ImportError:
        from contextlib import nullcontext

        style_ctx = nullcontext()
        logger.warning(
            "SciencePlots not installed; using default matplotlib style. "
            "Install with: pip install SciencePlots"
        )

    with style_ctx:
        plt.rcParams["text.usetex"] = False
        _configure_fonts()
        yield


def apply_axis_fonts(ax, *, integer_x=False):
    ax.xaxis.label.set_size(18)
    ax.yaxis.label.set_size(18)
    ax.tick_params(axis="both", labelsize=14)
    ax.yaxis.set_major_locator(MaxNLocator(integer=True))
    if integer_x:
        ax.xaxis.set_major_locator(MaxNLocator(integer=True))
    else:
        ax.xaxis.set_major_locator(MaxNLocator(nbins=10, steps=[1, 2, 5, 10]))


def clip_rows_to_time(rows, t_max=TIME_PLOT_MAX):
    """Keep CSV rows with time_s <= t_max for time-series plots."""
    return [r for r in rows if r["time_s"] <= t_max]


def clip_xy_by_time(xs, ys, t_max=TIME_PLOT_MAX):
    """Drop (x, y) pairs where x > t_max."""
    pairs = [(x, y) for x, y in zip(xs, ys) if x <= t_max]
    if not pairs:
        return [], []
    xs_out, ys_out = zip(*pairs)
    return list(xs_out), list(ys_out)


def apply_time_axis(ax, time_lists, *, t_max=TIME_PLOT_MAX):
    """Set X limits for time-series plots; cap display at t_max seconds."""
    all_t = [t for ts in time_lists for t in ts if t <= t_max]
    if not all_t:
        return
    tmin = min(all_t)
    span = max(min(max(all_t), t_max) - tmin, 1.0)
    pad = max(0.5, span * 0.02)
    ax.set_xlim(tmin - pad, t_max)


def apply_sample_axis(ax, sample_indices):
    """Sample-index X axis: show the full index range."""
    if not sample_indices:
        return
    smin, smax = min(sample_indices), max(sample_indices)
    pad = max(1, int((smax - smin) * 0.02))
    ax.set_xlim(smin - pad, smax + pad)


def apply_xtick_steps(ax, xtick_set):
    """FastReRoute fixed tick grids (bandwidth / short index lists only)."""
    n = len(xtick_set)
    if 30 <= n < 300:
        ax.set_xticks(range(0, 35, 5))
    elif n >= 300:
        ax.set_xticks(range(0, 305, 30))
    else:
        ax.set_xticks(xtick_set)


def plot_average_marker(ax, avg_y, idx, label, value, *, value_fmt="{:.1f}"):
    """Full-width average line; each series uses a distinct dash pattern (B&W friendly)."""
    del label, value, value_fmt  # kept for call-site compatibility
    color = COLOR_SET[idx % len(COLOR_SET)]
    ls = AVG_LINE_STYLES[idx % len(AVG_LINE_STYLES)]
    ax.axhline(
        avg_y,
        color=color,
        linestyle=ls,
        linewidth=1.0,
        alpha=0.55,
        zorder=3,
    )


# Vertical spacing for legends placed below axes (figure-fraction units).
LEGEND_BELOW_GAP = 0.018  # gap below x-axis labels and first legend row
LEGEND_ROW_HEIGHT = 0.050  # height of one legend row (fontsize=14)
LEGEND_ROW_SPACING = 0.012  # gap between two legend rows
LEGEND_BOTTOM_PAD = 0.025  # padding below the lowest legend row


def _anchor_content_bottom(fig, anchor):
    """Bottom edge of axes including x tick labels and x-axis title (figure fraction)."""
    from matplotlib.transforms import Bbox

    renderer = fig.canvas.get_renderer()
    boxes = [anchor.get_window_extent(renderer)]
    for tick in anchor.get_xticklabels():
        if tick.get_visible():
            boxes.append(tick.get_window_extent(renderer))
    xlabel = anchor.xaxis.label
    if xlabel.get_visible():
        boxes.append(xlabel.get_window_extent(renderer))
    bbox = Bbox.union(boxes)
    return bbox.transformed(fig.transFigure.inverted()).y0


def _legend_stack_height(n_rows):
    if n_rows <= 1:
        return LEGEND_ROW_HEIGHT
    return LEGEND_ROW_HEIGHT + LEGEND_ROW_SPACING + LEGEND_ROW_HEIGHT


def _reserve_bottom_for_legend(fig, n_rows, bottom_min=None):
    """Shrink axes from the bottom to leave room for legend rows below x labels."""
    needed = _legend_stack_height(n_rows) + LEGEND_BELOW_GAP + LEGEND_BOTTOM_PAD + 0.04
    if bottom_min is not None:
        needed = max(needed, bottom_min)
    bottom = max(fig.subplotpars.bottom, needed)
    fig.subplots_adjust(bottom=bottom)


def add_legend_below(
    fig, ax, ncol=4, loc="upper center", avg_items=None, anchor_ax=None, bottom_min=None
):
    """Legend below figure; optional second row for average-line dash patterns."""
    from matplotlib.lines import Line2D

    handles, labels = ax.get_legend_handles_labels()
    if not handles:
        return None
    existing = ax.get_legend()
    if existing is not None:
        existing.remove()

    anchor = anchor_ax if anchor_ax is not None else ax
    n_rows = 2 if avg_items else 1
    _reserve_bottom_for_legend(fig, n_rows, bottom_min=bottom_min)
    fig.canvas.draw()

    content_bottom = _anchor_content_bottom(fig, anchor)
    y_series = content_bottom - LEGEND_BELOW_GAP

    leg_series = fig.legend(
        handles,
        labels,
        ncol=ncol,
        loc=loc,
        bbox_to_anchor=(0.5, y_series),
        bbox_transform=fig.transFigure,
        **LEGEND_KW,
    )

    if not avg_items:
        return leg_series

    ax.add_artist(leg_series)
    avg_handles = []
    avg_labels = []
    for idx, label, _value in avg_items:
        color = COLOR_SET[idx % len(COLOR_SET)]
        ls = AVG_LINE_STYLES[idx % len(AVG_LINE_STYLES)]
        avg_handles.append(
            Line2D([0], [0], color=color, linestyle=ls, linewidth=1.0, alpha=0.55)
        )
        avg_labels.append(f"{label} average")

    avg_ncol = min(ncol, max(1, len(avg_handles)))
    y_avg = y_series - LEGEND_ROW_HEIGHT - LEGEND_ROW_SPACING
    return fig.legend(
        avg_handles,
        avg_labels,
        ncol=avg_ncol,
        loc=loc,
        bbox_to_anchor=(0.5, y_avg),
        bbox_transform=fig.transFigure,
        **LEGEND_KW,
    )


def plot_series_line(ax, xs, ys, idx, label):
    """Draw one formatted line series on ax."""
    color = COLOR_SET[idx % len(COLOR_SET)]
    linestyle, markevery = _line_style(idx, len(xs))
    _, marker = _parse_marker(MARKER_DICT[idx % len(MARKER_DICT)])

    kwargs = {
        "color": color,
        "linewidth": 1.0,
        "linestyle": linestyle,
        "marker": marker,
        "markersize": 6,
        "label": label,
    }
    if markevery is not None:
        kwargs["markevery"] = markevery
    if idx % 2 == 1:
        kwargs["markerfacecolor"] = "none"

    ax.plot(xs, ys, **kwargs)


def plot_grouped_bars(ax, xtick_labels, series_values, series_labels):
    """Grouped bar chart (FastReRoute bar format)."""
    import numpy as np

    n_groups = len(xtick_labels)
    n_series = len(series_labels)
    xpos = np.arange(0, n_groups * 4, 4)
    interval = 0.6
    xpos = xpos - 0.5 * interval * (n_series - 1)
    group_centers = np.arange(0, n_groups * 4, 4)

    for idx, (label, values) in enumerate(zip(series_labels, series_values)):
        edge = COLOR_SET[idx % len(COLOR_SET)]
        hatch = HATCH_SET[idx % len(HATCH_SET)]
        ax.bar(
            xpos,
            values,
            width=0.5,
            align="center",
            color="w",
            edgecolor=edge,
            hatch=hatch,
            alpha=0.9,
            label=label,
        )
        xpos = xpos + interval

    ax.set_xticks(group_centers)
    ax.set_xticklabels(xtick_labels)
    return group_centers


def plot_category_bars(ax, labels, values):
    """Single-metric categorical bars (one bar per label)."""
    import numpy as np

    x = np.arange(len(labels))
    for idx, val in enumerate(values):
        edge = COLOR_SET[idx % len(COLOR_SET)]
        hatch = HATCH_SET[idx % len(HATCH_SET)]
        ax.bar(
            x[idx],
            val,
            width=0.5,
            align="center",
            color="w",
            edgecolor=edge,
            hatch=hatch,
            alpha=0.9,
        )
    ax.set_xticks(x)
    ax.set_xticklabels(labels)
    return x


def save_figure(fig, output_path):
    fig.savefig(output_path, dpi=SAVE_DPI, bbox_inches="tight")
